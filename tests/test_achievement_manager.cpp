#include "test_framework.h"

#include "data/database.h"
#include "game/achievement_manager.h"
#include "game/chart.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace
{

struct TempDatabaseScope
{
    fs::path path;

    explicit TempDatabaseScope(const char* name)
        : path(fs::temp_directory_path() / name)
    {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(path.string() + "-wal", ec);
        fs::remove(path.string() + "-shm", ec);

        auto& db = sakura::data::Database::GetInstance();
        db.Shutdown();
        REQUIRE(db.Initialize(path.string()));
    }

    ~TempDatabaseScope()
    {
        auto& db = sakura::data::Database::GetInstance();
        db.Shutdown();

        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(path.string() + "-wal", ec);
        fs::remove(path.string() + "-shm", ec);
    }
};

sakura::game::GameResult MakeResult(int score,
                                    float accuracy,
                                    int maxCombo,
                                    bool isFullCombo,
                                    bool isAllPerfect,
                                    sakura::game::Grade grade = sakura::game::Grade::A,
                                    std::string chartId = "tutorial_song",
                                    std::string difficulty = "Easy",
                                    float difficultyLevel = 1.0f)
{
    sakura::game::GameResult result;
    result.chartId         = std::move(chartId);
    result.chartTitle      = result.chartId;
    result.difficulty      = std::move(difficulty);
    result.difficultyLevel = difficultyLevel;
    result.score           = score;
    result.accuracy        = accuracy;
    result.maxCombo        = maxCombo;
    result.grade           = grade;
    result.isFullCombo     = isFullCombo;
    result.isAllPerfect    = isAllPerfect;
    result.perfectCount    = maxCombo;
    return result;
}

} // namespace

TEST_CASE("AchievementManager 首次游玩触发 first_play", "[achievement]")
{
    TempDatabaseScope scope("sakura-achievement-first-play.db");
    auto& database = sakura::data::Database::GetInstance();
    auto& manager  = sakura::game::AchievementManager::GetInstance();

    REQUIRE(manager.LoadAchievements());

    auto result = MakeResult(850000, 95.2f, 48, false, false);
    REQUIRE(database.SaveScore(result));

    auto unlocked = manager.CheckAndUnlock(result);
    REQUIRE(unlocked.size() == 1);
    REQUIRE(unlocked[0].definition.id == "first_play");
    REQUIRE(database.IsAchievementUnlocked("first_play"));
}

TEST_CASE("AchievementManager 根据成绩和连击解锁一次性成就", "[achievement]")
{
    TempDatabaseScope scope("sakura-achievement-score.db");
    auto& database = sakura::data::Database::GetInstance();
    auto& manager  = sakura::game::AchievementManager::GetInstance();

    REQUIRE(manager.LoadAchievements());

    auto result = MakeResult(999500, 99.61f, 120, true, true, sakura::game::Grade::SS,
        "spring_breeze", "Hard", 5.5f);
    REQUIRE(database.SaveScore(result));

    auto unlocked = manager.CheckAndUnlock(result);
    REQUIRE(unlocked.size() >= 6);
    REQUIRE(database.IsAchievementUnlocked("first_play"));
    REQUIRE(database.IsAchievementUnlocked("first_fc"));
    REQUIRE(database.IsAchievementUnlocked("first_ap"));
    REQUIRE(database.IsAchievementUnlocked("combo_100"));
    REQUIRE(database.IsAchievementUnlocked("accuracy_99"));
    REQUIRE(database.IsAchievementUnlocked("score_999k"));
}

TEST_CASE("AchievementManager 累计游玩阈值可解锁", "[achievement]")
{
    TempDatabaseScope scope("sakura-achievement-play-count.db");
    auto& database = sakura::data::Database::GetInstance();
    auto& manager  = sakura::game::AchievementManager::GetInstance();

    REQUIRE(manager.LoadAchievements());

    for (int index = 0; index < 10; ++index)
    {
        auto result = MakeResult(800000 + index, 90.0f + static_cast<float>(index) * 0.1f,
            30 + index, false, false, sakura::game::Grade::B,
            "tutorial_song", "Easy", 1.0f);
        REQUIRE(database.SaveScore(result));
        manager.CheckAndUnlock(result);
    }

    REQUIRE(database.IsAchievementUnlocked("play_10"));

    auto progress = manager.GetProgress("play_50");
    REQUIRE(progress.has_value());
    REQUIRE(progress->current == 10);
    REQUIRE(progress->target == 50);
    REQUIRE_THAT(progress->progress, sakura::tests::Matchers::WithinAbs(0.2, 0.001));
}