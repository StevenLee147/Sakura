// tests/test_v140_course_daily_collections.cpp — v1.4.0 樱华道场段位认定、每日挑战与命名多曲单测试

#include "test_framework.h"

#include "core/config.h"
#include "data/database.h"
#include "game/chart_loader.h"
#include "game/course.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace sakura::game;
using sakura::core::Config;
using sakura::data::Database;
using sakura::tests::Matchers::WithinAbs;

namespace
{
struct ScopedV140Env
{
    std::filesystem::path dir;
    nlohmann::json        configSnapshot;
    std::string           previousConfigPath;

    ScopedV140Env()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v140_" + std::to_string(stamp));
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);

        auto& cfg          = Config::GetInstance();
        configSnapshot     = cfg.GetRoot();
        previousConfigPath = cfg.GetFilePath();
        Database::GetInstance().Shutdown();
        CourseSession::GetInstance().Abort();
    }

    ~ScopedV140Env()
    {
        CourseSession::GetInstance().Abort();
        Database::GetInstance().Shutdown();

        auto& cfg = Config::GetInstance();
        if (!previousConfigPath.empty())
            cfg.Load(previousConfigPath);
        cfg.Restore(configSnapshot);

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
} // namespace

TEST_CASE("CourseGauge clamps Perfect heal to 100, applies Good/Bad/Miss damage, latches failure at 0, and heals on stage clear", "[v140][course][gauge]")
{
    CourseDefinition def;
    def.id             = "test_dan";
    def.title          = "Test Course";
    def.badgeText      = "测试段";
    def.danRank        = 3;
    def.initialLife    = 100.0f;
    def.stageClearHeal = 15.0f;
    def.perfectHeal    = 2.5f;
    def.greatDelta     = 0.0f;
    def.goodDamage     = 5.0f;
    def.badDamage      = 12.0f;
    def.missDamage     = 25.0f;

    CourseGauge gauge(def);
    REQUIRE(!gauge.IsFailed());
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(100.0, 1e-4));

    // Perfect heal must stay clamped to 100
    gauge.OnJudge(JudgeResult::Perfect);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(100.0, 1e-4));

    // Good / Great / Perfect recovery + clamp
    gauge.OnJudge(JudgeResult::Good);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(95.0, 1e-4));
    gauge.OnJudge(JudgeResult::Great);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(95.0, 1e-4));
    gauge.OnJudge(JudgeResult::Perfect);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(97.5, 1e-4));
    gauge.OnJudge(JudgeResult::Perfect);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(100.0, 1e-4));
    gauge.OnJudge(JudgeResult::Perfect);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(100.0, 1e-4));

    // Bad & Miss damage, followed by OnStageCleared() heal while alive
    gauge.OnJudge(JudgeResult::Bad);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(88.0, 1e-4));
    gauge.OnJudge(JudgeResult::Miss);
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(63.0, 1e-4));
    REQUIRE(!gauge.IsFailed());

    gauge.OnStageCleared();
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(78.0, 1e-4));
    gauge.OnStageCleared();
    gauge.OnStageCleared();
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(100.0, 1e-4));

    // Reaching 0 latches IsFailed() == true and ignores subsequent Perfect / OnStageCleared heals
    gauge.OnJudge(JudgeResult::Miss);
    gauge.OnJudge(JudgeResult::Miss);
    gauge.OnJudge(JudgeResult::Miss);
    REQUIRE(!gauge.IsFailed());
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(25.0, 1e-4));

    gauge.OnJudge(JudgeResult::Miss);
    REQUIRE(gauge.IsFailed());
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(0.0, 1e-4));

    gauge.OnJudge(JudgeResult::Perfect);
    gauge.OnStageCleared();
    REQUIRE(gauge.IsFailed());
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(0.0, 1e-4));

    gauge.Reset();
    REQUIRE(!gauge.IsFailed());
    REQUIRE_THAT(gauge.GetLife(), WithinAbs(100.0, 1e-4));
}

