#pragma once

// video_decoder.h — 原生硬件加速背景视频异步解码器（4 帧环形缓冲 + 性能熔断）
// Windows 10/11 基于 Media Foundation IMFSourceReader 解码 MP4 (H.264) / WebM；
// 非 Windows 平台提供跨平台安全桩实现，无缝回退至月夜墨樱动态背景。

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sakura::effects
{

struct VideoFrame
{
    int                  width      = 0;
    int                  height     = 0;
    double               ptsSeconds = 0.0;
    uint64_t             sequence   = 0;
    std::vector<uint8_t> rgba;
};

enum class VideoStageMode : uint8_t
{
    Idle = 0,
    HardwareDecode = 1,
    Disabled = 2,
    Fallback = 3
};

class VideoDecoder
{
public:
    static constexpr std::size_t kRingCapacity          = 4;
    static constexpr double      kDecodeTimeoutBudgetMs = 85.0;
    static constexpr int         kMaxConsecutiveTimeouts = 6;

    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    static bool IsPlatformSupported();

    bool Open(std::string_view filePath, bool loop = true);
    void Close();

    void SyncTime(double playbackSeconds);
    bool AcquireFrame(double playbackSeconds, VideoFrame& outFrame, uint64_t& inOutLastSeq);
    bool RecordDecodeDurationMs(double elapsedMs);

    bool IsOpen() const { return m_open.load(std::memory_order_acquire); }
    bool IsFallbackActive() const { return m_fallback.load(std::memory_order_acquire); }
    bool IsCircuitBroken() const { return m_circuitBroken.load(std::memory_order_acquire); }
    int  GetWidth() const { return m_width.load(std::memory_order_relaxed); }
    int  GetHeight() const { return m_height.load(std::memory_order_relaxed); }
    double GetFps() const { return m_fps.load(std::memory_order_relaxed); }
    uint64_t GetDecodedFrames() const { return m_decodedFrames.load(std::memory_order_relaxed); }

    static void PublishGlobalStageMode(VideoStageMode mode);
    static VideoStageMode GetGlobalStageMode();
    static const char* StageModeLabel(VideoStageMode mode);

private:
    void DecodeWorker(std::string filePath, bool loop);
    void PushFrame(VideoFrame&& frame);

    mutable std::mutex              m_mutex;
    std::condition_variable         m_cv;
    std::array<VideoFrame, kRingCapacity> m_ring{};
    std::size_t                     m_head  = 0;
    std::size_t                     m_count = 0;

    std::thread                     m_worker;
    std::atomic<bool>               m_stopRequested{ false };
    std::atomic<bool>               m_open{ false };
    std::atomic<bool>               m_ready{ false };
    std::atomic<bool>               m_fallback{ true };
    std::atomic<bool>               m_circuitBroken{ false };
    std::atomic<int>                m_consecutiveTimeouts{ 0 };
    std::atomic<int>                m_width{ 0 };
    std::atomic<int>                m_height{ 0 };
    std::atomic<double>             m_fps{ 30.0 };
    std::atomic<double>             m_targetSeconds{ 0.0 };
    std::atomic<bool>               m_seekRequested{ false };
    std::atomic<double>             m_seekSeconds{ 0.0 };
    std::atomic<uint64_t>           m_decodedFrames{ 0 };
    double                          m_lastPresentedPts = -1.0;
};

} // namespace sakura::effects