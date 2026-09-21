#include "test_framework.h"
#include "game/play_session.h"
#include "game/replay.h"
#include "game/chart_loader.h"
#include "audio/fft.h"

#include <filesystem>
#include <fstream>
#include <numbers>

using namespace sakura::game;
using namespace sakura::tests::Matchers;

namespace
{
ChartData Fixture()
{
    ChartData chart;
    chart.timingPoints.push_back({0, 120, 4, 4});
    chart.keyboardNotes = {{1000, 0, NoteType::Tap, 0}, {1500, 1, NoteType::Hold, 1000}};
    MouseNote circle;
    circle.time = 1200; circle.x = 0.4f; circle.y = 0.4f;
    MouseNote slider;
    slider.time = 2000; slider.type = NoteType::Slider;
    slider.sliderDuration = 1000;
    slider.sliderPath = {{0.6f, 0.5f}, {0.7f, 0.5f}};
    chart.mouseNotes = {circle, slider};
    return chart;
}
}

TEST_CASE("Session counts hold tails and slider waypoints exactly once", "[session]")
{
    auto chart = Fixture();
    PlaySession session;
    session.Initialize(chart, true);
    REQUIRE(session.Total() == 7);
    for (int time = 0; time <= 3300; time += 7) session.Submit({time, InputKind::Tick});
    session.Finish();
    REQUIRE(session.Complete());
    REQUIRE(session.Score().GetPerfectCount() == 7);
    REQUIRE(session.Score().GetScore() == 1000000);
    REQUIRE(session.Score().GetHitErrors().size() == 4);
    session.Finish();
    REQUIRE(session.Score().GetJudgedCount() == 7);
}

TEST_CASE("A missed hold or slider consumes all its scoring units", "[session]")
{
    auto chart = Fixture();
    PlaySession session;
    session.Initialize(chart);
    session.Submit({5000, InputKind::Tick});
    session.Finish();
    REQUIRE(session.Score().GetMissCount() == 7);
    REQUIRE(session.Score().GetScore() == 0);
}

TEST_CASE("Early release cannot earn a hold tail and replays reproduce actual input", "[session][replay]")
{
    auto chart = Fixture();
    PlaySession original;
    original.Initialize(chart);
    for (const auto& input : std::vector<SessionInput>{
        {1008, InputKind::LaneDown, 0}, {1020, InputKind::LaneUp, 0},
        {1200, InputKind::MouseDown, 0, 0.4f, 0.4f}, {1220, InputKind::MouseUp},
        {1490, InputKind::LaneDown, 1}, {1600, InputKind::Tick}, {1700, InputKind::LaneUp, 1},
        {1800, InputKind::Tick}, {2000, InputKind::MouseDown, 0, 0.5f, 0.5f},
        {2500, InputKind::Pointer, 0, 0.6f, 0.5f}, {2500, InputKind::Tick},
        {3000, InputKind::Pointer, 0, 0.7f, 0.5f}, {3000, InputKind::Tick}, {3300, InputKind::Tick}})
        original.Submit(input);
    original.Finish();
    REQUIRE(original.Score().GetMissCount() == 1);
    REQUIRE(original.Score().GetJudgedCount() == 7);
    Replay replay;
    replay.chartId = "fixture";
    replay.chartHash = Replay::Hash(chart);
    replay.inputs = original.Recording();
    const auto path = std::filesystem::temp_directory_path() / "sakura-session-test.skr";
    REQUIRE(replay.Save(path.string()));
    auto loaded = Replay::Load(path.string());
    REQUIRE(loaded.has_value());
    auto replayChart = Fixture();
    REQUIRE(loaded->chartHash == Replay::Hash(replayChart));
    PlaySession player;
    player.Initialize(replayChart);
    for (const auto& input : loaded->inputs) player.Submit(input);
    player.Finish();
    REQUIRE(player.Score().GetScore() == original.Score().GetScore());
    REQUIRE(player.Score().GetMissCount() == original.Score().GetMissCount());
    REQUIRE(player.Score().GetHitErrors() == original.Score().GetHitErrors());
    std::filesystem::remove(path);
}

TEST_CASE("Every shipped chart can finish in demonstration with a perfect normalized score", "[content][session]")
{
    ChartLoader loader;
    const auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");
    REQUIRE(charts.size() >= 5);
    for (const auto& info : charts)
        for (const auto& difficulty : info.difficulties)
        {
            auto chart = loader.LoadChartData(info.folderPath + "/" + difficulty.chartFile);
            REQUIRE(chart.has_value());
            PlaySession session;
            session.Initialize(*chart, true);
            for (int time = 0; time <= ChartEndTime(*chart) + 300; time += 17)
                session.Submit({time, InputKind::Tick});
            session.Finish();
            REQUIRE(session.Complete());
            REQUIRE(session.Score().GetMissCount() == 0);
            REQUIRE(session.Score().GetScore() == 1000000);
        }
}

TEST_CASE("SV integration stays continuous at changes", "[scroll]")
{
    ChartData chart;
    chart.svPoints = {{0, 1.0f, "linear"}, {1000, 2.0f, "linear"}};
    REQUIRE_THAT(ScrollDistance(chart, 500, 1500), WithinAbs(1500, 0.01));
    REQUIRE_THAT(ScrollDistance(chart, 999, 1500) - ScrollDistance(chart, 1000, 1500), WithinAbs(1, 0.01));
    REQUIRE_THAT(ScrollDistance(chart, 1500, 500), WithinAbs(-1500, 0.01));
}

