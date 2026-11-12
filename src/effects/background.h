#pragma once

// background.h — 背景渲染系统
// BackgroundRenderer：加载图片作为背景，支持暗化和居中裁切
// DefaultBackground：渐变色 + 粒子，无自定义图片时使用

#include "particle_system.h"
#include "core/renderer.h"
#include "core/resource_manager.h"

#include <string>
#include <string_view>

namespace sakura::effects
{

// ── BackgroundRenderer ────────────────────────────────────────────────────────

class BackgroundRenderer
{
public:
    BackgroundRenderer();
    ~BackgroundRenderer() { UnloadImage(); }
    BackgroundRenderer(const BackgroundRenderer&)=delete;
    BackgroundRenderer& operator=(const BackgroundRenderer&)=delete;

    // 加载背景图片（路径相对工作目录）
    // 若文件不存在则静默回退到 DefaultBackground
    bool LoadImage(std::string_view path);

    // 卸载当前图片
    void UnloadImage();

    bool HasImage() const { return m_textureHandle != sakura::core::INVALID_HANDLE; }

    // 暗化程度 0.0 = 原图, 1.0 = 全黑
    void SetDimming(float dim) { m_dimming = dim; }
    float GetDimming() const   { return m_dimming; }

    // 启用模糊（暂停或高模糊度背景时使用 Dual-Kawase 模糊）
    void SetBlurEnabled(bool enabled) { m_blurEnabled = enabled; }
    bool IsBlurEnabled()  const       { return m_blurEnabled;    }

    void SetBlurStrength(float strength) { m_blurStrength = strength; }
    float GetBlurStrength() const        { return m_blurStrength; }

    SDL_Texture* GetBlurredTexture() const { return m_blurred; }

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);

private:
    sakura::core::TextureHandle m_textureHandle = sakura::core::INVALID_HANDLE;
    float m_dimming      = 0.5f;   // 默认暗化 50%
    float m_blurStrength = -1.0f;  // <0 时自动读取配置 graphics.background_blur
    float m_breathTime   = 0.0f;
    bool  m_blurEnabled  = false;
    SDL_Texture* m_blurred = nullptr;
    int   m_blurW = 0;
    int   m_blurH = 0;
    int   m_cachedBlurQuant = -1;
};

// ── DefaultBackground ─────────────────────────────────────────────────────────
// 无自定义图片时渲染：月夜墨樱插画背景 + 极光丝带 + 散景与樱花粒子
class DefaultBackground
{
public:
    DefaultBackground();

    void Initialize(float dimming = 0.5f);

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);

private:
    float m_colorPhase = 0.0f;   // 缓慢呼吸相位
    static constexpr float COLOR_SPEED = 0.08f;

    sakura::core::TextureHandle m_artworkHandle = sakura::core::INVALID_HANDLE;
    bool m_artworkTried = false;

    ParticleSystem m_particles;
    int m_floatEmitter = -1;
    int m_petalEmitter = -1;

    float m_dimming = 0.5f;

    sakura::core::Color CalcBgColor(float phase) const;
};

} // namespace sakura::effects
