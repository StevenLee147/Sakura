// panel.cpp — 通用面板容器绘制组件

#include "panel.h"
#include "core/theme.h"

namespace sakura::ui
{

Panel::Panel(sakura::core::NormRect bounds, bool modalStyle)
    : UIBase(bounds)
{
    ApplyTheme(modalStyle);
}

void Panel::ApplyTheme(bool modalStyle)
{
    const auto& style = modalStyle
        ? sakura::core::Theme::GetInstance().Components().modal
        : sakura::core::Theme::GetInstance().Components().panel;

    m_colors.fill   = style.fill;
    m_colors.border = style.border;
    m_colors.shadow = style.shadow;
    m_colors.accent = style.accent;
    m_cornerRadius  = style.cornerRadius;
    m_borderThickness = style.borderThickness;
}

void Panel::Update(float dt)
{
    (void)dt;
}

void Panel::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    if (m_shadowEnabled && m_colors.shadow.a > 0)
    {
        sakura::core::NormRect shadowRect = {
            m_bounds.x + m_shadowOffsetX,
            m_bounds.y + m_shadowOffsetY,
            m_bounds.width,
            m_bounds.height
        };
        renderer.DrawRoundedRect(
            shadowRect,
            m_cornerRadius,
            WithOpacity(m_colors.shadow, m_opacity),
            true);
    }

    renderer.DrawRoundedRect(
        m_bounds,
        m_cornerRadius,
        WithOpacity(m_colors.fill, m_opacity),
        true);

    if (m_accentEnabled && m_accentHeight > 0.0f)
    {
        renderer.DrawFilledRect(
            { m_bounds.x, m_bounds.y, m_bounds.width, m_accentHeight },
            WithOpacity(m_colors.accent, m_opacity));
    }

    if (m_colors.border.a > 0 && m_borderThickness > 0.0f)
    {
        renderer.DrawRoundedRect(
            m_bounds,
            m_cornerRadius,
            WithOpacity(m_colors.border, m_opacity),
            false,
            12,
            m_borderThickness);
    }
}

sakura::core::Color Panel::WithOpacity(sakura::core::Color color, float opacity)
{
    color.a = static_cast<uint8_t>(static_cast<float>(color.a) * opacity);
    return color;
}

} // namespace sakura::ui
