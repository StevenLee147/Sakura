#pragma once

// ui_base.h — UI 组件基类

#include "core/renderer.h"
#include <SDL3/SDL.h>

namespace sakura::ui
{

// UIBase — 所有 UI 组件的基类
// 所有坐标均为归一化坐标（0.0~1.0）
class UIBase
{
public:
    explicit UIBase(sakura::core::NormRect bounds)
        : m_bounds(bounds) {}
    virtual ~UIBase() = default;

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    virtual void Update(float dt) = 0;
    virtual void Render(sakura::core::Renderer& renderer) = 0;
    virtual bool HandleEvent(const SDL_Event& event) { (void)event; return false; }

    // ── 属性 ──────────────────────────────────────────────────────────────────

    sakura::core::NormRect GetBounds()   const { return m_bounds; }
    void SetBounds(sakura::core::NormRect bounds) { m_bounds = bounds; }

    bool  IsVisible()   const { return m_isVisible; }
    bool  IsEnabled()   const { return m_isEnabled; }
    float GetOpacity()  const { return m_opacity; }

    void SetVisible(bool visible) { m_isVisible = visible; }
    void SetEnabled(bool enabled) { m_isEnabled = enabled; }
    void SetOpacity(float opacity)
    {
        m_opacity = std::max(0.0f, std::min(1.0f, opacity));
    }

    // ── 命中测试 ──────────────────────────────────────────────────────────────

    // 检查归一化坐标点是否在此组件范围内
    bool HitTest(float normX, float normY) const
    {
        return normX >= m_bounds.x
            && normX <= m_bounds.x + m_bounds.width
            && normY >= m_bounds.y
            && normY <= m_bounds.y + m_bounds.height;
    }

protected:
    sakura::core::NormRect m_bounds;
    bool  m_isVisible = true;
    bool  m_isEnabled = true;
    float m_opacity   = 1.0f;
};

} // namespace sakura::ui
