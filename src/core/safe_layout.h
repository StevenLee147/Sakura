#pragma once

// safe_layout.h — 跨宽高比（16:9 / 16:10 / 21:9 / 32:9 / 4K）安全视口映射（零 SDL 依赖）

#include <algorithm>
#include <cmath>
#include <utility>

namespace sakura::core
{

struct SafeNormRect
{
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 1.0f;
    float height = 1.0f;
};

struct SafeAspectLayout
{
    SafeNormRect fullViewport = { 0.0f, 0.0f, 1.0f, 1.0f };
    SafeNormRect safeViewport = { 0.0f, 0.0f, 1.0f, 1.0f };
    float        aspectRatio  = 16.0f / 9.0f;

    static SafeAspectLayout Compute(int screenWidth,
                                    int screenHeight,
                                    float maxInteractiveAspect = 18.0f / 9.0f)
    {
        SafeAspectLayout out;
        const float w = static_cast<float>(std::max(1, screenWidth));
        const float h = static_cast<float>(std::max(1, screenHeight));
        out.aspectRatio = w / h;
        out.fullViewport = { 0.0f, 0.0f, 1.0f, 1.0f };

        const float clampedMax = std::max(1.0f, maxInteractiveAspect);
        if (out.aspectRatio > clampedMax + 1e-4f)
        {
            const float safeW = std::clamp(clampedMax / out.aspectRatio, 0.1f, 1.0f);
            out.safeViewport = {
                (1.0f - safeW) * 0.5f,
                0.0f,
                safeW,
                1.0f
            };
        }
        else
        {
            out.safeViewport = { 0.0f, 0.0f, 1.0f, 1.0f };
        }
        return out;
    }

    SafeNormRect Map(SafeNormRect localNorm) const
    {
        return {
            safeViewport.x + localNorm.x * safeViewport.width,
            safeViewport.y + localNorm.y * safeViewport.height,
            localNorm.width * safeViewport.width,
            localNorm.height * safeViewport.height
        };
    }

    std::pair<float, float> UnmapPoint(float screenNormX, float screenNormY) const
    {
        const float safeW = std::max(1e-6f, safeViewport.width);
        const float safeH = std::max(1e-6f, safeViewport.height);
        return {
            (screenNormX - safeViewport.x) / safeW,
            (screenNormY - safeViewport.y) / safeH
        };
    }
};

} // namespace sakura::core
