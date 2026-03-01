#pragma once

// trail.h — 拖尾特效（归一化坐标）
// 用 ring buffer 记录历史位置，渲染从不透明到全透明的渐变线段。
// 适用于：音符运动轨迹、Slider 路径可视化等。

#include "core/renderer.h"

#include <array>

namespace sakura::effects
{

// ============================================================================
// TrailEffect — 单条拖尾
// ============================================================================
class TrailEffect
{
public:
    // 拖尾最多保存的历史点数
    static constexpr int MAX_POINTS = 64;

    TrailEffect();

    // ── 控制 ─────────────────────────────────────────────────────────────────

    // 每帧添加最新位置
    void AddPoint(float x, float y);

    // 清除所有点
    void Clear();

    // ── 渲染 ─────────────────────────────────────────────────────────────────

    // baseColor: 最新端点颜色（最不透明），尾端渐变到 透明
    // thickness: 线宽（归一化）
    void Render(sakura::core::Renderer& renderer,
                sakura::core::Color baseColor,
                float thickness = 0.003f) const;

    // ── 查询 ─────────────────────────────────────────────────────────────────

    int GetPointCount() const { return m_count; }

private:
    // ring buffer
    struct Point { float x, y; };
    std::array<Point, MAX_POINTS> m_points;
    int m_head  = 0;   // 最新写入的位置
    int m_count = 0;
};

// ============================================================================
// TrailManager — 管理多条拖尾的简单容器
// ============================================================================
class TrailManager
{
public:
    static constexpr int MAX_TRAILS = 16;

    // 分配一条新拖尾，返回 id（-1=失败）
    int  AllocTrail();

    // 释放拖尾
    void FreeTrail(int id);

    // 添加点
    void AddPoint(int id, float x, float y);

    // 清除某条拖尾
    void ClearTrail(int id);

    // 渲染所有活跃拖尾
    void RenderAll(sakura::core::Renderer& renderer,
                   sakura::core::Color baseColor,
                   float thickness = 0.003f) const;

    // 渲染指定拖尾
    void Render(int id, sakura::core::Renderer& renderer,
                sakura::core::Color baseColor,
                float thickness = 0.003f) const;

private:
    std::array<TrailEffect, MAX_TRAILS> m_trails;
    std::array<bool,        MAX_TRAILS> m_active{};
};

} // namespace sakura::effects
