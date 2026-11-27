// tests/test_v230_ui_and_select_ux.cpp — v2.3.0 Song Select memory, Dual-Zone NPS, filters, SafeAspectLayout & UTF-8 mask unit tests

#include "test_framework.h"

#include "core/config.h"
#include "core/paths.h"
#include "core/safe_layout.h"
#include "game/chart.h"
#include "game/chart_utils.h"
#include "game/select_context.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sakura::core::Config;
using sakura::core::Paths;
using sakura::core::SafeAspectLayout;
using sakura::core::SafeNormRect;
using sakura::game::BuildPasswordMaskUtf8;
using sakura::game::ChartData;
using sakura::game::ChartInfo;
using sakura::game::ChartMatchesLevelRange;
using sakura::game::ComputeChartLamp;
using sakura::game::ComputeDualZoneNpsCurve;
using sakura::game::CountUtf8Codepoints;
using sakura::game::DifficultyInfo;
using sakura::game::GameResult;
using sakura::game::Grade;
using sakura::game::KeyboardNote;
using sakura::game::MatchesLampFilter;
using sakura::game::MouseNote;
using sakura::game::NoteType;
using sakura::game::PickRandomVisibleChart;
using sakura::game::SelectContextManager;
using sakura::game::SelectLamp;
using sakura::game::SelectLampFilter;
using sakura::game::SelectMemoryState;

namespace
{
std::vector<ChartInfo> BuildTestChartLibrary()
{
    std::vector<ChartInfo> charts;
    const char* ids[5] = {
        "distant_star",
        "moonlit_prelude",
        "sakura_awakening",
        "sakura_sprint",
        "shattered_threshold"
    };
    const float baseLevels[5] = { 3.5f, 4.5f, 5.0f, 6.5f, 10.0f };
    for (int i = 0; i < 5; ++i)
    {
        ChartInfo ci;
        ci.id     = ids[i];
        ci.title  = std::string("Song ") + ids[i];
        ci.artist = "Sakura Soundworks";
        ci.bpm    = 140.0f + static_cast<float>(i * 10);
        ci.difficulties = {
            DifficultyInfo{ "Normal", baseLevels[i], "normal.json" },
            DifficultyInfo{ "Hard",   baseLevels[i] + 3.0f, "hard.json" },
            DifficultyInfo{ "Expert", baseLevels[i] + 6.0f, "expert.json" }
        };
        charts.push_back(std::move(ci));
    }
    return charts;
}
} // namespace

