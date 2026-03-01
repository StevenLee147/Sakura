// judge.cpp — 判定系统实现

#include "judge.h"
#include "core/config.h"
#include "utils/logger.h"

#include <cmath>
#include <algorithm>

namespace sakura::game
{

// 鼠标点击命中容差（归一化欧氏距离）
static constexpr float MOUSE_HIT_TOLERANCE = 0.06f;

// ── Initialize ────────────────────────────────────────────────────────────────

void Judge::Initialize()
{
    // 从 Config 读取判定偏移（±5ms 微调）
    int offset = sakura::core::Config::GetInstance()
        .Get<int>("game.judge_offset", 0);

    // 确保偏移在合理范围内（-5ms ~ +5ms）
    offset = std::max(-5, std::min(5, offset));

    // 应用偏移到各窗口
    m_windows.perfect = std::max(5,  25  + offset);
    m_windows.great   = std::max(10, 50  + offset);
    m_windows.good    = std::max(20, 80  + offset);
    m_windows.bad     = std::max(40, 120 + offset);
    m_windows.miss    = std::max(60, 150 + offset);

    LOG_DEBUG("Judge 初始化: Perfect=±{}ms, Great=±{}ms, Good=±{}ms, Bad=±{}ms, Miss=±{}ms",
              m_windows.perfect, m_windows.great,
              m_windows.good, m_windows.bad, m_windows.miss);
}

// ── GetResultByTimeDiff ───────────────────────────────────────────────────────

JudgeResult Judge::GetResultByTimeDiff(int absDiffMs) const
{
    if (absDiffMs <= m_windows.perfect) return JudgeResult::Perfect;
    if (absDiffMs <= m_windows.great)   return JudgeResult::Great;
    if (absDiffMs <= m_windows.good)    return JudgeResult::Good;
    if (absDiffMs <= m_windows.bad)     return JudgeResult::Bad;
    return JudgeResult::Miss;
}

// ── JudgeKeyboardNote ─────────────────────────────────────────────────────────

JudgeResult Judge::JudgeKeyboardNote(KeyboardNote& note, int hitTimeMs)
{
    if (note.isJudged) return JudgeResult::None;

    int diff    = hitTimeMs - note.time;
    int absDiff = std::abs(diff);

    // 只接受在 Miss 窗口内的按下（太早不判定）
    if (diff < -m_windows.miss) return JudgeResult::None;

    JudgeResult result = GetResultByTimeDiff(absDiff);

    // Tap 音符：直接标记判定完成
    if (note.type == NoteType::Tap || note.type == NoteType::Circle)
    {
        note.isJudged = true;
        note.result   = result;
    }
    // Hold/Drag：只判定头部（后续由 UpdateHoldTick 处理）
    // 调用方需要据此创建 HoldState
    // 这里仅标记头部已开始，不完全标记 isJudged
    // 完全标记由场景在 Hold 结束时设置

    return result;
}

// ── JudgeMouseNote ────────────────────────────────────────────────────────────

JudgeResult Judge::JudgeMouseNote(MouseNote& note, int hitTimeMs, float hitX, float hitY)
{
    if (note.isJudged) return JudgeResult::None;

    // 时间判定
    int diff    = hitTimeMs - note.time;
    int absDiff = std::abs(diff);
    if (diff < -m_windows.miss) return JudgeResult::None;

    JudgeResult timeResult = GetResultByTimeDiff(absDiff);
    if (timeResult == JudgeResult::Miss)
    {
        note.isJudged = true;
        note.result   = JudgeResult::Miss;
        return JudgeResult::Miss;
    }

    // 距离判定（归一化欧氏距离）
    float dx = hitX - note.x;
    float dy = hitY - note.y;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist > MOUSE_HIT_TOLERANCE)
    {
        // 距离太远，不判定
        return JudgeResult::None;
    }

    // Circle：直接完成
    if (note.type == NoteType::Circle)
    {
        note.isJudged = true;
        note.result   = timeResult;
    }
    // Slider：头部判定（路径跟踪由 UpdateSliderTracking 处理）

    return timeResult;
}

// ── CheckMisses ───────────────────────────────────────────────────────────────

int Judge::CheckMisses(std::vector<KeyboardNote>& notes, int currentTimeMs)
{
    int missCount = 0;

    for (auto& note : notes)
    {
        if (note.isJudged) continue;

        // Hold/Drag 音符头部超时检查
        int diff = currentTimeMs - note.time;
        if (diff > m_windows.miss)
        {
            note.isJudged = true;
            note.result   = JudgeResult::Miss;
            ++missCount;
        }
    }

    return missCount;
}

int Judge::CheckMouseMisses(std::vector<MouseNote>& notes, int currentTimeMs)
{
    int missCount = 0;
    for (auto& note : notes)
    {
        if (note.isJudged) continue;

        int diff = currentTimeMs - note.time;
        if (diff > m_windows.miss)
        {
            note.isJudged = true;
            note.result   = JudgeResult::Miss;
            ++missCount;
        }
    }
    return missCount;
}

