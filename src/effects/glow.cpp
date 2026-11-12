#include "glow.h"
#include "core/config.h"
#include "core/resource_manager.h"
#include "core/theme.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sakura::effects
{

namespace
{
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
}

// ============================================================================
// 内部辅助
// ============================================================================

// 构造一个 alpha 衰减版本的颜色，用于外层发光
static sakura::core::Color GlowLayerColor(const sakura::core::Color& base,
                                           float alpha01)
{
    uint8_t a = static_cast<uint8_t>(std::clamp(base.a * alpha01, 0.0f, 255.0f));
    return { base.r, base.g, base.b, a };
}

// ============================================================================
// GlowEffect 实现
// ============================================================================

static sakura::core::TextureHandle GetRadialGlowSprite()
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    static sakura::core::TextureHandle s_handle = sakura::core::INVALID_HANDLE;
    if (s_handle == sakura::core::INVALID_HANDLE || rm.GetTexture(s_handle) == nullptr)
    {
        auto loaded = rm.LoadTexture("resources/images/particles/glow_radial.png");
        s_handle = loaded.value_or(sakura::core::INVALID_HANDLE);
    }
    return s_handle;
}

static sakura::core::TextureHandle GetStreakGlowSprite()
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    static sakura::core::TextureHandle s_handle = sakura::core::INVALID_HANDLE;
    if (s_handle == sakura::core::INVALID_HANDLE || rm.GetTexture(s_handle) == nullptr)
    {
        auto loaded = rm.LoadTexture("resources/images/particles/glow_streak.png");
        s_handle = loaded.value_or(sakura::core::INVALID_HANDLE);
    }
    return s_handle;
}

