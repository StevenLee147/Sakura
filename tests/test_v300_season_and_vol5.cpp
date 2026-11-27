// tests/test_v300_season_and_vol5.cpp — v3.0.0 Sakura Season Ranked & Official Pack Vol.05 (21 songs / 61 charts / Master 15.0) audit

#include "test_framework.h"

#include "game/chart_loader.h"
#include "game/play_session.h"
#include "game/season.h"
#include "net/api_client.h"
#include "net/http_client.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace sakura::game;

TEST_CASE("SeasonEngine deterministic monthly stage rotation, SP monotonicity, assisted/rate guard, and 5 rank tiers", "[season][v300]")
{
    const std::array<const char*, 4> months{{ "2026-01", "2026-06", "2026-11", "2027-03" }};
    for (const char* m : months)
    {
        REQUIRE(SeasonEngine::IsValidSeasonId(m));
        const auto spec1 = SeasonEngine::ComputeSeasonSpec(m);
        const auto spec2 = SeasonEngine::ComputeSeasonSpec(m);
        REQUIRE(spec1.seasonId == m);
        REQUIRE(spec1.stages[0].chartId == spec2.stages[0].chartId);
        REQUIRE(spec1.stages[1].chartId == spec2.stages[1].chartId);
        REQUIRE(spec1.stages[2].chartId == spec2.stages[2].chartId);

        REQUIRE(spec1.stages[0].difficultyLevel >= 6.0f);
        REQUIRE(spec1.stages[0].difficultyLevel <= 9.5f);
        REQUIRE(spec1.stages[1].difficultyLevel >= 9.5f);
        REQUIRE(spec1.stages[1].difficultyLevel <= 12.5f);
        REQUIRE(spec1.stages[2].difficultyLevel >= 12.5f);
        REQUIRE(spec1.stages[2].difficultyLevel <= 15.0f);
        REQUIRE(spec1.stages[0].maxStageSP == 1000);
        REQUIRE(spec1.stages[1].maxStageSP == 1150);
        REQUIRE(spec1.stages[2].maxStageSP == 1350);

        std::set<std::string> uniqueIds{
            spec1.stages[0].chartId,
            spec1.stages[1].chartId,
            spec1.stages[2].chartId
        };
        REQUIRE(uniqueIds.size() == 3);
    }
    REQUIRE(!SeasonEngine::IsValidSeasonId("2026-13"));
    REQUIRE(!SeasonEngine::IsValidSeasonId("bad"));

    auto makePlay = [](int score, float acc, bool fc, bool ap, bool assisted = false) {
        GameResult r{};
        r.score         = score;
        r.accuracy      = acc;
        r.isFullCombo   = fc;
        r.isAllPerfect  = ap;
        r.assisted      = assisted;
        r.playbackRate  = 1.0f;
        return r;
    };

    REQUIRE(SeasonEngine::CalculateStageSP(makePlay(1000000, 100.0f, true, true, true), 14.5f, 2, 1.0f) == 0);
    REQUIRE(SeasonEngine::CalculateStageSP(makePlay(1000000, 100.0f, true, true, false), 14.5f, 2, 1.25f) == 0);

    const int sp92 = SeasonEngine::CalculateStageSP(makePlay(920000, 92.0f, false, false), 14.5f, 2, 1.0f);
    const int sp97 = SeasonEngine::CalculateStageSP(makePlay(970000, 97.0f, false, false), 14.5f, 2, 1.0f);
    const int sp99 = SeasonEngine::CalculateStageSP(makePlay(995000, 99.5f, true,  false), 14.5f, 2, 1.0f);
    const int spAp = SeasonEngine::CalculateStageSP(makePlay(1000000, 100.0f, true, true), 14.5f, 2, 1.0f);
    REQUIRE(sp92 > 0);
    REQUIRE(sp97 > sp92);
    REQUIRE(sp99 > sp97);
    REQUIRE(spAp == 1350);

    REQUIRE(SeasonEngine::EvaluateTier(0).tierName == "青铜樱");
    REQUIRE(SeasonEngine::EvaluateTier(1199).tierName == "青铜樱");
    REQUIRE(SeasonEngine::EvaluateTier(1200).tierName == "白银月");
    REQUIRE(SeasonEngine::EvaluateTier(1900).tierName == "黄金华");
    REQUIRE(SeasonEngine::EvaluateTier(2500).tierName == "绯红霞");
    REQUIRE(SeasonEngine::EvaluateTier(3050).tierName == "极星神乐");
    REQUIRE(SeasonEngine::EvaluateTier(3500).spToNextTier == 0);
}

