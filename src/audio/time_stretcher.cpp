#include "audio/time_stretcher.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sakura::audio
{

std::vector<float> TimeStretcher::StretchInterleaved(
    std::span<const float> inputPcm,
    int channels,
    int sampleRate,
    float playbackRate)
{
    if (inputPcm.empty() || channels <= 0 || sampleRate <= 0)
        return {};

    const float rate = std::clamp(playbackRate, 0.25f, 4.0f);
    if (std::abs(rate - 1.0f) < 0.005f)
        return std::vector<float>(inputPcm.begin(), inputPcm.end());

    const size_t totalInFrames = inputPcm.size() / static_cast<size_t>(channels);
    const int winFrames = std::max(64, (sampleRate * 24) / 1000) & ~1; // ~24ms even window
    const int hopOut    = winFrames / 2;                               // 50% Hann overlap
    const int searchTol = std::max(16, (sampleRate * 8) / 1000);       // +-8ms similarity search

    if (totalInFrames < static_cast<size_t>(winFrames * 2))
        return std::vector<float>(inputPcm.begin(), inputPcm.end());

    const size_t targetOutFrames = std::max<size_t>(
        static_cast<size_t>(winFrames),
        static_cast<size_t>(std::llround(static_cast<double>(totalInFrames) / static_cast<double>(rate))));

    std::vector<float> hann(static_cast<size_t>(winFrames));
    for (int i = 0; i < winFrames; ++i)
    {
        hann[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(winFrames)));
    }

    std::vector<float> out(targetOutFrames * static_cast<size_t>(channels), 0.0f);
    std::vector<float> norm(targetOutFrames, 0.0f);

    // Copy first window at frame 0
    for (int i = 0; i < winFrames && static_cast<size_t>(i) < targetOutFrames; ++i)
    {
        const float w = hann[static_cast<size_t>(i)];
        norm[static_cast<size_t>(i)] += w;
        for (int c = 0; c < channels; ++c)
        {
            out[static_cast<size_t>(i * channels + c)] +=
                inputPcm[static_cast<size_t>(i * channels + c)] * w;
        }
    }

    int prevChosenIn = 0;
    const int maxInStart = static_cast<int>(totalInFrames) - winFrames;

    for (size_t outPos = static_cast<size_t>(hopOut);
         outPos + static_cast<size_t>(winFrames) <= targetOutFrames;
         outPos += static_cast<size_t>(hopOut))
    {
        const int nominalIn = std::clamp(
            static_cast<int>(std::llround(static_cast<double>(outPos) * static_cast<double>(rate))),
            0, maxInStart);
        const int naturalIn = std::clamp(prevChosenIn + hopOut, 0, maxInStart);

        const int lo = std::max(0, nominalIn - searchTol);
        const int hi = std::min(maxInStart, nominalIn + searchTol);

        int bestIn = nominalIn;
        double bestCorr = -1e30;

        // Compare candidate frames against natural continuation of previous grain (stride 2 for speed)
        const int cmpLen = hopOut;
        for (int cand = lo; cand <= hi; cand += 2)
        {
            double dot = 0.0;
            double eA  = 1e-9;
            double eB  = 1e-9;
            for (int k = 0; k < cmpLen; k += 2)
            {
                const float a = inputPcm[static_cast<size_t>((naturalIn + k) * channels)];
                const float b = inputPcm[static_cast<size_t>((cand + k) * channels)];
                dot += static_cast<double>(a) * static_cast<double>(b);
                eA  += static_cast<double>(a) * static_cast<double>(a);
                eB  += static_cast<double>(b) * static_cast<double>(b);
            }
            const double ncc = dot / std::sqrt(eA * eB);
            if (ncc > bestCorr)
            {
                bestCorr = ncc;
                bestIn   = cand;
            }
        }

        // Fine 1-sample refinement around bestIn
        for (int cand : { std::max(lo, bestIn - 1), std::min(hi, bestIn + 1) })
        {
            double dot = 0.0, eA = 1e-9, eB = 1e-9;
            for (int k = 0; k < cmpLen; k += 2)
            {
                const float a = inputPcm[static_cast<size_t>((naturalIn + k) * channels)];
                const float b = inputPcm[static_cast<size_t>((cand + k) * channels)];
                dot += static_cast<double>(a) * static_cast<double>(b);
                eA  += static_cast<double>(a) * static_cast<double>(a);
                eB  += static_cast<double>(b) * static_cast<double>(b);
            }
            const double ncc = dot / std::sqrt(eA * eB);
            if (ncc > bestCorr)
            {
                bestCorr = ncc;
                bestIn   = cand;
            }
        }

        for (int i = 0; i < winFrames; ++i)
        {
            const size_t dstF = outPos + static_cast<size_t>(i);
            const size_t srcF = static_cast<size_t>(bestIn + i);
            const float w = hann[static_cast<size_t>(i)];
            norm[dstF] += w;
            for (int c = 0; c < channels; ++c)
            {
                out[dstF * static_cast<size_t>(channels) + static_cast<size_t>(c)] +=
                    inputPcm[srcF * static_cast<size_t>(channels) + static_cast<size_t>(c)] * w;
            }
        }
        prevChosenIn = bestIn;
    }

    for (size_t f = 0; f < targetOutFrames; ++f)
    {
        const float g = norm[f] > 1e-4f ? (1.0f / norm[f]) : 1.0f;
        for (int c = 0; c < channels; ++c)
        {
            const size_t idx = f * static_cast<size_t>(channels) + static_cast<size_t>(c);
            out[idx] = std::clamp(out[idx] * g, -1.0f, 1.0f);
        }
    }
    return out;
}

} // namespace sakura::audio