TEST_CASE("Official 6 Dan courses resolve 100% of stages and CourseSession tracks clear vs gauge failure", "[v140][course][session]")
{
    ScopedV140Env env;
    ChartLoader loader;
    const auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");
    REQUIRE(!charts.empty());

    const auto& courses = GetOfficialCourses();
    REQUIRE(courses.size() == 6);
    REQUIRE(FindOfficialCourse("nonexistent_dan_course") == nullptr);

    auto& session = CourseSession::GetInstance();
    for (std::size_t i = 0; i < courses.size(); ++i)
    {
        const auto& c = courses[i];
        REQUIRE(!c.id.empty());
        REQUIRE(!c.title.empty());
        REQUIRE(!c.badgeText.empty());
        REQUIRE(c.stages.size() >= 3);
        REQUIRE(FindOfficialCourse(c.id) != nullptr);
        if (i > 0)
            REQUIRE(c.danRank > courses[i - 1].danRank);

        REQUIRE(session.Start(c, charts));
        REQUIRE(session.IsActive());
        REQUIRE(session.GetTotalStages() == static_cast<int>(c.stages.size()));

        for (int s = 0; s < session.GetTotalStages(); ++s)
        {
            REQUIRE(session.GetCurrentStageIndex() == s);
            ChartInfo resolvedChart;
            int resolvedDiff = -1;
            REQUIRE(session.GetCurrentStageChart(resolvedChart, resolvedDiff));
            REQUIRE(resolvedChart.id == c.stages[static_cast<std::size_t>(s)].chartId);
            REQUIRE(resolvedDiff >= 0);
            REQUIRE(resolvedDiff < static_cast<int>(resolvedChart.difficulties.size()));

            if (s + 1 < session.GetTotalStages())
            {
                REQUIRE(session.HasNextStage());
                REQUIRE(session.AdvanceStage());
            }
            else
            {
                REQUIRE(!session.HasNextStage());
            }
        }
        session.Abort();
        REQUIRE(!session.IsActive());
    }

    // Full multi-stage clear sums totalScore and averages accuracy
    const CourseDefinition& firstCourse = courses.front();
    REQUIRE(session.Start(firstCourse, charts));
    int expectedTotalScore = 0;
    double expectedAccSum  = 0.0;
    const int stageCount   = session.GetTotalStages();

    for (int s = 0; s < stageCount; ++s)
    {
        ChartInfo stChart;
        int stDiff = -1;
        REQUIRE(session.GetCurrentStageChart(stChart, stDiff));

        session.Gauge().OnJudge(JudgeResult::Good);

        GameResult gr;
        gr.chartId         = stChart.id;
        gr.chartTitle      = stChart.title;
        gr.difficulty      = stChart.difficulties[static_cast<std::size_t>(stDiff)].name;
        gr.difficultyIndex = stDiff;
        gr.difficultyLevel = stChart.difficulties[static_cast<std::size_t>(stDiff)].level;
        gr.score           = 940000 + s * 15000;
        gr.accuracy        = 95.5f + static_cast<float>(s) * 1.2f;
        gr.grade           = Grade::S;

        expectedTotalScore += gr.score;
        expectedAccSum += static_cast<double>(gr.accuracy);
        session.RecordStageResult(gr);

        if (s + 1 < stageCount)
        {
            session.Gauge().OnStageCleared();
            REQUIRE(session.HasNextStage());
            REQUIRE(session.AdvanceStage());
        }
    }

    const CourseRecord clearRec = session.BuildFinalRecord(1795651200LL);
    REQUIRE(clearRec.cleared);
    REQUIRE(clearRec.courseId == firstCourse.id);
    REQUIRE(clearRec.badgeText == firstCourse.badgeText);
    REQUIRE(clearRec.danRank == firstCourse.danRank);
    REQUIRE(clearRec.totalScore == expectedTotalScore);
    REQUIRE_THAT(clearRec.avgAccuracy, WithinAbs(expectedAccSum / static_cast<double>(stageCount), 0.02));
    REQUIRE(clearRec.remainingLife > 0.0f);
    REQUIRE(clearRec.clearedAt == 1795651200LL);
    session.Abort();

    // Gauge depletion sets cleared == false
    const CourseDefinition& hardCourse = courses.back();
    REQUIRE(session.Start(hardCourse, charts));
    for (int i = 0; i < 64 && !session.Gauge().IsFailed(); ++i)
        session.Gauge().OnJudge(JudgeResult::Miss);
    REQUIRE(session.Gauge().IsFailed());

    GameResult failedStage;
    failedStage.score    = 410000;
    failedStage.accuracy = 71.0f;
    session.RecordStageResult(failedStage);

    const CourseRecord failRec = session.BuildFinalRecord(1795651300LL);
    REQUIRE(!failRec.cleared);
    REQUIRE_THAT(failRec.remainingLife, WithinAbs(0.0, 1e-4));
    session.Abort();
}

