// tests/test_chart_loader_builtin.cpp — 官方测试谱面加载校验

#include "test_framework.h"

#include "game/chart_loader.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

using namespace sakura::game;

namespace
{

struct ExpectedDifficulty
{
    const char* chartId;
    const char* chartFile;
    const char* difficultyName;
    double      level;
    int         keyboardNotes;
    int         holdNotes;
    int         mouseNotes;
    int         minTimingPoints;
    int         minSvPoints;
    int         minSliderNotes;
};

constexpr std::array<const char*, 5> kOfficialChartIds{
    "tutorial_song",
    "spring_breeze",
    "cherry_blossom",
    "digital_dream",
    "sakura_storm",
};

constexpr std::array<ExpectedDifficulty, 9> kOfficialDifficulties{{
    { "tutorial_song",  "easy.json",   "Easy",   1.0,  20,  0,   0, 1, 0,  0 },
    { "spring_breeze",  "normal.json", "Normal", 3.0,  32,  4,   8, 1, 0,  0 },
    { "spring_breeze",  "hard.json",   "Hard",   5.5,  56, 11,  24, 1, 0,  1 },
    { "cherry_blossom", "normal.json", "Normal", 4.0,  38,  5,  12, 1, 0,  0 },
    { "cherry_blossom", "hard.json",   "Hard",   7.0,  65, 16,  35, 1, 0,  1 },
    { "cherry_blossom", "expert.json", "Expert",10.0, 125, 31,  75, 1, 4,  1 },
    { "digital_dream",  "hard.json",   "Hard",   8.5,  60, 60,  60, 1, 0, 60 },
    { "digital_dream",  "expert.json", "Expert",12.0, 160, 40,  90, 4, 3,  1 },
    { "sakura_storm",   "expert.json", "Expert",14.0, 200, 50, 120, 5, 7,  1 },
}};

const ChartInfo* FindChart(const std::vector<ChartInfo>& charts, const char* chartId)
{
    const auto it = std::find_if(charts.begin(), charts.end(),
        [chartId](const ChartInfo& info) { return info.id == chartId; });
    if (it == charts.end())
        return nullptr;
    return &(*it);
}

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

    for (const char* chartId : kOfficialChartIds)
    {
        const ChartInfo* chart = FindChart(charts, chartId);
        REQUIRE(chart != nullptr);
        REQUIRE(chart->folderPath == std::string(SAKURA_SOURCE_DIR) + "/resources/charts/" + chartId);
        REQUIRE(chart->version == 2);
        REQUIRE(chart->musicFile == "music.wav");
        REQUIRE(chart->coverFile == "cover.png");
        REQUIRE(std::filesystem::exists(std::filesystem::path(chart->folderPath) / chart->musicFile));
        REQUIRE(std::filesystem::exists(std::filesystem::path(chart->folderPath) / chart->coverFile));
    }

    const auto it = std::find_if(charts.begin(), charts.end(),
        [](const ChartInfo& info) { return info.id == "test-song"; });
    REQUIRE(it != charts.end());
}

TEST_CASE("官方测试谱面难度与数据满足 Step 4.2 要求", "[charts][load]")
{
    ChartLoader loader;
    auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");

    for (const auto& expected : kOfficialDifficulties)
    {
        const ChartInfo* chart = FindChart(charts, expected.chartId);
        REQUIRE(chart != nullptr);

        const DifficultyInfo* diff = FindDifficulty(*chart, expected.chartFile);
        REQUIRE(diff != nullptr);
        REQUIRE(diff->name == expected.difficultyName);
        REQUIRE_THAT(diff->level, sakura::tests::Matchers::WithinAbs(expected.level, 0.01));
        REQUIRE(diff->noteCount == expected.keyboardNotes);
        REQUIRE(diff->holdCount == expected.holdNotes);
        REQUIRE(diff->mouseNoteCount == expected.mouseNotes);

        auto data = loader.LoadChartData(chart->folderPath + "/" + diff->chartFile);
        REQUIRE(data.has_value());
        REQUIRE(loader.ValidateChartData(*data));
        REQUIRE(static_cast<int>(data->keyboardNotes.size()) == expected.keyboardNotes);
        REQUIRE(static_cast<int>(data->mouseNotes.size()) == expected.mouseNotes);
        REQUIRE(static_cast<int>(data->timingPoints.size()) >= expected.minTimingPoints);
        REQUIRE(static_cast<int>(data->svPoints.size()) >= expected.minSvPoints);

        const int holdCount = static_cast<int>(std::count_if(data->keyboardNotes.begin(), data->keyboardNotes.end(),
            [](const KeyboardNote& note) { return note.type == NoteType::Hold; }));
        const int sliderCount = static_cast<int>(std::count_if(data->mouseNotes.begin(), data->mouseNotes.end(),
            [](const MouseNote& note) { return note.type == NoteType::Slider; }));

        REQUIRE(holdCount == expected.holdNotes);
        REQUIRE(sliderCount >= expected.minSliderNotes);
        REQUIRE(static_cast<int>(data->keyboardNotes.size() + data->mouseNotes.size())
            == expected.keyboardNotes + expected.mouseNotes);
    }
}
