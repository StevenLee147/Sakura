#pragma once

// audio_visualizer.h — 音频可视化分析与渲染

#include "core/renderer.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

struct ma_decoder;

namespace sakura::audio
{

enum class VisualizerMode
{
    BarMode,
    CircleMode,
    WaveMode,
};

class AudioVisualizer
{
public:
    static AudioVisualizer& GetInstance();

    AudioVisualizer(const AudioVisualizer&) = delete;
    AudioVisualizer& operator=(const AudioVisualizer&) = delete;

    void SetSourceFile(std::string_view path);
    void ClearSource();
    void AddImpulse(float strength);
    void Update(float dt, double playbackPositionSeconds, bool isPlaying);

    const std::array<float, 32>& GetBands() const { return m_bands; }

    void RenderBars(sakura::core::Renderer& renderer,
                    sakura::core::NormRect area,
                    sakura::core::Color lowColor,
                    sakura::core::Color highColor,
                    float opacity = 1.0f) const;

    void RenderCircle(sakura::core::Renderer& renderer,
                      float centerX,
                      float centerY,
                      float baseRadius,
                      sakura::core::Color color,
                      float opacity = 1.0f) const;

    void RenderWave(sakura::core::Renderer& renderer,
                    sakura::core::NormRect area,
                    sakura::core::Color color,
                    float opacity = 1.0f) const;

private:
    AudioVisualizer() = default;
    ~AudioVisualizer();

    bool OpenDecoder(std::string_view path);
    void CloseDecoder();
    void ApplyDecay(float dt);
    void AnalyzeSamples(const float* samples, std::size_t frameCount, int channels, int sampleRate, float dt);

    ma_decoder* m_decoder = nullptr;
    std::string m_sourcePath;

    std::array<float, 32> m_bands = {};
    std::array<float, 32> m_peaks = {};
    std::vector<float> m_waveform;
    float m_impulse = 0.0f;
};

} // namespace sakura::audio