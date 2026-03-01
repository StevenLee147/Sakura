// sfx_generator.cpp — 合成占位音效 WAV 文件生成器

#include "sfx_generator.h"
#include "utils/logger.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

namespace sakura::audio
{

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

    int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    std::vector<int16_t> samples(numSamples);

    int fadeStart = static_cast<int>(numSamples * (1.0f - fadeRatio));

    for (int i = 0; i < numSamples; ++i)
    {
        float t   = static_cast<float>(i) / SAMPLE_RATE;
        float env = amplitude;

        // 末尾淡出包络
        if (i >= fadeStart)
        {
            float fadeT = static_cast<float>(i - fadeStart) /
                          static_cast<float>(numSamples - fadeStart);
            env *= (1.0f - fadeT);
        }
        // 首部短暂冲击包络（前 5ms）
        int attackSamples = SAMPLE_RATE * 5 / 1000;
        if (i < attackSamples)
        {
            env *= static_cast<float>(i) / static_cast<float>(attackSamples);
        }

        float sample = env * std::sinf(2.0f * 3.14159265f * frequency * t);
        samples[i] = static_cast<int16_t>(sample * 32767.0f);
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
    int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    std::vector<int16_t> samples(numSamples);

    float phase = 0.0f;
    int   fadeStart = static_cast<int>(numSamples * 0.7f);

    for (int i = 0; i < numSamples; ++i)
    {
        float t    = static_cast<float>(i) / static_cast<float>(numSamples);
        float freq = freqStart + (freqEnd - freqStart) * t;
        float env  = amplitude;
        if (i >= fadeStart)
        {
            float ft = static_cast<float>(i - fadeStart) /
                       static_cast<float>(numSamples - fadeStart);
            env *= (1.0f - ft);
        }
        phase += 2.0f * 3.14159265f * freq / static_cast<float>(SAMPLE_RATE);
        samples[i] = static_cast<int16_t>(env * std::sinf(phase) * 32767.0f);
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

// ── GenerateDefaults ──────────────────────────────────────────────────────────

void SfxGenerator::GenerateDefaults(std::string_view basePath)
{
    std::string base(basePath);

    // ── Hitsound Sets ─────────────────────────────────────────────────────────

    struct SetDef { const char* dir; float tap; float hold; float circle; };
    SetDef sets[] = {
        { "default", 880.0f, 660.0f, 1100.0f },
        { "soft",    660.0f, 528.0f,  880.0f },
        { "drum",    220.0f, 180.0f,  440.0f },
    };

    for (auto& s : sets)
    {
        std::string d = base + "/" + s.dir + "/";
        WriteWav(d + "tap.wav",          s.tap,    50,  0.6f, 0.5f);
        WriteWav(d + "hold_start.wav",   s.hold,   80,  0.5f, 0.4f);
        WriteWav(d + "hold_tick.wav",    s.hold,   20,  0.3f, 0.2f);
        WriteWav(d + "circle.wav",       s.circle, 40,  0.6f, 0.5f);
        WriteSweepWav(d + "slider_start.wav", s.hold, s.tap, 60, 0.5f);

        // 判定音效也放在各 set 目录
        WriteWav(d + "perfect.wav",  1320.0f, 80,  0.7f, 0.4f);
        WriteWav(d + "great.wav",    1100.0f, 70,  0.6f, 0.4f);
        WriteWav(d + "good.wav",      880.0f, 60,  0.5f, 0.4f);
        WriteWav(d + "bad.wav",       440.0f, 50,  0.4f, 0.3f);
        WriteWav(d + "miss.wav",      220.0f, 80,  0.4f, 0.2f);
    }

    // ── UI 音效 ────────────────────────────────────────────────────────────────

    std::string ui = base + "/ui/";
    WriteWav(ui + "button_hover.wav",    660.0f,  20,  0.25f, 0.3f);
    WriteWav(ui + "button_click.wav",    880.0f,  30,  0.45f, 0.4f);
    WriteSweepWav(ui + "transition.wav", 440.0f, 880.0f, 120, 0.4f);
    WriteWav(ui + "result_score.wav",    660.0f,  15,  0.2f,  0.2f);
    WriteWav(ui + "result_grade.wav",    880.0f, 200,  0.7f,  0.5f);
    WriteWav(ui + "toast.wav",           880.0f,  60,  0.5f,  0.4f);

    LOG_INFO("[SfxGenerator] 占位音效已生成至 {}", basePath);
}

} // namespace sakura::audio
