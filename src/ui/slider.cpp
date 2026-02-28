// slider.cpp — 滑块 UI 组件实现

#include "slider.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

Slider::Slider(sakura::core::NormRect bounds,
               float minValue,
               float maxValue,
               float initValue,
               float step,
               sakura::core::FontHandle fontHandle,
               float normFontSize)
    : UIBase(bounds)
    , m_minValue(minValue)
    , m_maxValue(maxValue)
    , m_value(std::clamp(initValue, minValue, maxValue))
    , m_step(step)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
{
}

// ── 属性 ──────────────────────────────────────────────────────────────────────

void Slider::SetValue(float value)
{
    float clamped = std::clamp(value, m_minValue, m_maxValue);
    if (m_step > 0.0f)
    {
        clamped = std::round((clamped - m_minValue) / m_step) * m_step + m_minValue;
        clamped = std::clamp(clamped, m_minValue, m_maxValue);
    }
    m_value = clamped;
}

void Slider::SetRange(float minVal, float maxVal)
{
    m_minValue = minVal;
    m_maxValue = maxVal;
    m_value    = std::clamp(m_value, minVal, maxVal);
}

// ── 辅助 ──────────────────────────────────────────────────────────────────────

float Slider::GetNormT() const
{
    if (m_maxValue <= m_minValue) return 0.0f;
    return (m_value - m_minValue) / (m_maxValue - m_minValue);
}

void Slider::UpdateValueFromMouseX(float mouseNormX)
{
    // 根据标签/数值文字保留的空间计算轨道范围
    float labelAreaW = m_label.empty()   ? 0.0f : m_bounds.width * 0.30f;
    float valueAreaW = !m_showValue      ? 0.0f : m_bounds.width * 0.12f;
    float trackX   = m_bounds.x + labelAreaW;
    float trackW   = m_bounds.width - labelAreaW - valueAreaW;

    float t = (mouseNormX - trackX) / trackW;
    t = std::clamp(t, 0.0f, 1.0f);
    float newValue = m_minValue + t * (m_maxValue - m_minValue);

    // 吸附到 step
    if (m_step > 0.0f)
    {
        newValue = std::round((newValue - m_minValue) / m_step) * m_step + m_minValue;
        newValue = std::clamp(newValue, m_minValue, m_maxValue);
    }

    if (std::abs(newValue - m_value) > 1e-6f)
    {
        m_value = newValue;
        if (m_onChange) m_onChange(m_value);
    }
}

std::string Slider::FormatValue(float v) const
{
    if (m_formatter) return m_formatter(v);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

// ── Update ────────────────────────────────────────────────────────────────────

void Slider::Update(float dt)
{
    if (!m_isVisible || !m_isEnabled) return;

    // 拇指动画
    float target = (m_isHovered || m_isDragging) ? 1.0f : 0.0f;
    float delta  = (target - m_thumbAnim) * (dt / THUMB_ANIM_DUR);
    m_thumbAnim  = std::clamp(m_thumbAnim + delta, 0.0f, 1.0f);
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool Slider::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    auto [mx, my] = sakura::core::Input::GetMousePosition();

    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        m_isHovered = HitTest(mx, my);
        if (m_isDragging) UpdateValueFromMouseX(mx);
        return m_isDragging;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 1)
    {
        if (HitTest(mx, my))
        {
            m_isDragging = true;
            UpdateValueFromMouseX(mx);
            return true;
        }
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == 1)
    {
        if (m_isDragging)
        {
            m_isDragging = false;
            return true;
        }
    }

    return false;
}

// ── Render ────────────────────────────────────────────────────────────────────

void Slider::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    float labelAreaW = m_label.empty()  ? 0.0f : m_bounds.width * 0.30f;
    float valueAreaW = !m_showValue     ? 0.0f : m_bounds.width * 0.12f;
    float trackX     = m_bounds.x + labelAreaW;
    float trackW     = m_bounds.width - labelAreaW - valueAreaW;
    float trackH     = 0.008f;
    float trackCY    = m_bounds.y + m_bounds.height * 0.5f;
    float trackY     = trackCY - trackH * 0.5f;

    float t           = GetNormT();
    float thumbRadius = 0.012f + m_thumbAnim * 0.003f;

    // 标签文字
    if (!m_label.empty() && m_fontHandle != 0)
    {
        renderer.DrawText(m_fontHandle, m_label,
                          m_bounds.x,
                          trackCY,
                          m_normFontSize,
                          m_labelColor,
                          sakura::core::TextAlign::Left);
    }

    // 轨道背景
    renderer.DrawRoundedRect(
        { trackX, trackY, trackW, trackH },
        trackH * 0.5f,
        m_trackColor,
        true);

    // 填充部分
    if (t > 0.0f)
    {
        renderer.DrawRoundedRect(
            { trackX, trackY, trackW * t, trackH },
            trackH * 0.5f,
            m_fillColor,
            true);
    }

    // 拇指
    float thumbX = trackX + trackW * t;
    renderer.DrawCircleFilled(thumbX, trackCY, thumbRadius, m_thumbColor);

    // 数值文字
    if (m_showValue && m_fontHandle != 0)
    {
        std::string valStr = FormatValue(m_value);
        renderer.DrawText(m_fontHandle, valStr,
                          trackX + trackW + m_bounds.width * 0.02f,
                          trackCY,
                          m_normFontSize,
                          m_labelColor,
                          sakura::core::TextAlign::Left);
    }
}

} // namespace sakura::ui
