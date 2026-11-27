// tests/test_v240_audio_and_sfx.cpp — v2.4.0 sfx-v5 10+2 hitsound synthesis, SkinManager overrides, polyphonic gain limiter & genuine menu BGM tests

#include "test_framework.h"

#include "audio/sfx_generator.h"
#include "core/config.h"
#include "core/skin_manager.h"
#include "utils/file_io.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sakura::audio::SfxGenerator;
using sakura::core::Config;
using sakura::core::SkinManager;

namespace
{
struct WavInspection
{
    bool   validHeader = false;
    int    sampleCount = 0;
    double rms         = 0.0;
    int    peakAbs     = 0;
};

WavInspection InspectPcm16MonoWav(const fs::path& wavPath)
{
    WavInspection out;
    std::ifstream f(wavPath, std::ios::binary);
    if (!f)
        return out;

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 46)
        return out;
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0 ||
        std::memcmp(bytes.data() + 12, "fmt ", 4) != 0 ||
        std::memcmp(bytes.data() + 36, "data", 4) != 0)
    {
        return out;
    }
    out.validHeader = true;

    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(bytes[40]) |
        (static_cast<std::uint32_t>(bytes[41]) << 8U) |
        (static_cast<std::uint32_t>(bytes[42]) << 16U) |
        (static_cast<std::uint32_t>(bytes[43]) << 24U);
    if (44ULL + dataBytes > bytes.size() || dataBytes < 4)
        return out;

    out.sampleCount = static_cast<int>(dataBytes / 2);
    double sumSq = 0.0;
    for (int i = 0; i < out.sampleCount; ++i)
    {
        const std::size_t off = 44 + static_cast<std::size_t>(i) * 2;
        const std::uint16_t raw = static_cast<std::uint16_t>(bytes[off]) |
                                  (static_cast<std::uint16_t>(bytes[off + 1]) << 8U);
        const auto sample = static_cast<std::int16_t>(raw);
        const int absVal  = std::abs(static_cast<int>(sample));
        out.peakAbs = std::max(out.peakAbs, absVal);
        const double norm = static_cast<double>(sample) / 32768.0;
        sumSq += norm * norm;
    }
    out.rms = std::sqrt(sumSq / static_cast<double>(out.sampleCount));
    return out;
}
} // namespace

TEST_CASE("SfxGenerator v5 synthesizes all 6 profiles x 12 dual-zone & stage WAVs cleanly without clipping", "[audio][v240]")
{
    std::error_code ec;
    const fs::path tempRoot = fs::current_path() / "build" / "qa" / "test_v240_sfx_v5";
    fs::remove_all(tempRoot, ec);
    fs::create_directories(tempRoot, ec);

    SfxGenerator::GenerateDefaults(tempRoot.string());

    static constexpr std::array<const char*, 6> kProfiles = {
        "default", "cyber", "pure", "ukiyo", "soft", "drum"
    };
    static constexpr std::array<const char*, 17> kPerSetFiles = {
        "tap.wav", "hold_start.wav", "hold_tick.wav", "hold_end.wav",
        "thumb_bar.wav", "circle.wav", "slider_start.wav", "slider_tick.wav",
        "link_chord.wav", "synergy_burst.wav", "fever_trigger.wav", "combo_break.wav",
        "perfect.wav", "great.wav", "good.wav", "bad.wav", "miss.wav"
    };

    for (const char* prof : kProfiles)
    {
        for (const char* wavName : kPerSetFiles)
        {
            const fs::path wavPath = tempRoot / prof / wavName;
            REQUIRE(fs::is_regular_file(wavPath, ec));
            const WavInspection info = InspectPcm16MonoWav(wavPath);
            REQUIRE(info.validHeader);
            REQUIRE(info.sampleCount >= 500);
            REQUIRE(info.rms > 0.005);
            REQUIRE(info.peakAbs <= 32760);
        }
    }

    static constexpr std::array<const char*, 8> kUiFiles = {
        "button_hover.wav", "button_click.wav", "transition.wav", "result_score.wav",
        "result_grade.wav", "toast.wav", "calibration_beat.wav", "calibration_hit.wav"
    };
    for (const char* uiWav : kUiFiles)
    {
        const WavInspection info = InspectPcm16MonoWav(tempRoot / "ui" / uiWav);
        REQUIRE(info.validHeader);
        REQUIRE(info.rms > 0.005);
    }

    fs::remove_all(tempRoot, ec);
}

