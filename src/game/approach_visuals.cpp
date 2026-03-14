#include "approach_visuals.h"

#include <algorithm>
#include <cmath>

namespace sakura::game
{

namespace
{

uint8_t LerpChannel(uint8_t from, uint8_t to, float t)
{
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const float value = static_cast<float>(from)
        + (static_cast<float>(to) - static_cast<float>(from)) * clamped;
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

} // namespace

GuidanceColor LerpGuidanceColor(const GuidanceColor& from,
                                const GuidanceColor& to,
                                float t)
{
    return {
        LerpChannel(from.r, to.r, t),
        LerpChannel(from.g, to.g, t),
        LerpChannel(from.b, to.b, t),
        LerpChannel(from.a, to.a, t)
    };
}

float ComputeApproachColorBlend(int absDiffMs, int approachRangeMs)
{
    if (absDiffMs == 0) return 1.0f;

    const int safeRange = std::max(1, approachRangeMs);
    const float progress = 1.0f - std::clamp(
        static_cast<float>(absDiffMs) / static_cast<float>(safeRange),
        0.0f,
        1.0f);
    const float eased = 1.0f - std::pow(1.0f - progress, 1.8f);
    return std::min(0.92f, 0.12f + eased * 0.80f);
}

ApproachGradient BuildApproachGradient(int noteTimeMs,
                                       int currentTimeMs,
                                       int approachRangeMs,
                                       const GuidanceColor& noteColor,
                                       const GuidanceColor& startAccent,
                                       const GuidanceColor& endAccent)
{
    const int absDiffMs = std::abs(noteTimeMs - currentTimeMs);
    const float blend = ComputeApproachColorBlend(absDiffMs, approachRangeMs);

    return {
        LerpGuidanceColor(startAccent, noteColor, blend),
        LerpGuidanceColor(endAccent, noteColor, blend)
    };
}

} // namespace sakura::game