TEST_CASE("Official Pack Vol.05 completes 21 authentic OGG songs / 61 charts including Master 15.0 AutoPlay 100% AP", "[charts][vol5][v300]")
{
    const fs::path chartsRoot = fs::path(SAKURA_SOURCE_DIR) / "resources" / "charts";
    ChartLoader loader;
    const auto charts = loader.ScanCharts(chartsRoot.string());
    REQUIRE(charts.size() == 21);

    int totalDiffs = 0;
    for (const auto& c : charts)
    {
        totalDiffs += static_cast<int>(c.difficulties.size());
        const fs::path ogg = fs::path(c.folderPath) / c.musicFile;
        REQUIRE(fs::is_regular_file(ogg));
        REQUIRE(fs::file_size(ogg) > 32768); // genuine full-length Vorbis audio
        std::ifstream in(ogg, std::ios::binary);
        char magic[4]{};
        REQUIRE(static_cast<bool>(in.read(magic, 4)));
        REQUIRE(std::string_view(magic, 4) == "OggS");
    }
    REQUIRE(totalDiffs == 61);

    constexpr std::array<const char*, 6> kVol5Ids{{
        "petal_storm_vip",
        "instant_bloom_vip",
        "distant_star_vip",
        "sakura_breath_vip",
        "sakura_sprint_vip",
        "shattered_threshold_omega",
    }};
    for (const char* id : kVol5Ids)
    {
        const auto it = std::find_if(charts.begin(), charts.end(), [id](const ChartInfo& c) { return c.id == id; });
        REQUIRE(it != charts.end());
        REQUIRE(it->version == 4);
        REQUIRE(fs::is_regular_file(fs::path(it->folderPath) / "cover.png"));
        for (const auto& d : it->difficulties)
        {
            auto data = loader.LoadChartData(it->folderPath + "/" + d.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(loader.ValidateChartData(*data));
            REQUIRE(!data->lyrics.empty());
        }
    }

    // Audit pinnacle boss chart shattered_threshold_omega Master 15.0 with deterministic AutoPlay 100% AP
    const auto omegaIt = std::find_if(charts.begin(), charts.end(),
        [](const ChartInfo& c) { return c.id == "shattered_threshold_omega"; });
    REQUIRE(omegaIt != charts.end());
    const auto masterDiffIt = std::find_if(omegaIt->difficulties.begin(), omegaIt->difficulties.end(),
        [](const DifficultyInfo& d) { return d.name == "Master"; });
    REQUIRE(masterDiffIt != omegaIt->difficulties.end());
    REQUIRE_THAT(masterDiffIt->level, sakura::tests::Matchers::WithinAbs(15.0, 0.01));

    auto masterData = loader.LoadChartData(omegaIt->folderPath + "/" + masterDiffIt->chartFile);
    REQUIRE(masterData.has_value());
    PlaySession session;
    session.Initialize(*masterData, true);
    int endTime = 10000;
    for (const auto& kn : masterData->keyboardNotes)
        endTime = std::max(endTime, kn.time + kn.duration + 2000);
    for (const auto& mn : masterData->mouseNotes)
        endTime = std::max(endTime, mn.time + mn.sliderDuration + 2000);
    for (int t = 0; t <= endTime; t += 8)
    {
        SessionInput tick{};
        tick.time = t;
        tick.kind = InputKind::Tick;
        session.Submit(tick);
    }
    session.Finish();
    REQUIRE(session.Complete());
    REQUIRE(session.Score().IsAllPerfect());
    REQUIRE(session.Score().GetScore() == 1000000);
}
