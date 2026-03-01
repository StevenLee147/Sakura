#pragma once

// screen_shake.h — 屏幕震动特效（单例）
// Trigger 触发一次震动，Update 每帧返回当前偏移量 {offsetX, offsetY}（归一化）。
// Renderer 将偏移量应用为全局渲染平移。
// Miss 触发轻微震动：intensity=0.003, duration=0.15s。

#include <utility>

namespace sakura::effects
{

// ============================================================================
// ScreenShake — 单例屏幕震动控制器
// ============================================================================
class ScreenShake
{
public:
    static ScreenShake& GetInstance()
    {
        static ScreenShake instance;
        return instance;
    }

    ScreenShake(const ScreenShake&)            = delete;
    ScreenShake& operator=(const ScreenShake&) = delete;

    // ── 触发 ─────────────────────────────────────────────────────────────────

    // intensity: 最大偏移幅度（归一化，0.003 ≈ 5像素@1080p）
    // duration:  震动持续时间（秒）
    // decay:     衰减速率（越大衰减越快；0 = 线性衰减到 duration 结束）
    void Trigger(float intensity = 0.003f,
                 float duration  = 0.15f,
                 float decay     = 8.0f);

    // ── 更新 ─────────────────────────────────────────────────────────────────

    // 每帧调用，返回当前帧的 {shakeX, shakeY}（归一化偏移）
    std::pair<float, float> Update(float dt);

    // ── 查询 ─────────────────────────────────────────────────────────────────

    bool IsActive() const { return m_timer > 0.0f; }

    // 手动停止
    void Stop();

private:
    ScreenShake() = default;

    float m_intensity = 0.0f;
    float m_timer     = 0.0f;    // 剩余时间
    float m_duration  = 0.15f;
    float m_decay     = 8.0f;

    // Perlin/噪声替代：用随机种子+时间计算伪噪声位移
    float m_seedX = 0.0f;
    float m_seedY = 3.7f;   // 不同种子让 X/Y 相互独立

    // 简单的伪随机噪声（无需额外依赖）
    static float Noise(float t, float seed);
};

} // namespace sakura::effects
