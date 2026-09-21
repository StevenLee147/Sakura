#pragma once

#include <array>
#include <complex>
#include <numbers>
#include <utility>

namespace sakura::audio
{
// Iterative radix-2 FFT. Fixed storage, no allocation in the analysis loop.
template<size_t N>
void FFT(std::array<std::complex<float>, N>& values)
{
    static_assert(N >= 2 && (N & (N - 1)) == 0);
    static const auto roots = []
    {
        std::array<std::complex<float>, N / 2> result{};
        for (size_t i = 0; i < N / 2; ++i)
            result[i] = std::polar(1.0f, -2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / N);
        return result;
    }();
    for (size_t i = 1, j = 0; i < N; ++i)
    {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (size_t length = 2; length <= N; length <<= 1)
        for (size_t start = 0; start < N; start += length)
            for (size_t j = 0; j < length / 2; ++j)
            {
                const auto even = values[start + j];
                const auto odd = values[start + j + length / 2] * roots[j * N / length];
                values[start + j] = even + odd;
                values[start + j + length / 2] = even - odd;
            }
}
}
