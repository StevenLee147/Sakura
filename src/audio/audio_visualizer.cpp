// audio_visualizer.cpp — 音频可视化分析与渲染

#include <miniaudio.h>

#include "audio_visualizer.h"
#include "fft.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cctype>
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

    m_analysisTimer += dt;
    if (m_analysisTimer < 1.0f / 30.0f) return;
    dt = m_analysisTimer;
    m_analysisTimer = 0.0f;
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

    if ((readResult != MA_SUCCESS && readResult != MA_AT_END) || framesRead == 0)
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
    if (opacity <= 0.001f || area.width <= 0.0f || area.height <= 0.0f) return;

    const float gap = area.width * 0.0065f;
    const float barWidth = (area.width - gap * static_cast<float>(kBandCount - 1)) / static_cast<float>(kBandCount);
    const float cornerR = std::min(0.004f, barWidth * 0.45f);
    const float baseY = area.y + area.height;

    std::array<sakura::core::NormPoint, kBandCount> crestPts{};

    for (std::size_t index = 0; index < kBandCount; ++index)
    {
        const float u = static_cast<float>(index) / static_cast<float>(kBandCount - 1);
        const float x = area.x + static_cast<float>(index) * (barWidth + gap);
        const float level = std::clamp(m_bands[index], 0.0f, 1.0f);
        const float peak = std::clamp(m_peaks[index], 0.0f, 1.0f);
        const float height = std::max(0.0025f, area.height * level);
        const float topY = baseY - height;

        const sakura::core::Color baseCol = LerpColor(lowColor, highColor, u);
        const sakura::core::Color topCol = ScaleAlpha(
            sakura::core::Color{
                static_cast<uint8_t>(std::min(255, baseCol.r + 36)),
                static_cast<uint8_t>(std::min(255, baseCol.g + 32)),
                static_cast<uint8_t>(std::min(255, baseCol.b + 36)),
                baseCol.a
            },
            opacity * (0.55f + 0.45f * level));
        const sakura::core::Color botCol = ScaleAlpha(baseCol, opacity * 0.14f);

        // Luminous rounded glass spectrum pillar
        renderer.DrawRoundedGradientRect(
            { x, topY, barWidth, height },
            cornerR,
            topCol, topCol,
            botCol, botCol,
            5, true);

        // Subtle downward water reflection under baseline
        if (level > 0.04f)
        {
            const float reflH = height * 0.24f;
            renderer.DrawGradientRect(
                { x, baseY + 0.001f, barWidth, reflH },
                ScaleAlpha(baseCol, opacity * 0.16f), ScaleAlpha(baseCol, opacity * 0.16f),
                { baseCol.r, baseCol.g, baseCol.b, 0 }, { baseCol.r, baseCol.g, baseCol.b, 0 });
        }

        // Floating gilded peak jewel
        const float peakY = baseY - area.height * peak - 0.0035f;
        renderer.DrawRoundedRect(
            { x, peakY, barWidth, 0.0026f },
            0.0013f,
            ScaleAlpha(sakura::core::Color{ 252, 226, 174, 225 }, opacity),
            true, 4);

        crestPts[index] = { x + barWidth * 0.5f, topY };
    }

    // Connecting silk harmonic ribbon over bar crests
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawCatmullRomRibbon(
        crestPts.data(),
        static_cast<int>(kBandCount),
        0.0035f, 0.0035f,
        ScaleAlpha(lowColor, opacity * 0.55f),
        ScaleAlpha(highColor, opacity * 0.55f),
        { lowColor.r, lowColor.g, lowColor.b, 0 },
        { highColor.r, highColor.g, highColor.b, 0 },
        4, true);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

