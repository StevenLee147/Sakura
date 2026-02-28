#pragma once

// note.h — 游戏音符相关数据结构与枚举

#include <vector>
#include <utility>

namespace sakura::game
{

// ── 枚举 ──────────────────────────────────────────────────────────────────────

// 音符类型
enum class NoteType
{
    Tap,        // 单击（键盘端普通按键）
    Hold,       // 长按（有持续时长）
    Drag,       // 滑动（从一个轨道到另一个轨道）
    Circle,     // 鼠标端圆圈（点击即可）
    Slider      // 鼠标端滑条（沿路径跟踪）
};

// 判定结果
enum class JudgeResult
{
    Perfect,    // 完美
    Great,      // 好
    Good,       // 不错
    Bad,        // 差
    Miss,       // 未命中
    None        // 未判定
};

// 游戏评级
enum class Grade
{
    SS,         // 99% 以上 + 全 P/Gr
    S,          // 95% 以上
    A,          // 90% 以上
    B,          // 80% 以上
    C,          // 70% 以上
    D           // 70% 以下
};

// ── 键盘端音符 ────────────────────────────────────────────────────────────────

struct KeyboardNote
{
    int        time        = 0;              // 判定时间（毫秒）
    int        lane        = 0;              // 轨道索引（0~3）
    NoteType   type        = NoteType::Tap;  // 音符类型
    int        duration    = 0;              // Hold/Drag 持续时长（毫秒），Tap=0
    int        dragToLane  = -1;             // Drag 目标轨道，-1=不适用

    // 运行时状态（不参与序列化）
    bool       isJudged    = false;
    JudgeResult result     = JudgeResult::None;
    float      renderY     = 0.0f;           // 归一化渲染 Y 坐标
    float      alpha       = 1.0f;           // 透明度（判定后淡出）
};

// ── 鼠标端音符 ────────────────────────────────────────────────────────────────

struct MouseNote
{
    int        time              = 0;                // 判定时间（毫秒）
    float      x                 = 0.5f;             // 归一化 X 坐标（鼠标区域内）
    float      y                 = 0.5f;             // 归一化 Y 坐标（鼠标区域内）
    NoteType   type              = NoteType::Circle; // 音符类型
    int        sliderDuration    = 0;                // Slider 持续时长（毫秒）
    std::vector<std::pair<float, float>> sliderPath; // Slider 路径节点（归一化坐标）

    // 运行时状态（不参与序列化）
    bool       isJudged          = false;
    JudgeResult result           = JudgeResult::None;
    float      approachScale     = 2.0f;             // 接近圈缩放（2.0→1.0）
    float      alpha             = 1.0f;             // 透明度
};

} // namespace sakura::game
