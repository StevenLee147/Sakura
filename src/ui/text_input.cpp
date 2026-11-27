// text_input.cpp — 单行文本输入框 UI 组件实现

#include "text_input.h"
#include "core/input.h"
#include "utils/easing.h"

#include "audio/audio_manager.h"
#include "ui/visual_style.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sakura::ui
{

namespace
{
TextInput* g_activeTextInput = nullptr;

std::size_t CountCodepoints(std::string_view sv)
{
    std::size_t n = 0;
    for (char ch : sv)
    {
        if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u)
            ++n;
    }
    return n;
}

std::string RepeatBullets(std::size_t count)
{
    static constexpr std::string_view kBullet = "\xE2\x80\xA2";
    std::string out;
    out.reserve(count * kBullet.size());
    for (std::size_t i = 0; i < count; ++i)
        out.append(kBullet);
    return out;
}
} // namespace

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
    size_t end = std::min(text.size(), static_cast<size_t>(m_maxLength));
    while (end < text.size() && end > 0 && (text[end] & 0xC0) == 0x80) --end;
    m_text      = text.substr(0, end);
    m_cursorPos = m_text.size();
    m_selectedAll = false;
}

void TextInput::SetFocused(bool focused)
{
    if (m_isFocused == focused)
        return;
    m_isFocused = focused;
    if (focused)
    {
        g_activeTextInput = this;
        if (auto* window = SDL_GetKeyboardFocus())
            SDL_StartTextInput(window);
        m_cursorBlink   = 0.0f;
        m_cursorVisible = true;
    }
    else
    {
        m_compositionText.clear();
        m_compositionCursor = 0;
        m_selectedAll       = false;
        if (g_activeTextInput == this)
        {
            g_activeTextInput = nullptr;
            if (auto* window = SDL_GetKeyboardFocus())
                SDL_StopTextInput(window);
        }
    }
}

void TextInput::SetPasswordMode(bool enabled)
{
    m_passwordMode = enabled;
    if (!enabled)
        m_passwordRevealed = false;
}

std::string TextInput::GetDisplayString() const
{
    if (!m_passwordMode || m_passwordRevealed)
        return m_text;
    return RepeatBullets(CountCodepoints(m_text));
}

// ── 文字操作 ──────────────────────────────────────────────────────────────────

void TextInput::InsertChar(const char* utf8Str)
{
    if (m_selectedAll) { m_text.clear(); m_cursorPos = 0; m_selectedAll = false; }
    if (static_cast<int>(m_text.size()) >= m_maxLength) return;
    // utf8 可能多字节，直接在 cursorPos 位置插入
    size_t insertLen = std::min(strlen(utf8Str), static_cast<size_t>(m_maxLength) - m_text.size());
    while (insertLen > 0 && (utf8Str[insertLen] & 0xC0) == 0x80) --insertLen;
    std::string clean(utf8Str, insertLen);
    std::erase_if(clean, [](unsigned char c) { return c < 32; });
    utf8Str = clean.c_str();
    insertLen = clean.size();
    m_text.insert(m_cursorPos, utf8Str, insertLen);
    m_cursorPos += insertLen;
    if (m_onChange) m_onChange(m_text);
}

void TextInput::DeleteCharBefore()
{
    if (m_selectedAll) { m_text.clear(); m_cursorPos = 0; m_selectedAll = false; if (m_onChange) m_onChange(m_text); return; }
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
    m_selectedAll = false;
    if (m_cursorPos == 0) return;
    size_t pos = m_cursorPos;
    do { --pos; }
    while (pos > 0 && (m_text[pos] & 0xC0) == 0x80);
    m_cursorPos = pos;
}

void TextInput::MoveCursorRight()
{
    m_selectedAll = false;
    if (m_cursorPos >= m_text.size()) return;
    size_t pos = m_cursorPos + 1;
    while (pos < m_text.size() && (m_text[pos] & 0xC0) == 0x80) ++pos;
    m_cursorPos = pos;
}

