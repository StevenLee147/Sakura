// label.cpp — 文字标签组件实现

#include "label.h"

namespace sakura::ui
{

Label::Label(sakura::core::NormRect bounds,
             const std::string& text,
             sakura::core::FontHandle fontHandle,
             float normFontSize,
             sakura::core::Color color,
             sakura::core::TextAlign align)
    : UIBase(bounds)
    , m_text(text)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
    , m_color(color)
    , m_align(align)
{
}

void Label::SetShadow(bool enabled, sakura::core::Color shadowColor,
                      float offsetX, float offsetY)
{
    m_shadowEnabled = enabled;
    m_shadowColor   = shadowColor;
    m_shadowOffsetX = offsetX;
    m_shadowOffsetY = offsetY;
}

void Label::Update(float /*dt*/)
{
    // Label 无需逻辑更新
}

void Label::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible || m_text.empty()) return;

    // 确定文字渲染锚点
    float x = m_bounds.x;
    float y = m_bounds.y;

    switch (m_align)
    {
    case sakura::core::TextAlign::Center:
        x = m_bounds.x + m_bounds.width * 0.5f;
        break;
    case sakura::core::TextAlign::Right:
        x = m_bounds.x + m_bounds.width;
        break;
    default:
        break;
    }

    // 应用透明度到颜色
    auto applyAlpha = [this](sakura::core::Color c) -> sakura::core::Color
    {
        c.a = static_cast<uint8_t>(c.a * m_opacity);
        return c;
    };

    // 绘制阴影
    if (m_shadowEnabled)
    {
        renderer.DrawText(m_fontHandle, m_text,
                          x + m_shadowOffsetX, y + m_shadowOffsetY,
                          m_normFontSize,
                          applyAlpha(m_shadowColor),
                          m_align);
    }

    // 绘制文字
    renderer.DrawText(m_fontHandle, m_text,
                      x, y,
                      m_normFontSize,
                      applyAlpha(m_color),
                      m_align);
}

} // namespace sakura::ui
