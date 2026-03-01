#pragma once

// glow.h — 发光特效（基于多层半透明叠加 + additive 混合）
// 使用归一化坐标，兼容各分辨率。

#include "core/renderer.h"

namespace sakura::effects
{

// ============================================================================
// GlowEffect — 发光工具函数集合（无状态，全部为静态调用）
// ============================================================================
class GlowEffect
{
public:
    // ── 圆形发光 ─────────────────────────────────────────────────────────────

    // 在 (cx, cy) 画多层半透明圆，形成发光效果
    // radius:    核心半径（归一化）
    // glowRange: 发光向外延伸的额外半径（归一化）
    // layers:    叠加层数（越多越柔和，通常 4~8）
    static void DrawGlow(sakura::core::Renderer& renderer,
                         float cx, float cy,
                         float radius,
                         sakura::core::Color baseColor,
                         float glowRange = 0.02f,
                         int layers = 6);

    // sin 脉冲发光（phase: 外部传入累计时间，用于动态呼吸效果）
    // sizeMin/Max: 发光半径的最小/最大值
    static void PulseGlow(sakura::core::Renderer& renderer,
                          float cx, float cy,
                          float sizeMin, float sizeMax,
                          sakura::core::Color baseColor,
                          float phase,
                          float frequency = 2.0f,
                          int layers = 5);

    // ── 线段发光 ─────────────────────────────────────────────────────────────

    // 在两点间画发光线段（多层向外扩散）
    static void DrawGlowLine(sakura::core::Renderer& renderer,
                             float x1, float y1,
                             float x2, float y2,
                             sakura::core::Color baseColor,
                             float coreThickness  = 0.002f,
                             float glowMultiplier = 4.0f,
                             int layers = 5);

    // ── 矩形发光（判定线使用）────────────────────────────────────────────────

    // 画一个横向发光条（适用于判定线脉冲）
    static void DrawGlowBar(sakura::core::Renderer& renderer,
                            float x, float y, float width, float height,
                            sakura::core::Color baseColor,
                            float glowExpand = 0.005f,
                            int layers = 5);
};

} // namespace sakura::effects
