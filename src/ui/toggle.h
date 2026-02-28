#pragma once

// toggle.h — 开关 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <functional>

namespace sakura::ui
{

// Toggle — on/off 开关组件
// 点击切换；滑动动画 200ms EaseOutQuad
class Toggle : public UIBase
{
public:
    Toggle(sakura::core::NormRect bounds,
           bool initValue             = false,
           sakura::core::FontHandle fontHandle  = 0,
           float normFontSize         = 0.025f);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    bool IsOn() const { return m_isOn; }
    void SetOn(bool on, bool animate = true);

    // 可选标签（显示在开关左侧）
    void SetLabel(const std::string& label) { m_label = label; }

    // 颜色
    void SetOnColor(sakura::core::Color c)   { m_onColor = c; }
    void SetOffColor(sakura::core::Color c)  { m_offColor = c; }
    void SetThumbColor(sakura::core::Color c){ m_thumbColor = c; }
    void SetLabelColor(sakura::core::Color c){ m_labelColor = c; }

    // 回调
    void SetOnChange(std::function<void(bool)> cb) { m_onChange = std::move(cb); }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    bool m_isOn;

    std::string              m_label;
    sakura::core::FontHandle m_fontHandle;
    float                    m_normFontSize;

    sakura::core::Color m_onColor    = { 140, 80, 200, 220 };
    sakura::core::Color m_offColor   = { 60, 60, 80, 200 };
    sakura::core::Color m_thumbColor = { 240, 230, 255, 255 };
    sakura::core::Color m_labelColor = { 220, 220, 220, 255 };

    std::function<void(bool)> m_onChange;

    // 动画进度（0=关，1=开）
    float m_animT    = 0.0f;
    static constexpr float ANIM_DURATION = 0.20f;

    // 点击防抖
    bool m_wasPressed = false;

    // 辅助：当前颜色插值
    static sakura::core::Color LerpColor(sakura::core::Color a,
                                          sakura::core::Color b, float t);
};

} // namespace sakura::ui
