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
    ~BackgroundRenderer() = default;

    // 加载背景图片（路径相对工作目录）
    // 若文件不存在则静默回退到 DefaultBackground
    bool LoadImage(std::string_view path);

    // 卸载当前图片
    void UnloadImage();

    bool HasImage() const { return m_textureHandle != sakura::core::INVALID_HANDLE; }

    // 暗化程度 0.0 = 原图, 1.0 = 全黑
    void SetDimming(float dim) { m_dimming = dim; }
    float GetDimming() const   { return m_dimming; }

    // 启用模糊（暂停时使用 ShaderManager 模糊）
    void SetBlurEnabled(bool enabled) { m_blurEnabled = enabled; }
    bool IsBlurEnabled()  const       { return m_blurEnabled;    }

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);

private:
    sakura::core::TextureHandle m_textureHandle = sakura::core::INVALID_HANDLE;
    float m_dimming     = 0.5f;   // 默认暗化 50%
    bool  m_blurEnabled = false;
};

// ── DefaultBackground ─────────────────────────────────────────────────────────
// 无自定义图片时渲染：缓慢渐变色背景 + BackgroundFloat 粒子

class DefaultBackground
{
public:
    DefaultBackground();

    void Initialize(float dimming = 0.5f);
    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);

private:
    // 缓慢颜色渐变状态
    float m_colorPhase = 0.0f;   // 0~2π，按 COLOR_SPEED 推进
    static constexpr float COLOR_SPEED = 0.08f;  // rad/s（约 80s 一圈）

    // 粒子发射器
    ParticleSystem m_particles;
    int m_floatEmitter = -1;

    float m_dimming = 0.5f;

    // 根据 phase 混合两种背景色
    sakura::core::Color CalcBgColor(float phase) const;
};

} // namespace sakura::effects
