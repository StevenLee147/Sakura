#include "chart_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sakura::game
{

int JudgmentCount(const ChartData& chart)
{
    int count = 0;
    for (const auto& note : chart.keyboardNotes)
        count += note.type == NoteType::Hold ? 2 : 1;
    for (const auto& note : chart.mouseNotes)
        count += 1 + (note.type == NoteType::Slider ? static_cast<int>(note.sliderPath.size()) : 0);
    return count;
}

int ChartEndTime(const ChartData& chart)
{
    int end = 0;
    for (const auto& note : chart.keyboardNotes)
        end = std::max(end, note.time + std::max(0, note.duration));
    for (const auto& note : chart.mouseNotes)
        end = std::max(end, note.time + std::max(0, note.sliderDuration));
    return end;
}

float ScrollDistance(const ChartData& chart, int fromMs, int toMs)
{
    if (fromMs > toMs) return -ScrollDistance(chart, toMs, fromMs);
    float speed = 1.0f;
    float distance = 0.0f;
    int cursor = fromMs;
    auto it = std::upper_bound(chart.svPoints.begin(), chart.svPoints.end(), fromMs,
        [](int time, const SVPoint& point) { return time < point.time; });
    if (it != chart.svPoints.begin()) speed = std::prev(it)->speed;
    for (; it != chart.svPoints.end() && it->time < toMs; ++it)
    {
        distance += static_cast<float>(it->time - cursor) * speed;
        cursor = it->time;
        speed = it->speed;
    }
    return distance + static_cast<float>(toMs - cursor) * speed;
}

std::string FormatDifficultyLevel(float level)
{
    if (!std::isfinite(level) || level < 0.0f)
        return "0";
    const int tenths = static_cast<int>(std::lround(level * 10.0f));
    if (tenths % 10 == 0)
        return std::to_string(tenths / 10);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

std::string FormatDifficultyBadge(std::string_view difficultyName, float level)
{
    return std::string(difficultyName) + "  ·  Lv." + FormatDifficultyLevel(level);
}

int ResolveMatchingDifficultyIndex(const ChartInfo& chart,
                                   std::string_view preferredName,
                                   float preferredLevel)
{
    if (chart.difficulties.empty())
        return 0;
    auto eqIgnoreCase = [](std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const unsigned char ca = static_cast<unsigned char>(a[i]);
            const unsigned char cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb)) return false;
        }
        return true;
    };
    if (!preferredName.empty())
    {
        for (int i = 0; i < static_cast<int>(chart.difficulties.size()); ++i)
        {
            if (eqIgnoreCase(chart.difficulties[i].name, preferredName))
                return i;
        }
    }
    int bestIdx = 0;
    float bestDiff = std::abs(chart.difficulties[0].level - preferredLevel);
    for (int i = 1; i < static_cast<int>(chart.difficulties.size()); ++i)
    {
        const float d = std::abs(chart.difficulties[i].level - preferredLevel);
        if (d < bestDiff)
        {
            bestDiff = d;
            bestIdx = i;
        }
    }
    return bestIdx;
}

bool RetryTapTracker::OnPress()
{
    if (m_timer <= 0.0f)
        m_count = 0;
    ++m_count;
    m_timer = m_windowSeconds;
    if (m_count >= m_requiredTaps)
    {
        Reset();
        return true;
    }
    return false;
}

void RetryTapTracker::Update(float dt)
{
    if (m_timer > 0.0f)
    {
        m_timer -= std::max(0.0f, dt);
        if (m_timer <= 0.0f)
            Reset();
    }
}

void RetryTapTracker::Reset()
{
    m_count = 0;
    m_timer = 0.0f;
}

std::vector<KeyboardChordGroup> FindKeyboardChordGroups(std::span<const KeyboardNote> notes,
                                                        int toleranceMs)
{
    std::vector<KeyboardChordGroup> groups;
    size_t i = 0;
    while (i < notes.size())
    {
        if (notes[i].isJudged)
        {
            ++i;
            continue;
        }
        const int anchorTime = notes[i].time;
        int minStd = 99;
        int maxStd = -1;
        bool hasThumb = false;
        int count = 0;
        size_t j = i;
        while (j < notes.size() && notes[j].time - anchorTime <= toleranceMs)
        {
            if (!notes[j].isJudged)
            {
                ++count;
                if (notes[j].lane == kThumbLaneIndex)
                    hasThumb = true;
                else if (notes[j].lane >= 0 && notes[j].lane < kStandardLaneCount)
                {
                    minStd = std::min(minStd, notes[j].lane);
                    maxStd = std::max(maxStd, notes[j].lane);
                }
            }
            ++j;
        }
        if (count >= 2)
        {
            KeyboardChordGroup g;
            g.timeMs        = anchorTime;
            g.hasStandard   = (maxStd >= 0);
            g.minLane       = g.hasStandard ? minStd : kThumbLaneIndex;
            g.maxLane       = g.hasStandard ? maxStd : kThumbLaneIndex;
            g.includesThumb = hasThumb;
            g.noteCount     = count;
            groups.push_back(g);
        }
        i = j;
    }
    return groups;
}

std::vector<CrossZoneSyncPair> FindCrossZoneSyncPairs(std::span<const KeyboardNote> kbNotes,
                                                      std::span<const MouseNote> msNotes,
                                                      int toleranceMs)
{
    std::vector<CrossZoneSyncPair> pairs;
    for (size_t mi = 0; mi < msNotes.size(); ++mi)
    {
        const auto& mn = msNotes[mi];
        if (mn.isJudged) continue;
        size_t bestKb = kbNotes.size();
        int bestDelta = toleranceMs + 1;
        for (size_t ki = 0; ki < kbNotes.size(); ++ki)
        {
            const auto& kn = kbNotes[ki];
            if (kn.isJudged) continue;
            if (mn.linkId > 0 && kn.linkId == mn.linkId)
            {
                bestKb = ki;
                bestDelta = 0;
                break;
            }
            const int delta = std::abs(kn.time - mn.time);
            if (delta <= toleranceMs && delta < bestDelta)
            {
                bestDelta = delta;
                bestKb = ki;
            }
        }
        if (bestKb < kbNotes.size())
            pairs.push_back({ bestKb, mi });
    }
    return pairs;
}

} // namespace sakura::game
