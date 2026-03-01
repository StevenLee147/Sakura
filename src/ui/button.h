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
    sakura::core::Color normal   = { 255, 255, 255, 25 };
    sakura::core::Color hover    = { 255, 255, 255, 45 };
    sakura::core::Color pressed  = { 255, 255, 255, 15 };
    sakura::core::Color disabled = { 100, 100, 100, 20 };
    sakura::core::Color text     = sakura::core::Color::White;
    sakura::core::Color border   = { 255, 255, 255, 153 }; // 60% opacity for border
};

// Button — 可点击按钮组件
// 支持悬停变色（150ms EaseOutCubic）和点击缩放弹回动画（100ms EaseOutBack）
class Button : public UIBase
{
public:
    Button(sakura::core::NormRect bounds,
           const std::string& text,
           sakura::core::FontHandle fontHandle,
           float normFontSize = 0.03f,
           float cornerRadius = 0.01f);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    void SetText(const std::string& text)           { m_text = text; }
    void SetColors(const ButtonColors& colors)      { m_colors = colors; }
    void SetFontSize(float size)                    { m_normFontSize = size; }
    void SetCornerRadius(float radius)              { m_cornerRadius = radius; }
    void SetTextPadding(float padding)              { m_textPadding = padding; }
    void SetTextAlign(sakura::core::TextAlign align) { m_textAlign = align; }
    void SetOnClick(std::function<void()> onClick)  { m_onClick = std::move(onClick); }

    // ── 全局 UI 音效回调（由 AudioManager 注册，无需逐按钮设置）────────────
    static void SetGlobalHoverSFX(std::function<void()> cb) { s_hoverSFX = std::move(cb); }
    static void SetGlobalClickSFX(std::function<void()> cb) { s_clickSFX = std::move(cb); }

    // ── 生명周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    std::string              m_text;
    sakura::core::FontHandle m_fontHandle;
    float                    m_normFontSize;
    float                    m_cornerRadius;
    float                    m_textPadding = 0.05f; // Left padding
    sakura::core::TextAlign  m_textAlign = sakura::core::TextAlign::Left;
    ButtonColors             m_colors;
    std::function<void()>    m_onClick;

    // 状态
    bool  m_isHovered  = false;
    bool  m_isPressed  = false;

    // 悬停动画进度（0=普通, 1=悬停色）
    float m_hoverProgress = 0.0f;
    static constexpr float HOVER_DURATION = 0.25f;   // 250ms

    // 点击缩放动画（>1.0 = 正常，0.95 = 最小，弹回到 1.0）
    float m_scaleAnim     = 1.0f;
    float m_scaleTimer    = 1.0f;  // 1.0 = 完成
    static constexpr float PRESS_DURATION = 0.15f;   // 150ms
    static constexpr float PRESS_SCALE    = 0.95f;

    // 混色辅助
    static sakura::core::Color LerpColor(sakura::core::Color a,
                                          sakura::core::Color b, float t);

    // 全局音效回调
    static std::function<void()> s_hoverSFX;
    static std::function<void()> s_clickSFX;
};

} // namespace sakura::ui
