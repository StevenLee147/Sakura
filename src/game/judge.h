#pragma once

// judge.h — 判定系统
// 处理所有类型音符的判定逻辑

#include "note.h"
#include <vector>
#include <span>

namespace sakura::game
{

// ── 判定窗口配置 ──────────────────────────────────────────────────────────────

struct JudgeWindows
{
    int perfect = 25;   // ±25ms → Perfect
    int great   = 50;   // ±50ms → Great
    int good    = 80;   // ±80ms → Good
    int bad     = 120;  // ±120ms → Bad
    int miss    = 150;  // >±150ms → Miss (自动标记)
};

// ── Hold 判定状态 ─────────────────────────────────────────────────────────────

// 每个 Hold 音符的持续判定状态（由游戏场景维护）
struct HoldState
{
    int   noteIndex       = -1;     // 对应 keyboardNotes 的索引
    bool  isHeld          = false;  // 当前是否被按住
    bool  headJudged      = false;  // 头部是否已判定
    JudgeResult headResult = JudgeResult::None;
    int   nextTickMs      = 0;      // 下一个 tick 判定时间（ms）
    int   tickCount       = 0;      // 已经过的 tick 数
    int   hitTickCount    = 0;      // 成功的 tick 数
    static constexpr int TICK_INTERVAL_MS = 100;
};

// ── Slider 判定状态 ───────────────────────────────────────────────────────────

struct SliderState
{
    int   noteIndex       = -1;
    bool  headJudged      = false;
    JudgeResult headResult = JudgeResult::None;
    int   sampleCount     = 0;     // 总采样次数
    int   hitSampleCount  = 0;     // 命中采样次数
    static constexpr float PATH_TOLERANCE = 0.08f;  // 路径跟踪容差（归一化）
};

// ── Judge — 判定计算器 ────────────────────────────────────────────────────────

class Judge
{
public:
    Judge()  = default;
    ~Judge() = default;

    // ── 判定窗口配置 ──────────────────────────────────────────────────────────

    // 从 Config 读取偏移并初始化（±5ms 可调）
    void Initialize();

    const JudgeWindows& GetWindows() const { return m_windows; }

    // ── 普通按键判定 ──────────────────────────────────────────────────────────

    // 键盘音符判定（Tap/Hold头部/Drag头部）
    // hitTimeMs: 玩家按下时刻（游戏时间，毫秒）
    JudgeResult JudgeKeyboardNote(KeyboardNote& note, int hitTimeMs);

    // 鼠标音符判定（Circle/Slider头部）
    // hitX, hitY: 鼠标点击位置（鼠标区域内的归一化坐标 0~1）
    JudgeResult JudgeMouseNote(MouseNote& note, int hitTimeMs, float hitX, float hitY);

    // ── 自动 Miss 检测 ────────────────────────────────────────────────────────

    // 检查并标记所有超时未判定的键盘音符为 Miss
    // currentTimeMs: 当前游戏时间
    // 返回本次新增 Miss 数量
    int CheckMisses(std::vector<KeyboardNote>& notes, int currentTimeMs);
    int CheckMouseMisses(std::vector<MouseNote>& notes, int currentTimeMs);

    // ── Hold 持续判定 ─────────────────────────────────────────────────────────

    // 每帧更新 Hold 判定状态（传入当前 Hold 状态List）
    // isKeyDown: 当前 Hold 对应轨道键是否被按住
    // 返回本 tick 的判定结果（None=无新判定，Perfect/Great/Good/Bad/Miss=有新 tick）
    JudgeResult UpdateHoldTick(HoldState& state,
                               const KeyboardNote& note,
                               int currentTimeMs,
                               bool isKeyDown);

    // ── Drag 判定 ─────────────────────────────────────────────────────────────

    // 判定 Drag 终点（在目标轨道按下时调用）
    JudgeResult JudgeDragEnd(KeyboardNote& note, int hitTimeMs, int hitLane);

    // ── Slider 路径判定 ───────────────────────────────────────────────────────

    // 每帧采样鼠标位置，更新 Slider 跟踪状态
    // mouseX, mouseY: 当前鼠标位置（鼠标区域归一化坐标）
    // isMouseDown: 鼠标左键是否按住
    JudgeResult UpdateSliderTracking(SliderState& state,
                                     const MouseNote& note,
                                     int currentTimeMs,
                                     float mouseX, float mouseY,
                                     bool isMouseDown);

    // 计算 Slider 路径在 t(0~1) 处的插值位置
    static std::pair<float, float> GetSliderPosition(const MouseNote& note, float t);

    // ── 偏差计算 ──────────────────────────────────────────────────────────────

    // 返回判定偏差（毫秒）：正值 = 偏早，负值 = 偏晚
    static int GetHitError(int noteTime, int hitTime);

    // ── 工具 ──────────────────────────────────────────────────────────────────

    // 根据时间差（绝对值毫秒）返回判定结果
    JudgeResult GetResultByTimeDiff(int absDiffMs) const;

private:
    JudgeWindows m_windows;
};

} // namespace sakura::game
