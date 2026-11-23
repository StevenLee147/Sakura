// video_decoder.cpp — Windows Media Foundation 异步视频硬解 + 跨平台安全回退桩

#include "video_decoder.h"
#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#endif

namespace sakura::effects
{

namespace
{
std::atomic<uint8_t> g_videoStageMode{ static_cast<uint8_t>(VideoStageMode::Idle) };

std::filesystem::path ToUtf8Path(std::string_view raw)
{
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(raw.data()), raw.size()));
}
} // namespace

VideoDecoder::~VideoDecoder()
{
    Close();
}

bool VideoDecoder::IsPlatformSupported()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool VideoDecoder::Open(std::string_view filePath, bool loop)
{
    Close();

    m_circuitBroken.store(false, std::memory_order_release);
    m_consecutiveTimeouts.store(0, std::memory_order_relaxed);
    m_decodedFrames.store(0, std::memory_order_relaxed);
    m_targetSeconds.store(0.0, std::memory_order_relaxed);
    m_seekRequested.store(false, std::memory_order_relaxed);
    m_lastPresentedPts = -1.0;

    if (filePath.empty())
    {
        m_fallback.store(true, std::memory_order_release);
        return false;
    }

    std::error_code ec;
    const auto fsPath = ToUtf8Path(filePath);
    if (!std::filesystem::exists(fsPath, ec) || ec)
    {
        LOG_WARN("[VideoDecoder] 视频文件不存在，回退动态夜樱背景: {}", filePath);
        m_fallback.store(true, std::memory_order_release);
        return false;
    }

#ifndef _WIN32
    (void)loop;
    LOG_INFO("[VideoDecoder] 当前平台使用安全回退桩: {}", filePath);
    m_fallback.store(true, std::memory_order_release);
    return false;
#else
    m_stopRequested.store(false, std::memory_order_release);
    m_ready.store(false, std::memory_order_release);
    m_fallback.store(false, std::memory_order_release);
    m_open.store(true, std::memory_order_release);

    m_worker = std::thread(&VideoDecoder::DecodeWorker, this, std::string(filePath), loop);

    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait_for(lock, std::chrono::milliseconds(600), [this] {
        return m_ready.load(std::memory_order_acquire)
            || m_fallback.load(std::memory_order_acquire)
            || m_stopRequested.load(std::memory_order_acquire);
    });

    if (m_fallback.load(std::memory_order_acquire))
    {
        lock.unlock();
        Close();
        m_fallback.store(true, std::memory_order_release);
        return false;
    }
    return true;
#endif
}

void VideoDecoder::Close()
{
    m_stopRequested.store(true, std::memory_order_release);
    m_cv.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& slot : m_ring)
        {
            slot.rgba.clear();
            slot.width = 0;
            slot.height = 0;
            slot.ptsSeconds = 0.0;
            slot.sequence = 0;
        }
        m_head = 0;
        m_count = 0;
    }
    m_open.store(false, std::memory_order_release);
    m_ready.store(false, std::memory_order_release);
}

void VideoDecoder::SyncTime(double playbackSeconds)
{
    const double clamped = std::max(0.0, playbackSeconds);
    const double prev = m_targetSeconds.exchange(clamped, std::memory_order_relaxed);
    if (std::abs(clamped - prev) > 1.25)
    {
        m_seekSeconds.store(clamped, std::memory_order_relaxed);
        m_seekRequested.store(true, std::memory_order_release);
        m_cv.notify_all();
    }
}

bool VideoDecoder::RecordDecodeDurationMs(double elapsedMs)
{
    if (elapsedMs > kDecodeTimeoutBudgetMs)
    {
        const int count = m_consecutiveTimeouts.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count >= kMaxConsecutiveTimeouts)
        {
            m_circuitBroken.store(true, std::memory_order_release);
            m_fallback.store(true, std::memory_order_release);
            LOG_WARN("[VideoDecoder] 连续 {} 帧解码超时 ({:.1f}ms)，触发性能熔断回退动态夜樱背景",
                     count, elapsedMs);
            return false;
        }
    }
    else
    {
        m_consecutiveTimeouts.store(0, std::memory_order_relaxed);
    }
    return !m_circuitBroken.load(std::memory_order_acquire);
}