TEST_CASE("FFT resolves a known sinusoid without leaking into neighboring bins", "[audio]")
{
    std::array<std::complex<float>, 1024> signal{};
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = std::sin(2.0f * std::numbers::pi_v<float> * 32.0f * static_cast<float>(i) / 1024.0f);
    sakura::audio::FFT(signal);
    REQUIRE_THAT(std::abs(signal[32]), WithinAbs(512, 0.1));
    REQUIRE(std::abs(signal[31]) < 0.01f);
}

TEST_CASE("Practice counts only complete notes inside the selected interval", "[session][practice]")
{
    auto chart=Fixture();PlaySession session;session.Initialize(chart,true,1100,2600);
    REQUIRE(session.Total()==3); // circle, hold head and tail; crossing slider excluded
    for(int t=1100;t<=2600;t+=5)session.Submit({t,InputKind::Tick});
    session.Finish();REQUIRE(session.Score().GetPerfectCount()==3);
    REQUIRE(session.Score().GetMissCount()==0);REQUIRE(session.Score().GetScore()==1000000);
}

TEST_CASE("Mouse hit radius matches the playfield aspect ratio", "[session][input]")
{
    ChartData chart;chart.timingPoints={{0,120,4,4}};
    MouseNote note;note.type=NoteType::Circle;note.time=1000;note.x=note.y=0.5f;chart.mouseNotes={note};
    PlaySession session;session.Initialize(chart);session.SetPointerScale(2,1);
    session.Submit({1000,InputKind::MouseDown,0,0.56f,0.5f});
    REQUIRE(session.Score().GetJudgedCount()==0);
    session.Submit({1000,InputKind::MouseUp});session.Submit({1000,InputKind::MouseDown,0,0.53f,0.5f});
    REQUIRE(session.Score().GetPerfectCount()==1);
}
TEST_CASE("Window resize is recorded and timing edits invalidate a replay", "[session][replay]")
{
    auto chart=Fixture();const auto hash=Replay::Hash(chart);
    chart.timingPoints[0].bpm+=1;REQUIRE(Replay::Hash(chart)!=hash);chart.timingPoints[0].bpm-=1;
    REQUIRE(Replay::Hash(chart,25)!=hash);
    ChartData target;target.timingPoints={{0,120,4,4}};
    MouseNote circle;circle.type=NoteType::Circle;circle.time=1000;circle.x=circle.y=0.5f;
    target.mouseNotes.push_back(circle);circle.time=2000;target.mouseNotes.push_back(circle);
    PlaySession live;live.Initialize(target);
    live.Submit({0,InputKind::PointerScale,0,2,1});
    live.Submit({1000,InputKind::MouseDown,0,0.55f,0.5f}); // outside the circle before resize
    REQUIRE(live.Score().GetJudgedCount()==0);
    live.Submit({1010,InputKind::MouseUp});live.Submit({1400,InputKind::Tick});
    live.Submit({1800,InputKind::PointerScale,0,1,2});
    live.Submit({2000,InputKind::MouseDown,0,0.55f,0.5f}); // same displacement now inside
    live.Finish();
    REQUIRE(live.Score().GetPerfectCount()==1);REQUIRE(live.Score().GetMissCount()==1);
    auto duplicate=target;PlaySession replay;replay.Initialize(duplicate);
    for(auto input:live.Recording())replay.Submit(input);replay.Finish();
    REQUIRE(replay.Score().GetScore()==live.Score().GetScore());
    REQUIRE(replay.Score().GetMissCount()==live.Score().GetMissCount());
}

TEST_CASE("A hold can be re-engaged during resume grace without replay divergence", "[session][pause]")
{
    ChartData chart;chart.timingPoints={{0,120,4,4}};chart.keyboardNotes={{1000,0,NoteType::Hold,1000}};
    PlaySession session;session.Initialize(chart);
    for(auto input:std::vector<SessionInput>{{1000,InputKind::LaneDown,0},{1400,InputKind::Tick},{1400,InputKind::LaneUp,0},
        {1400,InputKind::Resume},{1450,InputKind::Tick},{1470,InputKind::LaneDown,0},{2000,InputKind::Tick}})session.Submit(input);
    REQUIRE(session.Score().GetScore()==1000000);
    auto copy=chart;PlaySession replay;replay.Initialize(copy);for(auto input:session.Recording())replay.Submit(input);
    REQUIRE(replay.Score().GetScore()==session.Score().GetScore());
}

TEST_CASE("Starter charts have no overlapping holds or simultaneous mouse requirements", "[content]")
{
    ChartLoader loader;
    for(const auto& info:loader.ScanCharts("resources/charts")) {
        if(info.source.find("Sakura Originals")==std::string::npos)continue;
        for(const auto& difficulty:info.difficulties) {
            auto chart=loader.LoadChartData(info.folderPath+"/"+difficulty.chartFile);REQUIRE(chart.has_value());
            std::array<int,4> ends{-1,-1,-1,-1};
            for(const auto& note:chart->keyboardNotes){REQUIRE(note.time>ends[note.lane]);ends[note.lane]=note.time+note.duration;}
            int end=-1;for(const auto& note:chart->mouseNotes){REQUIRE(note.time>end);end=note.time+note.sliderDuration;}
            REQUIRE(ChartEndTime(*chart)>45000);
        }
    }
}