void TextInput::SelectAll()
{
    m_cursorPos = m_text.size();
    m_selectedAll = true;
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
        if (hit && m_passwordMode)
        {
            const float padX    = m_bounds.width * 0.03f;
            const float toggleW = std::min(0.042f, m_bounds.width * 0.18f);
            const float toggleX = m_bounds.x + m_bounds.width - padX - toggleW;
            if (mx >= toggleX && mx <= m_bounds.x + m_bounds.width)
            {
                m_passwordRevealed = !m_passwordRevealed;
                if (!m_isFocused)
                    SetFocused(true);
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                return true;
            }
        }
        if (hit && !m_isFocused)  SetFocused(true);
        if (!hit && m_isFocused)  SetFocused(false);
        return hit;
    }

    if (!m_isFocused) return false;

    if (event.type == SDL_EVENT_TEXT_EDITING)
    {
        m_compositionText   = (event.edit.text != nullptr) ? event.edit.text : "";
        m_compositionCursor = event.edit.start;
        m_cursorBlink       = 0.0f;
        m_cursorVisible     = true;
        return true;
    }

    if (event.type == SDL_EVENT_TEXT_INPUT)
    {
        m_compositionText.clear();
        m_compositionCursor = 0;
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
            case SDL_SCANCODE_DELETE:
                if (m_selectedAll) DeleteCharBefore();
                else if (m_cursorPos < m_text.size()) { MoveCursorRight(); DeleteCharBefore(); }
                return true;
            case SDL_SCANCODE_LEFT:
                MoveCursorLeft();
                return true;
            case SDL_SCANCODE_RIGHT:
                MoveCursorRight();
                return true;
            case SDL_SCANCODE_HOME:
                m_selectedAll = false;
                m_cursorPos = 0;
                return true;
            case SDL_SCANCODE_END:
                m_selectedAll = false;
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
                    if (!m_passwordMode)
                        SDL_SetClipboardText(m_text.c_str());
                    return true;
                }
                break;
            case SDL_SCANCODE_X:
                if (ctrl)
                {
                    if (!m_passwordMode && m_selectedAll)
                    {
                        SDL_SetClipboardText(m_text.c_str());
                        DeleteCharBefore();
                    }
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

    if (m_isFocused)
    {
        renderer.DrawSoftShadow(m_bounds, cornerR, 0.010f, { 246, 172, 200, 52 }, 0.0f, 10);
    }

    // 磨砂凹槽输入框背景
    renderer.DrawRoundedGradientRect(
        m_bounds, cornerR,
        { 12, 16, 30, m_bgColor.a }, { 12, 16, 30, m_bgColor.a },
        m_bgColor, m_bgColor, 10, true);

    // 聚焦鎏金渐变描边
    auto borderCol = m_isFocused ? m_focusBorderColor : m_borderColor;
    renderer.DrawRoundedRectOutlineGradient(
        m_bounds, cornerR,
        borderCol,
        { borderCol.r, borderCol.g, borderCol.b, static_cast<uint8_t>(borderCol.a * 0.48f) },
        m_isFocused ? 0.0016f : 0.0012f, 10);

    const bool masked = m_passwordMode && !m_passwordRevealed;
    const std::string displayFull = GetDisplayString();
    const std::size_t safeCursorByte = std::min(m_cursorPos, m_text.size());
    const std::string displayBefore = masked
        ? RepeatBullets(CountCodepoints(std::string_view(m_text).substr(0, safeCursorByte)))
        : m_text.substr(0, safeCursorByte);
    const std::string displayAfter = masked
        ? RepeatBullets(CountCodepoints(std::string_view(m_text).substr(safeCursorByte)))
        : m_text.substr(safeCursorByte);

    const float padX = m_bounds.width * 0.03f;
    const float toggleReserve = m_passwordMode ? (std::min(0.042f, m_bounds.width * 0.18f) + 0.006f) : 0.0f;
    float textX = m_bounds.x + padX;
    float textY = m_bounds.y + (m_bounds.height - renderer.MeasureText(m_fontHandle, "Ag", m_normFontSize).height) * 0.5f;
    const float beforeW = (m_fontHandle != 0) ? renderer.MeasureTextWidth(m_fontHandle, displayBefore, m_normFontSize) : 0.0f;
    const float compW   = (!m_compositionText.empty() && m_fontHandle != 0)
        ? renderer.MeasureTextWidth(m_fontHandle, m_compositionText, m_normFontSize)
        : 0.0f;
    const float cursorWidth = beforeW + compW;
    const float available = std::max(0.01f, m_bounds.width - padX * 2.0f - toggleReserve);
    m_scrollOffset = std::clamp(m_scrollOffset, std::max(0.0f, cursorWidth - available + 0.003f), cursorWidth);

    const int screenW = std::max(1, renderer.GetScreenWidth());
    const int screenH = std::max(1, renderer.GetScreenHeight());
    if (m_isFocused)
    {
        if (auto* win = SDL_GetKeyboardFocus())
        {
            SDL_Rect imeRect{
                static_cast<int>(std::lround(m_bounds.x * static_cast<float>(screenW))),
                static_cast<int>(std::lround(m_bounds.y * static_cast<float>(screenH))),
                std::max(1, static_cast<int>(std::lround(m_bounds.width * static_cast<float>(screenW)))),
                std::max(1, static_cast<int>(std::lround(m_bounds.height * static_cast<float>(screenH))))
            };
            const float caretNormInBox = std::clamp(padX - m_scrollOffset + cursorWidth, 0.0f, m_bounds.width);
            const int cursorPx = static_cast<int>(std::lround(caretNormInBox * static_cast<float>(screenW)));
            SDL_SetTextInputArea(win, &imeRect, cursorPx);
        }
    }

    SDL_Renderer* raw = renderer.GetSDLRenderer();
    SDL_Rect previous{};
    const bool clipped = SDL_RenderClipEnabled(raw);
    SDL_GetRenderClipRect(raw, &previous);
    SDL_Rect clip{
        static_cast<int>(textX * static_cast<float>(screenW)),
        static_cast<int>(m_bounds.y * static_cast<float>(screenH)),
        std::max(1, static_cast<int>(available * static_cast<float>(screenW))),
        std::max(1, static_cast<int>(m_bounds.height * static_cast<float>(screenH)))
    };
    SDL_SetRenderClipRect(raw, &clip);
    textX -= m_scrollOffset;

    if (m_selectedAll && m_isFocused && !displayFull.empty())
    {
        renderer.DrawFilledRect(
            { textX, textY, renderer.MeasureTextWidth(m_fontHandle, displayFull, m_normFontSize), m_normFontSize * 1.4f },
            { 133, 95, 145, 150 });
    }

    if (!m_compositionText.empty() && m_isFocused)
    {
        if (!displayBefore.empty())
            renderer.DrawText(m_fontHandle, displayBefore, textX, textY, m_normFontSize, m_textColor, sakura::core::TextAlign::Left);
        const float compX = textX + beforeW;
        renderer.DrawFilledRect({ compX, m_bounds.y + m_bounds.height * 0.16f, compW, m_bounds.height * 0.68f }, { 88, 52, 92, 130 });
        renderer.DrawText(m_fontHandle, m_compositionText, compX, textY, m_normFontSize, { 246, 178, 206, 255 }, sakura::core::TextAlign::Left);
        const float ulY = m_bounds.y + m_bounds.height * 0.84f;
        renderer.DrawLine(compX, ulY, compX + compW, ulY, { 250, 216, 157, 230 }, 0.0016f);
        if (!displayAfter.empty())
            renderer.DrawText(m_fontHandle, displayAfter, compX + compW, textY, m_normFontSize, m_textColor, sakura::core::TextAlign::Left);
    }
    else if (!displayFull.empty())
    {
        renderer.DrawText(m_fontHandle, displayFull, textX, textY, m_normFontSize, m_textColor, sakura::core::TextAlign::Left);
    }
    else if (!m_placeholder.empty())
    {
        renderer.DrawText(m_fontHandle, m_placeholder, textX, textY, m_normFontSize, m_placeholderColor, sakura::core::TextAlign::Left);
    }

    if (m_isFocused && m_cursorVisible && m_fontHandle != 0 && !m_selectedAll)
    {
        const float cx  = textX + cursorWidth;
        const float cy1 = m_bounds.y + m_bounds.height * 0.15f;
        const float cy2 = m_bounds.y + m_bounds.height * 0.85f;
        renderer.DrawLine(cx, cy1, cx, cy2, m_textColor, 0.002f);
    }
    renderer.Flush();
    SDL_SetRenderClipRect(raw, clipped ? &previous : nullptr);

    if (m_passwordMode)
    {
        const float toggleW = std::min(0.040f, m_bounds.width * 0.17f);
        const sakura::core::NormRect toggleRect{
            m_bounds.x + m_bounds.width - padX - toggleW,
            m_bounds.y + m_bounds.height * 0.17f,
            toggleW,
            m_bounds.height * 0.66f
        };
        VisualStyle::DrawBadgePill(
            renderer,
            toggleRect,
            m_passwordRevealed ? "隐" : "显",
            m_fontHandle,
            m_normFontSize * 0.68f,
            m_passwordRevealed ? sakura::core::Color{ 72, 46, 84, 215 } : sakura::core::Color{ 26, 32, 56, 200 },
            m_passwordRevealed ? sakura::core::Color{ 250, 216, 157, 185 } : sakura::core::Color{ 166, 176, 214, 95 },
            m_passwordRevealed ? sakura::core::Color{ 250, 216, 157, 255 } : sakura::core::Color{ 208, 214, 236, 235 });
    }
}

} // namespace sakura::ui