TEST_CASE("SelectContextManager preserves last viewed chart across reloads and obscuring filters", "[select][v230]")
{
    std::error_code ec;
    const fs::path tempDir = fs::current_path() / "build" / "qa" / "test_v230_select_ctx";
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    SelectContextManager::ClearSessionState();
    auto& cfg = Config::GetInstance();
    const fs::path cfgPath = tempDir / "settings.json";
    REQUIRE(cfg.Load(cfgPath.string()));

    const auto charts = BuildTestChartLibrary();
    const std::vector<int> allVisible = { 0, 1, 2, 3, 4 };

    SelectMemoryState saved;
    saved.chartId          = "sakura_sprint";
    saved.difficultyIndex  = 2;
    saved.difficultyName   = "Expert";
    saved.collectionFilter = 1;
    saved.sortMode         = 2;
    saved.scrollPos        = 1.75f;
    saved.minLevel         = 5.0f;
    saved.maxLevel         = 14.0f;
    saved.lampFilter       = SelectLampFilter::FullComboOrAbove;
    SelectContextManager::Save(cfg, saved, true);

    // Simulate cold process restart loading from disk Config
    SelectContextManager::ClearSessionState();
    cfg.ResetToDefaults();
    REQUIRE(cfg.Load(cfgPath.string()));
    const SelectMemoryState loaded = SelectContextManager::Load(cfg);
    REQUIRE(loaded.chartId == "sakura_sprint");
    REQUIRE(loaded.difficultyIndex == 2);
    REQUIRE(loaded.difficultyName == "Expert");
    REQUIRE(loaded.collectionFilter == 1);
    REQUIRE(loaded.sortMode == 2);
    REQUIRE(std::abs(loaded.scrollPos - 1.75f) < 0.01f);
    REQUIRE(loaded.lampFilter == SelectLampFilter::FullComboOrAbove);

    // Must restore chart #3 (sakura_sprint) and diff #2 (Expert), NEVER overwritten by visible.front() (#0)
    const auto target = SelectContextManager::ResolveRestoredSelection(charts, allVisible, loaded);
    REQUIRE(target.chartIndex == 3);
    REQUIRE(target.difficultyIndex == 2);
    REQUIRE(!target.clearedObscuringFilters);

    // Simulate collection/lamp filter hiding chart #3 -> must clear obscuring filters and still select chart #3
    const std::vector<int> subsetVisible = { 0, 1 };
    const auto fallbackTarget = SelectContextManager::ResolveRestoredSelection(charts, subsetVisible, loaded);
    REQUIRE(fallbackTarget.chartIndex == 3);
    REQUIRE(fallbackTarget.difficultyIndex == 2);
    REQUIRE(fallbackTarget.clearedObscuringFilters);

    // Simulate difficulty array change where index 0 became removed -> still matches by difficultyName "Expert"
    auto modifiedCharts = charts;
    modifiedCharts[3].difficulties.erase(modifiedCharts[3].difficulties.begin()); // Hard(0), Expert(1)
    const auto byName = SelectContextManager::ResolveRestoredSelection(modifiedCharts, allVisible, loaded);
    REQUIRE(byName.chartIndex == 3);
    REQUIRE(byName.difficultyIndex == 1);

    // Simulate deleted chart -> safely falls back to visibleIndices.front()
    SelectMemoryState missingState = loaded;
    missingState.chartId = "deleted_custom_chart";
    const auto deletedFallback = SelectContextManager::ResolveRestoredSelection(charts, { 2, 4 }, missingState);
    REQUIRE(deletedFallback.chartIndex == 2);
    REQUIRE(deletedFallback.difficultyIndex == 0);

    fs::remove_all(tempDir, ec);
}

TEST_CASE("ComputeDualZoneNpsCurve accurately buckets keyboard and mouse note density", "[chart][v230]")
{
    ChartData emptyChart;
    const auto emptyNps = ComputeDualZoneNpsCurve(emptyChart, 24);
    REQUIRE(emptyNps.keyboardNps.size() == 24);
    REQUIRE(emptyNps.mouseNps.size() == 24);
    REQUIRE(emptyNps.peakTotalNps == 0.0f);
    REQUIRE(emptyNps.durationMs == 0);

    ChartData hybrid;
    for (int i = 0; i < 8; ++i)
    {
        KeyboardNote kn;
        kn.time = i * 250;
        kn.lane = i % 4;
        kn.type = NoteType::Tap;
        hybrid.keyboardNotes.push_back(kn);
    }
    KeyboardNote hold;
    hold.time     = 2000;
    hold.lane     = 4;
    hold.type     = NoteType::Hold;
    hold.duration = 2000; // ends at 4000ms
    hybrid.keyboardNotes.push_back(hold);

    MouseNote slider;
    slider.time           = 1000;
    slider.x              = 0.3f;
    slider.y              = 0.5f;
    slider.type           = NoteType::Slider;
    slider.sliderDuration = 1000;
    slider.sliderPath     = { { 0.5f, 0.5f }, { 0.7f, 0.5f } };
    hybrid.mouseNotes.push_back(slider);

    const auto curve = ComputeDualZoneNpsCurve(hybrid, 8);
    REQUIRE(curve.durationMs == 4000);
    REQUIRE(curve.keyboardNps.size() == 8);
    REQUIRE(curve.mouseNps.size() == 8);
    REQUIRE(curve.peakTotalNps > 3.5f);
    // 8 taps + 1 hold (head+tail = 2) + 1 slider (head + 2 waypoints = 3) = 13 events over 4.0s = 3.25 avg NPS
    REQUIRE(std::abs(curve.avgTotalNps - 3.25f) < 0.01f);
}

