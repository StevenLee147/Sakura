// toggle.cpp — 开关 UI 组件实现

#include "toggle.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

Toggle::Toggle(sakura::core::NormRect bounds,
               bool initValue,
               sakura::core::FontHandle fontHandle,
               float normFontSize)
    : UIBase(bounds)
    , m_isOn(initValue)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
    , m_animT(initValue ? 1.0f : 0.0f)
{
}

// ── 颜色插值 ──────────────────────────────────────────────────────────────────

sakura::core::Color Toggle::LerpColor(sakura::core::Color a,
                                       sakura::core::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t)
    };
}

// ── SetOn ─────────────────────────────────────────────────────────────────────

void Toggle::SetOn(bool on, bool animate)
{
    m_isOn = on;
    if (!animate)
    {
        m_animT = on ? 1.0f : 0.0f;
    }
}

// ── Update ────────────────────────────────────────────────────────────────────

void Toggle::Update(float dt)
{
    if (!m_isVisible || !m_isEnabled) return;

    float target  = m_isOn ? 1.0f : 0.0f;
    float delta   = (target - m_animT) * (dt / ANIM_DURATION);
    m_animT       = std::clamp(m_animT + delta, 0.0f, 1.0f);
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool Toggle::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 1)
    {
        auto [mx, my] = sakura::core::Input::GetMousePosition();
        if (HitTest(mx, my))
        {
            m_wasPressed = true;
            m_isOn = !m_isOn;
            if (m_onChange) m_onChange(m_isOn);
            return true;
        }
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == 1)
    {
        m_wasPressed = false;
    }

    return false;
}

// ── Render ────────────────────────────────────────────────────────────────────

void Toggle::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    // 轨道区域（右侧，约占组件宽度 40%）
    float labelAreaW = m_label.empty() ? 0.0f : m_bounds.width * 0.60f;
    float trackW     = m_bounds.width  * 0.40f;
    float trackH     = m_bounds.height * 0.55f;
    float trackX     = m_bounds.x + labelAreaW;
    float trackY     = m_bounds.y + (m_bounds.height - trackH) * 0.5f;

    // 动画使用 EaseOutQuad
    float easedT = sakura::utils::EaseOutQuad(m_animT);

    // 轨道颜色（插值 off→on）
    sakura::core::Color trackColor = LerpColor(m_offColor, m_onColor, easedT);
    renderer.DrawRoundedRect(
        { trackX, trackY, trackW, trackH },
        trackH * 0.5f,
        trackColor,
        true);

    // 拇指（圆形，沿轨道滑动）
    float thumbRadius = trackH * 0.42f;
    float padding     = trackH * 0.08f;
    float thumbMinX   = trackX + thumbRadius + padding;
    float thumbMaxX   = trackX + trackW - thumbRadius - padding;
    float thumbX      = thumbMinX + easedT * (thumbMaxX - thumbMinX);
    float thumbY      = trackY + trackH * 0.5f;

    renderer.DrawCircleFilled(thumbX, thumbY, thumbRadius, m_thumbColor);

    // 标签
    if (!m_label.empty() && m_fontHandle != 0)
    {
        renderer.DrawText(
            m_fontHandle, m_label,
            m_bounds.x,
            m_bounds.y + m_bounds.height * 0.5f,
            m_normFontSize,
            m_labelColor,
            sakura::core::TextAlign::Left);
    }
}

} // namespace sakura::ui
