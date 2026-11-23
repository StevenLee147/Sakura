#pragma once

// clock_sync.h — 高精单调视觉时钟插值与音频播放头软硬抗漂移器
// 规则：
//   |drift| <= 5ms        : 保持单调帧间隔插值，消除底层声卡缓冲回调抖动
//   5ms < |drift| <= 50ms : 一阶指数平滑软追赶，消除微卡顿且无视觉瞬移
//   |drift| > 50ms        : 立即硬对齐（如蓝牙重连、切歌、Seek）

#include <cstdint>

namespace sakura::audio
{

enum class ClockSyncMode : uint8_t
{
    Idle = 0,
    Locked = 1,
    SoftSync = 2,
    HardSnap = 3
};

class AudioClockSync
{
public:
    static constexpr double kSoftThresholdMs = 5.0;
    static constexpr double kHardThresholdMs = 50.0;
    static constexpr double kSoftCatchupRate = 14.0;

    AudioClockSync() = default;

    void Reset(double initialTimeMs = 0.0);

    double Update(double dtSeconds, double rawAudioMs, double playbackRate = 1.0, bool audioPlaying = true);

    double GetTimeMs() const { return m_cursorMs; }
    double GetLastDriftMs() const { return m_lastDriftMs; }
    ClockSyncMode GetMode() const { return m_mode; }
    uint32_t GetSoftSyncCount() const { return m_softSyncCount; }
    uint32_t GetHardSnapCount() const { return m_hardSnapCount; }
    bool IsInitialized() const { return m_initialized; }

    static void PublishGlobalDiagnostics(double driftMs, ClockSyncMode mode);
    static double GetGlobalDriftMs();
    static ClockSyncMode GetGlobalMode();
    static const char* ModeLabel(ClockSyncMode mode);

private:
    double        m_cursorMs      = 0.0;
    double        m_lastDriftMs   = 0.0;
    ClockSyncMode m_mode          = ClockSyncMode::Idle;
    uint32_t      m_softSyncCount = 0;
    uint32_t      m_hardSnapCount = 0;
    bool          m_initialized   = false;
};

} // namespace sakura::audio