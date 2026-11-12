// visual_style.cpp — Sakura 唯一主视觉的页面级绘制与组件样式助手

#include "visual_style.h"
#include "core/theme.h"
#include "core/config.h"
#include "core/resource_manager.h"
#include "effects/shader_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace sakura::ui
{

namespace
{
sakura::core::Color WithAlpha(sakura::core::Color color, float alphaScale)
{
    alphaScale = std::clamp(alphaScale, 0.0f, 1.0f);
    color.a = static_cast<uint8_t>(std::clamp(static_cast<float>(color.a) * alphaScale, 0.0f, 255.0f));
    return color;
}

sakura::core::TextureHandle LoadCachedUITexture(sakura::core::TextureHandle& slot, const char* path)
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    if (slot == sakura::core::INVALID_HANDLE || rm.GetTexture(slot) == nullptr)
    {
        slot = rm.LoadTexture(path).value_or(sakura::core::INVALID_HANDLE);
    }
    return slot;
}
}

sakura::core::TextureHandle VisualStyle::GetSakuraCrestTexture()
{
    static sakura::core::TextureHandle s_tex = sakura::core::INVALID_HANDLE;
    return LoadCachedUITexture(s_tex, "resources/images/ui/sakura_crest.png");
}

sakura::core::TextureHandle VisualStyle::GetDefaultCoverTexture()
{
    static sakura::core::TextureHandle s_tex = sakura::core::INVALID_HANDLE;
    return LoadCachedUITexture(s_tex, "resources/images/covers/default_cover.png");
}

sakura::core::TextureHandle VisualStyle::GetMoonlitBackdropTexture()
{
    static sakura::core::TextureHandle s_tex = sakura::core::INVALID_HANDLE;
    return LoadCachedUITexture(s_tex, "resources/images/backgrounds/moonlit_sakura_bg.png");
}

sakura::core::TextureHandle VisualStyle::GetRankEmblemTexture(std::string_view grade)
{
    static std::array<sakura::core::TextureHandle, 6> s_ranks{
        sakura::core::INVALID_HANDLE, sakura::core::INVALID_HANDLE, sakura::core::INVALID_HANDLE,
        sakura::core::INVALID_HANDLE, sakura::core::INVALID_HANDLE, sakura::core::INVALID_HANDLE
    };
    if (grade == "SS") return LoadCachedUITexture(s_ranks[0], "resources/images/ui/rank_ss.png");
    if (grade == "S")  return LoadCachedUITexture(s_ranks[1], "resources/images/ui/rank_s.png");
    if (grade == "A")  return LoadCachedUITexture(s_ranks[2], "resources/images/ui/rank_a.png");
    if (grade == "B")  return LoadCachedUITexture(s_ranks[3], "resources/images/ui/rank_b.png");
    if (grade == "C")  return LoadCachedUITexture(s_ranks[4], "resources/images/ui/rank_c.png");
    return LoadCachedUITexture(s_ranks[5], "resources/images/ui/rank_d.png");
}

void VisualStyle::DrawSceneBackground(sakura::core::Renderer& renderer)
{
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, { 10, 14, 26, 255 });

    const auto bgTex = GetMoonlitBackdropTexture();
    if (bgTex != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawSprite(bgTex, { 0.0f, 0.0f, 1.0f, 1.0f }, 0.0f, sakura::core::Color::White, 0.42f);
    }

    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 12, 17, 32, 210 }, { 28, 22, 44, 165 },
        {  8, 11, 22, 238 }, { 16, 20, 36, 218 });

    // 柔和月晕与樱雾光斑
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(0.84f, 0.22f, 0.42f, 0.42f, { 246, 178, 208, 24 }, { 246, 178, 208, 0 }, 56);
    renderer.DrawRadialGradient(0.16f, 0.84f, 0.46f, 0.46f, { 112, 138, 214, 18 }, { 112, 138, 214, 0 }, 56);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    // 极细天球星轨装饰环
    renderer.DrawCircleOutline(0.86f, 0.20f, 0.34f, { 250, 216, 157, 18 }, 0.0009f, 96);
    renderer.DrawCircleOutline(0.86f, 0.20f, 0.375f, { 246, 172, 200, 14 }, 0.0008f, 96);
}

void VisualStyle::DrawTextFit(sakura::core::Renderer& r, sakura::core::FontHandle font,
    std::string_view text, float x, float y, float size, float width, sakura::core::Color color)
{
    if (width <= 0.0001f || text.empty()) return;
    const float measured = r.MeasureTextWidth(font, text, size);
    r.DrawText(font, text, x, y, measured > width ? size * width / measured : size, color);
}

