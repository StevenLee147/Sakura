#pragma once

// sfx_generator.h — 合成占位音效 WAV 文件生成器
// 在 resources/sound/sfx/ 目录下按 set 生成简易正弦波 WAV 占位文件

#include <string_view>

namespace sakura::audio
{

class SfxGenerator
{
public:
    // 生成所有占位音效（若文件已存在则跳过）
    // basePath: 例如 "resources/sound/sfx"
    static void GenerateDefaults(std::string_view basePath);

private:
    // 写入单个 WAV 文件（正弦波，单声道, 44100 Hz, 16-bit PCM）
    // frequency: Hz
    // durationMs: 毫秒
    // amplitude: 0.0~1.0
    // fadeRatio: 末尾淡出比例（0~1）
    static bool WriteWav(std::string_view path,
                         float frequency,
                         int   durationMs,
                         float amplitude = 0.6f,
                         float fadeRatio = 0.3f);

    // 写入扫频 WAV（freqStart → freqEnd）
    static bool WriteSweepWav(std::string_view path,
                              float freqStart,
                              float freqEnd,
                              int   durationMs,
                              float amplitude = 0.5f);
};

} // namespace sakura::audio
