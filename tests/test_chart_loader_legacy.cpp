// tests/test_chart_loader_legacy.cpp — 兼容旧版 Drag 谱面加载

#include "test_framework.h"

#include "game/chart_loader.h"

#include <filesystem>
#include <fstream>

using namespace sakura::game;

TEST_CASE("旧版 Drag 键盘音符按 Hold 兼容加载", "[charts][legacy]")
{
    const auto baseDir = std::filesystem::temp_directory_path() / "sakura-chart-loader-legacy";
    std::filesystem::create_directories(baseDir);

    const auto chartPath = baseDir / "legacy_drag.json";
    std::ofstream ofs(chartPath);
    ofs << R"({
        "version": 2,
        "timing_points": [{ "time": 0, "bpm": 120.0, "time_signature": [4, 4] }],
        "sv_points": [],
        "keyboard_notes": [
            { "time": 1000, "lane": 2, "type": "drag", "duration": 720, "drag_to_lane": 3 }
        ],
        "mouse_notes": []
    })";
    ofs.close();

    ChartLoader loader;
    auto data = loader.LoadChartData(chartPath.string());
    REQUIRE(data.has_value());
    REQUIRE(data->keyboardNotes.size() == 1);
    REQUIRE(data->keyboardNotes[0].type == NoteType::Hold);
    REQUIRE(data->keyboardNotes[0].duration == 720);
}
