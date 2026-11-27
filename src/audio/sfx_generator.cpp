// sfx_generator.cpp — 合成合成音效 WAV 文件生成器

#include "sfx_generator.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <numbers>
#include <vector>
#include <string>
#include <string_view>

namespace sakura::audio
{

namespace
{
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;

enum class SynthProfile
{
    Default,
    Soft,
    Drum,
    Cyber,
    Pure,
    Ukiyo,
};

SynthProfile DetectSynthProfile(std::string_view path)
{
    const std::string dirName = std::filesystem::path(path).parent_path().filename().string();
    if (dirName == "cyber")   return SynthProfile::Cyber;
    if (dirName == "pure")    return SynthProfile::Pure;
    if (dirName == "ukiyo")   return SynthProfile::Ukiyo;
    if (dirName == "drum")    return SynthProfile::Drum;
    if (dirName == "soft")    return SynthProfile::Soft;
    if (dirName == "default" || dirName == "ui") return SynthProfile::Default;

    if (path.find("cyber") != std::string_view::npos) return SynthProfile::Cyber;
    if (path.find("pure")  != std::string_view::npos) return SynthProfile::Pure;
    if (path.find("ukiyo") != std::string_view::npos) return SynthProfile::Ukiyo;
    if (path.find("drum")  != std::string_view::npos) return SynthProfile::Drum;
    if (path.find("soft")  != std::string_view::npos) return SynthProfile::Soft;
    return SynthProfile::Default;
}
}

// ── WAV 文件写入辅助 ──────────────────────────────────────────────────────────

static void WriteLE16(std::ofstream& f, uint16_t v)
{
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
}

static void WriteLE32(std::ofstream& f, uint32_t v)
{
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
    f.put(static_cast<char>((v >> 16) & 0xFF));
    f.put(static_cast<char>((v >> 24) & 0xFF));
}

bool SfxGenerator::WriteWav(std::string_view path,
                             float frequency,
                             int   durationMs,
                             float amplitude,
                             float fadeRatio)
{
    if (std::filesystem::exists(path)) return true;  // 已存在，跳过

    constexpr int   SAMPLE_RATE = 44100;
    constexpr int   CHANNELS    = 1;
    constexpr int   BITS        = 16;

    int numSamples = std::max(1, (SAMPLE_RATE * durationMs) / 1000);
    std::vector<int16_t> samples(numSamples);

    const float clampedFade = std::clamp(fadeRatio, 0.0f, 1.0f);
    int fadeStart = static_cast<int>(numSamples * (1.0f - clampedFade));

    const SynthProfile profile = DetectSynthProfile(path);
    uint32_t noiseState = static_cast<uint32_t>(frequency * 79.0f) ^ (static_cast<uint32_t>(profile) * 2654435761u);
    float phase      = 0.0f;
    float chirpPhase = 0.0f;
    float prevNoise  = 0.0f;

    const int attackSamples = std::max(1, SAMPLE_RATE / (profile == SynthProfile::Pure ? 3000 : 2000));

    for (int i = 0; i < numSamples; ++i)
    {
        float t   = static_cast<float>(i) / SAMPLE_RATE;
        float env = amplitude;

        // 末尾淡出包络
        if (i >= fadeStart && numSamples > fadeStart)
        {
            float fadeT = static_cast<float>(i - fadeStart) /
                          static_cast<float>(numSamples - fadeStart);
            env *= (1.0f - fadeT);
        }
        // 首部极短冲击斜坡（抑制 0 点直流跳变）
        if (i < attackSamples)
        {
            env *= static_cast<float>(i) / static_cast<float>(attackSamples);
        }

        noiseState = noiseState * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((noiseState >> 16) & 65535u) / 32767.5f - 1.0f);

        float rawSample = 0.0f;
        switch (profile)
        {
            case SynthProfile::Cyber:
            {
                // Neon Cyber-Pulse: 明亮 FM + 奇次方波泛音电子瞬态与高频科技感啁啾
                const float pitchSnap = 1.0f + 0.85f * std::exp(-t * 340.0f);
                phase      += kTwoPi * frequency * pitchSnap / SAMPLE_RATE;
                chirpPhase += kTwoPi * (2200.0f + 2600.0f * std::exp(-t * 420.0f)) / SAMPLE_RATE;

                const float bodyEnv = env * std::exp(-t * 62.0f);
                const float fmIndex = 1.35f * std::exp(-t * 95.0f) + 0.28f;
                const float fmMod   = std::sin(phase * 2.0f) * fmIndex;
                const float body    = 0.72f * std::sin(phase + fmMod)
                                    + 0.26f * std::sin(phase * 3.0f)
                                    + 0.14f * std::sin(phase * 5.0f);
                const float transientClick = std::sin(chirpPhase) * std::exp(-t * 580.0f) * 0.56f;
                const float transientNoise = noise * std::exp(-t * 680.0f) * 0.28f;
                rawSample = bodyEnv * body * 0.80f + amplitude * (transientClick + transientNoise);
                break;
            }
            case SynthProfile::Pure:
            {
                // Esports Pure: 极短促紧凑机械轴/木鱼清脆点按，快速衰减零低频浑浊
                const float pitchSnap = 1.0f + 0.18f * std::exp(-t * 420.0f);
                phase += kTwoPi * frequency * pitchSnap / SAMPLE_RATE;

                const float bodyEnv = env * std::exp(-t * 135.0f);
                const float body    = 0.78f * std::sin(phase)
                                    + 0.34f * std::sin(phase * 2.42f)
                                    + 0.16f * std::sin(phase * 3.85f);
                const float transientClick = (0.46f * std::sin(kTwoPi * 2850.0f * t)
                                            + 0.30f * std::sin(kTwoPi * 4600.0f * t)) * std::exp(-t * 950.0f);
                const float hpNoise        = (noise - prevNoise) * 0.5f;
                const float transientNoise = hpNoise * std::exp(-t * 1100.0f) * 0.44f;
                rawSample = bodyEnv * body * 0.82f + amplitude * (transientClick + transientNoise);
                break;
            }
            case SynthProfile::Ukiyo:
            {
                // Ukiyo Jade-Gold: 温润竹韵古筝/太鼓泛音共鸣与木质拨弦瞬态
                const float pitchSnap = 0.98f + 0.28f * std::exp(-t * 150.0f);
                phase += kTwoPi * frequency * pitchSnap / SAMPLE_RATE;

                const float bodyEnv = env * std::exp(-t * 38.0f);
                const float body    = 0.76f * std::sin(phase)
                                    + 0.32f * std::sin(phase * 2.0f)
                                    + 0.18f * std::sin(phase * 3.0f)
                                    + 0.14f * std::sin(phase * 4.12f) * std::exp(-t * 85.0f)
                                    + 0.22f * std::sin(phase * 0.5f) * std::exp(-t * 70.0f);
                const float transientClick = (0.44f * std::sin(kTwoPi * 1180.0f * t)
                                            + 0.22f * std::sin(kTwoPi * 2320.0f * t)) * std::exp(-t * 440.0f);
                const float woodyNoise     = (noise + prevNoise) * 0.5f;
                const float transientNoise = woodyNoise * std::exp(-t * 380.0f) * 0.34f;
                rawSample = bodyEnv * body * 0.78f + amplitude * (transientClick + transientNoise);
                break;
            }
            case SynthProfile::Drum:
            case SynthProfile::Soft:
            case SynthProfile::Default:
            default:
            {
                const bool drum = (profile == SynthProfile::Drum);
                const bool soft = (profile == SynthProfile::Soft);
                const float pitchSnap = drum ? (0.42f + 1.05f * std::exp(-t * 115.0f))
                                             : (1.0f + 0.36f * std::exp(-t * 210.0f));
                phase += kTwoPi * frequency * pitchSnap / SAMPLE_RATE;
                const float bodyEnv        = env * std::exp(-t * (soft ? 42.0f : drum ? 58.0f : 48.0f));
                const float clickFreq      = soft ? 2100.0f : (drum ? 1650.0f : 3150.0f);
                const float transientClick = std::sin(kTwoPi * clickFreq * t) * std::exp(-t * 520.0f) * (soft ? 0.28f : 0.48f);
                const float transientNoise = noise * std::exp(-t * 460.0f) * (drum ? 0.52f : (soft ? 0.20f : 0.36f));
                const float body           = std::sin(phase)
                                           + (soft ? 0.14f : 0.28f) * std::sin(phase * 2.0f)
                                           + (soft ? 0.06f : 0.22f) * std::sin(phase * 2.76f);
                rawSample = bodyEnv * body * 0.78f + amplitude * (transientClick + transientNoise);
                break;
            }
        }

        prevNoise = noise;
        const float saturated = std::tanh(rawSample * 1.42f) * 0.92f;
        samples[i] = static_cast<int16_t>(std::clamp(saturated, -1.0f, 1.0f) * 32767.0f);
    }

