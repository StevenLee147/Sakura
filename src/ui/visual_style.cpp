// visual_style.cpp — Sakura 唯一主视觉的页面级绘制与组件样式助手

#include "visual_style.h"

#include "core/theme.h"
#include "core/config.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, {12, 16, 29, 255});
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 19, 25, 43, 255 }, { 40, 29, 48, 255 },
        { 9, 13, 23, 255 }, { 17, 20, 34, 255 });
    renderer.DrawCircleOutline(0.90f, 0.16f, 0.36f, {192, 151, 166, 12}, 0.001f, 128);
    renderer.DrawCircleOutline(0.90f, 0.16f, 0.39f, {192, 151, 166, 8}, 0.001f, 128);
}

void VisualStyle::DrawTextFit(sakura::core::Renderer& r, sakura::core::FontHandle font,
    std::string_view text, float x, float y, float size, float width, sakura::core::Color color)
{
    const float measured = r.MeasureTextWidth(font, text, size);
    r.DrawText(font, text, x, y, measured > width ? size * width / measured : size, color);
}

void VisualStyle::DrawSakuraLandscape(sakura::core::Renderer& r, float time, float opacity)
{
    using sakura::core::Color;
    const auto& cfg = sakura::core::Config::GetInstance();
    if (cfg.Get<bool>("graphics.reduced_motion", false)) time = 0;
    const float aspect = static_cast<float>(r.GetScreenHeight()) / std::max(1, r.GetScreenWidth());
    auto tint = [opacity](Color c) { c.a = static_cast<uint8_t>(c.a * opacity); return c; };
    constexpr float cx = 0.735f, cy = 0.405f;
    // A moon, a musical orbit, and an ink-like flowering branch form the shared artwork.
    for (int i = 14; i > 0; --i)
        r.DrawCircleFilled(cx, cy, 0.22f + i * 0.009f, tint({210, 128, 159, 2}), 96);
    r.DrawCircleFilled(cx, cy, 0.219f, tint({223, 190, 192, 255}), 128);
    for (int i = 0; i < 48; ++i)
    {
        const float k = i / 47.0f;
        const float y = cy + (k * 2 - 1) * 0.217f;
        const float half = std::sqrt(std::max(0.0f, 0.217f * 0.217f - (y-cy)*(y-cy))) * aspect;
        r.DrawLine(cx-half, y, cx+half, y, tint({248, 222, 208, static_cast<uint8_t>(18 + (1-k)*50)}), 0.004f);
    }
    r.DrawCircleOutline(cx, cy, 0.256f, tint({213, 164, 182, 60}), 0.0008f, 128);
    r.DrawArc(cx, cy, 0.265f, 130 + time*2, 244 + time*2, tint({241, 180, 196, 160}), 0.002f, 80);
    r.DrawArc(cx, cy, 0.265f, -25 + time*2, 68 + time*2, tint({148, 195, 208, 140}), 0.0015f, 64);
    for (int i=0; i<80; ++i)
    {
        const float a=i*2*std::numbers::pi_v<float>/80;
        const float len=i%5==0 ? 0.008f : 0.003f;
        r.DrawLine(cx+std::cos(a)*0.279f*aspect, cy+std::sin(a)*0.279f,
            cx+std::cos(a)*(0.279f+len)*aspect, cy+std::sin(a)*(0.279f+len), tint({197,165,189,100}), 0.0008f);
    }
    // Distant ridgelines and their reflection.
    for (int layer=0; layer<4; ++layer)
    {
        const float base=0.64f+layer*0.063f;
        for (int i=0; i<180; ++i)
        {
            const float x=0.39f+i*0.0035f;
            const float ridge=base + 0.036f*std::sin(x*18+layer*2.1f)+0.018f*std::sin(x*39+layer);
            r.DrawFilledRect({x, ridge, 0.0037f, 0.97f-ridge}, tint({static_cast<uint8_t>(31-layer*5),
                static_cast<uint8_t>(37-layer*5), static_cast<uint8_t>(54-layer*6), 220}));
        }
    }
    // The branch is evaluated from fixed control points; no per-frame randomness or allocations.
    auto branch = [&](auto&& self, float x, float y, float angle, float length, int depth) -> void
    {
        const float ex=x+std::cos(angle)*length*aspect, ey=y+std::sin(angle)*length;
        r.DrawLine(x,y,ex,ey,tint({73,55,72,255}), 0.002f+depth*0.0022f);
        r.DrawLine(x,y-0.002f,ex,ey-0.002f,tint({175,112,137,95}), 0.0008f);
        if (depth > 0)
        {
            self(self,ex,ey,angle-0.39f,length*0.73f,depth-1);
            self(self,ex,ey,angle+0.61f,length*0.59f,depth-1);
        }
        if (depth < 3)
            for (int flower=0;flower<3;++flower)
            {
                const float fx=ex+std::sin(angle*7+flower*5)*0.023f*aspect;
                const float fy=ey+std::cos(angle*11+flower*4)*0.018f;
                const float size=0.007f + 0.0015f*flower;
                for(int petal=0;petal<5;++petal)
                {
                    const float a=petal*72.0f+angle*40;
                    const float rad=a*std::numbers::pi_v<float>/180;
                    r.DrawPetal(fx+std::cos(rad)*size*0.48f*aspect,fy+std::sin(rad)*size*0.48f,
                        size,rad,tint({static_cast<uint8_t>(228+flower*9),static_cast<uint8_t>(155+flower*15),static_cast<uint8_t>(181+flower*14),240}));
                }
                r.DrawCircleFilled(fx,fy,0.0018f,tint({253,222,193,240}),12);
            }
    };
    branch(branch,1.045f,0.095f,2.89f,0.24f,5);
    if(cfg.Get<bool>("graphics.particles",true))
        for(int i=0;i<22;++i)
        {
            const float phase=i*2.39996f;
            const float y=std::fmod(i*0.143f+time*(0.018f+(i%4)*0.003f),0.9f)+0.04f;
            const float x=0.56f+std::sin(phase)*0.30f+std::sin(time*0.3f+phase)*0.018f;
            r.DrawPetal(x,y,0.003f+(i%3)*0.0015f,phase+time*0.21f,tint({245,177,199,static_cast<uint8_t>(60+i%4*22)}));
        }
    r.DrawGradientRect({0.34f,0,0.23f,1}, tint({14,18,31,255}),{14,18,31,0},tint({9,13,23,255}),{9,13,23,0});
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
