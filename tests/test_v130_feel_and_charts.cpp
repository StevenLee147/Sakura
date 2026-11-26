// tests/test_v130_feel_and_charts.cpp — v1.3.0 手感升级、难度一致性与全曲库可玩性审计测试

#include "test_framework.h"

#include "game/chart_loader.h"
#include "game/chart_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace sakura::game;
using namespace sakura::tests::Matchers;

TEST_CASE("FormatDifficultyLevel and FormatDifficultyBadge format integer and decimal ratings", "[v130][difficulty]")
{
    REQUIRE(FormatDifficultyLevel(3.0f) == "3");
    REQUIRE(FormatDifficultyLevel(7.5f) == "7.5");
    REQUIRE(FormatDifficultyLevel(14.5f) == "14.5");
    REQUIRE(FormatDifficultyLevel(10.0f) == "10");
    REQUIRE(FormatDifficultyLevel(10.02f) == "10");
    REQUIRE(FormatDifficultyLevel(-2.0f) == "0");
    REQUIRE(FormatDifficultyLevel(std::numeric_limits<float>::quiet_NaN()) == "0");

    REQUIRE(FormatDifficultyBadge("Normal", 3.0f) == "Normal  ·  Lv.3");
    REQUIRE(FormatDifficultyBadge("Hard", 7.5f) == "Hard  ·  Lv.7.5");
    REQUIRE(FormatDifficultyBadge("Expert", 14.5f) == "Expert  ·  Lv.14.5");
}

TEST_CASE("ResolveMatchingDifficultyIndex prioritizes case-insensitive name then closest level", "[v130][difficulty]")
{
    ChartInfo emptyChart;
    REQUIRE(ResolveMatchingDifficultyIndex(emptyChart, "Hard", 7.5f) == 0);

    ChartInfo chart;
    chart.difficulties = {
        DifficultyInfo{ "Easy",   2.5f, "easy.json" },
        DifficultyInfo{ "Normal", 5.0f, "normal.json" },
        DifficultyInfo{ "Hard",   8.5f, "hard.json" },
        DifficultyInfo{ "Expert", 12.5f, "expert.json" },
    };

    REQUIRE(ResolveMatchingDifficultyIndex(chart, "hard", 2.5f) == 2);
    REQUIRE(ResolveMatchingDifficultyIndex(chart, "EXPERT", 3.0f) == 3);
    REQUIRE(ResolveMatchingDifficultyIndex(chart, "eAsY", 12.5f) == 0);

    REQUIRE(ResolveMatchingDifficultyIndex(chart, "Master", 11.8f) == 3);
    REQUIRE(ResolveMatchingDifficultyIndex(chart, "Challenge", 7.2f) == 2);
    REQUIRE(ResolveMatchingDifficultyIndex(chart, "Beginner", 4.4f) == 1);
    REQUIRE(ResolveMatchingDifficultyIndex(chart, "", 2.0f) == 0);
}

TEST_CASE("RetryTapTracker requires 3 presses within 1.5s and expires after timeout", "[v130][retry]")
{
    RetryTapTracker tracker(3, 1.5f);
    REQUIRE(tracker.GetCount() == 0);
    REQUIRE(tracker.GetRemaining() == 3);

    REQUIRE(!tracker.OnPress());
    REQUIRE(tracker.GetCount() == 1);
    REQUIRE(tracker.GetRemaining() == 2);

    tracker.Update(0.6f);
    REQUIRE(!tracker.OnPress());
    REQUIRE(tracker.GetCount() == 2);
    REQUIRE(tracker.GetRemaining() == 1);

    tracker.Update(0.8f);
    REQUIRE(tracker.OnPress());
    REQUIRE(tracker.GetCount() == 0);
    REQUIRE(tracker.GetRemaining() == 3);
    REQUIRE_THAT(tracker.GetTimer(), WithinAbs(0.0, 1e-5));

    REQUIRE(!tracker.OnPress());
    REQUIRE(tracker.GetRemaining() == 2);
    REQUIRE(!tracker.OnPress());
    REQUIRE(tracker.GetRemaining() == 1);
    tracker.Update(1.6f);
    REQUIRE(tracker.GetCount() == 0);
    REQUIRE(tracker.GetRemaining() == 3);

    REQUIRE(!tracker.OnPress());
    REQUIRE(tracker.GetCount() == 1);
    REQUIRE(tracker.GetRemaining() == 2);
}

