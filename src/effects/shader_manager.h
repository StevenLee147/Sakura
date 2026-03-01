#pragma once

// shader_manager.h — 后处理特效管理器
// 使用 SDL3 SDL_TEXTUREACCESS_TARGET 实现 render-to-texture，
// 通过 SDL_Renderer 的多通道绘制模拟后处理效果（无需自定义 SPIR-V Shader）。
//
// 支持效果：
//   - Blur         高斯模糊（多偏移采样拟合，适用于暂停背景）
//   - Vignette     暗角（边缘暗化，增强沉浸感）
//   - ColorCorrect 色彩校正（色偏叠加）
//   - Chromatic    色差（连击里程碑特效）

#include "core/renderer.h"
#include <SDL3/SDL.h>
#include <cstdint>

namespace sakura::effects
{

// ============================================================================
// 效果类型枚举
// ============================================================================
enum class EffectType
{
    Blur       = 0,
    Vignette   = 1,
    ChromaAberration = 2,
    ColorCorrection  = 3,
    COUNT            = 4
};

// ============================================================================
// ShaderManager — 后处理效果管理器（SDL render target 实现）
// ============================================================================
class ShaderManager
{
public:
    static ShaderManager& GetInstance()
    {
        static ShaderManager instance;
        return instance;
    }

    ShaderManager(const ShaderManager&)            = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    // 初始化，获取 SDL_Renderer 指针并创建 offscreen 纹理
    bool Initialize(SDL_Renderer* renderer, int screenW, int screenH);

    // 屏幕尺寸变化时重建 offscreen 纹理
    void OnResize(int newW, int newH);

    void Shutdown();

    // ── 帧捕获 ────────────────────────────────────────────────────────────────

    // 开始将后续渲染写入 offscreen 纹理（而非直接写屏幕）
    bool BeginCapture();

    // 结束捕获，还原渲染目标到屏幕，返回 offscreen 纹理
    SDL_Texture* EndCapture();

    // ── 效果绘制 ──────────────────────────────────────────────────────────────

    // 将纹理（全屏）以模糊效果绘制到当前渲染目标
    // intensity: 0.0~1.0，控制偏移像素数(0=无模糊, 1=最大约12像素)
    void DrawBlurred(SDL_Texture* tex, float intensity = 0.5f);

    // 绘制暗角叠加层（纯 SDL 绘制，无需纹理）
    // intensity: 0.0~1.0，控制暗角深度
    void DrawVignette(float intensity = 0.5f);

    // 绘制色差效果（将纹理 R/G/B 三通道分别以小偏移绘制）
    void DrawChromaticAberration(SDL_Texture* tex, float intensity = 0.3f);

    // 绘制色彩校正叠加（色相偏移 tint）
    void DrawColorCorrection(sakura::core::Color tint, float alpha = 0.08f);

    // ── 效果开关 ──────────────────────────────────────────────────────────────

    void EnableEffect(EffectType type);
    void DisableEffect(EffectType type);
    bool IsEffectEnabled(EffectType type) const;

    // ── 综合后处理（当前帧画面 → offscreen → 效果 → 屏幕）─────────────────────
    // 在 App::Render() 中 EndCapture() 后调用此方法完成后处理链
    void ApplyPostProcess();

    // ── 访问器 ────────────────────────────────────────────────────────────────

    SDL_Texture* GetCaptureTexture() const { return m_offscreen; }

private:
    ShaderManager() = default;

    SDL_Renderer* m_renderer  = nullptr;
    SDL_Texture*  m_offscreen = nullptr;   // 全屏 offscreen 缓冲
    int           m_width     = 0;
    int           m_height    = 0;
    bool          m_effects[static_cast<size_t>(EffectType::COUNT)] = {};

    // 辅助：在屏幕上绘制全屏纹理（含平移偏移）
    void BlitFull(SDL_Texture* tex, int offsetX = 0, int offsetY = 0,
                  uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t alpha = 255) const;
};

} // namespace sakura::effects
