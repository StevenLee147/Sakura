// audio_visualizer.cpp — 音频可视化分析与渲染

#include <miniaudio.h>

#include "audio_visualizer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>

namespace sakura::audio
{

namespace
{

constexpr std::size_t kBandCount = 32;
constexpr std::size_t kAnalysisFrames = 1024;
constexpr int kAnalysisChannels = 2;
constexpr int kAnalysisSampleRate = 44100;

sakura::core::Color ScaleAlpha(sakura::core::Color color, float opacity)
{
    color.a = static_cast<uint8_t>(std::clamp(opacity, 0.0f, 1.0f) * static_cast<float>(color.a));
    return color;
}

sakura::core::Color LerpColor(sakura::core::Color a, sakura::core::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return sakura::core::Color{
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t),
    };
}

} // namespace

AudioVisualizer& AudioVisualizer::GetInstance()
{
    static AudioVisualizer instance;
    return instance;
}

AudioVisualizer::~AudioVisualizer()
{
    CloseDecoder();
}

void AudioVisualizer::SetSourceFile(std::string_view path)
{
    if (path.empty())
    {
        ClearSource();
        return;
    }

    if (m_sourcePath == path)
        return;

    OpenDecoder(path);
}

void AudioVisualizer::ClearSource()
{
    CloseDecoder();
}

void AudioVisualizer::AddImpulse(float strength)
{
    m_impulse = std::max(m_impulse, std::clamp(strength, 0.0f, 1.0f));
}

void AudioVisualizer::Update(float dt, double playbackPositionSeconds, bool isPlaying)
{
    if (!isPlaying || !m_decoder)
    {
        ApplyDecay(dt);
        return;
    }

    ma_result seekResult = ma_decoder_seek_to_pcm_frame(
        m_decoder,
        static_cast<ma_uint64>(std::max(0.0, playbackPositionSeconds) * kAnalysisSampleRate));
    if (seekResult != MA_SUCCESS)
    {
        ApplyDecay(dt);
        return;
    }

    std::array<float, kAnalysisFrames * kAnalysisChannels> buffer = {};
    ma_uint64 framesRead = 0;
    ma_result readResult = ma_decoder_read_pcm_frames(
        m_decoder,
        buffer.data(),
        kAnalysisFrames,
        &framesRead);

    if (readResult != MA_SUCCESS || framesRead == 0)
    {
        ApplyDecay(dt);
        return;
    }

    AnalyzeSamples(buffer.data(), static_cast<std::size_t>(framesRead), kAnalysisChannels, kAnalysisSampleRate, dt);
}

void AudioVisualizer::RenderBars(sakura::core::Renderer& renderer,
                                 sakura::core::NormRect area,
                                 sakura::core::Color lowColor,
                                 sakura::core::Color highColor,
                                 float opacity) const
{
    float gap = area.width * 0.005f;
    float barWidth = (area.width - gap * static_cast<float>(kBandCount - 1)) / static_cast<float>(kBandCount);

    for (std::size_t index = 0; index < kBandCount; ++index)
    {
        float x = area.x + index * (barWidth + gap);
        float level = std::clamp(m_bands[index], 0.0f, 1.0f);
        float peak = std::clamp(m_peaks[index], 0.0f, 1.0f);
        float height = area.height * level;
        sakura::core::Color barColor = ScaleAlpha(
            LerpColor(lowColor, highColor, static_cast<float>(index) / static_cast<float>(kBandCount - 1)),
            opacity);

        renderer.DrawFilledRect({ x, area.y + area.height - height, barWidth, height }, barColor);
        renderer.DrawFilledRect(
            { x, area.y + area.height - area.height * peak - 0.003f, barWidth, 0.003f },
            ScaleAlpha(sakura::core::Color{ 255, 240, 220, 240 }, opacity));
    }
}

void AudioVisualizer::RenderCircle(sakura::core::Renderer& renderer,
                                   float centerX,
                                   float centerY,
                                   float baseRadius,
                                   sakura::core::Color color,
                                   float opacity) const
{
    sakura::core::Color ringColor = ScaleAlpha(color, opacity * 0.45f);
    renderer.DrawCircleOutline(centerX, centerY, baseRadius, ringColor, 0.002f, 64);

    for (std::size_t index = 0; index < kBandCount; ++index)
    {
        float angle = (static_cast<float>(index) / static_cast<float>(kBandCount)) * 2.0f * std::numbers::pi_v<float>;
        float level = std::clamp(m_bands[index], 0.0f, 1.0f);
        float innerRadius = baseRadius;
        float outerRadius = baseRadius + 0.01f + level * 0.05f;
        float x1 = centerX + std::cos(angle) * innerRadius;
        float y1 = centerY + std::sin(angle) * innerRadius;
        float x2 = centerX + std::cos(angle) * outerRadius;
        float y2 = centerY + std::sin(angle) * outerRadius;
        renderer.DrawLine(x1, y1, x2, y2, ScaleAlpha(color, opacity), 0.0014f + level * 0.0014f);
    }
}

