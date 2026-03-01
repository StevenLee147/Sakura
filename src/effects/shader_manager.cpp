#include "shader_manager.h"
#include "core/config.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>

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

    // 从 Config 读取效果开关
    auto& cfg = sakura::core::Config::GetInstance();
    m_effects[static_cast<size_t>(EffectType::Vignette)]  = true;   // 暗角默认开启
    m_effects[static_cast<size_t>(EffectType::Blur)]      = cfg.Get<bool>("graphics.bloom", true);
    m_effects[static_cast<size_t>(EffectType::ChromaAberration)]  = false;  // 连击特效触发
    m_effects[static_cast<size_t>(EffectType::ColorCorrection)]   = false;  // 可选

    OnResize(screenW, screenH);
    LOG_INFO("ShaderManager 初始化成功 ({}x{})", screenW, screenH);
    return true;
}

void ShaderManager::OnResize(int newW, int newH)
{
    if (m_offscreen)
    {
        SDL_DestroyTexture(m_offscreen);
        m_offscreen = nullptr;
    }

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
    }
}

void ShaderManager::Shutdown()
{
    if (m_offscreen)
    {
        SDL_DestroyTexture(m_offscreen);
        m_offscreen = nullptr;
    }
    m_renderer = nullptr;
}

// ============================================================================
// 帧捕获
// ============================================================================

bool ShaderManager::BeginCapture()
{
    if (!m_renderer || !m_offscreen) return false;

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
    SDL_SetRenderTarget(m_renderer, nullptr);
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

void ShaderManager::DrawBlurred(SDL_Texture* tex, float intensity)
{
    if (!tex || !m_renderer) return;

    intensity = std::clamp(intensity, 0.0f, 1.0f);
    float maxOffset = intensity * 12.0f;   // 最大偏移像素

    // 多方向采样模拟高斯模糊（box blur 近似）
    // 每层使用较低 alpha，叠加出模糊感
    const int passes = 8;
    const float alphaPerPass = 150.0f / static_cast<float>(passes);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // 先正常绘制一遍（最不透明，作为基底）
    BlitFull(tex, 0, 0, 255, 255, 255, 160);

    for (int i = 1; i <= passes; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(passes);
        float ofs = t * maxOffset;
        int o = static_cast<int>(ofs);
        uint8_t a = static_cast<uint8_t>(alphaPerPass * (1.0f - t * 0.3f));

        // 8方向偏移叠加
        BlitFull(tex,  o,  0, 255, 255, 255, a);
        BlitFull(tex, -o,  0, 255, 255, 255, a);
        BlitFull(tex,  0,  o, 255, 255, 255, a);
        BlitFull(tex,  0, -o, 255, 255, 255, a);
    }
}

void ShaderManager::DrawVignette(float intensity)
{
    if (!m_renderer) return;

    intensity = std::clamp(intensity, 0.0f, 1.0f);

    // 从屏幕四角向中心绘制渐变黑色矩形
    // 半径：屏幕对角线长度的一定比例
    const int layers = 16;
    const float diag = std::sqrt(static_cast<float>(m_width * m_width + m_height * m_height));
    const float maxRadius = diag * 0.5f * 0.75f;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < layers; ++i)
    {
        float t      = static_cast<float>(i) / static_cast<float>(layers - 1);
        float radius = maxRadius * (1.0f - t);

        // alpha: 外层最不透明，内层透明
        uint8_t a = static_cast<uint8_t>(intensity * 200.0f * t * t);

        // 绘制覆盖层：4个三角形从角出发
        int cx = m_width  / 2;
        int cy = m_height / 2;

        // 外矩形
        int outerX = static_cast<int>(cx - radius);
        int outerY = static_cast<int>(cy - radius);
        int outerW = static_cast<int>(radius * 2.0f);
        int outerH = static_cast<int>(radius * 2.0f);

        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, a);

        // 上带
        if (outerY > 0)
        {
            SDL_FRect top = { 0.0f, 0.0f, static_cast<float>(m_width),
                               static_cast<float>(outerY) };
            SDL_RenderFillRect(m_renderer, &top);
        }
        // 下带
        if (outerY + outerH < m_height)
        {
            SDL_FRect bot = { 0.0f, static_cast<float>(outerY + outerH),
                               static_cast<float>(m_width),
                               static_cast<float>(m_height - (outerY + outerH)) };
            SDL_RenderFillRect(m_renderer, &bot);
        }
        // 左带
        if (outerX > 0)
        {
            SDL_FRect lft = { 0.0f, static_cast<float>(outerY),
                               static_cast<float>(outerX),
                               static_cast<float>(outerH) };
            SDL_RenderFillRect(m_renderer, &lft);
        }
        // 右带
        if (outerX + outerW < m_width)
        {
            SDL_FRect rgt = { static_cast<float>(outerX + outerW), static_cast<float>(outerY),
                               static_cast<float>(m_width - (outerX + outerW)),
                               static_cast<float>(outerH) };
            SDL_RenderFillRect(m_renderer, &rgt);
        }
    }
}

void ShaderManager::DrawChromaticAberration(SDL_Texture* tex, float intensity)
{
    if (!tex || !m_renderer) return;

    intensity = std::clamp(intensity, 0.0f, 1.0f);
    int ofs = static_cast<int>(intensity * 8.0f);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);

    // R 通道：向右/上偏移
    SDL_SetTextureColorMod(tex, 255, 0, 0);
    SDL_SetTextureAlphaMod(tex, 180);
    SDL_FRect dstR = { static_cast<float>(ofs), static_cast<float>(-ofs),
                        static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dstR);

    // G 通道：无偏移
    SDL_SetTextureColorMod(tex, 0, 255, 0);
    SDL_SetTextureAlphaMod(tex, 180);
    SDL_FRect dstG = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dstG);

    // B 通道：向左/下偏移
    SDL_SetTextureColorMod(tex, 0, 0, 255);
    SDL_SetTextureAlphaMod(tex, 180);
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
    // 后处理链：目前不在全局层级叠加暗角
    // 暗角仅在明确需要的场景（如暂停）中由各场景主动调用 DrawVignette
}

} // namespace sakura::effects
