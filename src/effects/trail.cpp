#include "trail.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sakura::effects
{

// ============================================================================
// TrailEffect 实现
// ============================================================================

TrailEffect::TrailEffect()
{
    m_points.fill({ 0.0f, 0.0f });
}

void TrailEffect::AddPoint(float x, float y)
{
    if (m_count > 0)
    {
        const float dx = x - m_points[m_head].x;
        const float dy = y - m_points[m_head].y;
        if (dx * dx + dy * dy < 0.0000025f)
        {
            m_points[m_head] = { x, y };
            return;
        }
    }
    m_head = (m_head + 1) % MAX_POINTS;
    m_points[m_head] = { x, y };
    if (m_count < MAX_POINTS)
        ++m_count;
}

void TrailEffect::Clear()
{
    m_count = 0;
    m_head  = 0;
}

void TrailEffect::Render(sakura::core::Renderer& renderer,
                          sakura::core::Color baseColor,
                          float thickness) const
{
    if (m_count < 2 || baseColor.a == 0) return;

    static thread_local std::vector<sakura::core::NormPoint> ordered;
    ordered.clear();
    ordered.reserve(static_cast<size_t>(m_count));

    // Ordered from tail (oldest, index 0) -> head (newest, index m_count - 1)
    for (int i = m_count - 1; i >= 0; --i)
    {
        const int idx = (m_head - i + MAX_POINTS) % MAX_POINTS;
        ordered.push_back({ m_points[idx].x, m_points[idx].y });
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    sakura::core::Color tailCore{ baseColor.r, baseColor.g, baseColor.b, 0 };
    sakura::core::Color headCore{
        static_cast<uint8_t>(std::min(255, baseColor.r + 55)),
        static_cast<uint8_t>(std::min(255, baseColor.g + 55)),
        static_cast<uint8_t>(std::min(255, baseColor.b + 55)),
        baseColor.a
    };
    sakura::core::Color tailAura{ baseColor.r, baseColor.g, baseColor.b, 0 };
    sakura::core::Color headAura{ baseColor.r, baseColor.g, baseColor.b, 0 };

    renderer.DrawCatmullRomRibbon(
        ordered.data(),
        static_cast<int>(ordered.size()),
        thickness * 0.25f,
        thickness * 2.85f,
        tailCore,
        headCore,
        tailAura,
        headAura,
        5,
        true);

    // Subtle luminous pearl tip at the cursor/note head
    const auto& tip = ordered.back();
    renderer.DrawRadialGradient(
        tip.x, tip.y,
        thickness * 2.2f, thickness * 2.2f,
        headCore,
        { baseColor.r, baseColor.g, baseColor.b, 0 },
        24);

    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

// ============================================================================
// TrailManager 实现
// ============================================================================

int TrailManager::AllocTrail()
{
    for (int i = 0; i < MAX_TRAILS; ++i)
    {
        if (!m_active[i])
        {
            m_active[i] = true;
            m_trails[i].Clear();
            return i;
        }
    }
    return -1;
}

void TrailManager::FreeTrail(int id)
{
    if (id < 0 || id >= MAX_TRAILS) return;
    m_active[id] = false;
    m_trails[id].Clear();
}

void TrailManager::AddPoint(int id, float x, float y)
{
    if (id < 0 || id >= MAX_TRAILS || !m_active[id]) return;
    m_trails[id].AddPoint(x, y);
}

void TrailManager::ClearTrail(int id)
{
    if (id < 0 || id >= MAX_TRAILS) return;
    m_trails[id].Clear();
}

void TrailManager::RenderAll(sakura::core::Renderer& renderer,
                              sakura::core::Color baseColor,
                              float thickness) const
{
    for (int i = 0; i < MAX_TRAILS; ++i)
    {
        if (m_active[i])
            m_trails[i].Render(renderer, baseColor, thickness);
    }
}

void TrailManager::Render(int id, sakura::core::Renderer& renderer,
                           sakura::core::Color baseColor,
                           float thickness) const
{
    if (id < 0 || id >= MAX_TRAILS || !m_active[id]) return;
    m_trails[id].Render(renderer, baseColor, thickness);
}

} // namespace sakura::effects
