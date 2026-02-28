#pragma once

// button.h — 按钮 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <functional>

namespace sakura::ui
{

// 按钮各状态的颜色配置
struct ButtonColors
{
    sakura::core::Color normal   = { 60, 60, 90, 220 };
    sakura::core::Color hover    = { 90, 90, 130, 240 };
    sakura::core::Color pressed  = { 40, 40, 70, 240 };
    sakura::core::Color disabled = { 40, 40, 50, 120 };
    sakura::core::Color text     = sakura::core::Color::White;
};

// Button — 可点击按钮组件
// 支持悬停变色（150ms EaseOutQuad）和点击缩放弹回动画（100ms EaseOutBack）
class Button : public UIBase
{
public:
    Button(sakura::core::NormRect bounds,
           const std::string& text,
           sakura::core::FontHandle fontHandle,
           float normFontSize = 0.03f,
           float cornerRadius = 0.01f);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    void SetText(const std::string& text)         { m_text = text; }
    void SetColors(const ButtonColors& colors)     { m_colors = colors; }
    void SetFontSize(float size)                   { m_normFontSize = size; }
    void SetCornerRadius(float radius)              { m_cornerRadius = radius; }
    void SetOnClick(std::function<void()> onClick) { m_onClick = std::move(onClick); }

    // ── 生명周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    std::string              m_text;
    sakura::core::FontHandle m_fontHandle;
    float                    m_normFontSize;
    float                    m_cornerRadius;
    ButtonColors             m_colors;
    std::function<void()>    m_onClick;

    // 状态
    bool  m_isHovered  = false;
    bool  m_isPressed  = false;

    // 悬停动画进度（0=普通, 1=悬停色）
    float m_hoverProgress = 0.0f;
    static constexpr float HOVER_DURATION = 0.15f;   // 150ms

    // 点击缩放动画（>1.0 = 正常，0.95 = 最小，弹回到 1.0）
    float m_scaleAnim     = 1.0f;
    float m_scaleTimer    = 1.0f;  // 1.0 = 完成
    static constexpr float PRESS_DURATION = 0.10f;   // 100ms
    static constexpr float PRESS_SCALE    = 0.95f;

    // 混色辅助
    static sakura::core::Color LerpColor(sakura::core::Color a,
                                          sakura::core::Color b, float t);
};

} // namespace sakura::ui