void GlowEffect::DrawGlow(sakura::core::Renderer& renderer,
                           float cx, float cy,
                           float radius,
                           sakura::core::Color baseColor,
                           float glowRange,
                           int layers)
{
    if (!sakura::core::Theme::GetInstance().Settings().glowEnabled) return;
    const float intensity = sakura::core::Config::GetInstance().Get<float>("graphics.effect_intensity", 0.7f);
    if (intensity <= 0.001f || baseColor.a == 0) return;
    (void)layers;

    const float totalR = std::max(0.001f, radius + glowRange);
    const int sw = std::max(1, renderer.GetScreenWidth());
    const int sh = std::max(1, renderer.GetScreenHeight());
    const float minDim = static_cast<float>(std::min(sw, sh));
    const float normW = (totalR * 2.15f * minDim) / static_cast<float>(sw);
    const float normH = (totalR * 2.15f * minDim) / static_cast<float>(sh);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    const auto sprite = GetRadialGlowSprite();
    const float alpha01 = std::clamp((baseColor.a / 255.0f) * intensity * 0.92f, 0.0f, 1.0f);
    if (sprite != sakura::core::INVALID_HANDLE)
    {
        sakura::core::Color tint{ baseColor.r, baseColor.g, baseColor.b, 255 };
        renderer.DrawSprite(sprite, { cx - normW * 0.5f, cy - normH * 0.5f, normW, normH }, 0.0f, tint, alpha01);
        if (radius > 0.002f)
        {
            const float coreW = (radius * 2.1f * minDim) / static_cast<float>(sw);
            const float coreH = (radius * 2.1f * minDim) / static_cast<float>(sh);
            sakura::core::Color hotCore{
                static_cast<uint8_t>(std::min(255, baseColor.r + 55)),
                static_cast<uint8_t>(std::min(255, baseColor.g + 55)),
                static_cast<uint8_t>(std::min(255, baseColor.b + 55)),
                255
            };
            renderer.DrawSprite(sprite, { cx - coreW * 0.5f, cy - coreH * 0.5f, coreW, coreH }, 0.0f, hotCore, alpha01 * 0.65f);
        }
    }
    else
    {
        sakura::core::Color inner = GlowLayerColor(baseColor, intensity * 0.80f);
        sakura::core::Color outer{ baseColor.r, baseColor.g, baseColor.b, 0 };
        renderer.DrawRadialGradient(cx, cy, totalR, totalR, inner, outer, 40);
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

void GlowEffect::PulseGlow(sakura::core::Renderer& renderer,
                            float cx, float cy,
                            float sizeMin, float sizeMax,
                            sakura::core::Color baseColor,
                            float phase,
                            float frequency,
                            int layers)
{
    // sin 波形：0~1 之间
    float wave   = (std::sin(phase * frequency * kTwoPi) + 1.0f) * 0.5f;
    float radius = sizeMin + (sizeMax - sizeMin) * wave;
    float range  = radius * 0.6f;

    DrawGlow(renderer, cx, cy, radius, baseColor, range, layers);
}

void GlowEffect::DrawGlowLine(sakura::core::Renderer& renderer,
                               float x1, float y1,
                               float x2, float y2,
                               sakura::core::Color baseColor,
                               float coreThickness,
                               float glowMultiplier,
                               int layers)
{
    if (!sakura::core::Theme::GetInstance().Settings().glowEnabled) return;
    const float intensity = sakura::core::Config::GetInstance().Get<float>("graphics.effect_intensity", 0.7f);
    if (intensity <= 0.001f || baseColor.a == 0) return;
    (void)layers;

    const float fullWidth = std::max(0.001f, coreThickness * glowMultiplier);
    sakura::core::Color hotCore{
        static_cast<uint8_t>(std::min(255, baseColor.r + 48)),
        static_cast<uint8_t>(std::min(255, baseColor.g + 48)),
        static_cast<uint8_t>(std::min(255, baseColor.b + 48)),
        static_cast<uint8_t>(std::clamp(baseColor.a * intensity * 0.88f, 0.0f, 255.0f))
    };
    sakura::core::Color softEdge{ baseColor.r, baseColor.g, baseColor.b, 0 };

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    const sakura::core::RibbonNode nodes[2] = {
        { x1, y1, fullWidth, hotCore, softEdge },
        { x2, y2, fullWidth, hotCore, softEdge }
    };
    renderer.DrawRibbonStrip(nodes, 2, true);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

void GlowEffect::DrawGlowBar(sakura::core::Renderer& renderer,
                              float x, float y, float width, float height,
                              sakura::core::Color baseColor,
                              float glowExpand,
                              int layers)
{
    if (!sakura::core::Theme::GetInstance().Settings().glowEnabled) return;
    const float intensity = sakura::core::Config::GetInstance().Get<float>("graphics.effect_intensity", 0.7f);
    if (intensity <= 0.001f || baseColor.a == 0) return;
    (void)layers;

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    const auto streak = GetStreakGlowSprite();
    const float alpha01 = std::clamp((baseColor.a / 255.0f) * intensity * 0.85f, 0.0f, 1.0f);
    if (streak != sakura::core::INVALID_HANDLE)
    {
        sakura::core::NormRect haloRect{
            x - glowExpand * 1.4f,
            y - glowExpand * 2.4f,
            width + glowExpand * 2.8f,
            height + glowExpand * 4.8f
        };
        sakura::core::Color tint{ baseColor.r, baseColor.g, baseColor.b, 255 };
        renderer.DrawSprite(streak, haloRect, 0.0f, tint, alpha01);
    }
    else
    {
        renderer.DrawSoftShadow({ x, y, width, height }, height * 0.5f, glowExpand * 1.6f,
                                GlowLayerColor(baseColor, intensity * 0.72f), 0.0f, 8);
    }

    sakura::core::Color coreCol{
        static_cast<uint8_t>(std::min(255, baseColor.r + 50)),
        static_cast<uint8_t>(std::min(255, baseColor.g + 50)),
        static_cast<uint8_t>(std::min(255, baseColor.b + 50)),
        static_cast<uint8_t>(std::clamp(baseColor.a * intensity * 0.90f, 0.0f, 255.0f))
    };
    renderer.DrawRoundedRect({ x, y, width, height }, height * 0.5f, coreCol, true, 6);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

} // namespace sakura::effects