TEST_CASE("Database persists course records, ranks highest cleared Dan badge, and merges cloud records idempotently", "[v140][database][course]")
{
    ScopedV140Env env;
    auto& db = Database::GetInstance();
    REQUIRE(db.Initialize((env.dir / "courses_v140.db").string()));

    REQUIRE(db.GetAllCourseRecords().empty());
    REQUIRE(!db.GetCourseRecord("dan_1st").has_value());
    REQUIRE(!db.GetHighestClearedCourse().has_value());

    // Uncleared Kaiden attempt must not count as highest cleared Dan
    const CourseRecord failedKaiden{ "dan_kaiden", "皆传", 10, 1500000, 82.0f, 0.0f, false, 1795600100LL };
    REQUIRE(db.SaveCourseRecord(failedKaiden));
    REQUIRE(!db.GetHighestClearedCourse().has_value());

    // Cleared 1st Dan becomes highest cleared course
    const CourseRecord clearedShodan{ "dan_1st", "初段", 1, 2860000, 96.2f, 64.0f, true, 1795600200LL };
    REQUIRE(db.SaveCourseRecord(clearedShodan));
    const auto shodanFetched = db.GetCourseRecord("dan_1st");
    REQUIRE(shodanFetched.has_value());
    REQUIRE(shodanFetched->cleared);
    REQUIRE(shodanFetched->totalScore == 2860000);
    REQUIRE(shodanFetched->badgeText == "初段");

    const auto highestAfterShodan = db.GetHighestClearedCourse();
    REQUIRE(highestAfterShodan.has_value());
    REQUIRE(highestAfterShodan->danRank == 1);
    REQUIRE(highestAfterShodan->badgeText == "初段");

    // Lower failed run on already-cleared course must not downgrade cleared state or best score
    const CourseRecord worseAttempt{ "dan_1st", "初段", 1, 1200000, 75.0f, 0.0f, false, 1795600250LL };
    REQUIRE(db.SaveCourseRecord(worseAttempt));
    const auto shodanPreserved = db.GetCourseRecord("dan_1st");
    REQUIRE(shodanPreserved.has_value());
    REQUIRE(shodanPreserved->cleared);
    REQUIRE(shodanPreserved->totalScore == 2860000);

    // Cleared Kaiden (danRank 10 "皆传") outranks Shodan (danRank 1 "初段")
    const CourseRecord clearedKaiden{ "dan_kaiden", "皆传", 10, 3905000, 98.9f, 76.0f, true, 1795600500LL };
    REQUIRE(db.SaveCourseRecord(clearedKaiden));

    const auto allRecords = db.GetAllCourseRecords();
    REQUIRE(allRecords.size() == 2);

    const auto highestAfterKaiden = db.GetHighestClearedCourse();
    REQUIRE(highestAfterKaiden.has_value());
    REQUIRE(highestAfterKaiden->courseId == "dan_kaiden");
    REQUIRE(highestAfterKaiden->danRank == 10);
    REQUIRE(highestAfterKaiden->badgeText == "皆传");

    // Idempotent MergeCloudCourseRecord: upgrades when strictly better, no-op when repeated or lower
    const CourseRecord cloudUpgrade{ "dan_kaiden", "皆传", 10, 3948000, 99.4f, 88.5f, true, 1795600900LL };
    REQUIRE(db.MergeCloudCourseRecord(cloudUpgrade));
    REQUIRE(db.GetCourseRecord("dan_kaiden")->totalScore == 3948000);
    REQUIRE(!db.MergeCloudCourseRecord(cloudUpgrade));

    const CourseRecord cloudLower{ "dan_kaiden", "皆传", 10, 3800000, 97.0f, 50.0f, true, 1795601000LL };
    REQUIRE(!db.MergeCloudCourseRecord(cloudLower));
    REQUIRE(db.GetCourseRecord("dan_kaiden")->totalScore == 3948000);

    const CourseRecord cloudMidDan{ "dan_6th", "六段", 6, 3710000, 97.6f, 58.0f, true, 1795601100LL };
    REQUIRE(db.MergeCloudCourseRecord(cloudMidDan));
    REQUIRE(!db.MergeCloudCourseRecord(cloudMidDan));
    REQUIRE(db.GetAllCourseRecords().size() == 3);
    REQUIRE(db.GetHighestClearedCourse()->danRank == 10);
    REQUIRE(db.GetHighestClearedCourse()->badgeText == "皆传");
}

