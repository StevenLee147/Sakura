#pragma once

// judge.h — 判定系统
// 处理所有类型音符的判定逻辑

#include "note.h"
#include <array>
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
    int   noteIndex      = -1;     // 对应 keyboardNotes 的索引
    bool  isHeld         = false;  // 当前是否被按住
    bool  headJudged     = false;  // 头部是否已判定
    JudgeResult headResult = JudgeResult::None;
    int   releaseTimeMs  = -1;     // 松开时刻（-1=仍按住）
    int   lastHeldTimeMs = -1;     // 最近一次检测到按住的时刻
    bool  finalized      = false;  // UpdateHoldTick 返回终判后置 true，场景据此移除
    // Hold 断触容错（毫秒）：约 2~3 帧（60fps），过滤极短采样抖动，不掩盖真实松开
    static constexpr int INPUT_GAP_TOLERANCE_MS = 25;
    // 注意：无 tick 系统，UpdateHoldTick 负责最终判定
};

// ── Drag 判定状态 ─────────────────────────────────────────────────────────────

struct DragState
{
    int   noteIndex             = -1;     // 对应 keyboardNotes 的索引
    bool  headJudged            = false;  // 头部是否已判定
    JudgeResult headResult      = JudgeResult::None;
    std::vector<int> pathLanes;           // 包含起点和终点的连续轨道路径
    int   nextLaneIndex         = 1;      // 下一个必须命中的 pathLanes 索引
    std::array<int, 4> releaseTimeMs      = { -1, -1, -1, -1 };
    std::array<int, 4> lastHeldTimeMs     = { -1, -1, -1, -1 };
    bool  finalized             = false;  // 已终判，可从活跃列表移除
    static constexpr int INPUT_GAP_TOLERANCE_MS = HoldState::INPUT_GAP_TOLERANCE_MS;
};

// ── Slider 判定状态 ───────────────────────────────────────────────────────────

struct SliderState
{
    int   noteIndex          = -1;
    bool  headJudged         = false;
    JudgeResult headResult   = JudgeResult::None;
    int   nextWaypointIndex  = 0;    // 下一个待判定的拐点索引（0-based）
    bool  isMissed           = false; // 已进入 miss 状态，后续拐点全判 Miss
    bool  finalized          = false; // 所有拐点判定完毕，可从活跃列表移除
    int   lastDownTimeMs     = -1;    // 最近一次检测到鼠标按住的时刻
    static constexpr float PATH_TOLERANCE = 0.08f;  // 拐点命中容差（归一化）
    // Slider 容错略大于 Hold：鼠标路径跟踪对瞬时抖动更敏感，给 35ms 抗短断触
    static constexpr int INPUT_GAP_TOLERANCE_MS = 35;
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

    // 获取鼠标音符头部点击容差（鼠标区域内归一化距离）
    static float GetMouseHitTolerance(const MouseNote& note);

    // ── 自动 Miss 检测 ────────────────────────────────────────────────────────

    // 检查并标记所有超时未判定的键盘音符为 Miss
    // currentTimeMs: 当前游戏时间
    // 返回本次新增 Miss 数量
    int CheckMisses(std::vector<KeyboardNote>& notes, int currentTimeMs);
    int CheckMouseMisses(std::vector<MouseNote>& notes, int currentTimeMs);

    // ── Hold 持续判定 ─────────────────────────────────────────────────────────

    // 每帧检查 Hold 状态：
    //   - 提前松开（松开位置在 holdEnd - miss_window 之前）→ 立即返回 Miss
    //   - hold 结束后 → 返回 headResult（正常）或 Miss（松开过早）
    //   - 其他：返回 None
    // currentTimeMs: 当前游戏时间
    JudgeResult UpdateHoldTick(HoldState& state,
                               const KeyboardNote& note,
                               int currentTimeMs);

    // 每帧检查 Drag 状态：
    //   - 起始轨道提前松开（dragEnd - miss_window 之前）→ Miss
    //   - 超过 Drag 终点 miss 窗口仍未命中目标轨道 → Miss
    //   - 其他：返回 None
    JudgeResult UpdateDragTick(DragState& state,
                               const KeyboardNote& note,
                               int currentTimeMs);

    // 判定 Drag 路径上的下一个轨道（中间轨/终点轨）
    // 返回 None 表示当前按键不满足时序或轨道要求
    // isFinalStep 输出本次命中是否完成整个 Drag
    JudgeResult JudgeDragStep(DragState& state,
                              KeyboardNote& note,
                              int hitTimeMs,
                              int hitLane,
                              bool& isFinalStep);

    // ── Drag 判定 ─────────────────────────────────────────────────────────────

    // ── Slider 路径判定 ───────────────────────────────────────────────────────

    // 每帧检查下一个拐点是否到达，进行拐点判定
    // mouseX, mouseY: 当前鼠标位置（鼠标区域归一化坐标）
    // isMouseDown: 鼠标左键是否按住
    // 返回：到达拐点时的判定结果；未到达则返回 None
    // 当所有拐点判定完毕时，state.finalized 置 true
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
