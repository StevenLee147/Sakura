#pragma once

// easing.h — 常用缓动函数（header-only）
// 所有函数输入 t ∈ [0, 1]，输出亦约在 [0, 1]（Back/Elastic 可能超出该范围）
// 参考：https://easings.net/

#include <cmath>

namespace sakura::utils
{

// ── 数学常量 ──────────────────────────────────────────────────────────────────
namespace detail
{
    inline constexpr float kPi    = 3.14159265358979323846f;
    inline constexpr float kPi2   = kPi * 2.0f;
    inline constexpr float kHalfPi = kPi * 0.5f;
}

// ── 线性 ──────────────────────────────────────────────────────────────────────

inline constexpr float EaseLinear(float t) noexcept { return t; }

// ── 二次 ──────────────────────────────────────────────────────────────────────

inline constexpr float EaseInQuad(float t) noexcept
{
    return t * t;
}

inline constexpr float EaseOutQuad(float t) noexcept
{
    return t * (2.0f - t);
}

inline constexpr float EaseInOutQuad(float t) noexcept
{
    return t < 0.5f
        ? 2.0f * t * t
        : -1.0f + (4.0f - 2.0f * t) * t;
}

// ── 三次 ──────────────────────────────────────────────────────────────────────

inline constexpr float EaseInCubic(float t) noexcept
{
    return t * t * t;
}

inline constexpr float EaseOutCubic(float t) noexcept
{
    const float f = t - 1.0f;
    return f * f * f + 1.0f;
}

inline constexpr float EaseInOutCubic(float t) noexcept
{
    return t < 0.5f
        ? 4.0f * t * t * t
        : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}

// ── 四次 ──────────────────────────────────────────────────────────────────────

inline constexpr float EaseInQuart(float t) noexcept
{
    return t * t * t * t;
}

inline constexpr float EaseOutQuart(float t) noexcept
{
    const float f = t - 1.0f;
    return 1.0f - f * f * f * f;
}

inline constexpr float EaseInOutQuart(float t) noexcept
{
    const float f = t - 1.0f;
    return t < 0.5f
        ? 8.0f * t * t * t * t
        : 1.0f - 8.0f * f * f * f * f;
}

// ── 五次 ──────────────────────────────────────────────────────────────────────

inline constexpr float EaseInQuint(float t) noexcept
{
    return t * t * t * t * t;
}

inline constexpr float EaseOutQuint(float t) noexcept
{
    const float f = t - 1.0f;
    return f * f * f * f * f + 1.0f;
}

inline constexpr float EaseInOutQuint(float t) noexcept
{
    const float f = t - 1.0f;
    return t < 0.5f
        ? 16.0f * t * t * t * t * t
        : 1.0f + 16.0f * f * f * f * f * f;
}

// ── 指数 ──────────────────────────────────────────────────────────────────────

inline float EaseInExpo(float t) noexcept
{
    return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
}

inline float EaseOutExpo(float t) noexcept
{
    return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

inline float EaseInOutExpo(float t) noexcept
{
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f
        ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
        : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
}

// ── 正弦 ──────────────────────────────────────────────────────────────────────

inline float EaseInSine(float t) noexcept
{
    return 1.0f - std::cos(t * detail::kHalfPi);
}

inline float EaseOutSine(float t) noexcept
{
    return std::sin(t * detail::kHalfPi);
}

inline float EaseInOutSine(float t) noexcept
{
    return -(std::cos(detail::kPi * t) - 1.0f) * 0.5f;
}

// ── 圆形 ──────────────────────────────────────────────────────────────────────

inline float EaseInCirc(float t) noexcept
{
    return 1.0f - std::sqrt(1.0f - t * t);
}

inline float EaseOutCirc(float t) noexcept
{
    const float f = t - 1.0f;
    return std::sqrt(1.0f - f * f);
}

inline float EaseInOutCirc(float t) noexcept
{
    return t < 0.5f
        ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) * 0.5f
        : (std::sqrt(1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f)) + 1.0f) * 0.5f;
}

// ── 回弹（Back） ──────────────────────────────────────────────────────────────

inline constexpr float EaseInBack(float t) noexcept
{
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}

inline constexpr float EaseOutBack(float t) noexcept
{
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}

inline constexpr float EaseInOutBack(float t) noexcept
{
    constexpr float c1 = 1.70158f;
    constexpr float c2 = c1 * 1.525f;
    const float f2 = 2.0f * t;
    const float f2m2 = 2.0f * t - 2.0f;
    return t < 0.5f
        ? (f2 * f2 * ((c2 + 1.0f) * f2 - c2)) * 0.5f
        : (f2m2 * f2m2 * ((c2 + 1.0f) * f2m2 + c2) + 2.0f) * 0.5f;
}

// ── 弹性（Elastic） ───────────────────────────────────────────────────────────

inline float EaseInElastic(float t) noexcept
{
    if (t == 0.0f || t == 1.0f) return t;
    constexpr float c4 = detail::kPi2 / 3.0f;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}

inline float EaseOutElastic(float t) noexcept
{
    if (t == 0.0f || t == 1.0f) return t;
    constexpr float c4 = detail::kPi2 / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

inline float EaseInOutElastic(float t) noexcept
{
    if (t == 0.0f || t == 1.0f) return t;
    constexpr float c5 = detail::kPi2 / 4.5f;
    return t < 0.5f
        ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f
        : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f + 1.0f;
}

// ── 弹跳（Bounce） ────────────────────────────────────────────────────────────

inline float EaseOutBounce(float t) noexcept
{
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;

    if (t < 1.0f / d1)
        return n1 * t * t;
    if (t < 2.0f / d1)
    {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    }
    if (t < 2.5f / d1)
    {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

inline float EaseInBounce(float t) noexcept
{
    return 1.0f - EaseOutBounce(1.0f - t);
}

inline float EaseInOutBounce(float t) noexcept
{
    return t < 0.5f
        ? (1.0f - EaseOutBounce(1.0f - 2.0f * t)) * 0.5f
        : (1.0f + EaseOutBounce(2.0f * t - 1.0f)) * 0.5f;
}

// ── 工具：将缓动函数应用于值范围插值 ─────────────────────────────────────────

// 在 [from, to] 之间用给定缓动函数插值
// easingFn: float(*)(float) 类型的缓动函数
template<typename EasingFn>
inline float ApplyEasing(EasingFn easingFn, float t, float from, float to) noexcept
{
    return from + (to - from) * easingFn(t);
}

} // namespace sakura::utils
