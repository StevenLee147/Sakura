#pragma once

// layout.h — 归一化布局与度量工具

#include "renderer.h"
#include "safe_layout.h"

#include <algorithm>
#include <vector>

namespace sakura::core
{

struct EdgeInsets
{
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
};

class Layout
{
public:
    static float ShortSideRatio(const Renderer& renderer)
    {
        const int width  = renderer.GetScreenWidth();
        const int height = renderer.GetScreenHeight();
        if (height <= 0) return 1.0f;
        return static_cast<float>(std::min(width, height)) / static_cast<float>(height);
    }

    static float FontSizeFromShortSide(const Renderer& renderer, float normShortSideSize)
    {
        return normShortSideSize * ShortSideRatio(renderer);
    }

    static NormRect Inset(NormRect rect, EdgeInsets insets)
    {
        const float width  = std::max(0.0f, rect.width  - insets.left - insets.right);
        const float height = std::max(0.0f, rect.height - insets.top  - insets.bottom);
        return { rect.x + insets.left, rect.y + insets.top, width, height };
    }

    static NormRect Centered(NormRect container, float width, float height)
    {
        return {
            container.x + (container.width  - width)  * 0.5f,
            container.y + (container.height - height) * 0.5f,
            width,
            height
        };
    }

    static std::vector<NormRect> StackVertical(NormRect area,
                                               int count,
                                               float itemHeight,
                                               float gap,
                                               float topPadding = 0.0f)
    {
        std::vector<NormRect> result;
        if (count <= 0) return result;

        result.reserve(static_cast<std::size_t>(count));
        float y = area.y + topPadding;
        for (int index = 0; index < count; ++index)
        {
            result.push_back({ area.x, y, area.width, itemHeight });
            y += itemHeight + gap;
        }
        return result;
    }

    static SafeAspectLayout ComputeSafeAspect(const Renderer& renderer,
                                              float maxInteractiveAspect = 18.0f / 9.0f)
    {
        return SafeAspectLayout::Compute(
            renderer.GetScreenWidth(), renderer.GetScreenHeight(), maxInteractiveAspect);
    }

    static NormRect MapSafe(const SafeAspectLayout& sa, NormRect localRect)
    {
        const SafeNormRect mapped = sa.Map({ localRect.x, localRect.y, localRect.width, localRect.height });
        return { mapped.x, mapped.y, mapped.width, mapped.height };
    }
};

} // namespace sakura::core
