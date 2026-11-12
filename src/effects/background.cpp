// background.cpp — 背景渲染系统实现

#include "background.h"
#include "shader_manager.h"
#include "core/config.h"
#include "core/theme.h"
#include "utils/logger.h"

#include <cmath>
#include <algorithm>
#include <filesystem>

namespace sakura::effects
{

// ============================================================================
// BackgroundRenderer
// ============================================================================

BackgroundRenderer::BackgroundRenderer() = default;

bool BackgroundRenderer::LoadImage(std::string_view path)
{
    UnloadImage();

    if (path.empty()) return false;
    if (!std::filesystem::exists(path))
    {
        LOG_WARN("[BackgroundRenderer] 背景图不存在: {}", path);
        return false;
    }

    auto& rm = sakura::core::ResourceManager::GetInstance();
    auto  handle = rm.LoadTexture(std::string(path));
    if (!handle.has_value())
    {
        LOG_WARN("[BackgroundRenderer] 无法加载背景图: {}", path);
        return false;
    }

    m_textureHandle = handle.value();
    LOG_INFO("[BackgroundRenderer] 已加载背景图: {}", path);
    return true;
}

void BackgroundRenderer::UnloadImage()
{
    if (m_blurred)
    {
        SDL_DestroyTexture(m_blurred);
        m_blurred = nullptr;
    }
    m_blurW = 0;
    m_blurH = 0;
    m_cachedBlurQuant = -1;
    if (m_textureHandle != sakura::core::INVALID_HANDLE)
    {
        sakura::core::ResourceManager::GetInstance().UnloadTexture(m_textureHandle);
        m_textureHandle = sakura::core::INVALID_HANDLE;
    }
}

void BackgroundRenderer::Update(float dt)
{
    if (!sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false))
    {
        m_breathTime += dt;
    }
}

void BackgroundRenderer::Render(sakura::core::Renderer& renderer)
{
    auto* image = sakura::core::ResourceManager::GetInstance().GetTexture(m_textureHandle);
    if (image)
    {
        auto* native = renderer.GetSDLRenderer();
        const int width = std::max(1, renderer.GetScreenWidth());
        const int height = std::max(1, renderer.GetScreenHeight());

        float iw = 1.0f, ih = 1.0f;
        SDL_GetTextureSize(image, &iw, &ih);
        const float breathZoom = 1.0f + 0.016f * (0.5f + 0.5f * std::sin(m_breathTime * 0.22f));
        const float baseScale = std::max(static_cast<float>(width) / std::max(1.0f, iw),
                                         static_cast<float>(height) / std::max(1.0f, ih));
        const float baseCropW = static_cast<float>(width) / baseScale;
        const float baseCropH = static_cast<float>(height) / baseScale;
        const SDL_FRect baseCrop{
            (iw - baseCropW) * 0.5f,
            (ih - baseCropH) * 0.5f,
            baseCropW,
            baseCropH
        };

        const float cfgBlur = (m_blurStrength >= 0.0f)
            ? m_blurStrength
            : sakura::core::Config::GetInstance().Get<float>("graphics.background_blur", 0.18f);
        const float effectiveBlur = m_blurEnabled ? std::max(0.75f, cfgBlur) : cfgBlur;
        const int blurQuant = static_cast<int>(std::round(std::clamp(effectiveBlur, 0.0f, 1.0f) * 20.0f));

        if (blurQuant > 0)
        {
            const int targetW = std::max(1, width / 2);
            const int targetH = std::max(1, height / 2);
            if (!m_blurred || m_blurW != targetW || m_blurH != targetH)
            {
                if (m_blurred) SDL_DestroyTexture(m_blurred);
                m_blurred = SDL_CreateTexture(native, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, targetW, targetH);
                if (m_blurred)
                {
                    SDL_SetTextureScaleMode(m_blurred, SDL_SCALEMODE_LINEAR);
                    SDL_SetTextureBlendMode(m_blurred, SDL_BLENDMODE_BLEND);
                }
                m_blurW = targetW;
                m_blurH = targetH;
                m_cachedBlurQuant = -1;
            }
            if (m_blurred && m_cachedBlurQuant != blurQuant)
            {
                if (ShaderManager::GetInstance().RenderKawaseBlur(image, &baseCrop, m_blurred, effectiveBlur))
                {
                    m_cachedBlurQuant = blurQuant;
                }
            }
        }

        // Subtle breathing crop applied at final blit so the cached Kawase texture doesn't rebuild every frame
        SDL_Texture* activeTex = (blurQuant > 0 && m_blurred) ? m_blurred : image;
        float aw = 1.0f, ah = 1.0f;
        SDL_GetTextureSize(activeTex, &aw, &ah);
        SDL_FRect finalCrop;
        if (activeTex == m_blurred)
        {
            const float zw = aw / breathZoom;
            const float zh = ah / breathZoom;
            finalCrop = { (aw - zw) * 0.5f, (ah - zh) * 0.5f, zw, zh };
        }
        else
        {
            const float zw = baseCrop.w / breathZoom;
            const float zh = baseCrop.h / breathZoom;
            finalCrop = {
                baseCrop.x + (baseCrop.w - zw) * 0.5f,
                baseCrop.y + (baseCrop.h - zh) * 0.5f,
                zw, zh
            };
        }
        SDL_SetTextureScaleMode(activeTex, SDL_SCALEMODE_LINEAR);
        SDL_RenderTexture(native, activeTex, &finalCrop, nullptr);
    }

    // 月夜墨樱暗化与上下氛围渐晕
    if (m_dimming > 0.001f)
    {
        const float dim = std::clamp(m_dimming, 0.0f, 1.0f);
        const uint8_t topA = static_cast<uint8_t>(std::min(255.0f, dim * 245.0f));
        const uint8_t midA = static_cast<uint8_t>(std::min(255.0f, dim * 215.0f));
        renderer.DrawGradientRect(
            { 0.0f, 0.0f, 1.0f, 1.0f },
            { 8, 10, 22, topA }, { 12, 10, 26, midA },
            { 6, 8, 18, topA },  { 10, 12, 24, topA });
    }
}

