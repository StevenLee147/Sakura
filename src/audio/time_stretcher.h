#pragma once

// time_stretcher.h — Cross-platform WSOLA pitch-preserving time-stretch processor

#include <span>
#include <vector>

namespace sakura::audio
{

class TimeStretcher
{
public:
    // Stretches interleaved float32 PCM by playbackRate (0.5x ~ 2.0x) without altering pitch.
    // playbackRate < 1.0f slows down (produces more output frames);
    // playbackRate > 1.0f speeds up (produces fewer output frames).
    static std::vector<float> StretchInterleaved(
        std::span<const float> inputPcm,
        int channels,
        int sampleRate,
        float playbackRate);
};

} // namespace sakura::audio
