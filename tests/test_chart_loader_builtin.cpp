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

    REQUIRE(charts.size() >= 6);

    const std::array<std::string, 5> expectedIds = {
        "tutorial_song",
        "spring_breeze",
        "cherry_blossom",
        "digital_dream",
        "sakura_storm",
    };

    for (const auto& chartId : expectedIds)
    {
        auto it = std::find_if(charts.begin(), charts.end(),
            [&](const ChartInfo& info) { return info.id == chartId; });
        REQUIRE(it != charts.end());
    }
}

TEST_CASE("官方测试谱面难度与数据满足 Step 4.2 要求", "[charts][load]")
{
    ChartLoader loader;
    auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");

    const std::array<ExpectedDifficulty, 9> expected = {{
        { "tutorial_song",   "easy.json",   1.0,  20,  0,   0, 1, 0 },
        { "spring_breeze",   "normal.json", 3.0,  34,  4,   6, 1, 0 },
        { "spring_breeze",   "hard.json",   5.5,  56,  6,  24, 1, 0 },
        { "cherry_blossom",  "normal.json", 4.0,  40,  4,  10, 1, 0 },
        { "cherry_blossom",  "hard.json",   7.0,  76,  6,  24, 1, 0 },
        { "cherry_blossom",  "expert.json", 10.0, 156, 10,  50, 1, 5 },
        { "digital_dream",   "hard.json",   8.5,  60,  0,  60, 1, 0 },
        { "digital_dream",   "expert.json", 12.0, 168, 12,  84, 4, 0 },
        { "sakura_storm",    "expert.json", 14.0, 216, 17, 108, 5, 8 },
    }};

    for (const auto& item : expected)
    {
        auto chartIt = std::find_if(charts.begin(), charts.end(),
            [&](const ChartInfo& info) { return info.id == item.chartId; });
        REQUIRE(chartIt != charts.end());

        const DifficultyInfo* diff = FindDifficulty(*chartIt, item.chartFile);
        REQUIRE(diff != nullptr);
        REQUIRE_THAT(diff->level, sakura::tests::Matchers::WithinAbs(item.level, 0.01));
        REQUIRE(diff->noteCount == item.keyboardNotes);
        REQUIRE(diff->holdCount == item.holdNotes);
        REQUIRE(diff->mouseNoteCount == item.mouseNotes);

        auto data = loader.LoadChartData(chartIt->folderPath + "/" + diff->chartFile);
        REQUIRE(data.has_value());
        REQUIRE(loader.ValidateChartData(*data));
        REQUIRE(static_cast<int>(data->keyboardNotes.size()) == item.keyboardNotes);
        REQUIRE(static_cast<int>(data->mouseNotes.size()) == item.mouseNotes);
        REQUIRE(static_cast<int>(std::count_if(data->keyboardNotes.begin(), data->keyboardNotes.end(),
            [](const KeyboardNote& note) { return note.type == NoteType::Hold; })) == item.holdNotes);
        REQUIRE(static_cast<int>(data->timingPoints.size()) >= item.minTimingPoints);
        REQUIRE(static_cast<int>(data->svPoints.size()) >= item.minSvPoints);

        if (std::string(item.chartId) == "digital_dream" && std::string(item.chartFile) == "hard.json")
        {
            REQUIRE(std::all_of(data->keyboardNotes.begin(), data->keyboardNotes.end(),
                [](const KeyboardNote& note) { return note.type == NoteType::Drag; }));
            REQUIRE(std::all_of(data->mouseNotes.begin(), data->mouseNotes.end(),
                [](const MouseNote& note) { return note.type == NoteType::Slider; }));
        }
    }
}