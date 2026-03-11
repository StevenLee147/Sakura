// tests/test_chart_loader_builtin.cpp — 官方测试谱面加载校验

#include "test_framework.h"

#include "game/chart_loader.h"

#include <algorithm>
#include <array>
#include <string>

using namespace sakura::game;

namespace
{

struct ExpectedDifficulty
{
    const char* chartId;
    const char* chartFile;
    double      level;
    int         keyboardNotes;
    int         holdNotes;
    int         mouseNotes;
    int         minTimingPoints;
    int         minSvPoints;
};

const DifficultyInfo* FindDifficulty(const ChartInfo& info, const char* chartFile)
{
    for (const auto& diff : info.difficulties)
    {
        if (diff.chartFile == chartFile)
            return &diff;
    }
    return nullptr;
}

} // namespace

TEST_CASE("官方测试谱面可被扫描到", "[charts][scan]")
{
    ChartLoader loader;
    auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");

    REQUIRE(charts.size() == 1);

    const auto it = std::find_if(charts.begin(), charts.end(),
        [](const ChartInfo& info) { return info.id == "test-song"; });
    REQUIRE(it != charts.end());
    REQUIRE(it->folderPath == std::string(SAKURA_SOURCE_DIR) + "/resources/charts/test-song");
    REQUIRE(it->difficulties.size() == 1);
}

TEST_CASE("官方测试谱面难度与数据满足 Step 4.2 要求", "[charts][load]")
{
    ChartLoader loader;
    auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");

    const auto chartIt = std::find_if(charts.begin(), charts.end(),
        [](const ChartInfo& info) { return info.id == "test-song"; });
    REQUIRE(chartIt != charts.end());

    const DifficultyInfo* diff = FindDifficulty(*chartIt, "normal.json");
    REQUIRE(diff != nullptr);
    REQUIRE_THAT(diff->level, sakura::tests::Matchers::WithinAbs(5.0, 0.01));
    REQUIRE(diff->noteCount == 13);
    REQUIRE(diff->holdCount == 2);
    REQUIRE(diff->mouseNoteCount == 3);

    auto data = loader.LoadChartData(chartIt->folderPath + "/" + diff->chartFile);
    REQUIRE(data.has_value());
    REQUIRE(loader.ValidateChartData(*data));
    REQUIRE(static_cast<int>(data->keyboardNotes.size()) == 13);
    REQUIRE(static_cast<int>(data->mouseNotes.size()) == 3);
    REQUIRE(static_cast<int>(std::count_if(data->keyboardNotes.begin(), data->keyboardNotes.end(),
        [](const KeyboardNote& note) { return note.type == NoteType::Hold; })) == 2);
    REQUIRE(static_cast<int>(data->timingPoints.size()) == 1);
    REQUIRE(static_cast<int>(data->svPoints.size()) == 0);
    REQUIRE(std::count_if(data->mouseNotes.begin(), data->mouseNotes.end(),
        [](const MouseNote& note) { return note.type == NoteType::Circle; }) == 2);
    REQUIRE(std::count_if(data->mouseNotes.begin(), data->mouseNotes.end(),
        [](const MouseNote& note) { return note.type == NoteType::Slider; }) == 1);
}