void VisualStyle::DrawSakuraLandscape(sakura::core::Renderer& r, float time, float opacity)
{
    using sakura::core::Color;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity <= 0.001f) return;

    const auto& cfg = sakura::core::Config::GetInstance();
    const bool reducedMotion = cfg.Get<bool>("graphics.reduced_motion", false);
    if (reducedMotion) time = 0.0f;

    const float aspect = static_cast<float>(r.GetScreenHeight()) / std::max(1, r.GetScreenWidth());
    auto tint = [opacity](Color c)
    {
        c.a = static_cast<uint8_t>(std::clamp(static_cast<float>(c.a) * opacity, 0.0f, 255.0f));
        return c;
    };

    // 1. 高清月夜墨樱插画层（带缓慢呼吸推镜）
    const auto bgTex = GetMoonlitBackdropTexture();
    if (bgTex != sakura::core::INVALID_HANDLE)
    {
        const float zoom = reducedMotion ? 1.0f : (1.018f + 0.012f * std::sin(time * 0.22f));
        const float driftX = reducedMotion ? 0.0f : (0.006f * std::cos(time * 0.16f));
        const float driftY = reducedMotion ? 0.0f : (0.004f * std::sin(time * 0.19f));
        sakura::core::NormRect artRect{
            0.5f - 0.5f * zoom + driftX,
            0.5f - 0.5f * zoom + driftY,
            zoom,
            zoom
        };
        r.DrawSprite(bgTex, artRect, 0.0f, sakura::core::Color::White, 0.88f * opacity);
    }

    // 2. 矢量天球音律星轨与月轮呼吸光环
    constexpr float cx = 0.725f, cy = 0.345f;
    r.SetBlendMode(sakura::core::BlendMode::Additive);
    r.DrawRadialGradient(cx, cy, 0.30f, 0.30f, tint({ 252, 214, 198, 34 }), tint({ 246, 166, 204, 0 }), 64);
    r.DrawRingGradient(cx, cy, 0.214f, 0.224f, tint({ 250, 216, 157, 72 }), tint({ 250, 216, 157, 0 }), 80);

    // 流光极光丝带横贯月影
    std::array<sakura::core::NormPoint, 6> silkPts{{
        { 0.36f, 0.72f + 0.012f * std::sin(time * 0.45f) },
        { 0.50f, 0.62f + 0.016f * std::cos(time * 0.38f + 0.8f) },
        { 0.64f, 0.52f + 0.014f * std::sin(time * 0.42f + 1.6f) },
        { 0.78f, 0.46f + 0.015f * std::cos(time * 0.36f + 2.4f) },
        { 0.91f, 0.39f + 0.012f * std::sin(time * 0.40f + 3.1f) },
        { 1.03f, 0.34f }
    }};
    r.DrawCatmullRomRibbon(
        silkPts.data(), static_cast<int>(silkPts.size()),
        0.016f, 0.028f,
        tint({ 246, 172, 200, 32 }), tint({ 250, 216, 157, 46 }),
        tint({ 156, 142, 232, 0 }),  tint({ 246, 172, 200, 0 }),
        10, true);
    r.SetBlendMode(sakura::core::BlendMode::Alpha);

    // 精密天球刻度与旋转音律双弧
    r.DrawArc(cx, cy, 0.242f, 124.0f + time * 3.2f, 248.0f + time * 3.2f, tint({ 250, 216, 157, 155 }), 0.0018f, 80);
    r.DrawArc(cx, cy, 0.242f, -32.0f + time * 3.2f,  72.0f + time * 3.2f, tint({ 246, 172, 200, 145 }), 0.0016f, 64);
    r.DrawArc(cx, cy, 0.258f,  30.0f - time * 2.1f, 145.0f - time * 2.1f, tint({ 158, 206, 238, 110 }), 0.0012f, 64);
    for (int i = 0; i < 72; ++i)
    {
        const float a = static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / 72.0f + time * 0.015f;
        const float inner = 0.268f;
        const float len = (i % 6 == 0) ? 0.009f : 0.0032f;
        const Color tickCol = (i % 6 == 0) ? tint({ 250, 216, 157, 130 }) : tint({ 214, 184, 208, 68 });
        r.DrawLine(
            cx + std::cos(a) * inner * aspect, cy + std::sin(a) * inner,
            cx + std::cos(a) * (inner + len) * aspect, cy + std::sin(a) * (inner + len),
            tickCol, 0.0008f);
    }

    // 3. 3D 翻飞前景樱瓣（使用高精度樱瓣精灵或矢量回退）
    if (cfg.Get<bool>("graphics.particles", true))
    {
        static sakura::core::TextureHandle s_petalTex = sakura::core::INVALID_HANDLE;
        const auto petalTex = LoadCachedUITexture(s_petalTex, "resources/images/particles/petal_primary.png");
        for (int i = 0; i < 20; ++i)
        {
            const float phase = static_cast<float>(i) * 2.39996f;
            const float y = std::fmod(static_cast<float>(i) * 0.137f + time * (0.019f + static_cast<float>(i % 4) * 0.004f), 0.96f) + 0.02f;
            const float x = 0.54f + std::sin(phase) * 0.34f + std::sin(time * 0.36f + phase) * 0.022f;
            const float size = 0.009f + static_cast<float>(i % 3) * 0.0035f;
            const float rot = phase + time * 0.38f;
            const float tumble = 0.38f + 0.62f * std::abs(std::cos(time * 1.35f + phase));
            const Color pCol = tint({ 252, static_cast<uint8_t>(186 + (i % 3) * 14), static_cast<uint8_t>(208 + (i % 3) * 10), static_cast<uint8_t>(95 + (i % 4) * 28) });

            if (petalTex != sakura::core::INVALID_HANDLE)
            {
                const float ca = std::cos(rot), sa = std::sin(rot);
                const float hw = size * aspect;
                const float hh = size * tumble;
                auto mkPt = [&](float lx, float ly) -> sakura::core::NormPoint {
                    return { x + (lx * ca - ly * sa), y + (lx * sa + ly * ca) };
                };
                r.DrawSpriteQuad(
                    petalTex,
                    mkPt(-hw, -hh), mkPt(hw, -hh), mkPt(hw, hh), mkPt(-hw, hh),
                    pCol, static_cast<float>(pCol.a) / 255.0f);
            }
            else
            {
                r.DrawPetal(x, y, size * 0.55f, rot, pCol);
            }
        }
    }

    // 左侧菜单文字阅读保护渐变遮罩
    r.DrawGradientRect(
        { 0.0f, 0.0f, 0.52f, 1.0f },
        tint({ 10, 14, 26, 232 }), tint({ 10, 14, 26, 0 }),
        tint({  8, 11, 22, 244 }), tint({  8, 11, 22, 0 }));
}

