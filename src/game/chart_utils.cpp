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

StageVisualState EvaluateStageVisuals(const ChartData& chart, int nowMs, bool reducedMotion)
{
    constexpr float kPi = 3.14159265358979323846f;
    StageVisualState state;
    float bestTintRaw = 0.0f;

    for (const auto& ev : chart.stageEvents)
    {
        const int span = std::max(1, ev.duration);
        if (nowMs < ev.time || nowMs > ev.time + span)
            continue;
        if (!std::isfinite(ev.intensity) || ev.intensity <= 0.0f)
            continue;

        const float progress = std::clamp(static_cast<float>(nowMs - ev.time) / static_cast<float>(span), 0.0f, 1.0f);
        if (ev.type == "pulse")
        {
            const float inv = 1.0f - progress;
            const float env = inv * inv * inv;
            if (!reducedMotion)
                state.pulseZoom = std::max(state.pulseZoom, std::clamp(ev.intensity * env, 0.0f, 1.0f));
        }
        else
        {
            const float env = std::clamp(std::sin(progress * kPi), 0.0f, 1.0f);
            if (ev.type == "vignette")
            {
                state.vignetteBoost = std::max(state.vignetteBoost, std::clamp(ev.intensity * env, 0.0f, 1.0f));
            }
            else if (ev.type == "tint")
            {
                const float rawTint = ev.intensity * env;
                if (rawTint > bestTintRaw)
                {
                    bestTintRaw = rawTint;
                    state.tintStrength = std::clamp(rawTint, 0.0f, 1.0f);
                    state.tintR = std::clamp(ev.colorR, 0, 255);
                    state.tintG = std::clamp(ev.colorG, 0, 255);
                    state.tintB = std::clamp(ev.colorB, 0, 255);
                }
            }
            else if (ev.type == "petal_burst")
            {
                state.petalRateMult = std::max(
                    state.petalRateMult,
                    1.0f + std::clamp(ev.intensity, 0.0f, 3.0f) * env * 2.5f);
            }
        }
    }

    if (reducedMotion)
        state.pulseZoom = 0.0f;
    return state;
}

ActiveLyricState EvaluateActiveLyric(const ChartData& chart, int nowMs)
{
    ActiveLyricState state;
    const LyricLine* best = nullptr;
    for (const auto& line : chart.lyrics)
    {
        if (line.time <= nowMs && nowMs <= line.endTime && !line.text.empty())
        {
            if (!best || line.time >= best->time)
                best = &line;
        }
    }
    if (!best)
        return state;

    state.active  = true;
    state.text    = best->text;
    state.subText = best->subText;

    const int span      = std::max(0, best->endTime - best->time);
    const int fadeInMs  = std::min(180, span / 3);
    const int fadeOutMs = std::min(220, span / 3);
    const int elapsed   = nowMs - best->time;
    const int remaining = best->endTime - nowMs;

    float opacity = 1.0f;
    if (fadeInMs > 0 && elapsed < fadeInMs)
    {
        const float t = std::clamp(static_cast<float>(elapsed) / static_cast<float>(fadeInMs), 0.0f, 1.0f);
        opacity = t * t * (3.0f - 2.0f * t);
    }
    else if (fadeOutMs > 0 && remaining < fadeOutMs)
    {
        const float t = std::clamp(static_cast<float>(remaining) / static_cast<float>(fadeOutMs), 0.0f, 1.0f);
        opacity = t * t * (3.0f - 2.0f * t);
    }

    state.opacity = std::clamp(opacity, 0.0f, 1.0f);
    return state;
}

DualZoneNpsProfile ComputeDualZoneNpsCurve(const ChartData& chart, int bucketCount)
{
    DualZoneNpsProfile profile;
    const int buckets = std::max(1, bucketCount);
    profile.keyboardNps.assign(static_cast<std::size_t>(buckets), 0.0f);
    profile.mouseNps.assign(static_cast<std::size_t>(buckets), 0.0f);

    const int rawEndMs = ChartEndTime(chart);
    profile.durationMs = rawEndMs;
    if (chart.keyboardNotes.empty() && chart.mouseNotes.empty())
        return profile;

    const int spanMs      = std::max(1000, rawEndMs);
    const float bucketSec = static_cast<float>(spanMs) / (1000.0f * static_cast<float>(buckets));
    if (bucketSec <= 0.0f)
        return profile;

    auto addEventAt = [&](std::vector<float>& target, int timeMs)
    {
        const int clamped = std::clamp(timeMs, 0, spanMs - 1);
        const int idx     = std::clamp(
            static_cast<int>((static_cast<long long>(clamped) * buckets) / spanMs),
            0,
            buckets - 1);
        target[static_cast<std::size_t>(idx)] += 1.0f;
    };

    int totalEvents = 0;
    for (const auto& kn : chart.keyboardNotes)
    {
        addEventAt(profile.keyboardNps, kn.time);
        ++totalEvents;
        if (kn.type == NoteType::Hold && kn.duration > 0)
        {
            addEventAt(profile.keyboardNps, kn.time + kn.duration);
            ++totalEvents;
        }
    }

    for (const auto& mn : chart.mouseNotes)
    {
        addEventAt(profile.mouseNps, mn.time);
        ++totalEvents;
        if (mn.type == NoteType::Slider && !mn.sliderPath.empty())
        {
            const std::size_t wpCount = mn.sliderPath.size();
            const int dur = std::max(0, mn.sliderDuration);
            for (std::size_t w = 0; w < wpCount; ++w)
            {
                const int wpTime = mn.time + static_cast<int>(
                    (static_cast<long long>(dur) * static_cast<long long>(w + 1)) /
                    static_cast<long long>(wpCount));
                addEventAt(profile.mouseNps, wpTime);
                ++totalEvents;
            }
        }
    }

    float peak = 0.0f;
    for (int i = 0; i < buckets; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        profile.keyboardNps[idx] /= bucketSec;
        profile.mouseNps[idx]    /= bucketSec;
        peak = std::max(peak, profile.keyboardNps[idx] + profile.mouseNps[idx]);
    }
    profile.peakTotalNps = peak;
    profile.avgTotalNps  = static_cast<float>(totalEvents) / (static_cast<float>(spanMs) / 1000.0f);
    return profile;
}

} // namespace sakura::game
