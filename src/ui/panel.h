#pragma once

// panel.h — 通用面板容器绘制组件

#include "ui_base.h"
#include "core/renderer.h"

namespace sakura::ui
{

struct PanelColors
{
    sakura::core::Color fill   = { 15, 12, 30, 220 };
    sakura::core::Color border = { 100, 80, 150, 150 };
    sakura::core::Color shadow = { 0, 0, 0, 90 };
    sakura::core::Color accent = { 255, 150, 200, 170 };
};

class Panel : public UIBase
{
public:
    explicit Panel(sakura::core::NormRect bounds, bool modalStyle = false);

    void ApplyTheme(bool modalStyle = false);
    void SetColors(const PanelColors& colors) { m_colors = colors; }
    void SetCornerRadius(float radius) { m_cornerRadius = radius; }
    void SetBorderThickness(float thickness) { m_borderThickness = thickness; }
    void SetShadowEnabled(bool enabled) { m_shadowEnabled = enabled; }
    void SetAccentEnabled(bool enabled) { m_accentEnabled = enabled; }
    void SetAccentHeight(float height) { m_accentHeight = height; }

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;

private:
    static sakura::core::Color WithOpacity(sakura::core::Color color, float opacity);

    PanelColors m_colors;
    float m_cornerRadius    = 0.012f;
    float m_borderThickness = 0.0015f;
    float m_shadowOffsetX   = 0.004f;
    float m_shadowOffsetY   = 0.006f;
    bool  m_shadowEnabled   = true;
    bool  m_accentEnabled   = false;
    float m_accentHeight    = 0.004f;
};

} // namespace sakura::ui