void AudioVisualizer::RenderWave(sakura::core::Renderer& renderer,
                                 sakura::core::NormRect area,
                                 sakura::core::Color color,
                                 float opacity) const
{
    if (m_waveform.size() < 2)
        return;

    sakura::core::Color waveColor = ScaleAlpha(color, opacity);
    float centerY = area.y + area.height * 0.5f;
    float amplitude = area.height * 0.45f;
    for (std::size_t index = 1; index < m_waveform.size(); ++index)
    {
        float t1 = static_cast<float>(index - 1) / static_cast<float>(m_waveform.size() - 1);
        float t2 = static_cast<float>(index) / static_cast<float>(m_waveform.size() - 1);
        float x1 = area.x + area.width * t1;
        float x2 = area.x + area.width * t2;
        float y1 = centerY - m_waveform[index - 1] * amplitude;
        float y2 = centerY - m_waveform[index] * amplitude;
        renderer.DrawLine(x1, y1, x2, y2, waveColor, 0.0016f);
    }
}

bool AudioVisualizer::OpenDecoder(std::string_view path)
{
    CloseDecoder();

    if (path.empty() || !std::filesystem::exists(path))
        return false;

    ma_decoder_config config = ma_decoder_config_init(
        ma_format_f32,
        kAnalysisChannels,
        kAnalysisSampleRate);

    m_decoder = new ma_decoder();
    ma_result result = ma_decoder_init_file(path.data(), &config, m_decoder);
    if (result != MA_SUCCESS)
    {
        delete m_decoder;
        m_decoder = nullptr;
        return false;
    }

    m_sourcePath = std::string(path);
    return true;
}

void AudioVisualizer::CloseDecoder()
{
    if (m_decoder)
    {
        ma_decoder_uninit(m_decoder);
        delete m_decoder;
        m_decoder = nullptr;
    }

    m_sourcePath.clear();
}

void AudioVisualizer::ApplyDecay(float dt)
{
    for (std::size_t index = 0; index < kBandCount; ++index)
    {
        m_bands[index] = std::max(0.0f, m_bands[index] - dt * 0.7f);
        m_peaks[index] = std::max(m_bands[index], m_peaks[index] - dt * 0.4f);
    }
    m_impulse = std::max(0.0f, m_impulse - dt * 2.2f);
}

void AudioVisualizer::AnalyzeSamples(const float* samples, std::size_t frameCount, int channels, int sampleRate, float dt)
{
    if (!samples || frameCount == 0 || channels <= 0 || sampleRate <= 0)
    {
        ApplyDecay(dt);
        return;
    }

    std::vector<float> mono(frameCount, 0.0f);
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        float sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            sum += samples[frame * channels + channel];
        mono[frame] = (sum / static_cast<float>(channels))
                    * (0.54f - 0.46f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(frame) / static_cast<float>(std::max<std::size_t>(1, frameCount - 1))));
    }

    m_waveform.clear();
    constexpr std::size_t kWavePoints = 96;
    m_waveform.reserve(kWavePoints);
    for (std::size_t index = 0; index < kWavePoints; ++index)
    {
        std::size_t sampleIndex = (index * frameCount) / kWavePoints;
        sampleIndex = std::min(sampleIndex, frameCount - 1);
        m_waveform.push_back(std::clamp(mono[sampleIndex] * 1.8f, -1.0f, 1.0f));
    }

    std::array<float, kBandCount> targets = {};
    constexpr float minFreq = 40.0f;
    constexpr float maxFreq = 12000.0f;
    float logMin = std::log(minFreq);
    float logMax = std::log(maxFreq);

    for (std::size_t band = 0; band < kBandCount; ++band)
    {
        float bandT0 = static_cast<float>(band) / static_cast<float>(kBandCount);
        float bandT1 = static_cast<float>(band + 1) / static_cast<float>(kBandCount);
        float lowFreq = std::exp(logMin + (logMax - logMin) * bandT0);
        float highFreq = std::exp(logMin + (logMax - logMin) * bandT1);
        int lowBin = std::max(1, static_cast<int>(std::floor(lowFreq * static_cast<float>(frameCount) / static_cast<float>(sampleRate))));
        int highBin = std::min(static_cast<int>(frameCount / 2 - 1), static_cast<int>(std::ceil(highFreq * static_cast<float>(frameCount) / static_cast<float>(sampleRate))));

        float energy = 0.0f;
        int binCount = 0;
        for (int bin = lowBin; bin <= highBin; ++bin)
        {
            float real = 0.0f;
            float imag = 0.0f;
            for (std::size_t sample = 0; sample < frameCount; ++sample)
            {
                float phase = 2.0f * std::numbers::pi_v<float> * static_cast<float>(bin) * static_cast<float>(sample) / static_cast<float>(frameCount);
                real += mono[sample] * std::cos(phase);
                imag -= mono[sample] * std::sin(phase);
            }
            energy += std::sqrt(real * real + imag * imag);
            ++binCount;
        }

        if (binCount > 0)
            targets[band] = std::clamp((energy / static_cast<float>(binCount)) * 0.02f, 0.0f, 1.0f);
    }

    for (std::size_t band = 0; band < kBandCount; ++band)
    {
        float shapedTarget = std::clamp(targets[band] + m_impulse * (0.35f + 0.65f * (1.0f - static_cast<float>(band) / static_cast<float>(kBandCount))), 0.0f, 1.0f);
        float speed = shapedTarget > m_bands[band] ? 18.0f : 5.0f;
        float blend = std::clamp(dt * speed, 0.0f, 1.0f);
        m_bands[band] += (shapedTarget - m_bands[band]) * blend;
        m_peaks[band] = std::max(m_peaks[band] - dt * 0.25f, m_bands[band]);
    }

    m_impulse = std::max(0.0f, m_impulse - dt * 1.5f);
}

} // namespace sakura::audio