void AudioVisualizer::RenderCircle(sakura::core::Renderer& renderer,
                                   float centerX,
                                   float centerY,
                                   float baseRadius,
                                   sakura::core::Color color,
                                   float opacity) const
{
    if (opacity <= 0.001f || baseRadius <= 0.0f) return;

    const float aspect = static_cast<float>(renderer.GetScreenHeight()) /
                         static_cast<float>(std::max(1, renderer.GetScreenWidth()));
    float avgEnergy = 0.0f;
    for (float b : m_bands) avgEnergy += b;
    avgEnergy /= static_cast<float>(kBandCount);

    // Soft radial aura ring
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRingGradient(
        centerX, centerY,
        baseRadius * 0.96f,
        baseRadius * (1.06f + avgEnergy * 0.14f),
        ScaleAlpha(color, opacity * 0.36f),
        { color.r, color.g, color.b, 0 },
        72);

    // Closed smooth silk crown + radial harmonic ticks
    std::array<sakura::core::NormPoint, kBandCount + 3> crown{};
    for (std::size_t index = 0; index < kBandCount + 3; ++index)
    {
        const std::size_t bIdx = index % kBandCount;
        const float angle = (static_cast<float>(index) / static_cast<float>(kBandCount)) * 2.0f * std::numbers::pi_v<float> - std::numbers::pi_v<float> * 0.5f;
        const float level = std::clamp(m_bands[bIdx], 0.0f, 1.0f);
        const float outerR = baseRadius + 0.006f + level * 0.044f;
        crown[index] = {
            centerX + std::cos(angle) * outerR * aspect,
            centerY + std::sin(angle) * outerR
        };

        if (index < kBandCount)
        {
            const float x1 = centerX + std::cos(angle) * (baseRadius * 1.01f) * aspect;
            const float y1 = centerY + std::sin(angle) * (baseRadius * 1.01f);
            const sakura::core::Color tickCol = LerpColor(color, { 250, 218, 162, color.a }, level);
            renderer.DrawLine(x1, y1, crown[index].x, crown[index].y,
                              ScaleAlpha(tickCol, opacity * (0.38f + 0.62f * level)),
                              0.0012f + level * 0.0016f);
        }
    }

    renderer.DrawCatmullRomRibbon(
        crown.data(),
        static_cast<int>(crown.size()),
        0.0042f, 0.0042f,
        ScaleAlpha({ 252, 224, 176, 230 }, opacity * 0.72f),
        ScaleAlpha(color, opacity * 0.72f),
        { color.r, color.g, color.b, 0 },
        { color.r, color.g, color.b, 0 },
        4, false);

    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    renderer.DrawCircleOutline(centerX, centerY, baseRadius, ScaleAlpha(color, opacity * 0.50f), 0.0015f, 72);
}

void AudioVisualizer::RenderWave(sakura::core::Renderer& renderer,
                                 sakura::core::NormRect area,
                                 sakura::core::Color color,
                                 float opacity) const
{
    if (m_waveform.size() < 2 || opacity <= 0.001f)
        return;

    static thread_local std::vector<sakura::core::NormPoint> pts;
    pts.clear();
    pts.reserve(m_waveform.size());

    const float centerY = area.y + area.height * 0.5f;
    const float amplitude = area.height * 0.44f;
    for (std::size_t index = 0; index < m_waveform.size(); ++index)
    {
        const float t = static_cast<float>(index) / static_cast<float>(m_waveform.size() - 1);
        pts.push_back({
            area.x + area.width * t,
            centerY - m_waveform[index] * amplitude
        });
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    sakura::core::Color coreStart = ScaleAlpha(color, opacity * 0.85f);
    sakura::core::Color coreEnd   = ScaleAlpha({ 250, 218, 162, color.a }, opacity * 0.85f);
    renderer.DrawCatmullRomRibbon(
        pts.data(),
        static_cast<int>(pts.size()),
        0.0055f, 0.0055f,
        coreStart, coreEnd,
        { color.r, color.g, color.b, 0 },
        { 250, 218, 162, 0 },
        3, true);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
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

    const std::string filePath(path);
    auto extension = std::filesystem::path(filePath).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    const bool vorbis = extension==".ogg" || extension==".oga";
    if(vorbis){
        const auto size=std::filesystem::file_size(filePath);
        if(size==0 || size>256*1024*1024)return false;
        m_compressedSource.resize(static_cast<size_t>(size));
        std::ifstream source(filePath,std::ios::binary);
        if(!source.read(reinterpret_cast<char*>(m_compressedSource.data()),static_cast<std::streamsize>(size))){m_compressedSource.clear();return false;}
    }
    m_decoder = new ma_decoder();
    // Vorbis analysis needs random access at the music cursor, just like playback.
    ma_result result = vorbis ? ma_decoder_init_memory(m_compressedSource.data(),m_compressedSource.size(),&config,m_decoder)
                             : ma_decoder_init_file(filePath.c_str(), &config, m_decoder);
    if (result != MA_SUCCESS)
    {
        delete m_decoder;
        m_decoder = nullptr;
        m_compressedSource.clear();
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
    std::vector<unsigned char>{}.swap(m_compressedSource);
    m_bands.fill(0);m_peaks.fill(0);m_waveform.clear();m_analysisTimer=0;m_impulse=0;
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

    frameCount = std::min(frameCount, kAnalysisFrames);
    std::array<float, kAnalysisFrames> mono{};
    std::array<std::complex<float>, kAnalysisFrames> spectrum{};
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

    for (std::size_t i = 0; i < frameCount; ++i) spectrum[i] = mono[i];
    FFT(spectrum);

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
        int lowBin = std::max(1, static_cast<int>(std::floor(lowFreq * static_cast<float>(kAnalysisFrames) / static_cast<float>(sampleRate))));
        int highBin = std::min(static_cast<int>(kAnalysisFrames / 2 - 1), static_cast<int>(std::ceil(highFreq * static_cast<float>(kAnalysisFrames) / static_cast<float>(sampleRate))));

        float energy = 0.0f;
        int binCount = 0;
        for (int bin = lowBin; bin <= highBin; ++bin)
        {
            energy += std::abs(spectrum[bin]);
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
