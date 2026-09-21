// game_state.cpp — 游戏状态管理实现

#include "game_state.h"
#include "chart_loader.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "utils/logger.h"

#include <algorithm>
#include <filesystem>
#include <cmath>

namespace sakura::game
{

// ── Start ──────────────────────────────────────────────────────────────────────

bool GameState::Start(const ChartInfo& chartInfo, int difficultyIndex, PlayOptions options)
{
    m_error.clear();
    if (difficultyIndex < 0 || difficultyIndex >= static_cast<int>(chartInfo.difficulties.size()))
    {
        m_error = "谱面难度无效";
        return false;
    }
    ChartLoader loader;
    auto data = loader.LoadChartData(chartInfo.folderPath + "/" + chartInfo.difficulties[difficultyIndex].chartFile);
    if (!data || !loader.ValidateChartData(*data) || JudgmentCount(*data) == 0)
    {
        m_error = "谱面无法读取、包含无效音符或为空";
        return false;
    }
    m_chartInfo = chartInfo;
    m_chartData = std::move(*data);
    m_difficultyIndex = difficultyIndex;
    if(options.mode==PlayMode::Standard)options=PlayOptions{};
    m_options = options;
    m_options.rate = std::clamp(options.rate, 0.5f, 2.0f);
    m_chartEndMs = ChartEndTime(m_chartData);
    m_options.startMs = std::clamp(options.startMs, 0, m_chartEndMs);
    m_globalOffset = sakura::core::Config::GetInstance().Get<int>(sakura::core::ConfigKeys::kAudioOffset, 0);
    auto& audio = sakura::audio::AudioManager::GetInstance();
    audio.SetPlaybackSpeed(m_options.rate);
    const double startSeconds = m_options.startMs == 0 ? 0.0 : std::max(0.0, (m_options.startMs + m_chartInfo.offset + m_globalOffset) / 1000.0);
    if (!audio.PlayMusic(chartInfo.folderPath + "/" + chartInfo.musicFile, 0, startSeconds, true))
    {
        m_error = "音乐无法播放，请检查音频文件与输出设备";
        return false;
    }
    m_musicDuration = audio.GetMusicDuration();
    if (m_options.startMs > 0 && startSeconds >= m_musicDuration)
    {
        m_error = "练习起点超过音乐时长";
        audio.StopMusic();
        return false;
    }
    m_musicStarted = true;
    m_playbackStartMs = static_cast<int>(startSeconds*1000) - m_chartInfo.offset - m_globalOffset;
    m_currentTimeMs = m_playbackStartMs - static_cast<int>(COUNTDOWN_DURATION * 1000 * m_options.rate);
    m_countdownTimer = COUNTDOWN_DURATION;
    m_resumeCountdown = false;
    m_resumed = false;
    m_phase = GamePhase::Countdown;
    m_kbActiveBegin = m_kbActiveEnd = m_msActiveBegin = m_msActiveEnd = 0;
    m_forcedMissCount = 0;
    m_tailTimeMs = 0.0;
    UpdateActiveWindows();
    return true;
}

void GameState::Update(float dt)
{
    auto& audio = sakura::audio::AudioManager::GetInstance();
    if (m_phase == GamePhase::Countdown)
    {
        m_countdownTimer -= dt;
        if (!m_resumeCountdown)
            m_currentTimeMs = m_playbackStartMs - static_cast<int>(std::max(0.0f, m_countdownTimer) * 1000 * m_options.rate);
        if (m_countdownTimer <= 0.0f)
        {
            audio.ResumeMusic();
            m_phase = GamePhase::Playing;
            m_resumed = m_resumeCountdown;
            m_resumeCountdown = false;
            m_currentTimeMs = static_cast<int>(audio.GetMusicPosition() * 1000.0) - m_chartInfo.offset - m_globalOffset;
        }
        UpdateActiveWindows();
    }
    else if (m_phase == GamePhase::Playing)
    {
        if (audio.IsPlaying())
        {
            m_currentTimeMs = static_cast<int>(audio.GetMusicPosition() * 1000.0) - m_chartInfo.offset - m_globalOffset;
            m_tailTimeMs = m_currentTimeMs;
        }
        else
        {
            // Let late windows and sustain tails resolve after the last audio sample.
            // Keep fractional milliseconds: truncating dt on every frame drifts at high FPS.
            m_tailTimeMs += static_cast<double>(dt) * 1000.0 * m_options.rate;
            m_currentTimeMs = static_cast<int>(m_tailTimeMs);
        }
        UpdateActiveWindows();
        CheckFinished();
    }
}

void GameState::Pause()
{
    if (m_phase != GamePhase::Playing && m_phase != GamePhase::Countdown) return;
    m_pausedInitialCountdown = m_phase == GamePhase::Countdown && !m_resumeCountdown;
    m_phase = GamePhase::Paused;
    sakura::audio::AudioManager::GetInstance().PauseMusic();
}

void GameState::Resume()
{
    if (m_phase != GamePhase::Paused) return;
    m_resumeCountdown = !m_pausedInitialCountdown;
    m_countdownTimer = COUNTDOWN_DURATION;
    m_phase = GamePhase::Countdown;
}

void GameState::Reset()
{
    Start(m_chartInfo, m_difficultyIndex, m_options);
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
    return JudgmentCount(m_chartData);
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
    auto& audio = sakura::audio::AudioManager::GetInstance();
    const bool segmentEnded = m_options.mode == PlayMode::Practice && m_options.endMs > m_options.startMs
        && m_currentTimeMs >= m_options.endMs;
    if (segmentEnded || (!audio.IsPlaying() && !audio.IsPaused() && m_currentTimeMs > m_chartEndMs + 200))
    {
        audio.StopMusic();
        m_phase = GamePhase::Finished;
    }
}

} // namespace sakura::game