    // 写入 WAV 文件
    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path())
        std::filesystem::create_directories(fsPath.parent_path());

    std::ofstream f(fsPath, std::ios::binary);
    if (!f) return false;

    uint32_t dataSize  = static_cast<uint32_t>(numSamples * CHANNELS * (BITS / 8));
    uint32_t chunkSize = 36 + dataSize;
    uint32_t byteRate  = SAMPLE_RATE * CHANNELS * (BITS / 8);
    uint16_t blockAlign = static_cast<uint16_t>(CHANNELS * (BITS / 8));

    // RIFF 头
    f.write("RIFF", 4);   WriteLE32(f, chunkSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);   WriteLE32(f, 16);
    WriteLE16(f, 1);       // PCM
    WriteLE16(f, static_cast<uint16_t>(CHANNELS));
    WriteLE32(f, static_cast<uint32_t>(SAMPLE_RATE));
    WriteLE32(f, byteRate);
    WriteLE16(f, blockAlign);
    WriteLE16(f, static_cast<uint16_t>(BITS));
    f.write("data", 4);   WriteLE32(f, dataSize);
    f.write(reinterpret_cast<const char*>(samples.data()), dataSize);

    return f.good();
}

bool SfxGenerator::WriteSweepWav(std::string_view path,
                                  float freqStart,
                                  float freqEnd,
                                  int   durationMs,
                                  float amplitude)
{
    if (std::filesystem::exists(path)) return true;

    constexpr int SAMPLE_RATE = 44100;
    int numSamples = std::max(1, (SAMPLE_RATE * durationMs) / 1000);
    std::vector<int16_t> samples(numSamples);

    const SynthProfile profile = DetectSynthProfile(path);
    float phase = 0.0f;
    int   fadeStart = static_cast<int>(numSamples * 0.7f);

    for (int i = 0; i < numSamples; ++i)
    {
        float t    = static_cast<float>(i) / static_cast<float>(numSamples);
        float tSec = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
        float freq = freqStart + (freqEnd - freqStart) * t;
        float env  = amplitude;
        if (i >= fadeStart && numSamples > fadeStart)
        {
            float ft = static_cast<float>(i - fadeStart) /
                       static_cast<float>(numSamples - fadeStart);
            env *= (1.0f - ft);
        }
        phase += kTwoPi * freq / static_cast<float>(SAMPLE_RATE);
        float wave = std::sin(phase);
        if (profile == SynthProfile::Cyber)
            wave = 0.76f * std::sin(phase + 0.55f * std::sin(phase * 2.0f)) + 0.24f * std::sin(phase * 3.0f);
        else if (profile == SynthProfile::Pure)
            wave = (0.80f * std::sin(phase) + 0.20f * std::sin(phase * 2.42f)) * std::exp(-tSec * 85.0f);
        else if (profile == SynthProfile::Ukiyo)
            wave = 0.80f * std::sin(phase) + 0.20f * std::sin(phase * 2.0f);

        samples[i] = static_cast<int16_t>(std::clamp(env * wave, -1.0f, 1.0f) * 32767.0f);
    }

    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path())
        std::filesystem::create_directories(fsPath.parent_path());

    std::ofstream f(fsPath, std::ios::binary);
    if (!f) return false;

    uint32_t dataSize = static_cast<uint32_t>(numSamples * 2);
    f.write("RIFF", 4); WriteLE32(f, 36 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); WriteLE32(f, 16);
    WriteLE16(f, 1); WriteLE16(f, 1);
    WriteLE32(f, 44100); WriteLE32(f, 88200);
    WriteLE16(f, 2); WriteLE16(f, 16);
    f.write("data", 4); WriteLE32(f, dataSize);
    f.write(reinterpret_cast<const char*>(samples.data()), dataSize);
    return f.good();
}