TEST_CASE("FindKeyboardChordGroups and FindCrossZoneSyncPairs detect simultaneous notes", "[v130][chords][sync]")
{
    KeyboardNote singleTap{ 200, 1, NoteType::Tap, 0, 0 };
    KeyboardNote chord2A{ 500, 0, NoteType::Tap, 0, 0 };
    KeyboardNote chord2B{ 505, 3, NoteType::Tap, 0, 0 };
    KeyboardNote chord3A{ 900, 0, NoteType::Tap, 0, 0 };
    KeyboardNote chord3B{ 903, 2, NoteType::Tap, 0, 0 };
    KeyboardNote chord3C{ 906, 3, NoteType::Tap, 0, 0 };
    KeyboardNote thumbStd{ 1300, 1, NoteType::Tap, 0, 0 };
    KeyboardNote thumbBar{ 1304, kThumbLaneIndex, NoteType::Hold, 400, 0 };
    KeyboardNote wideGapA{ 1700, 0, NoteType::Tap, 0, 0 };
    KeyboardNote wideGapB{ 1709, 2, NoteType::Tap, 0, 0 };
    KeyboardNote judgedA{ 2100, 1, NoteType::Tap, 0, 0 };
    judgedA.isJudged = true;
    KeyboardNote unjudgedB{ 2102, 3, NoteType::Tap, 0, 0 };

    const std::vector<KeyboardNote> kbNotes = {
        singleTap,
        chord2A, chord2B,
        chord3A, chord3B, chord3C,
        thumbStd, thumbBar,
        wideGapA, wideGapB,
        judgedA, unjudgedB,
    };

    const auto groups = FindKeyboardChordGroups(kbNotes, 6);
    REQUIRE(groups.size() == 3);

    REQUIRE(groups[0].timeMs == 500);
    REQUIRE(groups[0].hasStandard);
    REQUIRE(groups[0].minLane == 0);
    REQUIRE(groups[0].maxLane == 3);
    REQUIRE(!groups[0].includesThumb);
    REQUIRE(groups[0].noteCount == 2);

    REQUIRE(groups[1].timeMs == 900);
    REQUIRE(groups[1].hasStandard);
    REQUIRE(groups[1].minLane == 0);
    REQUIRE(groups[1].maxLane == 3);
    REQUIRE(!groups[1].includesThumb);
    REQUIRE(groups[1].noteCount == 3);

    REQUIRE(groups[2].timeMs == 1300);
    REQUIRE(groups[2].hasStandard);
    REQUIRE(groups[2].minLane == 1);
    REQUIRE(groups[2].maxLane == 1);
    REQUIRE(groups[2].includesThumb);
    REQUIRE(groups[2].noteCount == 2);

    KeyboardNote syncKb0{ 1000, 0, NoteType::Tap, 0, 0 };
    KeyboardNote syncKb1{ 1500, 2, NoteType::Tap, 0, 0 };
    KeyboardNote syncKbFar{ 2000, 1, NoteType::Tap, 0, 0 };
    KeyboardNote syncKbJudged{ 2500, 3, NoteType::Tap, 0, 0 };
    syncKbJudged.isJudged = true;
    KeyboardNote syncKbLinked{ 3000, kThumbLaneIndex, NoteType::Tap, 0, 9 };

    MouseNote syncMs0; syncMs0.time = 1007; syncMs0.x = 0.4f; syncMs0.y = 0.5f; syncMs0.linkId = 0;
    MouseNote syncMs1; syncMs1.time = 1492; syncMs1.x = 0.6f; syncMs1.y = 0.4f; syncMs1.linkId = 0;
    MouseNote syncMsFar; syncMsFar.time = 2011; syncMsFar.x = 0.5f; syncMsFar.y = 0.5f; syncMsFar.linkId = 0;
    MouseNote syncMsUnpaired; syncMsUnpaired.time = 2500; syncMsUnpaired.x = 0.3f; syncMsUnpaired.y = 0.6f; syncMsUnpaired.linkId = 0;
    MouseNote syncMsLinked; syncMsLinked.time = 3000; syncMsLinked.x = 0.7f; syncMsLinked.y = 0.3f; syncMsLinked.linkId = 9;

    const std::vector<KeyboardNote> syncKb = { syncKb0, syncKb1, syncKbFar, syncKbJudged, syncKbLinked };
    const std::vector<MouseNote> syncMs = { syncMs0, syncMs1, syncMsFar, syncMsUnpaired, syncMsLinked };

    const auto pairs = FindCrossZoneSyncPairs(syncKb, syncMs, 8);
    REQUIRE(pairs.size() == 3);
    REQUIRE(pairs[0].kbIndex == 0);
    REQUIRE(pairs[0].msIndex == 0);
    REQUIRE(pairs[1].kbIndex == 1);
    REQUIRE(pairs[1].msIndex == 1);
    REQUIRE(pairs[2].kbIndex == 4);
    REQUIRE(pairs[2].msIndex == 4);
}

