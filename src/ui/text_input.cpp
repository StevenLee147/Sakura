// text_input.cpp — 单行文本输入框 UI 组件实现

#include "text_input.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cstring>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

TextInput::TextInput(sakura::core::NormRect bounds,
                     sakura::core::FontHandle fontHandle,
                     float normFontSize,
                     int maxLength)
    : UIBase(bounds)
    , m_maxLength(maxLength)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
{
}

// ── 属性 ──────────────────────────────────────────────────────────────────────

void TextInput::SetText(const std::string& text)
{
    m_text      = text.substr(0, m_maxLength);
    m_cursorPos = m_text.size();
}

void TextInput::SetFocused(bool focused)
{
    m_isFocused = focused;
    if (focused)
    {
        SDL_StartTextInput(nullptr);  // SDL3 中接受 SDL_Window* 参数，nullptr 使用默认
        m_cursorBlink   = 0.0f;
        m_cursorVisible = true;
    }
    else
    {
        SDL_StopTextInput(nullptr);
    }
}

// ── 文字操作 ──────────────────────────────────────────────────────────────────

void TextInput::InsertChar(const char* utf8Str)
{
    if (static_cast<int>(m_text.size()) >= m_maxLength) return;
    // utf8 可能多字节，直接在 cursorPos 位置插入
    size_t insertLen = strlen(utf8Str);
    m_text.insert(m_cursorPos, utf8Str, insertLen);
    m_cursorPos += insertLen;
    if (m_onChange) m_onChange(m_text);
}

void TextInput::DeleteCharBefore()
{
    if (m_cursorPos == 0 || m_text.empty()) return;
    // 退格要正确删除 UTF-8 多字节字符（找到前一个字符的起始位置）
    size_t pos = m_cursorPos;
    // 向前找 UTF-8 字符起始（首字节：0xxxxxxx 或 11xxxxxx）
    do { --pos; }
    while (pos > 0 && (m_text[pos] & 0xC0) == 0x80);
    m_text.erase(pos, m_cursorPos - pos);
    m_cursorPos = pos;
    if (m_onChange) m_onChange(m_text);
}

void TextInput::MoveCursorLeft()
{
    if (m_cursorPos == 0) return;
    size_t pos = m_cursorPos;
    do { --pos; }
    while (pos > 0 && (m_text[pos] & 0xC0) == 0x80);
    m_cursorPos = pos;
}

void TextInput::MoveCursorRight()
{
    if (m_cursorPos >= m_text.size()) return;
    size_t pos = m_cursorPos + 1;
    while (pos < m_text.size() && (m_text[pos] & 0xC0) == 0x80) ++pos;
    m_cursorPos = pos;
}

void TextInput::SelectAll()
{
    m_cursorPos = m_text.size();
}

// ── Update ────────────────────────────────────────────────────────────────────

void TextInput::Update(float dt)
{
    if (!m_isVisible) return;

    if (m_isFocused)
    {
        m_cursorBlink += dt;
        if (m_cursorBlink >= CURSOR_PERIOD)
        {
            m_cursorBlink   -= CURSOR_PERIOD;
            m_cursorVisible  = !m_cursorVisible;
        }
    }
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool TextInput::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 1)
    {
        auto [mx, my] = sakura::core::Input::GetMousePosition();
        bool hit = HitTest(mx, my);
        if (hit && !m_isFocused)  SetFocused(true);
        if (!hit && m_isFocused)  SetFocused(false);
        return hit;
    }

    if (!m_isFocused) return false;

    if (event.type == SDL_EVENT_TEXT_INPUT)
    {
        InsertChar(event.text.text);
        m_cursorBlink   = 0.0f;
        m_cursorVisible = true;
        return true;
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
        switch (event.key.scancode)
        {
            case SDL_SCANCODE_BACKSPACE:
                DeleteCharBefore();
                m_cursorBlink = 0.0f; m_cursorVisible = true;
                return true;
            case SDL_SCANCODE_LEFT:
                MoveCursorLeft();
                return true;
            case SDL_SCANCODE_RIGHT:
                MoveCursorRight();
                return true;
            case SDL_SCANCODE_HOME:
                m_cursorPos = 0;
                return true;
            case SDL_SCANCODE_END:
                m_cursorPos = m_text.size();
                return true;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                if (m_onSubmit) m_onSubmit(m_text);
                SetFocused(false);
                return true;
            case SDL_SCANCODE_ESCAPE:
                SetFocused(false);
                return true;
            case SDL_SCANCODE_A:
                if (ctrl) { SelectAll(); return true; }
                break;
            case SDL_SCANCODE_C:
                if (ctrl)
                {
                    SDL_SetClipboardText(m_text.c_str());
                    return true;
                }
                break;
            case SDL_SCANCODE_V:
                if (ctrl)
                {
                    if (SDL_HasClipboardText())
                    {
                        char* cb = SDL_GetClipboardText();
                        if (cb)
                        {
                            // 逐字插入（受 maxLength 限制）
                            InsertChar(cb);
                            SDL_free(cb);
                        }
                    }
                    return true;
                }
                break;
            default:
                break;
        }
    }

    return false;
}

// ── Render ────────────────────────────────────────────────────────────────────

void TextInput::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    float cornerR = m_bounds.height * 0.25f;

    // 背景
    renderer.DrawRoundedRect(m_bounds, cornerR, m_bgColor, true);

    // 边框
    auto borderCol = m_isFocused ? m_focusBorderColor : m_borderColor;
    renderer.DrawRoundedRect(m_bounds, cornerR, borderCol, false);

    // 文字或占位符
    float padX = m_bounds.width * 0.03f;
    float textX = m_bounds.x + padX;
    float textY = m_bounds.y + m_bounds.height * 0.5f;

    if (!m_text.empty())
    {
        renderer.DrawText(m_fontHandle, m_text,
                          textX, textY,
                          m_normFontSize,
                          m_textColor,
                          sakura::core::TextAlign::Left);
    }
    else if (!m_placeholder.empty())
    {
        renderer.DrawText(m_fontHandle, m_placeholder,
                          textX, textY,
                          m_normFontSize,
                          m_placeholderColor,
                          sakura::core::TextAlign::Left);
    }

    // 光标
    if (m_isFocused && m_cursorVisible && m_fontHandle != 0)
    {
        std::string beforeCursor = m_text.substr(0, m_cursorPos);
        float cursorOffX = (m_fontHandle != 0)
            ? renderer.MeasureTextWidth(m_fontHandle, beforeCursor, m_normFontSize)
            : 0.0f;
        float cx         = textX + cursorOffX;
        float cy1        = m_bounds.y + m_bounds.height * 0.15f;
        float cy2        = m_bounds.y + m_bounds.height * 0.85f;
        renderer.DrawLine(cx, cy1, cx, cy2, m_textColor, 0.002f);
    }
}

} // namespace sakura::ui