// ============================================================================
// DefaultBackground
// ============================================================================

DefaultBackground::DefaultBackground() = default;

void DefaultBackground::Initialize(float dimming)
{
    m_dimming    = dimming;
    m_colorPhase = 0.0f;

    if (!m_artworkTried)
    {
        m_artworkTried = true;
        if (std::filesystem::exists("resources/images/backgrounds/moonlit_sakura_bg.png"))
        {
            auto loaded = sakura::core::ResourceManager::GetInstance().LoadTexture(
                "resources/images/backgrounds/moonlit_sakura_bg.png");
            m_artworkHandle = loaded.value_or(sakura::core::INVALID_HANDLE);
        }
    }

    m_particles.Clear();
    if (m_floatEmitter >= 0) m_particles.StopEmitter(m_floatEmitter);
    if (m_petalEmitter >= 0) m_particles.StopEmitter(m_petalEmitter);
    m_floatEmitter = m_particles.EmitContinuous(0.5f, 0.5f, 2.8f, ParticlePresets::BackgroundFloat());
    m_petalEmitter = m_particles.EmitContinuous(0.5f, -0.04f, 1.8f, ParticlePresets::SakuraPetalBackground());
}

void DefaultBackground::Update(float dt)
{
    if (!sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false))
    {
        m_colorPhase += COLOR_SPEED * dt;
        if (m_colorPhase > 62.83185f) m_colorPhase -= 62.83185f;
    }

    m_particles.Update(dt);
}

sakura::core::Color DefaultBackground::CalcBgColor(float phase) const
{
    // 在 3 个色调之间缓慢切换
    // 色调 A: 深夜蓝  (10, 8, 22)
    // 色调 B: 深紫    (18, 8, 30)
    // 色调 C: 深蓝绿  (5, 12, 22)
    float t1 = (std::sinf(phase) + 1.0f) * 0.5f;
    float t2 = (std::sinf(phase + 2.094f) + 1.0f) * 0.5f;  // +120°

    uint8_t r = static_cast<uint8_t>(10 + t1 * 8  + t2 * 3);
    uint8_t g = static_cast<uint8_t>( 8 + t1 * 2  + t2 * 4);
    uint8_t b = static_cast<uint8_t>(22 + t1 * 8  + t2 * 4);

    auto base = sakura::core::Theme::GetInstance().BgColor();
    return {
        static_cast<uint8_t>(std::clamp<int>(base.r + r - 10, 0, 255)),
        static_cast<uint8_t>(std::clamp<int>(base.g + g - 8, 0, 255)),
        static_cast<uint8_t>(std::clamp<int>(base.b + b - 22, 0, 255)),
        255
    };
}

void DefaultBackground::Render(sakura::core::Renderer& renderer)
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    if (m_artworkHandle != sakura::core::INVALID_HANDLE && rm.GetTexture(m_artworkHandle) != nullptr)
    {
        const float zoom = 1.02f + 0.015f * std::sin(m_colorPhase * 1.4f);
        const float driftX = 0.008f * std::cos(m_colorPhase * 0.9f);
        renderer.DrawSprite(
            m_artworkHandle,
            { 0.5f - 0.5f * zoom + driftX, 0.5f - 0.5f * zoom, zoom, zoom },
            0.0f, sakura::core::Color::White, 1.0f);
    }
    else
    {
        auto bgColor = CalcBgColor(m_colorPhase);
        renderer.DrawGradientRect(
            { 0.0f, 0.0f, 1.0f, 1.0f },
            { 16, 20, 38, 255 }, { 34, 24, 46, 255 },
            bgColor, { 12, 15, 28, 255 });
    }

    // Luminous moonlit silk aurora ribbon
    std::array<sakura::core::NormPoint, 6> silkPts{};
    for (int i = 0; i < 6; ++i)
    {
        const float u = static_cast<float>(i) / 5.0f;
        const float wave = std::sin(u * 3.6f + m_colorPhase * 2.2f) * 0.045f;
        silkPts[static_cast<size_t>(i)] = { -0.05f + u * 1.10f, 0.28f + u * 0.24f + wave };
    }
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawCatmullRomRibbon(
        silkPts.data(), 6,
        0.045f, 0.075f,
        { 244, 174, 200, 18 }, { 148, 196, 225, 16 },
        { 244, 174, 200, 0 },  { 148, 196, 225, 0 },
        6, false);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    // 散景与樱花粒子
    m_particles.Render(renderer);

    // 月夜深靛暗化层
    if (m_dimming > 0.001f)
    {
        const uint8_t alpha = static_cast<uint8_t>(std::clamp(m_dimming * 205.0f, 0.0f, 255.0f));
        renderer.DrawGradientRect(
            { 0.0f, 0.0f, 1.0f, 1.0f },
            { 8, 10, 22, alpha }, { 12, 11, 26, static_cast<uint8_t>(alpha * 0.85f) },
            { 6, 8, 18, alpha },  { 9, 11, 22, alpha });
    }
}

} // namespace sakura::effects