TEST_CASE("SkinManager resolves all 12 custom hitsound overrides and polyphonic/latency helpers match v2.4 spec", "[audio][v240]")
{
    // 1. Latency presets & polyphonic gain scaling curve
    REQUIRE(SfxGenerator::LatencyFramesForMode("ultra_low") == 128);
    REQUIRE(SfxGenerator::LatencyFramesForMode("balanced") == 256);
    REQUIRE(SfxGenerator::LatencyFramesForMode("safe") == 512);
    REQUIRE(SfxGenerator::LatencyFramesForMode("unknown") == 256);

    REQUIRE(std::abs(SfxGenerator::ComputePolyphonicGainScale(1) - 1.0f) < 1e-6f);
    REQUIRE(std::abs(SfxGenerator::ComputePolyphonicGainScale(2) - 1.0f) < 1e-6f);
    REQUIRE(SfxGenerator::ComputePolyphonicGainScale(4) < 0.85f);
    REQUIRE(SfxGenerator::ComputePolyphonicGainScale(12) >= 0.55f);

    // 2. SkinManager custom hitsounds override for new v2.4 WAVs
    std::error_code ec;
    const fs::path skinRoot = fs::current_path() / "build" / "qa" / "test_v240_skins";
    const fs::path customDir = skinRoot / "custom_v24" / "hitsounds";
    fs::remove_all(skinRoot, ec);
    fs::create_directories(customDir, ec);

    REQUIRE(sakura::utils::AtomicWrite(
        skinRoot / "custom_v24" / "skin.json",
        R"({"id":"custom_v24","name":"Custom V24","hitsound_set":"cyber"})"));
    REQUIRE(sakura::utils::AtomicWrite(customDir / "thumb_bar.wav", "RIFF"));
    REQUIRE(sakura::utils::AtomicWrite(customDir / "synergy_burst.wav", "RIFF"));
    REQUIRE(sakura::utils::AtomicWrite(customDir / "link_chord.wav", "RIFF"));

    auto& skinMgr = SkinManager::GetInstance();
    skinMgr.ScanSkins("resources/skins", skinRoot.string());
    REQUIRE(skinMgr.SetActiveSkin("custom_v24"));
    REQUIRE(!skinMgr.ResolveCustomHitsoundFile("thumb_bar.wav").empty());
    REQUIRE(!skinMgr.ResolveCustomHitsoundFile("synergy_burst.wav").empty());
    REQUIRE(!skinMgr.ResolveCustomHitsoundFile("link_chord.wav").empty());
    REQUIRE(skinMgr.ResolveCustomHitsoundFile("hold_end.wav").empty()); // falls back to hitsound_set
    skinMgr.SetActiveSkin("moonlit_sakura");
    fs::remove_all(skinRoot, ec);

    // 3. Genuine OggS Vorbis ambient menu BGM verification
    const fs::path srcRoot(SAKURA_SOURCE_DIR);
    const fs::path menuBgm = srcRoot / "resources" / "sound" / "bgm" / "moonlit_menu.ogg";
    REQUIRE(fs::is_regular_file(menuBgm, ec));
    REQUIRE(fs::file_size(menuBgm, ec) > 32768);
    std::ifstream bgmStream(menuBgm, std::ios::binary);
    char magic[4]{};
    bgmStream.read(magic, 4);
    REQUIRE(std::memcmp(magic, "OggS", 4) == 0);

    // 4. Config v2.4 audio defaults
    const fs::path cfgPath = fs::current_path() / "build" / "qa" / "test_v240_cfg.json";
    fs::remove(cfgPath, ec);
    auto& cfg = Config::GetInstance();
    REQUIRE(cfg.Load(cfgPath.string()));
    REQUIRE(cfg.Get<std::string>("audio.latency_mode", "") == "balanced");
    REQUIRE(cfg.Get<bool>("audio.sfx_humanize", false) == true);
    REQUIRE(cfg.Get<bool>("audio.menu_bgm_enabled", false) == true);
    REQUIRE(std::abs(cfg.Get<float>("audio.hitsound_volume", 0.0f) - 0.85f) < 0.01f);
    REQUIRE(std::abs(cfg.Get<float>("audio.ui_sfx_volume", 0.0f) - 0.75f) < 0.01f);
    fs::remove(cfgPath, ec);
}
