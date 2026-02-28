#pragma once

// input_field.h — 文本输入框 UI 组件
// 支持 SDL 文本输入事件、退格删除、光标显示、中文输入法（SDL_StartTextInput）

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"

#include <string>
#include <functional>

namespace sakura::ui
{

// InputField — 单行文本输入框
class InputField : public UIBase
{
public:
    InputField(sakura::core::NormRect  bounds,
               const std::string&      placeholder,
               sakura::core::FontHandle fontHandle,
               float normFontSize = 0.022f);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    void SetText(const std::string& text) { m_text = text; }
    const std::string& GetText() const    { return m_text; }

    void SetPlaceholder(const std::string& ph) { m_placeholder = ph; }
    void SetMaxLength(int maxLen)               { m_maxLen = maxLen; }

    // 焦点状态（用于外部强制聚焦/取消聚焦）
    void SetFocused(bool focused);
    bool IsFocused() const { return m_focused; }

    // 文本变化回调（每次内容修改时触发）
    void SetOnChange(std::function<void(const std::string&)> cb) { m_onChange = std::move(cb); }

    // Enter 确认回调
    void SetOnConfirm(std::function<void(const std::string&)> cb) { m_onConfirm = std::move(cb); }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    std::string              m_text;
    std::string              m_placeholder;
    sakura::core::FontHandle m_fontHandle;
    float                    m_normFontSize;
    int                      m_maxLen = 256;

    bool  m_focused      = false;
    float m_cursorBlink  = 0.0f;   // 光标闪烁计时（0~0.5s 显示，0.5~1.0s 隐藏）

    std::function<void(const std::string&)> m_onChange;
    std::function<void(const std::string&)> m_onConfirm;

    // 颜色
    static constexpr sakura::core::Color BG_NORMAL   = { 20, 16, 45, 200 };
    static constexpr sakura::core::Color BG_FOCUSED  = { 28, 22, 60, 220 };
    static constexpr sakura::core::Color BORDER_NORMAL  = { 60, 50, 100, 160 };
    static constexpr sakura::core::Color BORDER_FOCUSED = { 120, 100, 220, 230 };
    static constexpr sakura::core::Color TEXT_COLOR     = { 230, 220, 255, 240 };
    static constexpr sakura::core::Color PLACEHOLDER_COLOR = { 120, 110, 160, 150 };
    static constexpr sakura::core::Color CURSOR_COLOR   = { 200, 180, 255, 220 };
};

} // namespace sakura::ui
