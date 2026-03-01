// button.cpp — 按钮 UI 组件实现

#include "button.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ── 静态成员定义 ──────────────────────────────────────────────────────────────

std::function<void()> Button::s_hoverSFX;
std::function<void()> Button::s_clickSFX;

// ── 构造 ──────────────────────────────────────────────────────────────────────

Button::Button(sakura::core::NormRect bounds,
               const std::string& text,
               sakura::core::FontHandle fontHandle,
               float normFontSize,
               float cornerRadius)
    : UIBase(bounds)
    , m_text(text)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
    , m_cornerRadius(cornerRadius)
{
}

// ── 颜色插值 ──────────────────────────────────────────────────────────────────

sakura::core::Color Button::LerpColor(sakura::core::Color a,
                                       sakura::core::Color b, float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    return {
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t)
    };
}

// ── Update ────────────────────────────────────────────────────────────────────

void Button::Update(float dt)
{
    if (!m_isVisible || !m_isEnabled) return;

    // 悬停进度动画（0.25s EaseOutCubic）
    float targetHover = m_isHovered ? 1.0f : 0.0f;
    float delta = (targetHover - m_hoverProgress)
                * (dt / HOVER_DURATION);
    m_hoverProgress += delta;
    m_hoverProgress = std::max(0.0f, std::min(1.0f, m_hoverProgress));

    // 点击缩放动画（0.15s EaseOutBack）
    if (m_scaleTimer < 1.0f)
    {
        m_scaleTimer += dt / PRESS_DURATION;
        m_scaleTimer = std::min(1.0f, m_scaleTimer);

        // 从 PRESS_SCALE 弹回到 1.0（EaseOutBack）
        float t = sakura::utils::EaseOutBack(m_scaleTimer);
        m_scaleAnim = PRESS_SCALE + (1.0f - PRESS_SCALE) * t;
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void Button::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    // 选择背景色
    sakura::core::Color bgColor;
    if (!m_isEnabled)
    {
        bgColor = m_colors.disabled;
    }
    else if (m_isPressed)
    {
        bgColor = m_colors.pressed;
    }
    else
    {
        float t = sakura::utils::EaseOutCubic(m_hoverProgress);
        bgColor = LerpColor(m_colors.normal, m_colors.hover, t);
    }

    // 应用透明度
    bgColor.a = static_cast<uint8_t>(bgColor.a * m_opacity);

    // 计算展开效果（如果宽度有弹性变化）
    float hoverExp = sakura::utils::EaseOutCubic(m_hoverProgress) * 0.02f; // 横向最多膨胀 2%

    // 计算缩放后的实际矩形
    float cx = m_bounds.x + m_bounds.width  * 0.5f;
    float cy = m_bounds.y + m_bounds.height * 0.5f;
    float hw  = m_bounds.width  * 0.5f * m_scaleAnim + hoverExp;
    float hh  = m_bounds.height * 0.5f * m_scaleAnim;

    sakura::core::NormRect scaledRect = {
        cx - hw, cy - hh, hw * 2.0f, hh * 2.0f
    };

    // 绘制圆角矩形背景 (glassmorphism)
    renderer.DrawRoundedRect(scaledRect, m_cornerRadius, bgColor, true);

    // 绘制边框
    if (m_isEnabled)
    {
        sakura::core::Color borderColor = m_colors.border;
        // 浮起微光: hover 时亮度及透明度增加
        float t = sakura::utils::EaseOutCubic(m_hoverProgress);
        borderColor.a = static_cast<uint8_t>(borderColor.a + static_cast<int>(t * 40)); 
        borderColor.a = static_cast<uint8_t>(borderColor.a * m_opacity);
        renderer.DrawRoundedRect(scaledRect, m_cornerRadius, borderColor, false, 12, 0.0015f); // 1.5px 边框厚度
    }

    // 绘制文字
    if (!m_text.empty())
    {
        sakura::core::Color textColor = m_colors.text;
        if (!m_isEnabled)
            textColor = { 150, 150, 150, 180 };
        textColor.a = static_cast<uint8_t>(textColor.a * m_opacity);

        float textX = cx;
        float textY = cy - m_normFontSize * 0.5f;
        
        if (m_textAlign == sakura::core::TextAlign::Left) {
            textX = scaledRect.x + m_textPadding;
        } else if (m_textAlign == sakura::core::TextAlign::Right) {
            textX = scaledRect.x + scaledRect.width - m_textPadding;
        }

        renderer.DrawText(m_fontHandle, m_text,
                          textX, textY, m_normFontSize,
                          textColor,
                          m_textAlign);
    }
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool Button::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    // 获取归一化鼠标坐标
    auto [mx, my] = sakura::core::Input::GetMousePosition();

    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
    {
        bool wasHovered = m_isHovered;
        m_isHovered = HitTest(mx, my);
        if (!wasHovered && m_isHovered)
        {
            // 鼠标进入 — 播放 hover 音效
            if (s_hoverSFX) s_hoverSFX();
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT && HitTest(mx, my))
        {
            m_isPressed  = true;
            // 触发点击缩放动画
            m_scaleAnim  = PRESS_SCALE;
            m_scaleTimer = 0.0f;
            return true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT && m_isPressed)
        {
            m_isPressed = false;
            if (HitTest(mx, my) && m_onClick)
            {
                // 播放 click 音效
                if (s_clickSFX) s_clickSFX();
                m_onClick();
            }
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

} // namespace sakura::ui