void VideoDecoder::PushFrame(VideoFrame&& frame)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] {
        return m_stopRequested.load(std::memory_order_acquire)
            || m_seekRequested.load(std::memory_order_acquire)
            || m_count < kRingCapacity;
    });
    if (m_stopRequested.load(std::memory_order_acquire) || m_seekRequested.load(std::memory_order_acquire))
        return;

    const std::size_t writeIdx = (m_head + m_count) % kRingCapacity;
    m_ring[writeIdx] = std::move(frame);
    ++m_count;
    m_ready.store(true, std::memory_order_release);
    m_cv.notify_all();
}

bool VideoDecoder::AcquireFrame(double playbackSeconds, VideoFrame& outFrame, uint64_t& inOutLastSeq)
{
    if (m_fallback.load(std::memory_order_acquire) || !m_open.load(std::memory_order_acquire))
        return false;

    SyncTime(playbackSeconds);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == 0)
        return false;

    const double fps = std::max(1.0, m_fps.load(std::memory_order_relaxed));
    const double frameDur = 1.0 / fps;

    while (m_count > 1)
    {
        const VideoFrame& nextSlot = m_ring[(m_head + 1) % kRingCapacity];
        if (nextSlot.ptsSeconds <= playbackSeconds + frameDur * 0.5
            || (m_ring[m_head].ptsSeconds > playbackSeconds + 1.0))
        {
            m_head = (m_head + 1) % kRingCapacity;
            --m_count;
        }
        else
        {
            break;
        }
    }

    const VideoFrame& current = m_ring[m_head];
    if (current.sequence == inOutLastSeq || current.rgba.empty())
        return false;

    if (inOutLastSeq != 0 && current.ptsSeconds > playbackSeconds + frameDur * 1.5
        && current.ptsSeconds < playbackSeconds + 1.0)
    {
        return false;
    }

    outFrame = current;
    inOutLastSeq = current.sequence;
    m_lastPresentedPts = current.ptsSeconds;

    if (m_count > 1)
    {
        m_head = (m_head + 1) % kRingCapacity;
        --m_count;
    }
    m_cv.notify_all();
    return true;
}