TEST_CASE("Built-in chart library passes metadata parity and mouse spatial playability audit", "[v130][charts][audit]")
{
    ChartLoader loader;
    const auto charts = loader.ScanCharts(std::string(SAKURA_SOURCE_DIR) + "/resources/charts");
    REQUIRE(!charts.empty());

    for (const auto& chart : charts)
    {
        REQUIRE(!chart.difficulties.empty());
        for (const auto& diff : chart.difficulties)
        {
            const auto data = loader.LoadChartData(chart.folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(loader.ValidateChartData(*data));

            const int kbCount = static_cast<int>(data->keyboardNotes.size());
            const int holdCount = static_cast<int>(std::count_if(
                data->keyboardNotes.begin(), data->keyboardNotes.end(),
                [](const KeyboardNote& n) { return n.type == NoteType::Hold; }));
            const int msCount = static_cast<int>(data->mouseNotes.size());

            REQUIRE(kbCount == diff.noteCount);
            REQUIRE(holdCount == diff.holdCount);
            REQUIRE(msCount == diff.mouseNoteCount);

            if (data->mouseNotes.size() < 2)
                continue;

            double longDistSum  = 0.0;
            int    longCount    = 0;
            double shortDistSum = 0.0;
            int    shortCount   = 0;

            const auto& ms = data->mouseNotes;
            for (size_t i = 1; i < ms.size(); ++i)
            {
                const auto& prev = ms[i - 1];
                const auto& curr = ms[i];

                const int prevEndTime = prev.time +
                    (prev.type == NoteType::Slider ? std::max(0, prev.sliderDuration) : 0);
                float prevEndX = prev.x;
                float prevEndY = prev.y;
                if (prev.type == NoteType::Slider && !prev.sliderPath.empty())
                {
                    prevEndX = prev.sliderPath.back().first;
                    prevEndY = prev.sliderPath.back().second;
                }

                const int dt = curr.time - prevEndTime;
                const double jumpDist = std::hypot(static_cast<double>(curr.x - prevEndX),
                                                   static_cast<double>(curr.y - prevEndY));
                if (dt >= 900)
                {
                    longDistSum += jumpDist;
                    ++longCount;
                }
                else if (dt <= 350)
                {
                    shortDistSum += jumpDist;
                    ++shortCount;
                }

                for (size_t j = 0; j < i; ++j)
                {
                    if (std::abs(curr.time - ms[j].time) <= 400)
                    {
                        const double headDist = std::hypot(static_cast<double>(curr.x - ms[j].x),
                                                           static_cast<double>(curr.y - ms[j].y));
                        REQUIRE(headDist >= 0.08 - 1e-4);
                    }
                }
            }

            if (longCount > 0)
            {
                const double avgLong = longDistSum / static_cast<double>(longCount);
                if (shortCount > 0)
                {
                    const double avgShort = shortDistSum / static_cast<double>(shortCount);
                    REQUIRE(avgLong > avgShort || avgLong >= 0.22);
                }
                else
                {
                    REQUIRE(avgLong >= 0.22);
                }
            }
        }
    }
}
