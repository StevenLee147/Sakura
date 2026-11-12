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
    float trackW     = m_label.empty() ? m_bounds.width : m_bounds.width * 0.40f;
    float trackH     = m_bounds.height * 0.55f;
    float trackX     = m_bounds.x + labelAreaW;
    float trackY     = m_bounds.y + (m_bounds.height - trackH) * 0.5f;

    // 动画使用 EaseOutQuad
    float easedT = sakura::utils::EaseOutQuad(m_animT);

    // 轨道磨砂琉璃渐变与鎏金细边
    sakura::core::Color trackColor = LerpColor(m_offColor, m_onColor, easedT);
    sakura::core::Color trackRight = LerpColor(m_offColor, { 250, 216, 157, 232 }, easedT * 0.65f);
    const sakura::core::NormRect trackRect{ trackX, trackY, trackW, trackH };
    renderer.DrawRoundedGradientRect(
        trackRect, trackH * 0.5f,
        trackColor, trackRight, trackColor, trackRight, 12, true);
    sakura::core::Color borderTop = LerpColor({ 158, 168, 206, 86 }, { 255, 236, 204, 205 }, easedT);
    renderer.DrawRoundedRectOutlineGradient(
        trackRect, trackH * 0.5f,
        borderTop, { borderTop.r, borderTop.g, borderTop.b, static_cast<uint8_t>(borderTop.a * 0.45f) },
        0.0013f, 12);

    // 拇指（珍珠圆钮 + 激活泛光）
    float thumbRadius = trackH * 0.40f;
    float padding     = trackH * 0.10f;
    const float aspect = static_cast<float>(renderer.GetScreenHeight()) / std::max(1, renderer.GetScreenWidth());
    float thumbMinX   = trackX + (thumbRadius + padding) * aspect;
    float thumbMaxX   = trackX + trackW - (thumbRadius + padding) * aspect;
    float thumbX      = thumbMinX + easedT * (thumbMaxX - thumbMinX);
    float thumbY      = trackY + trackH * 0.5f;
    if (easedT > 0.05f)
    {
        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        renderer.DrawRadialGradient(
            thumbX, thumbY, thumbRadius * 1.9f, thumbRadius * 1.9f,
            { 252, 196, 218, static_cast<uint8_t>(90.0f * easedT) }, { 252, 196, 218, 0 }, 28);
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    }
    renderer.DrawCircleFilled(thumbX, thumbY, thumbRadius, m_thumbColor, 32);
    renderer.DrawCircleOutline(thumbX, thumbY, thumbRadius, { 250, 216, 157, static_cast<uint8_t>(110 + 110 * easedT) }, 0.0012f, 32);

    // 标签
    if (!m_label.empty() && m_fontHandle != 0)
    {
        renderer.DrawText(
            m_fontHandle, m_label,
            m_bounds.x,
            m_bounds.y + m_bounds.height * 0.5f - m_normFontSize * 0.5f,
            m_normFontSize,
            m_labelColor,
            sakura::core::TextAlign::Left);
    }
}

} // namespace sakura::ui
