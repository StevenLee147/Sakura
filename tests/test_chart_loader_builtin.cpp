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
    int         minTimingPoints;
    int         minSvPoints;
};

struct ExpectedChart
{
    const char* chartId;
    const char* musicFile;
};

constexpr std::array<ExpectedChart, 6> kOfficialCharts{{
    { "tutorial_song",  "music.wav" },
    { "spring_breeze",  "music.wav" },
    { "cherry_blossom", "music.wav" },
    { "digital_dream",  "music.wav" },
    { "sakura_storm",   "music.wav" },
    { "sakura_breath",  "music.ogg" },
}};

constexpr std::array<ExpectedDifficulty, 12> kOfficialDifficulties{{
    { "tutorial_song",  "easy.json",   "Easy",    1.0, 1, 0 },
    { "spring_breeze",  "normal.json", "Normal",  3.0, 1, 0 },
    { "spring_breeze",  "hard.json",   "Hard",    5.5, 1, 0 },
    { "cherry_blossom", "normal.json", "Normal",  4.0, 1, 0 },
    { "cherry_blossom", "hard.json",   "Hard",    7.0, 1, 0 },
    { "cherry_blossom", "expert.json", "Expert", 10.0, 1, 3 },
    { "digital_dream",  "hard.json",   "Hard",    8.5, 1, 0 },
    { "digital_dream",  "expert.json", "Expert", 12.0, 1, 3 },
    { "sakura_storm",   "expert.json", "Expert", 14.0, 1, 3 },
    { "sakura_breath",  "normal.json", "Normal",  4.0, 1, 0 },
    { "sakura_breath",  "hard.json",   "Hard",    7.5, 1, 0 },
    { "sakura_breath",  "expert.json", "Expert", 10.5, 1, 4 },
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

    for (const auto& expected : kOfficialCharts)
    {
        const ChartInfo* chart = FindChart(charts, expected.chartId);
        REQUIRE(chart != nullptr);
        REQUIRE(chart->folderPath == std::string(SAKURA_SOURCE_DIR) + "/resources/charts/" + expected.chartId);
        REQUIRE((chart->version == 2 || chart->version == 3));
        REQUIRE(chart->musicFile == expected.musicFile);
        REQUIRE(chart->coverFile == "cover.png");
        REQUIRE(std::filesystem::exists(std::filesystem::path(chart->folderPath) / chart->musicFile));
        REQUIRE(std::filesystem::exists(std::filesystem::path(chart->folderPath) / chart->coverFile));
    }

    const auto it = std::find_if(charts.begin(), charts.end(),
        [](const ChartInfo& info) { return info.id == "test-song"; });
    REQUIRE(it != charts.end());
}

TEST_CASE("樱落呼吸以正式曲名收录并在曲库中提供完整预览", "[charts][scan]")
{
    ChartLoader loader;
    auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");
    const ChartInfo* chart = FindChart(charts, "sakura_breath");
    REQUIRE(chart != nullptr);
    REQUIRE(chart->title == "樱落呼吸");
    REQUIRE_THAT(chart->bpm, sakura::tests::Matchers::WithinAbs(178.0, 0.001));
    REQUIRE(chart->offset == 0);
    REQUIRE(chart->previewTime > 0);
    REQUIRE(chart->difficulties.size() == 3);
}

TEST_CASE("内置谱面难度与音符元数据保持一致", "[charts][load]")
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

        auto data = loader.LoadChartData(chart->folderPath + "/" + diff->chartFile);
        REQUIRE(data.has_value());
        REQUIRE(loader.ValidateChartData(*data));
        REQUIRE(diff->noteCount > 0);
        REQUIRE(static_cast<int>(data->keyboardNotes.size()) == diff->noteCount);
        REQUIRE(static_cast<int>(data->mouseNotes.size()) == diff->mouseNoteCount);
        REQUIRE(static_cast<int>(data->timingPoints.size()) >= expected.minTimingPoints);
        REQUIRE(static_cast<int>(data->svPoints.size()) >= expected.minSvPoints);

        const int holdCount = static_cast<int>(std::count_if(data->keyboardNotes.begin(), data->keyboardNotes.end(),
            [](const KeyboardNote& note) { return note.type == NoteType::Hold; }));
        const int sliderCount = static_cast<int>(std::count_if(data->mouseNotes.begin(), data->mouseNotes.end(),
            [](const MouseNote& note) { return note.type == NoteType::Slider; }));

        REQUIRE(holdCount == diff->holdCount);
        if (chart->id != "tutorial_song")
        {
            REQUIRE(holdCount > 0);
            REQUIRE(sliderCount > 0);
        }
        REQUIRE(static_cast<int>(data->keyboardNotes.size() + data->mouseNotes.size())
            == diff->noteCount + diff->mouseNoteCount);
    }
}

TEST_CASE("Official Vol.03 Chart v3 song pack loads with thumb bar and synergy notes", "[charts][v3]")
{
    constexpr std::array<const char*, 8> kV3PackIds{{
        "sakura_awakening", "starry_sky", "distant_star", "unfallen_light",
        "sidechain_dream", "flowing_petals", "instant_bloom", "petal_storm_overdrive"
    }};
    ChartLoader loader;
    auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");
    REQUIRE(charts.size() >= 15);

    for (const char* id : kV3PackIds)
    {
        const ChartInfo* chart = FindChart(charts, id);
        REQUIRE(chart != nullptr);
        REQUIRE(chart->version == 3);
        REQUIRE(chart->musicFile == "music.ogg");
        REQUIRE(std::filesystem::exists(std::filesystem::path(chart->folderPath) / chart->musicFile));
        REQUIRE(std::filesystem::exists(std::filesystem::path(chart->folderPath) / chart->coverFile));
        REQUIRE(chart->difficulties.size() >= 2);

        for (const auto& diff : chart->difficulties)
        {
            auto data = loader.LoadChartData(chart->folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(data->version == 3);
            REQUIRE(static_cast<int>(data->keyboardNotes.size()) == diff.noteCount);
            REQUIRE(static_cast<int>(data->mouseNotes.size()) == diff.mouseNoteCount);

            const int thumbs = static_cast<int>(std::count_if(data->keyboardNotes.begin(), data->keyboardNotes.end(),
                [](const KeyboardNote& n) { return n.lane == kThumbLaneIndex; }));
            const int synergies = static_cast<int>(std::count_if(data->mouseNotes.begin(), data->mouseNotes.end(),
                [](const MouseNote& n) { return n.linkId > 0 || n.requireLane >= 0; }));
            REQUIRE(thumbs == diff.thumbCount);
            REQUIRE(thumbs > 0);
            REQUIRE(synergies == diff.synergyCount);
            if (diff.name == "Hard" || diff.name == "Expert")
                REQUIRE(synergies > 0);
        }
    }
}