void VideoDecoder::DecodeWorker(std::string filePath, bool loop)
{
#ifndef _WIN32
    (void)filePath;
    (void)loop;
#else
    using Microsoft::WRL::ComPtr;

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needCoUninit = SUCCEEDED(coHr);

    const HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(mfHr))
    {
        LOG_WARN("[VideoDecoder] MFStartup 失败 (0x{:08X})", static_cast<uint32_t>(mfHr));
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        if (needCoUninit) CoUninitialize();
        return;
    }

    struct MfThreadGuard {
        bool uninitCom = false;
        ~MfThreadGuard() { MFShutdown(); if (uninitCom) CoUninitialize(); }
    } mfGuard{ needCoUninit };

    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(&attrs, 2)))
    {
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        return;
    }
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    const std::wstring widePath = std::filesystem::absolute(ToUtf8Path(filePath)).wstring();
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), attrs.Get(), &reader);
    if (FAILED(hr) || !reader)
    {
        LOG_WARN("[VideoDecoder] 无法打开视频源 '{}' (0x{:08X})，回退动态夜樱背景",
                 filePath, static_cast<uint32_t>(hr));
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        return;
    }

    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE)))
    {
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        return;
    }

    ComPtr<IMFMediaType> targetType;
    MFCreateMediaType(&targetType);
    targetType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    targetType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, targetType.Get());
    if (FAILED(hr))
    {
        LOG_WARN("[VideoDecoder] RGB32 输出协商失败 (0x{:08X})，回退动态夜樱背景", static_cast<uint32_t>(hr));
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        return;
    }

    ComPtr<IMFMediaType> actualType;
    if (FAILED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actualType)) || !actualType)
    {
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        return;
    }

    UINT32 frameW = 0, frameH = 0;
    if (FAILED(MFGetAttributeSize(actualType.Get(), MF_MT_FRAME_SIZE, &frameW, &frameH)) || frameW == 0 || frameH == 0)
    {
        m_fallback.store(true, std::memory_order_release);
        m_cv.notify_all();
        return;
    }

    UINT32 fpsNum = 30, fpsDen = 1;
    if (SUCCEEDED(MFGetAttributeRatio(actualType.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen)) && fpsDen > 0 && fpsNum > 0)
    {
        m_fps.store(static_cast<double>(fpsNum) / static_cast<double>(fpsDen), std::memory_order_relaxed);
    }
    INT32 defaultStride = static_cast<INT32>(frameW * 4);
    (void)actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&defaultStride));

    double clipDurationSec = 0.0;
    {
        PROPVARIANT varDur;
        PropVariantInit(&varDur);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                                                       MF_PD_DURATION, &varDur)))
        {
            if (varDur.vt == VT_UI8)
                clipDurationSec = static_cast<double>(varDur.uhVal.QuadPart) / 10000000.0;
            PropVariantClear(&varDur);
        }
    }

    m_width.store(static_cast<int>(frameW), std::memory_order_relaxed);
    m_height.store(static_cast<int>(frameH), std::memory_order_relaxed);
    m_ready.store(true, std::memory_order_release);
    m_cv.notify_all();

    LOG_INFO("[VideoDecoder] Media Foundation 硬解管线就绪: {} ({}x{} @ {:.2f}fps)",
             filePath, frameW, frameH, m_fps.load());

    double loopBasePts = 0.0;
    double lastRawPts  = 0.0;
    uint64_t seqCounter = 0;

    auto seekReaderTo = [&](double sec) {
        PROPVARIANT pos;
        PropVariantInit(&pos);
        pos.vt = VT_I8;
        const double localSec = (clipDurationSec > 0.1 && loop)
            ? std::fmod(std::max(0.0, sec), clipDurationSec)
            : std::max(0.0, sec);
        pos.hVal.QuadPart = static_cast<LONGLONG>(localSec * 10000000.0);
        reader->SetCurrentPosition(GUID_NULL, pos);
        PropVariantClear(&pos);
        loopBasePts = (clipDurationSec > 0.1 && loop)
            ? (std::floor(std::max(0.0, sec) / clipDurationSec) * clipDurationSec)
            : 0.0;
    };

    while (!m_stopRequested.load(std::memory_order_acquire))
    {
        if (m_seekRequested.exchange(false, std::memory_order_acq_rel))
        {
            const double seekTarget = m_seekSeconds.load(std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_head = 0;
                m_count = 0;
            }
            seekReaderTo(seekTarget);
        }

        const auto t0 = std::chrono::steady_clock::now();
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG llTimeStamp = 0;
        ComPtr<IMFSample> sample;

        hr = reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, &streamIndex, &flags, &llTimeStamp, &sample);
        const double decodeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        if (FAILED(hr) || !RecordDecodeDurationMs(decodeMs))
        {
            m_fallback.store(true, std::memory_order_release);
            m_cv.notify_all();
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (loop)
            {
                PROPVARIANT zeroPos;
                PropVariantInit(&zeroPos);
                zeroPos.vt = VT_I8;
                zeroPos.hVal.QuadPart = 0;
                reader->SetCurrentPosition(GUID_NULL, zeroPos);
                PropVariantClear(&zeroPos);
                loopBasePts += (clipDurationSec > 0.05) ? clipDurationSec : (lastRawPts + 1.0 / std::max(1.0, m_fps.load()));
                continue;
            }
            break;
        }

        if (!sample)
            continue;

        ComPtr<IMFMediaBuffer> mediaBuf;
        if (FAILED(sample->ConvertToContiguousBuffer(&mediaBuf)) || !mediaBuf)
            continue;

        VideoFrame frame;
        frame.width = static_cast<int>(frameW);
        frame.height = static_cast<int>(frameH);
        lastRawPts = static_cast<double>(llTimeStamp) / 10000000.0;
        frame.ptsSeconds = loopBasePts + std::max(0.0, lastRawPts);
        frame.sequence = ++seqCounter;
        frame.rgba.resize(static_cast<std::size_t>(frameW) * static_cast<std::size_t>(frameH) * 4u);

        bool copied = false;
        ComPtr<IMF2DBuffer> buf2D;
        if (SUCCEEDED(mediaBuf.As(&buf2D)) && buf2D)
        {
            BYTE* scanline0 = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(buf2D->Lock2D(&scanline0, &pitch)) && scanline0)
            {
                for (UINT32 y = 0; y < frameH; ++y)
                {
                    const uint8_t* srcRow = scanline0 + static_cast<std::ptrdiff_t>(y) * pitch;
                    uint8_t* dstRow = frame.rgba.data() + static_cast<std::size_t>(y) * frameW * 4u;
                    for (UINT32 x = 0; x < frameW; ++x)
                    {
                        dstRow[x * 4u + 0] = srcRow[x * 4u + 2];
                        dstRow[x * 4u + 1] = srcRow[x * 4u + 1];
                        dstRow[x * 4u + 2] = srcRow[x * 4u + 0];
                        dstRow[x * 4u + 3] = 255;
                    }
                }
                buf2D->Unlock2D();
                copied = true;
            }
        }

        if (!copied)
        {
            BYTE* rawData = nullptr;
            DWORD curLen = 0;
            if (SUCCEEDED(mediaBuf->Lock(&rawData, nullptr, &curLen)) && rawData)
            {
                const std::ptrdiff_t stride = (defaultStride != 0)
                    ? static_cast<std::ptrdiff_t>(defaultStride)
                    : static_cast<std::ptrdiff_t>(frameW * 4u);
                const uint8_t* row0 = (stride < 0)
                    ? (rawData + static_cast<std::size_t>(frameH - 1) * static_cast<std::size_t>(-stride))
                    : rawData;
                if (curLen >= frameW * frameH * 4u)
                {
                    for (UINT32 y = 0; y < frameH; ++y)
                    {
                        const uint8_t* srcRow = row0 + static_cast<std::ptrdiff_t>(y) * stride;
                        uint8_t* dstRow = frame.rgba.data() + static_cast<std::size_t>(y) * frameW * 4u;
                        for (UINT32 x = 0; x < frameW; ++x)
                        {
                            dstRow[x * 4u + 0] = srcRow[x * 4u + 2];
                            dstRow[x * 4u + 1] = srcRow[x * 4u + 1];
                            dstRow[x * 4u + 2] = srcRow[x * 4u + 0];
                            dstRow[x * 4u + 3] = 255;
                        }
                    }
                    copied = true;
                }
                mediaBuf->Unlock();
            }
        }

        if (copied)
        {
            m_decodedFrames.fetch_add(1, std::memory_order_relaxed);
            PushFrame(std::move(frame));
        }
    }

#endif
}

void VideoDecoder::PublishGlobalStageMode(VideoStageMode mode)
{
    g_videoStageMode.store(static_cast<uint8_t>(mode), std::memory_order_relaxed);
}

VideoStageMode VideoDecoder::GetGlobalStageMode()
{
    return static_cast<VideoStageMode>(g_videoStageMode.load(std::memory_order_relaxed));
}

const char* VideoDecoder::StageModeLabel(VideoStageMode mode)
{
    switch (mode)
    {
    case VideoStageMode::HardwareDecode: return "HW MP4";
    case VideoStageMode::Disabled:       return "BGA OFF";
    case VideoStageMode::Fallback:       return "SAKURA STAGE";
    case VideoStageMode::Idle:
    default:                             return "IDLE";
    }
}

} // namespace sakura::effects