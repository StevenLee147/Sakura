#include "shader_manager.h"
#include "core/config.h"
#include "utils/logger.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace sakura::effects
{

// ============================================================================
// 生命周期
// ============================================================================

bool ShaderManager::Initialize(SDL_Renderer* renderer, int screenW, int screenH)
{
    if (!renderer)
    {
        LOG_ERROR("ShaderManager::Initialize: renderer 为 nullptr");
        return false;
    }
    m_renderer = renderer;

    const char* backendName = SDL_GetRendererName(renderer);
    m_hasGpuDevice = (backendName != nullptr &&
                      (std::string_view(backendName) == "gpu" ||
                       std::string_view(backendName) == "direct3d12" ||
                       std::string_view(backendName) == "vulkan"));

    // 从 Config 读取效果开关
    auto& cfg = sakura::core::Config::GetInstance();
    m_effects[static_cast<size_t>(EffectType::Vignette)]  = true;
    m_effects[static_cast<size_t>(EffectType::Blur)]      = cfg.Get<bool>("graphics.glow", true);
    m_effects[static_cast<size_t>(EffectType::ChromaAberration)]  = false;
    m_effects[static_cast<size_t>(EffectType::ColorCorrection)]   = false;

    OnResize(screenW, screenH);
    LOG_INFO("ShaderManager 初始化成功 ({}x{}, backend={})",
             screenW, screenH, backendName ? backendName : "unknown");
    return true;
}

void ShaderManager::DestroyTargets()
{
    if (m_offscreen)
    {
        SDL_DestroyTexture(m_offscreen);
        m_offscreen = nullptr;
    }
    for (int i = 0; i < kKawaseLevels; ++i)
    {
        if (m_kawaseMips[i])
        {
            SDL_DestroyTexture(m_kawaseMips[i]);
            m_kawaseMips[i] = nullptr;
        }
        m_mipW[i] = 0;
        m_mipH[i] = 0;
    }
}

void ShaderManager::OnResize(int newW, int newH)
{
    if (newW == m_width && newH == m_height && m_offscreen) return;
    DestroyTargets();
    m_width  = newW;
    m_height = newH;
    if (!m_renderer || newW <= 0 || newH <= 0) return;

    m_offscreen = SDL_CreateTexture(m_renderer,
                                    SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_TARGET,
                                    newW, newH);
    if (!m_offscreen)
    {
        LOG_ERROR("ShaderManager: 创建 offscreen 纹理失败 — {}", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(m_offscreen, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(m_offscreen, SDL_BLENDMODE_BLEND);

    int mw = newW;
    int mh = newH;
    for (int i = 0; i < kKawaseLevels; ++i)
    {
        mw = std::max(1, mw / 2);
        mh = std::max(1, mh / 2);
        m_mipW[i] = mw;
        m_mipH[i] = mh;
        m_kawaseMips[i] = SDL_CreateTexture(m_renderer,
                                            SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET,
                                            mw, mh);
        if (m_kawaseMips[i])
        {
            SDL_SetTextureScaleMode(m_kawaseMips[i], SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(m_kawaseMips[i], SDL_BLENDMODE_BLEND);
        }
    }
}

void ShaderManager::Shutdown()
{
    DestroyTargets();
    m_renderer = nullptr;
}

// ============================================================================
// 帧捕获
// ============================================================================

bool ShaderManager::BeginCapture()
{
    if (!m_renderer || !m_offscreen || SDL_GetRenderTarget(m_renderer)==m_offscreen) return false;
    m_previousTarget = SDL_GetRenderTarget(m_renderer);

    if (!SDL_SetRenderTarget(m_renderer, m_offscreen))
    {
        LOG_WARN("ShaderManager::BeginCapture: 设置渲染目标失败 — {}", SDL_GetError());
        return false;
    }
    return true;
}

SDL_Texture* ShaderManager::EndCapture()
{
    if (!m_renderer) return nullptr;

    // 恢复到默认渲染目标（屏幕）
    SDL_SetRenderTarget(m_renderer, m_previousTarget);
    return m_offscreen;
}

// ============================================================================
// 辅助：全屏 blit
// ============================================================================

void ShaderManager::BlitFull(SDL_Texture* tex,
                               int offsetX, int offsetY,
                               uint8_t r, uint8_t g, uint8_t b,
                               uint8_t alpha) const
{
    if (!tex || !m_renderer) return;

    SDL_SetTextureColorMod(tex, r, g, b);
    SDL_SetTextureAlphaMod(tex, alpha);

    SDL_FRect dst = {
        static_cast<float>(offsetX),
        static_cast<float>(offsetY),
        static_cast<float>(m_width),
        static_cast<float>(m_height)
    };
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(m_renderer, tex, nullptr, &dst);

    // 还原 mod
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

// ============================================================================
// 效果绘制
// ============================================================================

bool ShaderManager::RenderKawaseBlur(SDL_Texture* src,
                                     const SDL_FRect* srcCrop,
                                     SDL_Texture* dstTarget,
                                     float intensity)
{
    if (!src || !m_renderer) return false;
    intensity = std::clamp(intensity, 0.0f, 1.0f);

    SDL_Texture* prevTarget = SDL_GetRenderTarget(m_renderer);

    if (intensity <= 0.01f || !m_kawaseMips[0])
    {
        if (dstTarget)
        {
            if (!SDL_SetRenderTarget(m_renderer, dstTarget)) return false;
            SDL_RenderTexture(m_renderer, src, srcCrop, nullptr);
            SDL_SetRenderTarget(m_renderer, prevTarget);
        }
        else
        {
            SDL_RenderTexture(m_renderer, src, srcCrop, nullptr);
        }
        return true;
    }

    const int activeLevels = (intensity < 0.28f) ? 1 : ((intensity < 0.62f) ? 2 : 3);
    const float spread = 0.5f + intensity * 1.35f;

    // Porter-Duff energy-conserving 5-tap Dual-Kawase downsample helper
    auto downsamplePass = [&](SDL_Texture* inTex, const SDL_FRect* crop, SDL_Texture* outTex, int outW, int outH) -> bool
    {
        if (!outTex || !SDL_SetRenderTarget(m_renderer, outTex)) return false;
        SDL_SetTextureScaleMode(inTex, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(inTex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(inTex, 255, 255, 255);

        const float fw = static_cast<float>(outW);
        const float fh = static_cast<float>(outH);
        const float ox = spread;
        const float oy = spread;

        // Exact average of 4 diagonal half-texel taps (weights 1, 1/2, 1/3, 1/4) + center (weight 1/3)
        const SDL_FRect taps[5] = {
            { -ox, -oy, fw, fh },
            {  ox, -oy, fw, fh },
            { -ox,  oy, fw, fh },
            {  ox,  oy, fw, fh },
            { 0.0f, 0.0f, fw, fh }
        };
        const uint8_t alphas[5] = { 255, 128, 85, 64, 85 };
        for (int t = 0; t < 5; ++t)
        {
            SDL_SetTextureAlphaMod(inTex, alphas[t]);
            SDL_RenderTexture(m_renderer, inTex, crop, &taps[t]);
        }
        SDL_SetTextureAlphaMod(inTex, 255);
        return true;
    };

    // 8-tap Dual-Kawase tent upsample helper
    auto upsamplePass = [&](SDL_Texture* inTex, SDL_Texture* outTex, int outW, int outH) -> bool
    {
        if (!outTex || !SDL_SetRenderTarget(m_renderer, outTex)) return false;
        SDL_SetTextureScaleMode(inTex, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(inTex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(inTex, 255, 255, 255);

        const float fw = static_cast<float>(outW);
        const float fh = static_cast<float>(outH);
        const float ox = spread;
        const float oy = spread;

        const SDL_FRect taps[8] = {
            { -ox, -oy, fw, fh },
            {  ox, -oy, fw, fh },
            { -ox,  oy, fw, fh },
            {  ox,  oy, fw, fh },
            { -ox * 1.8f, 0.0f, fw, fh },
            {  ox * 1.8f, 0.0f, fw, fh },
            { 0.0f, -oy * 1.8f, fw, fh },
            { 0.0f,  oy * 1.8f, fw, fh }
        };
        const uint8_t alphas[8] = { 255, 128, 85, 64, 51, 43, 36, 32 };
        for (int t = 0; t < 8; ++t)
        {
            SDL_SetTextureAlphaMod(inTex, alphas[t]);
            SDL_RenderTexture(m_renderer, inTex, nullptr, &taps[t]);
        }
        SDL_SetTextureAlphaMod(inTex, 255);
        return true;
    };

    // Downsample chain
    if (!downsamplePass(src, srcCrop, m_kawaseMips[0], m_mipW[0], m_mipH[0]))
    {
        SDL_SetRenderTarget(m_renderer, prevTarget);
        return false;
    }
    for (int lvl = 1; lvl < activeLevels; ++lvl)
    {
        if (!downsamplePass(m_kawaseMips[lvl - 1], nullptr, m_kawaseMips[lvl], m_mipW[lvl], m_mipH[lvl]))
        {
            SDL_SetRenderTarget(m_renderer, prevTarget);
            return false;
        }
    }

    // Upsample chain back to m_kawaseMips[0]
    for (int lvl = activeLevels - 2; lvl >= 0; --lvl)
    {
        if (!upsamplePass(m_kawaseMips[lvl + 1], m_kawaseMips[lvl], m_mipW[lvl], m_mipH[lvl]))
        {
            SDL_SetRenderTarget(m_renderer, prevTarget);
            return false;
        }
    }

    // Composite to destination
    if (dstTarget && dstTarget != m_kawaseMips[0])
    {
        if (SDL_SetRenderTarget(m_renderer, dstTarget))
        {
            SDL_SetTextureAlphaMod(m_kawaseMips[0], 255);
            SDL_SetTextureBlendMode(m_kawaseMips[0], SDL_BLENDMODE_NONE);
            SDL_RenderTexture(m_renderer, m_kawaseMips[0], nullptr, nullptr);
            SDL_SetTextureBlendMode(m_kawaseMips[0], SDL_BLENDMODE_BLEND);
        }
        SDL_SetRenderTarget(m_renderer, prevTarget);
    }
    else
    {
        SDL_SetRenderTarget(m_renderer, prevTarget);
        if (!dstTarget)
        {
            SDL_SetTextureAlphaMod(m_kawaseMips[0], 255);
            SDL_SetTextureBlendMode(m_kawaseMips[0], SDL_BLENDMODE_BLEND);
            SDL_RenderTexture(m_renderer, m_kawaseMips[0], nullptr, nullptr);
        }
    }
    return true;
}

void ShaderManager::DrawBlurred(SDL_Texture* tex, float intensity)
{
    if (!tex || !m_renderer) return;
    RenderKawaseBlur(tex, nullptr, nullptr, intensity);
}

void ShaderManager::DrawBloom(SDL_Texture* tex, float intensity)
{
    if (!tex || !m_renderer || intensity <= 0.005f) return;
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    if (!RenderKawaseBlur(tex, nullptr, m_kawaseMips[0], std::clamp(intensity * 1.15f, 0.25f, 1.0f)))
        return;

    // Base crisp frame
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_RenderTexture(m_renderer, tex, nullptr, nullptr);

    // Soft Dual-Kawase sakura-gold bloom overlay
    SDL_Texture* bloomMip = m_kawaseMips[0];
    if (bloomMip)
    {
        SDL_SetTextureBlendMode(bloomMip, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(bloomMip, 255, 236, 244);
        SDL_SetTextureAlphaMod(bloomMip, static_cast<uint8_t>(std::clamp(intensity * 92.0f, 1.0f, 180.0f)));
        SDL_RenderTexture(m_renderer, bloomMip, nullptr, nullptr);
        SDL_SetTextureColorMod(bloomMip, 255, 255, 255);
        SDL_SetTextureAlphaMod(bloomMip, 255);
        SDL_SetTextureBlendMode(bloomMip, SDL_BLENDMODE_BLEND);
    }
}

void ShaderManager::DrawFrostedRegion(SDL_Texture* blurredTex,
                                      sakura::core::NormRect normRect,
                                      sakura::core::Color tint)
{
    if (!blurredTex || !m_renderer) return;
    float tw = 0.0f, th = 0.0f;
    if (!SDL_GetTextureSize(blurredTex, &tw, &th) || tw <= 0.0f || th <= 0.0f) return;

    const float nx = std::clamp(normRect.x, 0.0f, 1.0f);
    const float ny = std::clamp(normRect.y, 0.0f, 1.0f);
    const float nw = std::clamp(normRect.width, 0.0f, 1.0f - nx);
    const float nh = std::clamp(normRect.height, 0.0f, 1.0f - ny);
    if (nw <= 0.0005f || nh <= 0.0005f) return;

    SDL_FRect srcRect{ nx * tw, ny * th, nw * tw, nh * th };
    SDL_FRect dstRect{
        nx * static_cast<float>(m_width),
        ny * static_cast<float>(m_height),
        nw * static_cast<float>(m_width),
        nh * static_cast<float>(m_height)
    };

    SDL_SetTextureBlendMode(blurredTex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(blurredTex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(blurredTex, tint.a);
    SDL_RenderTexture(m_renderer, blurredTex, &srcRect, &dstRect);
    SDL_SetTextureColorMod(blurredTex, 255, 255, 255);
    SDL_SetTextureAlphaMod(blurredTex, 255);
}

void ShaderManager::DrawVignette(float intensity)
{
    if (!m_renderer || m_width <= 0 || m_height <= 0) return;
    intensity = std::clamp(intensity, 0.0f, 1.0f);

    // Two-band atmospheric moonlit indigo vignette (inner clear -> mid feather -> corner depth)
    constexpr int count = 64;
    SDL_Vertex vertices[count * 3];
    int indices[count * 12];

    const SDL_FColor cInner{ 0.02f, 0.03f, 0.07f, 0.0f };
    const SDL_FColor cMid  { 0.02f, 0.03f, 0.07f, intensity * 0.38f };
    const SDL_FColor cOuter{ 0.015f, 0.02f, 0.055f, std::min(1.0f, intensity * 1.15f) };

    for (int i = 0; i < count; ++i)
    {
        const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(count);
        const float ca = std::cos(a);
        const float sa = std::sin(a);

        vertices[i * 3 + 0] = { { m_width * (0.5f + ca * 0.42f), m_height * (0.5f + sa * 0.42f) }, cInner, { 0.0f, 0.0f } };
        vertices[i * 3 + 1] = { { m_width * (0.5f + ca * 0.64f), m_height * (0.5f + sa * 0.64f) }, cMid,   { 0.0f, 0.0f } };
        vertices[i * 3 + 2] = { { m_width * (0.5f + ca * 0.88f), m_height * (0.5f + sa * 0.88f) }, cOuter, { 0.0f, 0.0f } };

        const int j = (i + 1) % count;
        const int base = i * 12;
        // Inner-to-mid ring
        indices[base + 0] = i * 3 + 0; indices[base + 1] = i * 3 + 1; indices[base + 2] = j * 3 + 0;
        indices[base + 3] = j * 3 + 0; indices[base + 4] = i * 3 + 1; indices[base + 5] = j * 3 + 1;
        // Mid-to-outer ring
        indices[base + 6] = i * 3 + 1; indices[base + 7] = i * 3 + 2; indices[base + 8] = j * 3 + 1;
        indices[base + 9] = j * 3 + 1; indices[base + 10] = i * 3 + 2; indices[base + 11] = j * 3 + 2;
    }
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(m_renderer, nullptr, vertices, count * 3, indices, count * 12);
}

void ShaderManager::DrawChromaticAberration(SDL_Texture* tex, float intensity)
{
    if (!tex || !m_renderer) return;

    intensity = std::clamp(intensity, 0.0f, 1.0f);
    int ofs = std::max(1, static_cast<int>(intensity * m_width * 0.003f));

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);

    // R 通道：向右/上偏移
    SDL_SetTextureColorMod(tex, 255, 0, 0);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(35*intensity));
    SDL_FRect dstR = { static_cast<float>(ofs), static_cast<float>(-ofs),
                        static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dstR);

    // G 通道：无偏移
    SDL_SetTextureColorMod(tex, 0, 255, 0);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(35*intensity));
    SDL_FRect dstG = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dstG);

    // B 通道：向左/下偏移
    SDL_SetTextureColorMod(tex, 0, 0, 255);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(35*intensity));
    SDL_FRect dstB = { static_cast<float>(-ofs), static_cast<float>(ofs),
                        static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dstB);

    // 还原
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
}

void ShaderManager::DrawColorCorrection(sakura::core::Color tint, float alpha)
{
    if (!m_renderer) return;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, tint.r, tint.g, tint.b,
                           static_cast<uint8_t>(alpha * 255.0f));
    SDL_FRect full = { 0.0f, 0.0f,
                        static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderFillRect(m_renderer, &full);
}

// ============================================================================
// 效果开关
// ============================================================================

void ShaderManager::EnableEffect(EffectType type)
{
    m_effects[static_cast<size_t>(type)] = true;
}

void ShaderManager::DisableEffect(EffectType type)
{
    m_effects[static_cast<size_t>(type)] = false;
}

bool ShaderManager::IsEffectEnabled(EffectType type) const
{
    return m_effects[static_cast<size_t>(type)];
}

// ============================================================================
// 综合后处理
// ============================================================================

void ShaderManager::ApplyPostProcess()
{
    if(sakura::core::Config::GetInstance().Get<bool>("graphics.vignette",true))
        DrawVignette(0.26f);
}

} // namespace sakura::effects