float SfxGenerator::ComputePolyphonicGainScale(int concurrentTriggers)
{
    if (concurrentTriggers <= 2)
        return 1.0f;
    const float scale = 1.0f / std::sqrt(1.0f + 0.22f * static_cast<float>(concurrentTriggers - 2));
    return std::clamp(scale, 0.55f, 1.0f);
}

int SfxGenerator::LatencyFramesForMode(std::string_view mode)
{
    if (mode == "ultra_low")
        return 128;
    if (mode == "safe")
        return 512;
    return 256;
}

bool SfxGenerator::WriteChordWav(std::string_view path,
                                 float rootFreq,
                                 float ratio2,
                                 float ratio3,
                                 int durationMs,
                                 float amplitude,
                                 bool upwardShimmer)
{
    if (std::filesystem::exists(path))
        return true;

    constexpr int SAMPLE_RATE = 44100;
    const int numSamples = std::max(1, (SAMPLE_RATE * durationMs) / 1000);
    std::vector<int16_t> samples(static_cast<std::size_t>(numSamples));

    const SynthProfile profile = DetectSynthProfile(path);
    const int attackSamples = std::max(1, SAMPLE_RATE / 1800);
    const int fadeStart     = static_cast<int>(numSamples * 0.45f);

    float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float t    = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
        const float norm = static_cast<float>(i) / static_cast<float>(numSamples);
        float env        = amplitude;
        if (i < attackSamples)
            env *= static_cast<float>(i) / static_cast<float>(attackSamples);
        if (i >= fadeStart && numSamples > fadeStart)
            env *= (1.0f - static_cast<float>(i - fadeStart) / static_cast<float>(numSamples - fadeStart));

        const float bend = upwardShimmer ? (1.0f + 0.12f * norm) : (1.0f + 0.16f * std::exp(-t * 180.0f));
        p1 += kTwoPi * (rootFreq * bend) / static_cast<float>(SAMPLE_RATE);
        p2 += kTwoPi * (rootFreq * ratio2 * bend) / static_cast<float>(SAMPLE_RATE);
        p3 += kTwoPi * (rootFreq * ratio3 * bend) / static_cast<float>(SAMPLE_RATE);

        const float decay = std::exp(-t * (profile == SynthProfile::Pure ? 48.0f : 24.0f));
        float wave = 0.50f * std::sin(p1) + 0.32f * std::sin(p2) + 0.24f * std::sin(p3);
        if (profile == SynthProfile::Cyber)
            wave += 0.15f * std::sin(p2 * 2.0f);
        const float crispClick = std::sin(kTwoPi * 2640.0f * t) * std::exp(-t * 540.0f) * 0.26f;
        const float raw = env * decay * wave + amplitude * crispClick;
        const float sat = std::tanh(raw * 1.25f) * 0.88f;
        samples[static_cast<std::size_t>(i)] = static_cast<int16_t>(std::clamp(sat, -1.0f, 1.0f) * 32767.0f);
    }

    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path())
        std::filesystem::create_directories(fsPath.parent_path());

    std::ofstream f(fsPath, std::ios::binary);
    if (!f)
        return false;

    const uint32_t dataSize = static_cast<uint32_t>(numSamples * 2);
    f.write("RIFF", 4); WriteLE32(f, 36 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); WriteLE32(f, 16);
    WriteLE16(f, 1); WriteLE16(f, 1);
    WriteLE32(f, 44100); WriteLE32(f, 88200);
    WriteLE16(f, 2); WriteLE16(f, 16);
    f.write("data", 4); WriteLE32(f, dataSize);
    f.write(reinterpret_cast<const char*>(samples.data()), dataSize);
    return f.good();
}