void VisualStyle::DrawPlayfieldBackground(sakura::core::Renderer& renderer)
{
    const auto& colors = sakura::core::Theme::GetInstance().Colors();
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, colors.bg);
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        {  9, 13, 25, 255 }, { 22, 18, 38, 255 },
        {  6,  9, 18, 255 }, { 12, 15, 28, 255 });
}

void VisualStyle::DrawGlassCard(sakura::core::Renderer& renderer,
                                sakura::core::NormRect rect,
                                sakura::core::Color topTint,
                                sakura::core::Color bottomTint,
                                sakura::core::Color topBorder,
                                sakura::core::Color bottomBorder,
                                float cornerRadius,
                                float shadowAlpha)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;

    if (shadowAlpha > 0.005f)
    {
        renderer.DrawSoftShadow(
            rect,
            cornerRadius,
            0.016f,
            { 3, 5, 14, static_cast<uint8_t>(std::clamp(shadowAlpha * 255.0f, 0.0f, 255.0f)) },
            0.006f,
            12);
    }

    // 如果存在 Kawase 模糊背景缓存，先投射一层轻度磨砂折射纹理
    if (auto* blurredMip = sakura::effects::ShaderManager::GetInstance().GetBlurredMipTexture())
    {
        sakura::effects::ShaderManager::GetInstance().DrawFrostedRegion(
            blurredMip, rect, { 220, 226, 245, 38 });
    }

    // 主磨砂玻璃四角渐变填充
    sakura::core::Color topRight{
        static_cast<uint8_t>(std::min(255, static_cast<int>(topTint.r) + 8)),
        static_cast<uint8_t>(std::min(255, static_cast<int>(topTint.g) + 6)),
        static_cast<uint8_t>(std::min(255, static_cast<int>(topTint.b) + 12)),
        topTint.a
    };
    renderer.DrawRoundedGradientRect(
        rect, cornerRadius,
        topTint, topRight,
        bottomTint, bottomTint,
        14, true);

    // 顶部玻璃镜面反光带（内缩薄层）
    const float specH = std::min(rect.height * 0.28f, 0.036f);
    if (specH > cornerRadius * 0.6f)
    {
        renderer.DrawRoundedGradientRect(
            { rect.x + 0.0015f, rect.y + 0.0015f, std::max(0.0f, rect.width - 0.003f), specH },
            cornerRadius * 0.9f,
            { 255, 246, 250, 26 }, { 250, 226, 198, 18 },
            { 255, 246, 250,  0 }, { 250, 226, 198,  0 },
            10, false);
    }

    // 上沿鎏金 + 下沿月影无接缝渐变描边
    renderer.DrawRoundedRectOutlineGradient(
        rect, cornerRadius,
        topBorder, bottomBorder,
        0.0014f, 14);
}

