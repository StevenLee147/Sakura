#pragma once

#include <SDL3/SDL.h>

namespace sakura::core
{

// 高精度计时器，封装 SDL_GetPerformanceCounter
class Timer
{
public:
    Timer();

    // 刷新本帧时间，每帧开头调用
    void Tick();

    // 重置计时器起点
    void Reset();

    // 上一帧到本帧的时间差（秒）
    float GetDeltaTime() const { return m_deltaTime; }

    // 自重置起的总运行时间（秒）
    float GetElapsedTime() const { return m_elapsedTime; }

    // 当前帧率（1 帧平滑）
    float GetFPS() const { return m_fps; }

    // 帧率计数（累计）
    uint64_t GetFrameCount() const { return m_frameCount; }

private:
    uint64_t m_startCounter;
    uint64_t m_lastCounter;
    uint64_t m_frequency;
    uint64_t m_frameCount;

    float    m_deltaTime;
    float    m_elapsedTime;
    float    m_fps;

    // FPS 平滑窗口
    static constexpr int FPS_SAMPLE_COUNT = 60;
    float m_fpsSamples[FPS_SAMPLE_COUNT];
    int   m_fpsSampleIndex;
};

} // namespace sakura::core
