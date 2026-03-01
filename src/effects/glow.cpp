#include "glow.h"

#include <cmath>
#include <algorithm>

namespace sakura::effects
{

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

void GlowEffect::DrawGlow(sakura::core::Renderer& renderer,
                           float cx, float cy,
                           float radius,
                           sakura::core::Color baseColor,
                           float glowRange,
                           int layers)
{
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    for (int i = layers; i >= 0; --i)
    {
        float t      = static_cast<float>(i) / static_cast<float>(layers); // 1=外层, 0=核心
        float r      = radius + t * glowRange;
        float alpha  = (1.0f - t) * 0.85f + 0.15f;   // 核心最亮，外层渐暗
        // 外层使用更低 alpha
        float layerAlpha = alpha * (1.0f - t * 0.6f);
        renderer.DrawCircleFilled(cx, cy, r,
                                  GlowLayerColor(baseColor, layerAlpha), 32);
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
    float wave   = (std::sin(phase * frequency * 2.0f * 3.14159265f) + 1.0f) * 0.5f;
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
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    for (int i = layers; i >= 0; --i)
    {
        float t         = static_cast<float>(i) / static_cast<float>(layers);
        float thickness = coreThickness * (1.0f + t * (glowMultiplier - 1.0f));
        float layerAlpha = (1.0f - t * 0.8f);
        renderer.DrawLine(x1, y1, x2, y2,
                          GlowLayerColor(baseColor, layerAlpha),
                          thickness);
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

void GlowEffect::DrawGlowBar(sakura::core::Renderer& renderer,
                              float x, float y, float width, float height,
                              sakura::core::Color baseColor,
                              float glowExpand,
                              int layers)
{
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    for (int i = layers; i >= 0; --i)
    {
        float t       = static_cast<float>(i) / static_cast<float>(layers);
        float expand  = t * glowExpand;
        float alpha   = 1.0f - t * 0.85f;

        sakura::core::NormRect rect{
            x - expand,
            y - expand,
            width  + expand * 2.0f,
            height + expand * 2.0f
        };
        renderer.DrawFilledRect(rect, GlowLayerColor(baseColor, alpha));
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

} // namespace sakura::effects
