#pragma once

#include <cstdint>

namespace sakura::game
{

struct GuidanceColor
{
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct ApproachGradient
{
    GuidanceColor startColor;
    GuidanceColor endColor;
};

GuidanceColor LerpGuidanceColor(const GuidanceColor& from,
                                const GuidanceColor& to,
                                float t);

float ComputeApproachColorBlend(int absDiffMs, int approachRangeMs);

ApproachGradient BuildApproachGradient(int noteTimeMs,
                                       int currentTimeMs,
                                       int approachRangeMs,
                                       const GuidanceColor& noteColor,
                                       const GuidanceColor& startAccent,
                                       const GuidanceColor& endAccent);

} // namespace sakura::game