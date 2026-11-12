// button.cpp — 按钮 UI 组件实现

#include "button.h"
#include "core/input.h"
#include "core/theme.h"
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
    ApplyThemeVariant(ButtonVariant::Secondary);
}

void Button::ApplyThemeVariant(ButtonVariant variant, bool applyMetrics)
{
    const auto& styles = sakura::core::Theme::GetInstance().Components();
    const sakura::core::ThemeButtonStyle* style = &styles.secondaryButton;
    switch (variant)
    {
    case ButtonVariant::Primary:   style = &styles.primaryButton;   break;
    case ButtonVariant::Accent:    style = &styles.accentButton;    break;
    case ButtonVariant::Danger:    style = &styles.dangerButton;    break;
    case ButtonVariant::Secondary:
    default:                       style = &styles.secondaryButton; break;
    }

    m_colors.normal   = style->normal;
    m_colors.hover    = style->hover;
    m_colors.pressed  = style->pressed;
    m_colors.disabled = style->disabled;
    m_colors.text     = style->text;
    m_colors.border   = style->border;
    if (applyMetrics)
        m_cornerRadius = style->cornerRadius;
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
    float hoverExp = sakura::utils::EaseOutCubic(m_hoverProgress) * 0.002f; // 横向最多膨胀 2%

    // 计算缩放后的实际矩形
    float cx = m_bounds.x + m_bounds.width  * 0.5f;
    float cy = m_bounds.y + m_bounds.height * 0.5f;
    float hw  = m_bounds.width  * 0.5f * m_scaleAnim + hoverExp;
    float hh  = m_bounds.height * 0.5f * m_scaleAnim;

    sakura::core::NormRect scaledRect = {
        cx - hw, cy - hh, hw * 2.0f, hh * 2.0f
    };

    const float hoverEased = sakura::utils::EaseOutCubic(m_hoverProgress);

    // 柔和悬浮投影
    if (m_isEnabled && !m_isPressed)
    {
        renderer.DrawSoftShadow(
            scaledRect, m_cornerRadius,
            0.008f + hoverEased * 0.006f,
            { 4, 6, 16, static_cast<uint8_t>((75.0f + hoverEased * 45.0f) * m_opacity) },
            0.003f + hoverEased * 0.0015f, 10);
    }

    // 四角磨砂琉璃渐变填充 + 顶部镜面光泽
    sakura::core::Color topCol{
        static_cast<uint8_t>(std::min(255, static_cast<int>(bgColor.r) + 12 + static_cast<int>(hoverEased * 10.0f))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(bgColor.g) + 10 + static_cast<int>(hoverEased * 8.0f))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(bgColor.b) + 14 + static_cast<int>(hoverEased * 10.0f))),
        bgColor.a
    };
    sakura::core::Color botCol{
        static_cast<uint8_t>(std::max(0, static_cast<int>(bgColor.r) - 10)),
        static_cast<uint8_t>(std::max(0, static_cast<int>(bgColor.g) - 10)),
        static_cast<uint8_t>(std::max(0, static_cast<int>(bgColor.b) - 8)),
        static_cast<uint8_t>(std::min(255, static_cast<int>(bgColor.a) + 12))
    };
    renderer.DrawRoundedGradientRect(
        scaledRect, m_cornerRadius,
        topCol, bgColor, botCol, botCol, 12, true);

    // 悬停流光扫掠高光带
    if (m_isEnabled && hoverEased > 0.02f)
    {
        const uint8_t sheenA = static_cast<uint8_t>(38.0f * hoverEased * m_opacity);
        const float specH = std::min(scaledRect.height * 0.46f, 0.026f);
        renderer.DrawRoundedGradientRect(
            { scaledRect.x + 0.001f, scaledRect.y + 0.001f, std::max(0.0f, scaledRect.width - 0.002f), specH },
            m_cornerRadius * 0.85f,
            { 255, 248, 252, sheenA }, { 250, 224, 184, static_cast<uint8_t>(sheenA * 0.7f) },
            { 255, 248, 252, 0 },      { 250, 224, 184, 0 },
            10, false);
    }

    // 上沿鎏金 + 下沿柔光无接缝描边
    if (m_isEnabled)
    {
        sakura::core::Color borderTop = m_colors.border;
        const int boostedA = std::min(255, static_cast<int>(borderTop.a) + static_cast<int>(hoverEased * 62.0f));
        borderTop.a = static_cast<uint8_t>(static_cast<float>(boostedA) * m_opacity);
        if (hoverEased > 0.05f)
        {
            borderTop = LerpColor(borderTop, { 250, 216, 157, borderTop.a }, hoverEased * 0.55f);
        }
        sakura::core::Color borderBot = borderTop;
        borderBot.a = static_cast<uint8_t>(borderTop.a * 0.42f);
        renderer.DrawRoundedRectOutlineGradient(
            scaledRect, m_cornerRadius, borderTop, borderBot, 0.0014f, 12);
    }

    // 绘制文字
    if (!m_text.empty())
    {
        sakura::core::Color textColor = m_colors.text;
        if (!m_isEnabled)
            textColor = { 150, 150, 150, 180 };
        textColor.a = static_cast<uint8_t>(textColor.a * m_opacity);

        float textX = cx;
        float textY = cy - renderer.MeasureText(m_fontHandle,m_text,m_normFontSize).height * 0.5f;
        
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
