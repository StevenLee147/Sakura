// input_field.cpp — 文本输入框 UI 组件实现

#include "input_field.h"
#include "core/input.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ═════════════════════════════════════════════════════════════════════════════
// 构造
// ═════════════════════════════════════════════════════════════════════════════

InputField::InputField(sakura::core::NormRect  bounds,
                       const std::string&      placeholder,
                       sakura::core::FontHandle fontHandle,
                       float normFontSize)
    : UIBase(bounds)
    , m_placeholder(placeholder)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
{
}

// ═════════════════════════════════════════════════════════════════════════════
// SetFocused
// ═════════════════════════════════════════════════════════════════════════════

void InputField::SetFocused(bool focused)
{
    if (m_focused == focused) return;
    m_focused = focused;
    m_cursorBlink = 0.0f;

    if (focused)
        SDL_StartTextInput(SDL_GetMouseFocus());
    else
        SDL_StopTextInput(SDL_GetMouseFocus());
}

// ═════════════════════════════════════════════════════════════════════════════
// Update
// ═════════════════════════════════════════════════════════════════════════════

void InputField::Update(float dt)
{
    if (!m_isVisible) return;

    if (m_focused)
    {
        m_cursorBlink += dt;
        if (m_cursorBlink >= 1.0f)
            m_cursorBlink -= 1.0f;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Render
// ═════════════════════════════════════════════════════════════════════════════

void InputField::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    sakura::core::Color bgColor     = m_focused ? BG_FOCUSED   : BG_NORMAL;
    sakura::core::Color borderColor = m_focused ? BORDER_FOCUSED : BORDER_NORMAL;

    // 背景
    renderer.DrawRoundedRect(m_bounds, 0.006f, bgColor, true);
    // 边框
    renderer.DrawRoundedRect(m_bounds, 0.006f, borderColor, false);

    if (m_fontHandle == sakura::core::INVALID_HANDLE) return;

    // 文字区域（左内边距）
    float padX = m_bounds.width  * 0.03f;
    float textX = m_bounds.x + padX;
    float textY = m_bounds.y + m_bounds.height * 0.5f;

    if (m_text.empty())
    {
        // 占位符
        renderer.DrawText(m_fontHandle, m_placeholder,
            textX, textY, m_normFontSize,
            PLACEHOLDER_COLOR,
            sakura::core::TextAlign::Left);
    }
    else
    {
        // 实际文字
        renderer.DrawText(m_fontHandle, m_text,
            textX, textY, m_normFontSize,
            TEXT_COLOR,
            sakura::core::TextAlign::Left);
    }

    // 光标（聚焦时，在文字末尾闪烁）
    if (m_focused && m_cursorBlink < 0.5f)
    {
        // 简单：画在文字区域右侧固定位置（精确位置需要字体宽度测量，暂用简化版）
        float cursorX = textX + static_cast<float>(m_text.size()) * m_normFontSize * 0.55f;
        cursorX = std::min(cursorX, m_bounds.x + m_bounds.width - padX);

        float cursorTop    = m_bounds.y + m_bounds.height * 0.15f;
        float cursorBottom = m_bounds.y + m_bounds.height * 0.85f;

        renderer.DrawLine(cursorX, cursorTop, cursorX, cursorBottom,
            CURSOR_COLOR, 0.002f);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// HandleEvent
// ═════════════════════════════════════════════════════════════════════════════

bool InputField::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    auto [mx, my] = sakura::core::Input::GetMousePosition();

    switch (event.type)
    {
    // 点击获取焦点
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            bool clicked = HitTest(mx, my);
            SetFocused(clicked);
            return clicked;
        }
        break;

    // 文本输入（IME/键盘 Unicode 字符）
    case SDL_EVENT_TEXT_INPUT:
        if (m_focused)
        {
            std::string input(event.text.text);
            // 检查长度限制（UTF-8 字节数）
            if (static_cast<int>(m_text.size() + input.size()) <= m_maxLen)
            {
                m_text += input;
                if (m_onChange) m_onChange(m_text);
            }
            return true;
        }
        break;

    // 按键处理
    case SDL_EVENT_KEY_DOWN:
        if (m_focused)
        {
            switch (event.key.scancode)
            {
            case SDL_SCANCODE_BACKSPACE:
                if (!m_text.empty())
                {
                    // 删除最后一个 UTF-8 字符（回退到上一个有效字节边界）
                    // UTF-8 续字节以 10xxxxxx 开头
                    size_t pos = m_text.size();
                    --pos;  // 指向最后一字节
                    while (pos > 0 && (m_text[pos] & 0xC0) == 0x80)
                        --pos;  // 跳过续字节
                    m_text.erase(pos);
                    if (m_onChange) m_onChange(m_text);
                }
                return true;

            case SDL_SCANCODE_DELETE:
                m_text.clear();
                if (m_onChange) m_onChange(m_text);
                return true;

            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                if (m_onConfirm) m_onConfirm(m_text);
                return true;

            case SDL_SCANCODE_ESCAPE:
                SetFocused(false);
                return true;

            default:
                break;
            }
        }
        break;

    default:
        break;
    }

    return false;
}

} // namespace sakura::ui
