// background.cpp — 背景渲染系统实现

#include "background.h"
#include "shader_manager.h"
#include "core/config.h"
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
    if (m_textureHandle != sakura::core::INVALID_HANDLE)
    {
        sakura::core::ResourceManager::GetInstance().UnloadTexture(m_textureHandle);
        m_textureHandle = sakura::core::INVALID_HANDLE;
    }
}

void BackgroundRenderer::Update(float /*dt*/) {}

void BackgroundRenderer::Render(sakura::core::Renderer& renderer)
{
    if (m_textureHandle != sakura::core::INVALID_HANDLE)
    {
        // 铺满屏幕（居中裁切由 DrawSprite 的 NormRect{0,0,1,1} 决定）
        // 使用 BlurEnabled 选择是否先捕获到 ShaderManager 然后模糊
        if (m_blurEnabled)
        {
            renderer.DrawSprite(m_textureHandle,
                { 0.0f, 0.0f, 1.0f, 1.0f }, 0.0f,
                sakura::core::Color::White, 1.0f);
            ShaderManager::GetInstance().DrawBlurred(nullptr, 0.5f);
        }
        else
        {
            renderer.DrawSprite(m_textureHandle,
                { 0.0f, 0.0f, 1.0f, 1.0f }, 0.0f,
                sakura::core::Color::White, 1.0f);
        }
    }
    else
    {
        // 无图片：黑色背景
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
                                sakura::core::Color{ 10, 8, 22, 255 });
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

    return { r, g, b, 255 };
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