TEST_CASE("GetCurrentDateUtc8 and ComputeOfflineDailyChallenge provide deterministic UTC+8 FNV-1a rotation", "[v140][daily]")
{
    // Known Unix epoch timestamps at UTC+8 boundaries
    // 2026-11-26 00:00:00 UTC+8 == 2026-11-25 16:00:00 UTC == 1795622400
    REQUIRE(GetCurrentDateUtc8(1795651200LL) == "2026-11-26");
    REQUIRE(GetCurrentDateUtc8(1795622400LL) == "2026-11-26");
    REQUIRE(GetCurrentDateUtc8(1795622399LL) == "2026-11-25");

    // Year rollover & leap day in UTC+8
    REQUIRE(GetCurrentDateUtc8(1704038399LL) == "2023-12-31");
    REQUIRE(GetCurrentDateUtc8(1704038400LL) == "2024-01-01");
    REQUIRE(GetCurrentDateUtc8(1709164800LL) == "2024-02-29");

    const std::string liveToday = GetCurrentDateUtc8(0);
    REQUIRE(liveToday.size() == 10);
    REQUIRE(liveToday[4] == '-');
    REQUIRE(liveToday[7] == '-');

    ChartLoader loader;
    const auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");
    REQUIRE(!charts.empty());

    const auto findChart = [&](const std::string& id) -> const ChartInfo*
    {
        for (const auto& c : charts)
        {
            if (c.id == id)
                return &c;
        }
        return nullptr;
    };

    // Verify deterministic FNV-1a output for 2026-11-26 and built-in chart existence
    const DailyChallengeSpec d1 = ComputeOfflineDailyChallenge("2026-11-26");
    const DailyChallengeSpec d2 = ComputeOfflineDailyChallenge("2026-11-26");
    REQUIRE(d1.dateUtc8 == "2026-11-26");
    REQUIRE(!d1.chartId.empty());
    REQUIRE(!d1.chartTitle.empty());
    REQUIRE(!d1.difficultyName.empty());
    REQUIRE(!d1.goalText.empty());
    REQUIRE(d1.difficultyLevel >= 1.0f);
    REQUIRE(d1.targetScore > 0);

    REQUIRE(d1.chartId == d2.chartId);
    REQUIRE(d1.chartTitle == d2.chartTitle);
    REQUIRE(d1.difficultyName == d2.difficultyName);
    REQUIRE_THAT(d1.difficultyLevel, WithinAbs(d2.difficultyLevel, 1e-4));
    REQUIRE(d1.goalText == d2.goalText);
    REQUIRE(d1.targetScore == d2.targetScore);

    const ChartInfo* nov26Chart = findChart(d1.chartId);
    REQUIRE(nov26Chart != nullptr);
    const bool hasMatchingDiff = std::any_of(
        nov26Chart->difficulties.begin(), nov26Chart->difficulties.end(),
        [&](const DifficultyInfo& diff) { return diff.name == d1.difficultyName; });
    REQUIRE(hasMatchingDiff);

    // Verify pool rotation across a 14-day window resolves 100% against resources/charts
    std::set<std::string> distinctCharts;
    for (int day = 15; day <= 28; ++day)
    {
        const std::string dateStr = "2026-11-" + std::to_string(day);
        const DailyChallengeSpec spec = ComputeOfflineDailyChallenge(dateStr);
        REQUIRE(spec.dateUtc8 == dateStr);
        const ChartInfo* chart = findChart(spec.chartId);
        REQUIRE(chart != nullptr);
        distinctCharts.insert(spec.chartId);
    }
    REQUIRE(distinctCharts.size() >= 2);
}

