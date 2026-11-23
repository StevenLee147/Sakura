#include "test_framework.h"
#include "game/chart_loader.h"
#include "game/play_session.h"
#include "game/replay.h"
#include "utils/file_io.h"

#include <filesystem>
#include <nlohmann/json.hpp>

using namespace sakura::game;
using namespace sakura::tests::Matchers;

TEST_CASE("ChartLoader parses Chart v3 thumb bar, link_id and require_lane fields", "[v3][loader]")
{
    const auto dir = std::filesystem::temp_directory_path() / "sakura-v3-loader-test";
    std::filesystem::create_directories(dir);
    const auto chartPath = dir / "expert.json";

    const nlohmann::json doc = {
        {"version", 3},
        {"timing_points", {{{"time", 0}, {"bpm", 160.0}}}},
        {"keyboard_notes", {
            {{"time", 500}, {"lane", 4}, {"type", "tap"}},
            {{"time", 1000}, {"lane", 1}, {"type", "tap"}, {"link_id", 7}},
            {{"time", 1600}, {"lane", 4}, {"type", "hold"}, {"duration", 600}}
        }},
        {"mouse_notes", {
            {{"time", 1000}, {"x", 0.4f}, {"y", 0.5f}, {"type", "circle"}, {"link_id", 7}},
            {{"time", 2400}, {"x", 0.6f}, {"y", 0.4f}, {"type", "circle"}, {"require_lane", 4}},
            {{"time", 3000}, {"x", 0.5f}, {"y", 0.6f}, {"type", "circle"}, {"require_lane", 5}}
        }}
    };
    REQUIRE(sakura::utils::AtomicWrite(chartPath, doc.dump(2)));

    ChartLoader loader;
    auto loaded = loader.LoadChartData(chartPath.string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->version == 3);
    REQUIRE(loaded->keyboardNotes.size() == 3);
    REQUIRE(loaded->keyboardNotes[0].lane == kThumbLaneIndex);
    REQUIRE(loaded->keyboardNotes[1].linkId == 7);
    REQUIRE(loaded->keyboardNotes[2].lane == kThumbLaneIndex);
    REQUIRE(loaded->mouseNotes.size() == 3);
    REQUIRE(loaded->mouseNotes[0].linkId == 7);
    REQUIRE(loaded->mouseNotes[1].requireLane == kThumbLaneIndex);
    REQUIRE(loaded->mouseNotes[2].requireLane == kSynergyRightClick);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("PlaySession handles Spacebar thumb bar, Link resonance, Synergy modifiers and Replay v2", "[v3][session]")
{
    ChartData chart;
    chart.version = 3;
    chart.timingPoints = {{0, 150.0f, 4, 4}};
    chart.keyboardNotes = {
        {500, kThumbLaneIndex, NoteType::Tap, 0, 0},
        {1000, 2, NoteType::Tap, 0, 11},
        {1500, kThumbLaneIndex, NoteType::Hold, 500, 0}
    };
    MouseNote linkedCircle;
    linkedCircle.time = 1000; linkedCircle.x = 0.5f; linkedCircle.y = 0.5f; linkedCircle.linkId = 11;
    MouseNote spaceSynergy;
    spaceSynergy.time = 1700; spaceSynergy.x = 0.6f; spaceSynergy.y = 0.5f; spaceSynergy.requireLane = kThumbLaneIndex;
    MouseNote rmbSynergy;
    rmbSynergy.time = 2400; rmbSynergy.x = 0.4f; rmbSynergy.y = 0.4f; rmbSynergy.requireLane = kSynergyRightClick;
    chart.mouseNotes = {linkedCircle, spaceSynergy, rmbSynergy};

    PlaySession live;
    live.Initialize(chart, false);
    for (const auto& ev : std::vector<SessionInput>{
        {500, InputKind::LaneDown, kThumbLaneIndex},
        {530, InputKind::LaneUp, kThumbLaneIndex},
        {1000, InputKind::LaneDown, 2},
        {1004, InputKind::MouseDown, 0, 0.5f, 0.5f},
        {1030, InputKind::LaneUp, 2},
        {1035, InputKind::MouseUp},
        {1500, InputKind::LaneDown, kThumbLaneIndex},
        {1700, InputKind::MouseDown, 0, 0.6f, 0.5f},
        {1730, InputKind::MouseUp},
        {2005, InputKind::Tick},
        {2010, InputKind::LaneUp, kThumbLaneIndex},
        {2400, InputKind::RightMouseDown, 0, 0.4f, 0.4f},
        {2430, InputKind::RightMouseUp},
        {2600, InputKind::Tick}
    })
    {
        live.Submit(ev);
    }
    live.Finish();

    REQUIRE(live.Complete());
    REQUIRE(live.Score().GetScore() == 1000000);
    REQUIRE(live.Score().GetMissCount() == 0);
    REQUIRE(live.Score().GetThumbHitCount() == 3); // 1 thumb tap + 1 thumb hold head + 1 thumb hold tail
    REQUIRE(live.Score().GetSynergyCount() == 3);  // 1 link resonance + 1 space synergy + 1 RMB synergy

    // Missing a required modifier downgrades Perfect -> Great without crashing
    ChartData penaltyChart;
    penaltyChart.version = 3;
    penaltyChart.timingPoints = {{0, 120.0f, 4, 4}};
    MouseNote unheld;
    unheld.time = 1000; unheld.x = 0.5f; unheld.y = 0.5f; unheld.requireLane = kThumbLaneIndex;
    penaltyChart.mouseNotes = {unheld};
    PlaySession unheldSession;
    unheldSession.Initialize(penaltyChart, false);
    unheldSession.Submit({1000, InputKind::MouseDown, 0, 0.5f, 0.5f});
    unheldSession.Finish();
    REQUIRE(unheldSession.Score().GetGreatCount() == 1);
    REQUIRE(unheldSession.Score().GetSynergyCount() == 0);

    // Replay v2 round-trip determinism
    Replay rep;
    rep.chartId = "v3_fixture";
    rep.chartHash = Replay::Hash(chart);
    rep.inputs = live.Recording();
    const auto skrPath = std::filesystem::temp_directory_path() / "sakura-v3-replay.skr";
    REQUIRE(rep.Save(skrPath.string()));
    auto loadedRep = Replay::Load(skrPath.string());
    REQUIRE(loadedRep.has_value());

    auto chartCopy = chart;
    PlaySession playback;
    playback.Initialize(chartCopy, false);
    for (const auto& ev : loadedRep->inputs)
        playback.Submit(ev);
    playback.Finish();
    REQUIRE(playback.Score().GetScore() == live.Score().GetScore());
    REQUIRE(playback.Score().GetSynergyCount() == live.Score().GetSynergyCount());
    REQUIRE(playback.Score().GetThumbHitCount() == live.Score().GetThumbHitCount());

    std::error_code ec;
    std::filesystem::remove(skrPath, ec);
}

TEST_CASE("Sakura Bloom Fever activates deterministically and preserves 1,000,000 AP cap", "[v3][fever]")
{
    ScoreCalculator calc;
    calc.Initialize(30);
    for (int i = 0; i < 14; ++i)
        calc.OnJudge(JudgeResult::Perfect, 0, true, true, false); // 14 * 0.075 >= 1.0 -> triggers Fever
    REQUIRE(calc.IsFeverActive());
    REQUIRE(calc.GetFeverCount() == 1);
    for (int i = 14; i < 30; ++i)
        calc.OnJudge(JudgeResult::Perfect, 0, true, false, false);
    REQUIRE(calc.GetScore() == 1000000);
    REQUIRE(calc.GetFeverBonus() > 0);
    calc.AdvanceFeverMs(8500);
    REQUIRE(!calc.IsFeverActive());
}
