#include "test_framework.h"
#include "audio/time_stretcher.h"
#include "audio/fft.h"

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace sakura::audio;
using namespace sakura::tests::Matchers;

namespace
{

float EstimateDominantFrequencyHz(const std::vector<float>& stereoPcm, int sampleRate)
{
    constexpr std::size_t kN = 1024;
    const std::size_t totalFrames = stereoPcm.size() / 2;
    const std::size_t startFrame = totalFrames / 4;
    std::array<std::complex<float>, kN> spec{};
    for (std::size_t i = 0; i < kN && (startFrame + i) < totalFrames; ++i)
    {
        const float w = 0.5f * (1.0f - std::cos(
            2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kN)));
        spec[i] = stereoPcm[(startFrame + i) * 2] * w;
    }
    FFT(spec);
    std::size_t bestBin = 1;
    float bestMag = 0.0f;
    for (std::size_t b = 1; b < kN / 2; ++b)
    {
        const float m = std::abs(spec[b]);
        if (m > bestMag)
        {
            bestMag = m;
            bestBin = b;
        }
    }
    return static_cast<float>(bestBin) * static_cast<float>(sampleRate) / static_cast<float>(kN);
}

} // namespace

TEST_CASE("WSOLA TimeStretcher scales duration accurately while preserving 440Hz pitch", "[audio][wsola]")
{
    constexpr int kSampleRate = 32768; // exact 32 Hz per 1024-FFT bin
    constexpr float kFreqHz = 448.0f;  // exact bin 14
    constexpr int kFrames = kSampleRate * 1; // 1.0s stereo

    std::vector<float> input(static_cast<std::size_t>(kFrames * 2));
    for (int i = 0; i < kFrames; ++i)
    {
        const float s = 0.75f * std::sin(
            2.0f * std::numbers::pi_v<float> * kFreqHz * static_cast<float>(i) / static_cast<float>(kSampleRate));
        input[static_cast<std::size_t>(i * 2)]     = s;
        input[static_cast<std::size_t>(i * 2 + 1)] = s;
    }

    // 1.0x pass-through
    auto same = TimeStretcher::StretchInterleaved(input, 2, kSampleRate, 1.0f);
    REQUIRE(same.size() == input.size());

    // 0.75x slow-down -> ~1.333s duration, same 448Hz pitch
    auto slow = TimeStretcher::StretchInterleaved(input, 2, kSampleRate, 0.75f);
    const double slowFrames = static_cast<double>(slow.size() / 2);
    REQUIRE_THAT(slowFrames / kFrames, WithinAbs(1.0 / 0.75, 0.02));
    REQUIRE_THAT(EstimateDominantFrequencyHz(slow, kSampleRate), WithinAbs(kFreqHz, 35.0));

    // 1.50x speed-up -> ~0.667s duration, same 448Hz pitch
    auto fast = TimeStretcher::StretchInterleaved(input, 2, kSampleRate, 1.50f);
    const double fastFrames = static_cast<double>(fast.size() / 2);
    REQUIRE_THAT(fastFrames / kFrames, WithinAbs(1.0 / 1.50, 0.02));
    REQUIRE_THAT(EstimateDominantFrequencyHz(fast, kSampleRate), WithinAbs(kFreqHz, 35.0));
}
