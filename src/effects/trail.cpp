#include "trail.h"

#include <algorithm>

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
    // ring buffer：向前移动 head
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
    if (m_count < 2) return;

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    // 从最新点（head）向旧点方向遍历
    for (int i = 0; i < m_count - 1; ++i)
    {
        // i=0 最新（最亮），i=m_count-2 最旧（最暗）
        float t1 = 1.0f - static_cast<float>(i)     / static_cast<float>(m_count - 1);
        float t2 = 1.0f - static_cast<float>(i + 1) / static_cast<float>(m_count - 1);

        int idx1 = (m_head - i     + MAX_POINTS) % MAX_POINTS;
        int idx2 = (m_head - i - 1 + MAX_POINTS) % MAX_POINTS;

        // alpha 随位置线性衰减
        uint8_t a1 = static_cast<uint8_t>(baseColor.a * t1 * t1);
        uint8_t a2 = static_cast<uint8_t>(baseColor.a * t2 * t2);

        sakura::core::Color c1 = { baseColor.r, baseColor.g, baseColor.b, a1 };
        // 线段用前端颜色绘制（TODO: 若 DrawLine 支持渐变则更佳）
        renderer.DrawLine(m_points[idx1].x, m_points[idx1].y,
                          m_points[idx2].x, m_points[idx2].y,
                          c1, thickness);
        (void)c1; (void)a2; // 避免未使用警告
    }

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