TEST_CASE("CollectionManager supports CRUD, config persistence, and duplicate-free multi-device union merge", "[v140][collections]")
{
    ScopedV140Env env;
    const auto settingsFile = env.dir / "collections_settings.json";
    auto& cfg = Config::GetInstance();
    cfg.Load(settingsFile.string());

    std::vector<SongCollection> cols;
    const SongCollection colWarmup = CollectionManager::CreateCollection(cols, "热身曲单", 1795600100LL);
    const SongCollection colPush   = CollectionManager::CreateCollection(cols, "高难冲分", 1795600200LL);
    REQUIRE(cols.size() == 2);
    REQUIRE(!colWarmup.id.empty());
    REQUIRE(!colPush.id.empty());
    REQUIRE(colWarmup.id != colPush.id);
    REQUIRE(cols[0].name == "热身曲单");
    REQUIRE(cols[1].name == "高难冲分");

    // ToggleChartInCollection adds then removes on repeat toggle
    CollectionManager::ToggleChartInCollection(cols, colWarmup.id, "sakura_breath", 1795600300LL);
    CollectionManager::ToggleChartInCollection(cols, colWarmup.id, "spring_breeze", 1795600350LL);
    REQUIRE(CollectionManager::ContainsChart(cols[0], "sakura_breath"));
    REQUIRE(CollectionManager::ContainsChart(cols[0], "spring_breeze"));
    REQUIRE(cols[0].chartIds.size() == 2);

    CollectionManager::ToggleChartInCollection(cols, colWarmup.id, "sakura_breath", 1795600400LL);
    REQUIRE(!CollectionManager::ContainsChart(cols[0], "sakura_breath"));
    REQUIRE(CollectionManager::ContainsChart(cols[0], "spring_breeze"));
    REQUIRE(cols[0].chartIds.size() == 1);

    CollectionManager::ToggleChartInCollection(cols, colWarmup.id, "sakura_breath", 1795600450LL);
    REQUIRE(cols[0].chartIds.size() == 2);

    // RenameCollection
    REQUIRE(CollectionManager::RenameCollection(cols, colWarmup.id, "每日热身", 1795600500LL));
    REQUIRE(cols[0].name == "每日热身");
    REQUIRE(cols[0].updatedAt == 1795600500LL);
    REQUIRE(!CollectionManager::RenameCollection(cols, "nonexistent_col", "无效", 1795600600LL));

    // SaveToConfig & LoadFromConfig round-trip
    CollectionManager::SaveToConfig(cols);
    REQUIRE(cfg.SaveForce());
    REQUIRE(cfg.Load(settingsFile.string()));

    std::vector<SongCollection> loaded = CollectionManager::LoadFromConfig();
    REQUIRE(loaded.size() == 2);
    REQUIRE(loaded[0].id == colWarmup.id);
    REQUIRE(loaded[0].name == "每日热身");
    REQUIRE(CollectionManager::ContainsChart(loaded[0], "spring_breeze"));
    REQUIRE(CollectionManager::ContainsChart(loaded[0], "sakura_breath"));

    // DeleteCollection
    REQUIRE(CollectionManager::DeleteCollection(loaded, colPush.id));
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded[0].id == colWarmup.id);
    REQUIRE(!CollectionManager::DeleteCollection(loaded, colPush.id));

    // MergeCollections unions chartIds across devices without duplicates
    SongCollection localShared{ "col_shared", "主力练习", { "sakura_breath", "starry_sky" }, 1795601000LL };
    SongCollection localOnly{ "col_local", "左手耐力", { "digital_dream" }, 1795601100LL };

    SongCollection remoteShared{ "col_shared", "主力冲分", { "starry_sky", "unfallen_light", "sakura_breath" }, 1795602000LL };
    SongCollection remoteOnly{ "col_remote", "右手定位", { "instant_bloom" }, 1795601500LL };

    const std::vector<SongCollection> merged = CollectionManager::MergeCollections(
        { localShared, localOnly },
        { remoteShared, remoteOnly });

    REQUIRE(merged.size() == 3);
    const auto itShared = std::find_if(merged.begin(), merged.end(),
        [](const SongCollection& c) { return c.id == "col_shared"; });
    REQUIRE(itShared != merged.end());
    REQUIRE(itShared->name == "主力冲分");
    REQUIRE(itShared->updatedAt == 1795602000LL);
    REQUIRE(itShared->chartIds.size() == 3);
    REQUIRE(CollectionManager::ContainsChart(*itShared, "sakura_breath"));
    REQUIRE(CollectionManager::ContainsChart(*itShared, "starry_sky"));
    REQUIRE(CollectionManager::ContainsChart(*itShared, "unfallen_light"));

    const std::set<std::string> uniqueIds(itShared->chartIds.begin(), itShared->chartIds.end());
    REQUIRE(uniqueIds.size() == itShared->chartIds.size());
}