TEST_CASE("Lamp badges, level range filters, random roulette, SafeAspectLayout, and UTF-8 password mask", "[ui][v230]")
{
    // 1. Lamp computation & filter predicates
    REQUIRE(ComputeChartLamp(std::nullopt) == SelectLamp::Unplayed);
    GameResult clearRes; clearRes.score = 880000; clearRes.accuracy = 91.2f; clearRes.grade = Grade::A;
    REQUIRE(ComputeChartLamp(clearRes) == SelectLamp::Clear);
    GameResult fcRes = clearRes; fcRes.isFullCombo = true;
    REQUIRE(ComputeChartLamp(fcRes) == SelectLamp::FullCombo);
    GameResult apRes = fcRes; apRes.isAllPerfect = true;
    REQUIRE(ComputeChartLamp(apRes) == SelectLamp::AllPerfect);

    REQUIRE(MatchesLampFilter(SelectLamp::Unplayed, SelectLampFilter::Unplayed));
    REQUIRE(!MatchesLampFilter(SelectLamp::Clear, SelectLampFilter::Unplayed));
    REQUIRE(MatchesLampFilter(SelectLamp::Clear, SelectLampFilter::ClearOrAbove));
    REQUIRE(MatchesLampFilter(SelectLamp::FullCombo, SelectLampFilter::ClearOrAbove));
    REQUIRE(!MatchesLampFilter(SelectLamp::Clear, SelectLampFilter::FullComboOrAbove));
    REQUIRE(MatchesLampFilter(SelectLamp::AllPerfect, SelectLampFilter::FullComboOrAbove));
    REQUIRE(MatchesLampFilter(SelectLamp::AllPerfect, SelectLampFilter::AllPerfectOnly));

    // 2. Level range & deterministic F2 random roulette
    const auto charts = BuildTestChartLibrary();
    REQUIRE(ChartMatchesLevelRange(charts[3], 2, 12.0f, 13.0f)); // Expert is 12.5
    REQUIRE(!ChartMatchesLevelRange(charts[0], 0, 12.0f, 15.0f)); // max is 9.5

    const std::vector<int> vis = { 1, 3, 4 };
    for (std::uint64_t seed = 1; seed <= 25; ++seed)
    {
        const int picked = PickRandomVisibleChart(vis, 3, seed);
        REQUIRE((picked == 1 || picked == 4));
    }
    REQUIRE(PickRandomVisibleChart({ 2 }, 2, 99) == 2);

    // 3. SafeAspectLayout across 16:9, 16:10, 21:9, and 4K
    const auto lay1080 = SafeAspectLayout::Compute(1920, 1080);
    REQUIRE(std::abs(lay1080.safeViewport.x) < 1e-6f);
    REQUIRE(std::abs(lay1080.safeViewport.width - 1.0f) < 1e-6f);

    const auto lay16x10 = SafeAspectLayout::Compute(2560, 1600);
    REQUIRE(std::abs(lay16x10.safeViewport.x) < 1e-6f);
    REQUIRE(std::abs(lay16x10.safeViewport.width - 1.0f) < 1e-6f);

    const auto lay4k = SafeAspectLayout::Compute(3840, 2160);
    REQUIRE(std::abs(lay4k.safeViewport.width - 1.0f) < 1e-6f);

    const auto lay21x9 = SafeAspectLayout::Compute(3440, 1440);
    REQUIRE(lay21x9.safeViewport.x > 0.05f);
    REQUIRE(std::abs(lay21x9.safeViewport.x * 2.0f + lay21x9.safeViewport.width - 1.0f) < 1e-5f);
    const SafeNormRect mapped = lay21x9.Map({ 0.25f, 0.40f, 0.50f, 0.20f });
    const auto [ux, uy] = lay21x9.UnmapPoint(mapped.x, mapped.y);
    REQUIRE(std::abs(ux - 0.25f) < 1e-5f);
    REQUIRE(std::abs(uy - 0.40f) < 1e-5f);

    // 4. UTF-8 codepoint counting & password bullet masking
    REQUIRE(CountUtf8Codepoints("") == 0);
    REQUIRE(CountUtf8Codepoints("Sakura123!") == 10);
    REQUIRE(CountUtf8Codepoints("樱花Pass#230") == 10);
    const std::string masked = BuildPasswordMaskUtf8("樱花A1", "*");
    REQUIRE(masked == "****");
    const std::string bulletMasked = BuildPasswordMaskUtf8("樱花A1");
    REQUIRE(CountUtf8Codepoints(bulletMasked) == 4);
}