void VisualStyle::DrawPanel(sakura::core::Renderer& renderer,
                            sakura::core::NormRect rect,
                            bool modal,
                            bool accent)
{
    const auto& style = modal
        ? sakura::core::Theme::GetInstance().Components().modal
        : sakura::core::Theme::GetInstance().Components().panel;

    DrawGlassCard(
        renderer,
        rect,
        style.fill,
        style.fillBottom,
        style.border,
        style.borderSubtle,
        style.cornerRadius,
        static_cast<float>(style.shadow.a) / 255.0f);

    if (accent)
    {
        const float barInset = style.cornerRadius * 0.85f;
        const float barW = std::max(0.0f, rect.width - barInset * 2.0f);
        if (barW > 0.004f)
        {
            const float y = rect.y + 0.0022f;
            const float midX = rect.x + rect.width * 0.5f;
            sakura::core::Color edgeCol = WithAlpha(style.accent, 0.0f);
            sakura::core::Color centerCol = style.accent;
            renderer.DrawGradientRect(
                { rect.x + barInset, y, barW * 0.5f, 0.0028f },
                edgeCol, centerCol, edgeCol, centerCol);
            renderer.DrawGradientRect(
                { midX, y, barW * 0.5f, 0.0028f },
                centerCol, edgeCol, centerCol, edgeCol);
        }
    }
}

void VisualStyle::DrawBadgePill(sakura::core::Renderer& renderer,
                                sakura::core::NormRect rect,
                                std::string_view text,
                                sakura::core::FontHandle font,
                                float fontSize,
                                sakura::core::Color fill,
                                sakura::core::Color border,
                                sakura::core::Color textColor)
{
    const float r = rect.height * 0.5f;
    sakura::core::Color fillBot = WithAlpha(fill, 0.78f);
    renderer.DrawRoundedGradientRect(rect, r, fill, fill, fillBot, fillBot, 10, true);
    renderer.DrawRoundedRectOutlineGradient(
        rect, r, border, WithAlpha(border, 0.35f), 0.0012f, 10);
    if (font != sakura::core::INVALID_HANDLE && !text.empty())
    {
        const auto m = renderer.MeasureText(font, text, fontSize);
        renderer.DrawText(
            font, text,
            rect.x + rect.width * 0.5f,
            rect.y + (rect.height - m.height) * 0.5f,
            fontSize, textColor, sakura::core::TextAlign::Center);
    }
}

void VisualStyle::DrawGildedDivider(sakura::core::Renderer& renderer,
                                    float x1, float x2, float y,
                                    float alpha)
{
    if (x2 <= x1) return;
    const auto& theme = sakura::core::Theme::GetInstance().Colors();
    const sakura::core::Color mid = WithAlpha(theme.gildedHairline, alpha);
    const sakura::core::Color edge = { mid.r, mid.g, mid.b, 0 };
    const float mx = (x1 + x2) * 0.5f;
    renderer.DrawGradientRect({ x1, y, mx - x1, 0.0014f }, edge, mid, edge, mid);
    renderer.DrawGradientRect({ mx, y, x2 - mx, 0.0014f }, mid, edge, mid, edge);
}

void VisualStyle::DrawScrim(sakura::core::Renderer& renderer, float opacity)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    const uint8_t aTop = static_cast<uint8_t>(235.0f * opacity);
    const uint8_t aBot = static_cast<uint8_t>(255.0f * opacity);
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 6, 9, 18, aTop }, { 10, 12, 24, aTop },
        { 4, 6, 14, aBot }, {  8, 10, 20, aBot });
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
    list->SetBgColor({ 14, 19, 34, 205 });
    list->SetNormalColor({ 24, 31, 54, 196 });
    list->SetHoverColor({ 46, 56, 90, 226 });
    list->SetSelectedColor({ 86, 62, 118, 242 });
    list->SetTextColor(colors.text);
}

} // namespace sakura::ui
