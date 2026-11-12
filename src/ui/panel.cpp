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
        renderer.DrawSoftShadow(
            m_bounds,
            m_cornerRadius,
            0.016f,
            WithOpacity(m_colors.shadow, m_opacity),
            m_shadowOffsetY,
            12);
    }

    sakura::core::Color topFill = WithOpacity(m_colors.fill, m_opacity);
    sakura::core::Color botFill{
        static_cast<uint8_t>(std::max(0, static_cast<int>(topFill.r) - 8)),
        static_cast<uint8_t>(std::max(0, static_cast<int>(topFill.g) - 8)),
        static_cast<uint8_t>(std::max(0, static_cast<int>(topFill.b) - 12)),
        static_cast<uint8_t>(std::min(255, static_cast<int>(topFill.a) + 14))
    };
    renderer.DrawRoundedGradientRect(
        m_bounds, m_cornerRadius,
        topFill, topFill, botFill, botFill, 14, true);

    // 顶部玻璃光泽
    const float specH = std::min(m_bounds.height * 0.25f, 0.032f);
    if (specH > m_cornerRadius * 0.6f)
    {
        renderer.DrawRoundedGradientRect(
            { m_bounds.x + 0.0015f, m_bounds.y + 0.0015f, std::max(0.0f, m_bounds.width - 0.003f), specH },
            m_cornerRadius * 0.88f,
            { 255, 246, 250, static_cast<uint8_t>(24 * m_opacity) },
            { 250, 226, 198, static_cast<uint8_t>(16 * m_opacity) },
            { 255, 246, 250, 0 }, { 250, 226, 198, 0 },
            10, false);
    }

    if (m_accentEnabled && m_accentHeight > 0.0f)
    {
        const float inset = m_cornerRadius * 0.85f;
        const float w = std::max(0.0f, m_bounds.width - inset * 2.0f);
        if (w > 0.004f)
        {
            const auto cMid = WithOpacity(m_colors.accent, m_opacity);
            const sakura::core::Color cEdge{ cMid.r, cMid.g, cMid.b, 0 };
            const float mx = m_bounds.x + m_bounds.width * 0.5f;
            renderer.DrawGradientRect({ m_bounds.x + inset, m_bounds.y + 0.002f, w * 0.5f, m_accentHeight * 0.7f }, cEdge, cMid, cEdge, cMid);
            renderer.DrawGradientRect({ mx, m_bounds.y + 0.002f, w * 0.5f, m_accentHeight * 0.7f }, cMid, cEdge, cMid, cEdge);
        }
    }

    if (m_colors.border.a > 0 && m_borderThickness > 0.0f)
    {
        const auto topB = WithOpacity(m_colors.border, m_opacity);
        const sakura::core::Color botB{ topB.r, topB.g, topB.b, static_cast<uint8_t>(topB.a * 0.42f) };
        renderer.DrawRoundedRectOutlineGradient(
            m_bounds, m_cornerRadius,
            topB, botB, m_borderThickness, 14);
    }
}

sakura::core::Color Panel::WithOpacity(sakura::core::Color color, float opacity)
{
    color.a = static_cast<uint8_t>(static_cast<float>(color.a) * opacity);
    return color;
}

} // namespace sakura::ui
