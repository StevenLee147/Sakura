// game_state.cpp — 游戏状态管理实现

#include "game_state.h"
#include "chart_loader.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "utils/logger.h"

#include <algorithm>
#include <filesystem>

namespace sakura::game
{

// ── Start ──────────────────────────────────────────────────────────────────────

bool GameState::Start(const ChartInfo& chartInfo, int difficultyIndex)
{
    if (difficultyIndex < 0 ||
        difficultyIndex >= static_cast<int>(chartInfo.difficulties.size()))
    {
        LOG_ERROR("GameState::Start: 难度索引 {} 越界（共 {} 个难度）",
                  difficultyIndex, chartInfo.difficulties.size());
        return false;
    }

    m_chartInfo       = chartInfo;
    m_difficultyIndex = difficultyIndex;

    // 加载谱面数据
    const auto& diff      = chartInfo.difficulties[difficultyIndex];
    std::string dataPath  = chartInfo.folderPath + "/" + diff.chartFile;

    ChartLoader loader;
    auto chartData = loader.LoadChartData(dataPath);
    if (!chartData)
    {
        LOG_ERROR("GameState::Start: 无法加载谱面数据: {}", dataPath);
        return false;
    }
    m_chartData = std::move(*chartData);

    if (!loader.ValidateChartData(m_chartData))
    {
        LOG_WARN("GameState::Start: 谱面校验有警告，继续加载");
    }

    // 读取 Config 全局偏移
    m_globalOffset = sakura::core::Config::GetInstance()
        .Get<int>(std::string(sakura::core::ConfigKeys::kAudioOffset), 0);

    // 音乐文件路径
    std::string musicPath = chartInfo.folderPath + "/" + chartInfo.musicFile;

    // 检查音乐文件是否存在
    if (!std::filesystem::exists(musicPath))
    {
        LOG_WARN("GameState::Start: 音乐文件不存在: {}，游戏继续（无音乐）", musicPath);
        m_musicDuration = 30.0; // 默认30秒
    }
    else
    {
        m_musicDuration = 30.0; // 先用估算值，播放后从 audio 读取实际值
    }

    // 初始化活跃窗口
    m_kbActiveBegin = 0;
    m_kbActiveEnd   = 0;
    m_msActiveBegin = 0;
    m_msActiveEnd   = 0;

    // 重置时间
    m_currentTimeMs  = 0;
    m_musicStarted   = false;
    m_countdownTimer = COUNTDOWN_DURATION;
    m_phase          = GamePhase::Countdown;

    LOG_INFO("GameState 启动: {} - {} (Lv.{:.1f}), 键盘音符={}, 鼠标音符={}",
             chartInfo.title,
             diff.name,
             diff.level,
             m_chartData.keyboardNotes.size(),
             m_chartData.mouseNotes.size());

    return true;
}

// ── Update ─────────────────────────────────────────────────────────────────────

void GameState::Update(float dt)
{
    switch (m_phase)
    {
    case GamePhase::Countdown:
    {
        m_countdownTimer -= dt;
        if (m_countdownTimer <= 0.0f)
        {
            // 倒计时结束，开始播放音乐
            auto& audio = sakura::audio::AudioManager::GetInstance();
            const std::string musicPath = m_chartInfo.folderPath + "/" + m_chartInfo.musicFile;

            if (std::filesystem::exists(musicPath))
            {
                if (audio.PlayMusic(musicPath, 0))
                {
                    double dur = audio.GetMusicDuration();
                    if (dur > 0.0) m_musicDuration = dur;
                    m_musicStarted = true;
                    LOG_DEBUG("音乐开始播放: {}，时长={:.1f}s", musicPath, m_musicDuration);
                }
                else
                {
                    LOG_WARN("音乐播放失败，游戏以无音乐模式运行");
                    m_musicStarted = false;
                }
            }

            m_phase          = GamePhase::Playing;
            m_currentTimeMs  = 0;
        }
        break;
    }

    case GamePhase::Playing:
    {
        auto& audio = sakura::audio::AudioManager::GetInstance();

        if (m_musicStarted && audio.IsPlaying())
        {
            // 基于音乐播放位置同步（避免累加 dt 的误差）
            double musicPos = audio.GetMusicPosition();
            // 应用 chart offset + 全局 offset
            int offsetMs = m_chartInfo.offset + m_globalOffset;
            m_currentTimeMs = static_cast<int>(musicPos * 1000.0) - offsetMs;
        }
        else if (m_musicStarted && !audio.IsPlaying() && !audio.IsPaused())
        {
            // 音乐自然结束
            double musicPos = audio.GetMusicPosition();
            m_currentTimeMs = static_cast<int>(musicPos * 1000.0);
        }
        else if (!m_musicStarted)
        {
            // 无音乐模式：使用 dt 累加
            m_currentTimeMs += static_cast<int>(dt * 1000.0f);
        }

        // 更新活跃音符窗口
        UpdateActiveWindows();

        // 检查是否结束
        CheckFinished();
        break;
    }

    case GamePhase::Paused:
        // 暂停中不更新时间
        break;

    default:
        break;
    }
}

// ── Pause / Resume ────────────────────────────────────────────────────────────

void GameState::Pause()
{
    if (m_phase != GamePhase::Playing) return;

    m_phase = GamePhase::Paused;
    if (m_musicStarted)
    {
        sakura::audio::AudioManager::GetInstance().PauseMusic();
    }
    LOG_DEBUG("GameState: 游戏已暂停，当前时间={}ms", m_currentTimeMs);
}

void GameState::Resume()
{
    if (m_phase != GamePhase::Paused) return;

    m_phase = GamePhase::Playing;
    if (m_musicStarted)
    {
        sakura::audio::AudioManager::GetInstance().ResumeMusic();
    }
    LOG_DEBUG("GameState: 游戏已恢复，当前时间={}ms", m_currentTimeMs);
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void GameState::Reset()
{
    auto& audio = sakura::audio::AudioManager::GetInstance();
    audio.StopMusic();

    // 重置所有音符的判定状态
    for (auto& note : m_chartData.keyboardNotes)
    {
        note.isJudged  = false;
        note.result    = JudgeResult::None;
        note.renderY   = 0.0f;
        note.alpha     = 1.0f;
    }
    for (auto& note : m_chartData.mouseNotes)
    {
        note.isJudged      = false;
        note.result        = JudgeResult::None;
        note.approachScale = 2.0f;
        note.alpha         = 1.0f;
    }

    m_currentTimeMs  = 0;
    m_musicStarted   = false;
    m_countdownTimer = COUNTDOWN_DURATION;
    m_phase          = GamePhase::Countdown;
    m_kbActiveBegin  = 0;
    m_kbActiveEnd    = 0;
    m_msActiveBegin  = 0;
    m_msActiveEnd    = 0;
    m_forcedMissCount = 0;
}

// ── GetProgress ───────────────────────────────────────────────────────────────

float GameState::GetProgress() const
{
    if (m_musicDuration <= 0.0) return 0.0f;
    float progress = static_cast<float>(m_currentTimeMs) / static_cast<float>(m_musicDuration * 1000.0);
    return std::max(0.0f, std::min(1.0f, progress));
}

// ── GetCountdownNumber ────────────────────────────────────────────────────────

int GameState::GetCountdownNumber() const
{
    return std::max(1, static_cast<int>(std::ceil(m_countdownTimer)));
}

// ── GetActiveKeyboardNotes ────────────────────────────────────────────────────

std::span<KeyboardNote> GameState::GetActiveKeyboardNotes()
{
    if (m_kbActiveBegin >= m_kbActiveEnd || m_chartData.keyboardNotes.empty())
        return {};
    return std::span<KeyboardNote>(
        m_chartData.keyboardNotes.data() + m_kbActiveBegin,
        m_kbActiveEnd - m_kbActiveBegin
    );
}

std::span<const KeyboardNote> GameState::GetActiveKeyboardNotes() const
{
    if (m_kbActiveBegin >= m_kbActiveEnd || m_chartData.keyboardNotes.empty())
        return {};
    return std::span<const KeyboardNote>(
        m_chartData.keyboardNotes.data() + m_kbActiveBegin,
        m_kbActiveEnd - m_kbActiveBegin
    );
}

std::span<MouseNote> GameState::GetActiveMouseNotes()
{
    if (m_msActiveBegin >= m_msActiveEnd || m_chartData.mouseNotes.empty())
        return {};
    return std::span<MouseNote>(
        m_chartData.mouseNotes.data() + m_msActiveBegin,
        m_msActiveEnd - m_msActiveBegin
    );
}

std::span<const MouseNote> GameState::GetActiveMouseNotes() const
{
    if (m_msActiveBegin >= m_msActiveEnd || m_chartData.mouseNotes.empty())
        return {};
    return std::span<const MouseNote>(
        m_chartData.mouseNotes.data() + m_msActiveBegin,
        m_msActiveEnd - m_msActiveBegin
    );
}

// ── GetCurrentSVSpeed ─────────────────────────────────────────────────────────

float GameState::GetCurrentSVSpeed(int timeMs) const
{
    if (m_chartData.svPoints.empty()) return 1.0f;

    // 二分查找最后一个 time <= timeMs 的 SV 点
    auto it = std::upper_bound(
        m_chartData.svPoints.begin(),
        m_chartData.svPoints.end(),
        timeMs,
        [](int t, const SVPoint& sv) { return t < sv.time; }
    );

    if (it == m_chartData.svPoints.begin()) return 1.0f;
    --it;
    return it->speed;
}

// ── GetCurrentBPM ─────────────────────────────────────────────────────────────

float GameState::GetCurrentBPM(int timeMs) const
{
    if (m_chartData.timingPoints.empty()) return 120.0f;

    auto it = std::upper_bound(
        m_chartData.timingPoints.begin(),
        m_chartData.timingPoints.end(),
        timeMs,
        [](int t, const TimingPoint& tp) { return t < tp.time; }
    );

    if (it == m_chartData.timingPoints.begin())
        return m_chartData.timingPoints.front().bpm;
    --it;
    return it->bpm;
}

// ── GetTotalNoteCount ─────────────────────────────────────────────────────────

int GameState::GetTotalNoteCount() const
{
    return static_cast<int>(m_chartData.keyboardNotes.size()
                           + m_chartData.mouseNotes.size());
}

// ── UpdateActiveWindows ────────────────────────────────────────────────────────

void GameState::UpdateActiveWindows()
{
    int windowStart = m_currentTimeMs - ACTIVE_AFTER_MS;
    int windowEnd   = m_currentTimeMs + ACTIVE_BEFORE_MS;

    // ── 键盘音符 ──────────────────────────────────────────────────────────────
    {
        auto& notes = m_chartData.keyboardNotes;
        const size_t n = notes.size();

        // 移动起始索引：跳过已超出后边界（time + duration < windowStart）的音符
        while (m_kbActiveBegin < n)
        {
            const auto& note = notes[m_kbActiveBegin];
            int noteEnd = note.time + std::max(note.duration, 0);
            if (noteEnd < windowStart && note.isJudged)
                ++m_kbActiveBegin;
            else
                break;
        }

        // 移动结束索引：扩展到 time > windowEnd
        m_kbActiveEnd = m_kbActiveBegin;
        while (m_kbActiveEnd < n && notes[m_kbActiveEnd].time <= windowEnd)
            ++m_kbActiveEnd;
    }

    // ── 鼠标音符 ──────────────────────────────────────────────────────────────
    {
        auto& notes = m_chartData.mouseNotes;
        const size_t n = notes.size();

        while (m_msActiveBegin < n)
        {
            const auto& note = notes[m_msActiveBegin];
            int noteEnd = note.time + std::max(note.sliderDuration, 0);
            if (noteEnd < windowStart && note.isJudged)
                ++m_msActiveBegin;
            else
                break;
        }

        m_msActiveEnd = m_msActiveBegin;
        while (m_msActiveEnd < n && notes[m_msActiveEnd].time <= windowEnd)
            ++m_msActiveEnd;
    }
}

// ── CheckFinished ─────────────────────────────────────────────────────────────

void GameState::CheckFinished()
{
    // 音乐已结束（或无音乐模式）
    auto& audio = sakura::audio::AudioManager::GetInstance();
    bool musicEnded = !m_musicStarted
                   || (!audio.IsPlaying() && !audio.IsPaused());

    // 若音乐已结束，将所有仍未判定的音符强制判为 Miss
    // （防止末尾 miss 窗口内的音符阻塞游戏结束流程）
    if (musicEnded)
    {
        m_forcedMissCount = 0;
        for (auto& n : m_chartData.keyboardNotes)
        {
            if (!n.isJudged)
            {
                n.isJudged = true;
                n.result   = JudgeResult::Miss;
                ++m_forcedMissCount;
            }
        }
        for (auto& n : m_chartData.mouseNotes)
        {
            if (!n.isJudged)
            {
                n.isJudged = true;
                n.result   = JudgeResult::Miss;
                ++m_forcedMissCount;
            }
        }

        m_phase = GamePhase::Finished;
        LOG_INFO("游戏结束！");
        return;
    }

    // 音乐仍在播放，检查是否所有音符都已判定
    for (const auto& n : m_chartData.keyboardNotes)
    {
        if (!n.isJudged) return;
    }
    for (const auto& n : m_chartData.mouseNotes)
    {
        if (!n.isJudged) return;
    }

    // 所有音符已判定，等待音乐结束（通常是 AP 跑完）
    // 实际上这里 musicEnded 已经是 false，留空让下帧再判断
}

} // namespace sakura::game
