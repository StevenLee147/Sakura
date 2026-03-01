#include "screen_shake.h"

#include <cmath>
#include <random>

namespace sakura::effects
{

// ============================================================================
// 内部工具
// ============================================================================

// 简单平滑噪声（基于 sin 叠加，避免突兀跳变）
float ScreenShake::Noise(float t, float seed)
{
    // 多频率叠加产生类噪声
    float v = 0.0f;
    v += std::sin((t + seed) * 13.1f) * 0.5f;
    v += std::sin((t + seed) *  7.3f) * 0.3f;
    v += std::sin((t + seed) * 23.7f) * 0.2f;
    return v;   // 范围约 ±1.0
}

// ============================================================================
// ScreenShake 实现
// ============================================================================

void ScreenShake::Trigger(float intensity, float duration, float decay)
{
    // 如果已有更强震动，保留较强的
    if (intensity >= m_intensity || m_timer <= 0.0f)
    {
        m_intensity = intensity;
        m_duration  = duration;
        m_timer     = duration;
        m_decay     = decay;

        // 每次触发更新随机种子
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        m_seedX = dist(rng);
        m_seedY = dist(rng);
    }
}

std::pair<float, float> ScreenShake::Update(float dt)
{
    if (m_timer <= 0.0f)
        return { 0.0f, 0.0f };

    m_timer -= dt;
    if (m_timer < 0.0f)
        m_timer = 0.0f;

    // 衰减系数：从 1 衰减到 0
    float progress = m_timer / m_duration;   // 1.0 → 0.0

    // 指数衰减
    float envelope = progress * std::exp(-m_decay * (1.0f - progress));
    // 保证范围 [0, 1]
    envelope = std::max(0.0f, std::min(1.0f, envelope * (1.0f + m_decay * 0.15f)));

    float timeStamp = m_duration - m_timer;
    float nx = Noise(timeStamp, m_seedX) * m_intensity * envelope;
    float ny = Noise(timeStamp, m_seedY) * m_intensity * envelope;

    return { nx, ny };
}

void ScreenShake::Stop()
{
    m_timer     = 0.0f;
    m_intensity = 0.0f;
}

} // namespace sakura::effects
