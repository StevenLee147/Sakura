#pragma once

// text_input.h — 单行文本输入框 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <functional>

namespace sakura::ui
{

// TextInput — 单行文字输入框
// 支持：光标闪烁、退格/左右方向键、全选(Ctrl+A)、复制(Ctrl+C)、粘贴(Ctrl+V)、
//        placeholder、maxLength。
class TextInput : public UIBase
{
public:
    TextInput(sakura::core::NormRect bounds,
              sakura::core::FontHandle fontHandle = 0,
              float normFontSize                  = 0.028f,
              int   maxLength                     = 128);
    ~TextInput() override { if (m_isFocused) SetFocused(false); }

    // ── 属性 ──────────────────────────────────────────────────────────────────

    const std::string& GetText() const { return m_text; }
    void SetText(const std::string& text);

    void SetPlaceholder(const std::string& text) { m_placeholder = text; }
    void SetMaxLength(int len)                    { m_maxLength = len; }

    bool IsFocused() const { return m_isFocused; }
    void SetFocused(bool focused);

    // 颜色
    void SetBgColor(sakura::core::Color c)          { m_bgColor = c; }
    void SetBorderColor(sakura::core::Color c)      { m_borderColor = c; }
    void SetFocusBorderColor(sakura::core::Color c) { m_focusBorderColor = c; }
    void SetTextColor(sakura::core::Color c)        { m_textColor = c; }
    void SetPlaceholderColor(sakura::core::Color c) { m_placeholderColor = c; }

    // 回调
    void SetOnChange(std::function<void(const std::string&)> cb) { m_onChange = std::move(cb); }
    void SetOnSubmit(std::function<void(const std::string&)> cb) { m_onSubmit = std::move(cb); }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    std::string              m_text;
    std::string              m_placeholder;
    int                      m_maxLength;
    size_t                   m_cursorPos = 0;    // 字符索引
    bool                     m_isFocused = false;
    bool                     m_selectedAll = false;

    sakura::core::FontHandle m_fontHandle;
    float                    m_normFontSize;

    sakura::core::Color m_bgColor          = { 16, 22, 38, 224 };
    sakura::core::Color m_borderColor      = { 176, 168, 208, 105 };
    sakura::core::Color m_focusBorderColor = { 250, 216, 157, 238 };
    sakura::core::Color m_textColor        = { 247, 244, 252, 255 };
    sakura::core::Color m_placeholderColor = { 128, 136, 166, 168 };

    std::function<void(const std::string&)> m_onChange;
    std::function<void(const std::string&)> m_onSubmit;

    // 光标闪烁计时
    float m_cursorBlink   = 0.0f;
    bool  m_cursorVisible = true;
    static constexpr float CURSOR_PERIOD = 0.53f;

    // 文字水平滚动偏移（当文字超出宽度时）
    float m_scrollOffset = 0.0f;

    void InsertChar(const char* utf8Str);
    void DeleteCharBefore();
    void MoveCursorLeft();
    void MoveCursorRight();
    void SelectAll();
};

} // namespace sakura::ui
