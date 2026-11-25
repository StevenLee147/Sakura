#pragma once

#include "chart.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sakura::game
{

// A hold has a head and a tail; a slider has a head and one judgment per waypoint.
int JudgmentCount(const ChartData& chart);
int ChartEndTime(const ChartData& chart);
// Integrate SV over the interval so notes remain continuous at speed changes.
float ScrollDistance(const ChartData& chart, int fromMs, int toMs);

// Consistent difficulty level & badge formatting across Select / Game / Result / Market.
std::string FormatDifficultyLevel(float level);
std::string FormatDifficultyBadge(std::string_view difficultyName, float level);

// Match preferred difficulty name (case-insensitive) or closest numeric level when switching songs.
int ResolveMatchingDifficultyIndex(const ChartInfo& chart,
                                   std::string_view preferredName,
                                   float preferredLevel);

// Anti-mispress multi-tap retry state machine (default: 3 presses within 1.5s).
class RetryTapTracker
{
public:
    explicit RetryTapTracker(int requiredTaps = 3, float windowSeconds = 1.5f)
        : m_requiredTaps(requiredTaps), m_windowSeconds(windowSeconds) {}

    bool OnPress();
    void Update(float dt);
    void Reset();

    int   GetCount() const { return m_count; }
    int   GetRequired() const { return m_requiredTaps; }
    int   GetRemaining() const { return m_count > 0 ? (m_requiredTaps - m_count) : m_requiredTaps; }
    float GetTimer() const { return m_timer; }

private:
    int   m_requiredTaps  = 3;
    float m_windowSeconds = 1.5f;
    int   m_count         = 0;
    float m_timer         = 0.0f;
};

struct KeyboardChordGroup
{
    int  timeMs        = 0;
    int  minLane       = 0;     // 0..3 standard lane min (or 4 if thumb-only)
    int  maxLane       = 0;     // 0..3 standard lane max
    bool hasStandard   = false;
    bool includesThumb = false;
    int  noteCount     = 0;
};

struct CrossZoneSyncPair
{
    size_t kbIndex = 0;
    size_t msIndex = 0;
};

std::vector<KeyboardChordGroup> FindKeyboardChordGroups(std::span<const KeyboardNote> notes,
                                                        int toleranceMs = 6);
std::vector<CrossZoneSyncPair> FindCrossZoneSyncPairs(std::span<const KeyboardNote> kbNotes,
                                                      std::span<const MouseNote> msNotes,
                                                      int toleranceMs = 8);

} // namespace sakura::game
