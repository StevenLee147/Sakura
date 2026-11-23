// test_video_and_clock.cpp — 视频解码回退熔断与音频时钟软硬抗漂移测试

#include "test_framework.h"
#include "audio/clock_sync.h"
#include "effects/video_decoder.h"
#include "game/chart_loader.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

using sakura::audio::AudioClockSync;
using sakura::audio::ClockSyncMode;
using sakura::effects::VideoDecoder;
using sakura::effects::VideoFrame;

TEST_CASE("音频时钟同步在 <=5ms 死区内维持纯单调插值无抖动", "[audio][clock]")
{
    AudioClockSync sync;
    REQUIRE(std::abs(sync.Update(0.0, 1000.0, 1.0, true) - 1000.0) < 1e-4);
    REQUIRE(sync.GetMode() == ClockSyncMode::Locked);

    // 240Hz 帧率下声卡回调停留在旧位置（偏差 -4.16ms），视觉游标仍平滑单调前进
    const double t1 = sync.Update(1.0 / 240.0, 1000.0, 1.0, true);
    REQUIRE(t1 > 1004.0);
    REQUIRE(sync.GetMode() == ClockSyncMode::Locked);
    REQUIRE(sync.GetSoftSyncCount() == 0);
    REQUIRE(sync.GetHardSnapCount() == 0);
}

TEST_CASE("音频时钟同步在 5ms~50ms 区间平滑一阶软追赶，>50ms 立即硬对齐", "[audio][clock]")
{
    AudioClockSync sync;
    sync.Update(0.0, 2000.0, 1.0, true);

    // 注入 +24ms 偏移：应触发软追赶且单帧不会瞬移到位
    const double softT = sync.Update(0.016, 2040.0, 1.0, true);
    REQUIRE(sync.GetMode() == ClockSyncMode::SoftSync);
    REQUIRE(softT > 2016.0);
    REQUIRE(softT < 2040.0);
    REQUIRE(sync.GetSoftSyncCount() == 1);

    // 连续多帧推进后收敛至声卡播放头附近
    double raw = 2040.0;
    for (int i = 0; i < 30; ++i)
    {
        raw += 16.0;
        sync.Update(0.016, raw, 1.0, true);
    }
    REQUIRE(std::abs(sync.GetTimeMs() - raw) < 5.0);

    // 模拟蓝牙重连 / 切设备造成 +180ms 突变：立即硬对齐
    const double snapped = sync.Update(0.016, raw + 196.0, 1.0, true);
    REQUIRE(sync.GetMode() == ClockSyncMode::HardSnap);
    REQUIRE(std::abs(snapped - (raw + 196.0)) < 1e-4);
    REQUIRE(sync.GetHardSnapCount() == 1);
}

TEST_CASE("VideoDecoder 缺失视频与连续解码超时触发性能熔断回退", "[video][bga]")
{
    VideoDecoder decoder;
    REQUIRE(!decoder.Open("nonexistent_folder/missing_stage.mp4", true));
    REQUIRE(decoder.IsFallbackActive());

    // 验证连续帧超时性能熔断器
    VideoDecoder breaker;
    for (int i = 0; i < VideoDecoder::kMaxConsecutiveTimeouts - 1; ++i)
    {
        REQUIRE(breaker.RecordDecodeDurationMs(VideoDecoder::kDecodeTimeoutBudgetMs + 25.0));
        REQUIRE(!breaker.IsCircuitBroken());
    }
    REQUIRE(!breaker.RecordDecodeDurationMs(VideoDecoder::kDecodeTimeoutBudgetMs + 25.0));
    REQUIRE(breaker.IsCircuitBroken());
    REQUIRE(breaker.IsFallbackActive());
}

TEST_CASE("内置曲目《樱落呼吸》MP4 动态舞台元数据与原生硬解读取", "[video][bga]")
{
    const auto chartDir = std::filesystem::path(SAKURA_SOURCE_DIR) / "resources" / "charts" / "sakura_breath";
    const auto mp4Path  = chartDir / "bg.mp4";
    REQUIRE(std::filesystem::exists(mp4Path));

    VideoDecoder decoder;
    if (VideoDecoder::IsPlatformSupported())
    {
        const bool opened = decoder.Open(mp4Path.generic_string(), true);
        REQUIRE(opened);
        REQUIRE(!decoder.IsFallbackActive());
        REQUIRE(decoder.GetWidth() == 1280);
        REQUIRE(decoder.GetHeight() == 720);

        VideoFrame frame;
        uint64_t lastSeq = 0;
        bool gotFrame = false;
        for (int attempt = 0; attempt < 40 && !gotFrame; ++attempt)
        {
            gotFrame = decoder.AcquireFrame(0.05, frame, lastSeq);
            if (!gotFrame)
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        REQUIRE(gotFrame);
        REQUIRE(frame.width == 1280);
        REQUIRE(frame.height == 720);
        REQUIRE(frame.rgba.size() == static_cast<std::size_t>(1280 * 720 * 4));
        decoder.Close();
    }
    else
    {
        REQUIRE(!decoder.Open(mp4Path.generic_string(), true));
        REQUIRE(decoder.IsFallbackActive());
    }
}