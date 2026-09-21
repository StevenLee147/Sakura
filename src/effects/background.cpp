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
    if(m_blurred){SDL_DestroyTexture(m_blurred);m_blurred=nullptr;}
    if (m_textureHandle != sakura::core::INVALID_HANDLE)
    {
        sakura::core::ResourceManager::GetInstance().UnloadTexture(m_textureHandle);
        m_textureHandle = sakura::core::INVALID_HANDLE;
    }
}

void BackgroundRenderer::Update(float /*dt*/) {}

void BackgroundRenderer::Render(sakura::core::Renderer& renderer)
{
    auto* image=sakura::core::ResourceManager::GetInstance().GetTexture(m_textureHandle);
    if(image){
        auto* native=renderer.GetSDLRenderer();
        const int width=renderer.GetScreenWidth(),height=renderer.GetScreenHeight();
        float iw=1,ih=1;SDL_GetTextureSize(image,&iw,&ih);
        const float scale=std::max(width/iw,height/ih);
        SDL_FRect crop{(iw-width/scale)*0.5f,(ih-height/scale)*0.5f,width/scale,height/scale};
        if(m_blurEnabled && (!m_blurred || m_blurW!=width || m_blurH!=height)){
            if(m_blurred)SDL_DestroyTexture(m_blurred);
            m_blurred=SDL_CreateTexture(native,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,std::max(1,width/32),std::max(1,height/32));
            m_blurW=width;m_blurH=height;
            if(m_blurred){
                auto* previous=SDL_GetRenderTarget(native);
                if(SDL_SetRenderTarget(native,m_blurred)){
                    SDL_SetRenderDrawColor(native,0,0,0,255);SDL_RenderClear(native);
                    SDL_RenderTexture(native,image,&crop,nullptr);
                    SDL_SetRenderTarget(native,previous);
                    SDL_SetTextureScaleMode(m_blurred,SDL_SCALEMODE_LINEAR);
                }else{SDL_DestroyTexture(m_blurred);m_blurred=nullptr;}
            }
        }
        if(m_blurEnabled&&m_blurred)SDL_RenderTexture(native,m_blurred,nullptr,nullptr);
        else SDL_RenderTexture(native,image,&crop,nullptr);
    }
    // 暗化遮罩
    if (m_dimming > 0.001f)
    {
        uint8_t alpha = static_cast<uint8_t>(m_dimming * 255.0f);
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
                                sakura::core::Color{ 0, 0, 0, alpha });
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

    // 背景漂浮粒子
    m_particles.Clear();
    auto floatCfg = ParticlePresets::BackgroundFloat();
    m_floatEmitter = m_particles.EmitContinuous(0.5f, 0.5f, 2.5f, floatCfg);
}

void DefaultBackground::Update(float dt)
{
    m_colorPhase += COLOR_SPEED * dt;
    if (m_colorPhase > 6.28318f) m_colorPhase -= 6.28318f;
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
    // 渐变背景
    auto bgColor = CalcBgColor(m_colorPhase);
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, bgColor);

    // 粒子
    m_particles.Render(renderer);

    // 暗化遮罩（可选）
    if (m_dimming > 0.001f)
    {
        uint8_t alpha = static_cast<uint8_t>(m_dimming * 180.0f);
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
                                sakura::core::Color{ 0, 0, 0, alpha });
    }
}

} // namespace sakura::effects
