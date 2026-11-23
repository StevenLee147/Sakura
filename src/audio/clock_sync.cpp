// clock_sync.cpp — 音频时钟软硬抗漂移实现

#include "clock_sync.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace sakura::audio
{

namespace
{
std::atomic<double>  g_clockDriftMs{ 0.0 };
std::atomic<uint8_t> g_clockSyncMode{ static_cast<uint8_t>(ClockSyncMode::Idle) };
} // namespace

void AudioClockSync::Reset(double initialTimeMs)
{
    m_cursorMs      = initialTimeMs;
    m_lastDriftMs   = 0.0;
    m_mode          = ClockSyncMode::Idle;
    m_softSyncCount = 0;
    m_hardSnapCount = 0;
    m_initialized   = false;
}

double AudioClockSync::Update(double dtSeconds, double rawAudioMs, double playbackRate, bool audioPlaying)
{
    const double clampedDt   = std::clamp(dtSeconds, 0.0, 0.25);
    const double clampedRate = std::clamp(playbackRate, 0.25, 4.0);

    if (!audioPlaying)
    {
        if (m_initialized)
        {
            m_cursorMs += clampedDt * 1000.0 * clampedRate;
        }
        else
        {
            m_cursorMs = rawAudioMs;
        }
        m_lastDriftMs = 0.0;
        m_mode        = ClockSyncMode::Idle;
        return m_cursorMs;
    }

    if (!m_initialized)
    {
        m_cursorMs    = rawAudioMs;
        m_lastDriftMs = 0.0;
        m_mode        = ClockSyncMode::Locked;
        m_initialized = true;
        return m_cursorMs;
    }

    m_cursorMs += clampedDt * 1000.0 * clampedRate;

    const double drift = rawAudioMs - m_cursorMs;
    const double absDrift = std::abs(drift);
    m_lastDriftMs = drift;

    if (absDrift > kHardThresholdMs)
    {
        m_cursorMs    = rawAudioMs;
        m_lastDriftMs = 0.0;
        m_mode        = ClockSyncMode::HardSnap;
        ++m_hardSnapCount;
    }
    else if (absDrift > kSoftThresholdMs)
    {
        const double alpha = std::clamp(1.0 - std::exp(-clampedDt * kSoftCatchupRate), 0.02, 1.0);
        m_cursorMs += drift * alpha;
        m_lastDriftMs = rawAudioMs - m_cursorMs;
        m_mode = ClockSyncMode::SoftSync;
        ++m_softSyncCount;
    }
    else
    {
        m_mode = ClockSyncMode::Locked;
    }

    return m_cursorMs;
}

void AudioClockSync::PublishGlobalDiagnostics(double driftMs, ClockSyncMode mode)
{
    g_clockDriftMs.store(driftMs, std::memory_order_relaxed);
    g_clockSyncMode.store(static_cast<uint8_t>(mode), std::memory_order_relaxed);
}

double AudioClockSync::GetGlobalDriftMs()
{
    return g_clockDriftMs.load(std::memory_order_relaxed);
}

ClockSyncMode AudioClockSync::GetGlobalMode()
{
    return static_cast<ClockSyncMode>(g_clockSyncMode.load(std::memory_order_relaxed));
}

const char* AudioClockSync::ModeLabel(ClockSyncMode mode)
{
    switch (mode)
    {
    case ClockSyncMode::Locked:   return "LOCKED";
    case ClockSyncMode::SoftSync: return "SOFT";
    case ClockSyncMode::HardSnap: return "SNAP";
    case ClockSyncMode::Idle:
    default:                      return "IDLE";
    }
}

} // namespace sakura::audio