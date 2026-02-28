// progress_bar.cpp — 进度条组件实现

#include "progress_bar.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

ProgressBar::ProgressBar(sakura::core::NormRect bounds, float normCornerRadius)
    : UIBase(bounds)
    , m_normCornerRadius(normCornerRadius)
{
}

// ── 进度控制 ──────────────────────────────────────────────────────────────────

void ProgressBar::SetValue(float value)
{
    value = std::max(0.0f, std::min(1.0f, value));
    if (std::abs(value - m_targetValue) < 0.0001f) return;

    m_animFrom    = m_displayValue;
    m_targetValue = value;
    m_animTimer   = 0.0f;
}

void ProgressBar::SetValueImmediate(float value)
{
    value = std::max(0.0f, std::min(1.0f, value));
    m_targetValue  = value;
    m_displayValue = value;
    m_animTimer    = 1.0f;
}

void ProgressBar::SetShowPercentage(bool show, sakura::core::FontHandle font,
                                     float normFontSize)
{
    m_showPercentage  = show;
    m_percentFont     = font;
    m_percentFontSize = normFontSize;
}

// ── Update ────────────────────────────────────────────────────────────────────

void ProgressBar::Update(float dt)
{
    if (!m_isVisible) return;

    if (m_animTimer < 1.0f)
    {
        m_animTimer += dt / ANIM_DURATION;
        m_animTimer  = std::min(1.0f, m_animTimer);

        float t = sakura::utils::EaseOutCubic(m_animTimer);
        m_displayValue = m_animFrom + (m_targetValue - m_animFrom) * t;
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void ProgressBar::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    // 应用透明度
    sakura::core::Color bg = m_bgColor;
    bg.a = static_cast<uint8_t>(bg.a * m_opacity);

    // 背景轨道
    renderer.DrawRoundedRect(m_bounds, m_normCornerRadius, bg, true);

    // 填充条（根据 displayValue 缩放宽度）
    float fillW = m_bounds.width * std::max(0.0f, std::min(1.0f, m_displayValue));
    if (fillW > m_normCornerRadius * 2.0f)
    {
        sakura::core::NormRect fillRect = {
            m_bounds.x, m_bounds.y, fillW, m_bounds.height
        };
        sakura::core::Color fill = m_fillColor;
        fill.a = static_cast<uint8_t>(fill.a * m_opacity);
        renderer.DrawRoundedRect(fillRect, m_normCornerRadius, fill, true);
    }

    // 边框
    if (m_showBorder)
    {
        sakura::core::Color border = m_borderColor;
        border.a = static_cast<uint8_t>(border.a * m_opacity);
        renderer.DrawRoundedRect(m_bounds, m_normCornerRadius, border, false);
    }

    // 百分比文字
    if (m_showPercentage && m_percentFont != 0)
    {
        int pct = static_cast<int>(std::round(m_displayValue * 100.0f));
        std::string pctStr = std::to_string(pct) + "%";

        float textX = m_bounds.x + m_bounds.width  * 0.5f;
        float textY = m_bounds.y + (m_bounds.height - m_percentFontSize) * 0.5f;

        sakura::core::Color textColor = { 240, 240, 255,
            static_cast<uint8_t>(220 * m_opacity) };
        renderer.DrawText(m_percentFont, pctStr,
                          textX, textY, m_percentFontSize,
                          textColor,
                          sakura::core::TextAlign::Center);
    }

    // 外部标签
    if (!m_label.empty())
    {
        // 标签绘制在进度条右侧（0.01 间距）
        float labelX = m_bounds.x + m_bounds.width + 0.01f;
        float labelY = m_bounds.y + (m_bounds.height - m_percentFontSize) * 0.5f;
        sakura::core::Color labelColor = { 200, 200, 220,
            static_cast<uint8_t>(200 * m_opacity) };
        // 需要字体句柄才渲染，若未设置则跳过
        if (m_percentFont != 0)
        {
            renderer.DrawText(m_percentFont, m_label,
                              labelX, labelY, m_percentFontSize,
                              labelColor,
                              sakura::core::TextAlign::Left);
        }
    }
}

} // namespace sakura::ui
