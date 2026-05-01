// visual_style.cpp — Sakura 唯一主视觉的页面级绘制与组件样式助手

#include "visual_style.h"

#include "core/theme.h"

#include <algorithm>

namespace sakura::ui
{

namespace
{
sakura::core::Color WithAlpha(sakura::core::Color color, float alphaScale)
{
    alphaScale = std::clamp(alphaScale, 0.0f, 1.0f);
    color.a = static_cast<uint8_t>(static_cast<float>(color.a) * alphaScale);
    return color;
}
}

void VisualStyle::DrawSceneBackground(sakura::core::Renderer& renderer)
{
    const auto& colors = sakura::core::Theme::GetInstance().Colors();
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, colors.bg);
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 0.42f },
        { 30, 22, 58, 255 }, { 46, 24, 70, 255 },
        colors.bg, { 16, 10, 30, 255 });
    renderer.DrawGradientRect(
        { 0.0f, 0.42f, 1.0f, 0.58f },
        { 16, 10, 30, 255 }, colors.bg,
        { 8, 7, 18, 255 }, { 12, 8, 22, 255 });
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 0.003f }, WithAlpha(colors.primary, 0.45f));
    renderer.DrawFilledRect({ 0.0f, 0.997f, 1.0f, 0.003f }, WithAlpha(colors.secondary, 0.25f));
}

void VisualStyle::DrawPlayfieldBackground(sakura::core::Renderer& renderer)
{
    const auto& colors = sakura::core::Theme::GetInstance().Colors();
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, colors.bg);
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 10, 8, 22, 255 }, { 24, 14, 40, 255 },
        { 6, 5, 16, 255 }, { 10, 8, 22, 255 });
}

void VisualStyle::DrawPanel(sakura::core::Renderer& renderer,
                            sakura::core::NormRect rect,
                            bool modal,
                            bool accent)
{
    const auto& style = modal
        ? sakura::core::Theme::GetInstance().Components().modal
        : sakura::core::Theme::GetInstance().Components().panel;

    if (style.shadow.a > 0)
    {
        renderer.DrawRoundedRect(
            { rect.x + 0.004f, rect.y + 0.006f, rect.width, rect.height },
            style.cornerRadius,
            style.shadow,
            true);
    }
    renderer.DrawRoundedRect(rect, style.cornerRadius, style.fill, true);
    if (accent)
    {
        renderer.DrawFilledRect({ rect.x, rect.y, rect.width, 0.004f }, style.accent);
    }
    renderer.DrawRoundedRect(
        rect,
        style.cornerRadius,
        style.border,
        false,
        12,
        style.borderThickness);
}

void VisualStyle::DrawScrim(sakura::core::Renderer& renderer, float opacity)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    renderer.DrawFilledRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 0, 0, 0, static_cast<uint8_t>(255.0f * opacity) });
}

ButtonColors VisualStyle::ButtonColorsFor(ButtonVariant variant)
{
    ButtonColors result;
    const auto& styles = sakura::core::Theme::GetInstance().Components();
    const sakura::core::ThemeButtonStyle* style = &styles.secondaryButton;
    switch (variant)
    {
    case ButtonVariant::Primary: style = &styles.primaryButton; break;
    case ButtonVariant::Accent:  style = &styles.accentButton;  break;
    case ButtonVariant::Danger:  style = &styles.dangerButton;  break;
    case ButtonVariant::Secondary:
    default:                     style = &styles.secondaryButton; break;
    }
    result.normal   = style->normal;
    result.hover    = style->hover;
    result.pressed  = style->pressed;
    result.disabled = style->disabled;
    result.text     = style->text;
    result.border   = style->border;
    return result;
}

void VisualStyle::ApplyButton(Button* button, ButtonVariant variant)
{
    if (!button) return;
    button->ApplyThemeVariant(variant);
}

void VisualStyle::ApplyScrollList(ScrollList* list)
{
    if (!list) return;
    const auto& colors = sakura::core::Theme::GetInstance().Colors();
    list->SetBgColor({ 15, 12, 30, 210 });
    list->SetNormalColor({ 27, 22, 52, 210 });
    list->SetHoverColor({ 55, 42, 92, 228 });
    list->SetSelectedColor({ 104, 66, 145, 242 });
    list->SetTextColor(colors.text);
}

} // namespace sakura::ui