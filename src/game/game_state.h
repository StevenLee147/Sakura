#pragma once

// game_state.h — 游戏状态管理
// 一局游戏的完整生命周期：加载→倒计时→游戏中→结算

#include "chart.h"
#include "note.h"
#include <span>
#include <string>

namespace sakura::game
{

// 游戏阶段枚举
enum class GamePhase
{
    Idle,           // 未开始
    Countdown,      // 倒计时（3-2-1）
    Playing,        // 游戏中
    Paused,         // 已暂停
    Finished        // 游戏结束（所有音符已判定 + 音乐播放完毕）
};

// GameState — 管理单局游戏的所有状态
class GameState
{
public:
    GameState()  = default;
    ~GameState() = default;

    // 禁止拷贝
    GameState(const GameState&)            = delete;
    GameState& operator=(const GameState&) = delete;

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    // 开始一局游戏（加载谱面数据和音乐）
    // difficultyIndex: ChartInfo.difficulties 中的难度索引
    bool Start(const ChartInfo& chartInfo, int difficultyIndex = 0);

    // 每帧更新（同步音乐播放位置作为游戏时间）
    void Update(float dt);

    // 暂停 / 恢复
    void Pause();
    void Resume();

    // 重置到初始状态
    void Reset();

    // ── 状态查询 ──────────────────────────────────────────────────────────────

    GamePhase GetPhase()       const { return m_phase; }
    bool      IsPlaying()      const { return m_phase == GamePhase::Playing; }
    bool      IsPaused()       const { return m_phase == GamePhase::Paused; }
    bool      IsFinished()     const { return m_phase == GamePhase::Finished; }
    bool      IsInCountdown()  const { return m_phase == GamePhase::Countdown; }

    // 当前游戏时间（毫秒，基于音乐播放位置 + Config offset）
    int GetCurrentTime() const { return m_currentTimeMs; }

    // 歌曲进度（0.0~1.0）
    float GetProgress() const;

    // 倒计时剩余秒数（Countdown 阶段有效）
    float GetCountdownRemaining() const { return m_countdownTimer; }

    // 倒计时当前数字（3/2/1）
    int GetCountdownNumber() const;

    // ── 音符访问 ──────────────────────────────────────────────────────────────

    // 当前窗口内的活跃键盘音符（time-500ms ~ time+2000ms）
    // 返回 span 指向内部数据，不拷贝
    std::span<KeyboardNote>       GetActiveKeyboardNotes();
    std::span<const KeyboardNote> GetActiveKeyboardNotes() const;

    std::span<MouseNote>          GetActiveMouseNotes();
    std::span<const MouseNote>    GetActiveMouseNotes() const;

    // 完整音符数据（供 judge 系统使用）
    std::vector<KeyboardNote>&    GetKeyboardNotes()        { return m_chartData.keyboardNotes; }
    std::vector<MouseNote>&       GetMouseNotes()           { return m_chartData.mouseNotes; }

    // ── SV / BPM 查询 ─────────────────────────────────────────────────────────

    // 当前 SV 速度倍率（1.0 = 正常）
    float GetCurrentSVSpeed(int timeMs) const;

    // 当前 BPM
    float GetCurrentBPM(int timeMs) const;

    // ── 谱面元信息 ────────────────────────────────────────────────────────────

    const ChartInfo&  GetChartInfo()  const { return m_chartInfo; }
    const ChartData&  GetChartData()  const { return m_chartData; }
    int               GetDifficultyIndex() const { return m_difficultyIndex; }

    // 当前难度总音符数（键盘 + 鼠标）
    int GetTotalNoteCount() const;

private:
    // 更新活跃音符窗口（二分查找）
    void UpdateActiveWindows();

    // 检查游戏是否已完成（Update 内部调用，非 const）
    void CheckFinished();

    // ── 状态 ──────────────────────────────────────────────────────────────────
    GamePhase   m_phase            = GamePhase::Idle;
    int         m_currentTimeMs    = 0;       // 当前游戏时间（毫秒）
    int         m_globalOffset     = 0;       // Config 中的全局偏移（毫秒）
    float       m_countdownTimer   = 3.0f;    // 倒计时剩余（秒）
    bool        m_musicStarted     = false;   // 音乐是否已开始

    // ── 谱面数据 ──────────────────────────────────────────────────────────────
    ChartInfo   m_chartInfo;
    ChartData   m_chartData;
    int         m_difficultyIndex  = 0;
    double      m_musicDuration    = 0.0;     // 音乐总时长（秒）

    // ── 活跃音符窗口索引 ──────────────────────────────────────────────────────
    // 使用首端/尾端索引，避免每帧遍历全部音符
    size_t m_kbActiveBegin = 0;   // 键盘活跃音符起始索引
    size_t m_kbActiveEnd   = 0;   // 键盘活跃音符结束索引（不含）
    size_t m_msActiveBegin = 0;   // 鼠标活跃音符起始索引
    size_t m_msActiveEnd   = 0;   // 鼠标活跃音符结束索引（不含）

    // 活跃窗口时间范围（毫秒）
    static constexpr int ACTIVE_BEFORE_MS = 2000;  // 提前显示 2000ms
    static constexpr int ACTIVE_AFTER_MS  = 500;   // 判定后保留 500ms

    // 倒计时总时长
    static constexpr float COUNTDOWN_DURATION = 3.0f;
};

} // namespace sakura::game