// ── UpdateHoldTick ────────────────────────────────────────────────────────────
//
// 新逻辑（无 tick）：
//   - 头部尚未判定 → None
//   - 已松开 (releaseTimeMs >= 0)，且松开时间在 holdEnd - miss_window 之前
//     → 立即返回 Miss（提前松开）
//   - currentTimeMs > holdEnd → hold 结束，返回 headResult 或 Miss
//   - 其他 → None（hold 仍在进行中）
//
JudgeResult Judge::UpdateHoldTick(HoldState& state,
                                  const KeyboardNote& note,
                                  int currentTimeMs)
{
    if (!state.headJudged) return JudgeResult::None;

    int holdEnd = note.time + note.duration;

    // ── 提前松开检测 ──────────────────────────────────────────────────────────
    if (state.releaseTimeMs >= 0)
    {
        // 如果松开时间在 holdEnd - miss_window 之前，视为提前松开 → Miss
        if (state.releaseTimeMs < holdEnd - m_windows.miss)
        {
            state.finalized = true;
            return JudgeResult::Miss;
        }
        // 在容忍窗口内松开，等 hold 结束后再最终判定
    }

    // ── Hold 结束判定 ─────────────────────────────────────────────────────────
    if (currentTimeMs > holdEnd + m_windows.good)
    {
        // 已持续到 hold 末端（或在容忍窗口内）→ 使用头部结果
        state.finalized = true;
        return state.headResult;
    }

    // hold 仍在进行中
    return JudgeResult::None;
}

// ── JudgeDragEnd ──────────────────────────────────────────────────────────────

JudgeResult Judge::JudgeDragEnd(KeyboardNote& note, int hitTimeMs, int hitLane)
{
    if (note.type != NoteType::Drag) return JudgeResult::None;

    // 检查是否在目标轨道
    if (hitLane != note.dragToLane) return JudgeResult::None;

    // 时间判定（Drag 结束时间 = start + duration）
    int endTime = note.time + note.duration;
    int diff    = hitTimeMs - endTime;
    int absDiff = std::abs(diff);

    JudgeResult result = GetResultByTimeDiff(absDiff);
    note.isJudged = true;
    note.result   = result;
    return result;
}

// ── UpdateSliderTracking ──────────────────────────────────────────────────────

JudgeResult Judge::UpdateSliderTracking(SliderState& state,
                                        const MouseNote& note,
                                        int currentTimeMs,
                                        float mouseX, float mouseY,
                                        bool isMouseDown)
{
    if (!state.headJudged) return JudgeResult::None;

    int sliderEnd = note.time + note.sliderDuration;
    if (currentTimeMs > sliderEnd + 50) return JudgeResult::None;

    // 计算进度 t (0~1)
    float t = static_cast<float>(currentTimeMs - note.time)
            / std::max(1, note.sliderDuration);
    t = std::max(0.0f, std::min(1.0f, t));

    // 获取路径上的期望位置
    auto [expectedX, expectedY] = GetSliderPosition(note, t);

    ++state.sampleCount;

    // 检查鼠标是否在容差范围内
    if (isMouseDown)
    {
        float dx   = mouseX - expectedX;
        float dy   = mouseY - expectedY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= SliderState::PATH_TOLERANCE)
        {
            ++state.hitSampleCount;
        }
    }

    return JudgeResult::None;  // 逐帧采样，最终结果由 scene 在 Slider 结束后计算
}

// ── GetSliderPosition ─────────────────────────────────────────────────────────

std::pair<float, float> Judge::GetSliderPosition(const MouseNote& note, float t)
{
    // 起点 = note 坐标
    // 路径节点 = note.sliderPath
    // 在路径节点之间进行线性插值

    if (note.sliderPath.empty())
    {
        // 无路径：保持在起点
        return { note.x, note.y };
    }

    // 构建完整路径（包含起点）
    // 路径总节点数 = 1(起点) + sliderPath.size()
    size_t totalNodes = 1 + note.sliderPath.size();
    float segLen = 1.0f / static_cast<float>(totalNodes - 1);

    size_t segIdx = static_cast<size_t>(t / segLen);
    segIdx = std::min(segIdx, totalNodes - 2);

    float localT = (t - segIdx * segLen) / segLen;
    localT = std::max(0.0f, std::min(1.0f, localT));

    float x0, y0, x1, y1;
    if (segIdx == 0)
    {
        x0 = note.x;  y0 = note.y;
        x1 = note.sliderPath[0].first;
        y1 = note.sliderPath[0].second;
    }
    else
    {
        x0 = note.sliderPath[segIdx - 1].first;
        y0 = note.sliderPath[segIdx - 1].second;
        x1 = note.sliderPath[segIdx].first;
        y1 = note.sliderPath[segIdx].second;
    }

    return { x0 + (x1 - x0) * localT, y0 + (y1 - y0) * localT };
}

// ── GetHitError ───────────────────────────────────────────────────────────────

int Judge::GetHitError(int noteTime, int hitTime)
{
    // 正 = 偏早（hit 先于 note），负 = 偏晚
    return noteTime - hitTime;
}

} // namespace sakura::game
