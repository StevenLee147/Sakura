#include "timer.h"

#include <algorithm>
#include <numeric>

namespace sakura::core
{

Timer::Timer()
    : m_startCounter(0)
    , m_lastCounter(0)
    , m_frequency(SDL_GetPerformanceFrequency())
    , m_frameCount(0)
    , m_deltaTime(0.0f)
    , m_elapsedTime(0.0f)
    , m_fps(0.0f)
    , m_fpsSamples{}
    , m_fpsSampleIndex(0)
{
    Reset();
}

void Timer::Tick()
{
    uint64_t currentCounter = SDL_GetPerformanceCounter();

    m_deltaTime   = static_cast<float>(currentCounter - m_lastCounter)
                  / static_cast<float>(m_frequency);
    m_elapsedTime = static_cast<float>(currentCounter - m_startCounter)
                  / static_cast<float>(m_frequency);

    // 防止 deltaTime 过大（如调试断点后恢复）
    if (m_deltaTime > 0.25f)
    {
        m_deltaTime = 0.25f;
    }

    m_lastCounter = currentCounter;
    m_frameCount++;

    // 瞬时 FPS（带平滑窗口）
    if (m_deltaTime > 0.0f)
    {
        m_fpsSamples[m_fpsSampleIndex] = 1.0f / m_deltaTime;
        m_fpsSampleIndex = (m_fpsSampleIndex + 1) % FPS_SAMPLE_COUNT;

        float sum = 0.0f;
        for (int i = 0; i < FPS_SAMPLE_COUNT; ++i)
        {
            sum += m_fpsSamples[i];
        }
        m_fps = sum / static_cast<float>(FPS_SAMPLE_COUNT);
    }
}

void Timer::Reset()
{
    m_startCounter  = SDL_GetPerformanceCounter();
    m_lastCounter   = m_startCounter;
    m_frameCount    = 0;
    m_deltaTime     = 0.0f;
    m_elapsedTime   = 0.0f;
    m_fps           = 0.0f;
    m_fpsSampleIndex = 0;

    for (int i = 0; i < FPS_SAMPLE_COUNT; ++i)
    {
        m_fpsSamples[i] = 0.0f;
    }
}

} // namespace sakura::core