// ── GenerateDefaults ──────────────────────────────────────────────────────────

void SfxGenerator::GenerateDefaults(std::string_view basePath)
{
    std::string base(basePath);

    // ── Hitsound Sets (v5: 10 dual-zone gameplay hitsounds + 2 stage SFX + 5 judge SFX) ──

    struct SetDef
    {
        const char* dir;
        float tap;
        float hold;
        float circle;
        float thumb;
        int   tapMs;
        int   holdMs;
        int   tickMs;
        int   circleMs;
        int   sliderMs;
    };
    const SetDef sets[] = {
        { "default",  880.0f, 660.0f, 1100.0f, 330.0f, 50, 80, 20, 40, 60 },
        { "soft",     660.0f, 528.0f,  880.0f, 264.0f, 50, 80, 20, 40, 60 },
        { "drum",     220.0f, 180.0f,  440.0f, 138.0f, 52, 82, 20, 42, 62 },
        { "cyber",   1240.0f, 960.0f, 1480.0f, 440.0f, 44, 70, 18, 38, 52 },
        { "pure",    1040.0f, 780.0f, 1320.0f, 390.0f, 32, 36, 16, 32, 36 },
        { "ukiyo",    520.0f, 390.0f,  780.0f, 260.0f, 58, 88, 22, 48, 64 },
    };

    for (const auto& s : sets)
    {
        const bool isPure = (std::string_view(s.dir) == "pure");
        std::string d = base + "/" + s.dir + "/";
        WriteWav(d + "tap.wav",               s.tap,           s.tapMs,               0.58f, 0.50f);
        WriteWav(d + "hold_start.wav",        s.hold,          s.holdMs,              0.50f, 0.42f);
        WriteWav(d + "hold_tick.wav",         s.hold * 1.12f,  s.tickMs,              0.28f, 0.25f);
        WriteSweepWav(d + "hold_end.wav",     s.hold,          s.tap * 1.25f,         isPure ? 34 : 52, 0.46f);
        WriteChordWav(d + "thumb_bar.wav",    s.thumb,         1.50f, 2.00f,          isPure ? 42 : 68, 0.60f, false);
        WriteWav(d + "circle.wav",            s.circle,        s.circleMs,            0.56f, 0.50f);
        WriteSweepWav(d + "slider_start.wav", s.hold,          s.tap,                 s.sliderMs, 0.48f);
        WriteWav(d + "slider_tick.wav",       s.circle * 1.18f, isPure ? 15 : 20,     0.26f, 0.30f);
        WriteChordWav(d + "link_chord.wav",   s.tap,           1.50f, 2.00f,          isPure ? 48 : 84, 0.58f, false);
        WriteChordWav(d + "synergy_burst.wav", s.tap,          1.25f, 1.50f,          isPure ? 58 : 105, 0.62f, true);
        WriteChordWav(d + "fever_trigger.wav", 523.25f,        1.25f, 1.50f,          180, 0.60f, true);
        WriteSweepWav(d + "combo_break.wav",  390.0f,          155.0f,                95,  0.46f);

        // 判定音效也放在各 set 目录
        WriteWav(d + "perfect.wav",  1320.0f, isPure ? 42 : 80, 0.66f, 0.4f);
        WriteWav(d + "great.wav",    1100.0f, isPure ? 38 : 70, 0.58f, 0.4f);
        WriteWav(d + "good.wav",      880.0f, isPure ? 34 : 60, 0.48f, 0.4f);
        WriteWav(d + "bad.wav",       440.0f, isPure ? 32 : 50, 0.40f, 0.3f);
        WriteWav(d + "miss.wav",      220.0f, isPure ? 44 : 80, 0.40f, 0.2f);
    }

    // ── UI 音效 ────────────────────────────────────────────────────────────────

    std::string ui = base + "/ui/";
    WriteWav(ui + "button_hover.wav",    660.0f,  20,  0.25f, 0.3f);
    WriteWav(ui + "button_click.wav",    880.0f,  30,  0.45f, 0.4f);
    WriteSweepWav(ui + "transition.wav", 440.0f, 880.0f, 120, 0.4f);
    WriteWav(ui + "result_score.wav",    660.0f,  15,  0.2f,  0.2f);
    WriteWav(ui + "result_grade.wav",    880.0f, 200,  0.7f,  0.5f);
    WriteWav(ui + "toast.wav",           880.0f,  60,  0.5f,  0.4f);
    WriteWav(ui + "calibration_beat.wav", 1760.0f, 35, 0.55f, 0.35f);
    WriteWav(ui + "calibration_hit.wav",  1320.0f, 25, 0.40f, 0.30f);

    LOG_INFO("[SfxGenerator] 合成音效已生成至 {}", basePath);
}

} // namespace sakura::audio
