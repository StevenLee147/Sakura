#pragma once

#include "chart_utils.h"
#include "judge.h"
#include "score.h"

#include <array>
#include <functional>

namespace sakura::game
{

enum class PlayMode { Standard, Practice, AutoPlay, Replay };
enum class GhostMode { Off = 0, PersonalBest = 1, OnlineRival = 2 };

struct PlayOptions
{
    PlayMode mode = PlayMode::Standard;
    float rate = 1.0f;
    int startMs = 0;
    int endMs = 0; // 0 = entire song
    bool loop = false;
    bool returnToEditor = false;
    bool isCourseMode = false;
    bool isDailyChallenge = false;
    std::string dailyDate;
    GhostMode ghostMode = GhostMode::Off;
    std::string ghostReplayFile;
    std::string ghostRivalName;
    bool isArenaMatch = false;
    std::string arenaRoomCode;
    bool isSeasonMatch = false;
    std::string seasonId;
    int seasonSlotIndex = -1;
    std::string replayFile;
};

enum class InputKind { Tick, LaneDown, LaneUp, Pointer, MouseDown, MouseUp, Resume, PointerScale, RightMouseDown, RightMouseUp };

struct SessionInput
{
    int time = 0;
    InputKind kind = InputKind::Tick;
    int lane = 0;
    float x = 0.5f;
    float y = 0.5f;
};

enum class HitEventKind
{
    Tap,
    HoldHead,
    HoldTail,
    ThumbTap,
    Circle,
    SliderHead,
    SliderTick,
    LinkChord,
    SynergyBurst
};

struct JudgmentEvent
{
    JudgeResult result = JudgeResult::None;
    bool keyboard = true;
    int lane = 0;
    float x = 0.0f;
    float y = 0.0f;
    int error = 0;
    bool timed = false;
    bool synergy = false;
    HitEventKind noteKind = HitEventKind::Tap;
};

// The same deterministic engine drives human play, demonstration, replay and tests.
// It owns no audio, window, filesystem, or rendering resources.
class PlaySession
{
public:
    void Initialize(ChartData& chart, bool autoPlay = false, int startMs = 0, int endMs = 0);
    void SetPointerScale(float x, float y) { m_scaleX=x; m_scaleY=y; m_judge.SetPointerScale(x,y); }
    void Submit(SessionInput input);
    void Finish();
    void SetOnJudge(std::function<void(const JudgmentEvent&)> callback) { m_onJudge = std::move(callback); }
    const ScoreCalculator& Score() const { return m_score; }
    const std::vector<HoldState>& Holds() const { return m_holds; }
    const std::vector<SliderState>& Sliders() const { return m_sliders; }
    const std::vector<SessionInput>& Recording() const { return m_recording; }
    bool Complete() const { return m_score.GetJudgedCount() == m_total; }
    int Total() const { return m_total; }
    std::array<bool, kTotalKeyboardLanes> Lanes() const { return m_lanes; }
    bool RightMouseDown() const { return m_rightMouseDown; }
    float MouseX() const { return m_x; }
    float MouseY() const { return m_y; }

private:
    struct LinkHitRecord
    {
        int linkId = 0;
        int timeMs = -100000;
        bool fromKeyboard = true;
    };

    void Advance(int now);
    void PressLane(int lane, int now);
    void PressMouse(int now, bool rightButton = false);
    bool IsModifierSatisfied(int requireLane, int now) const;
    bool TryConsumeLinkSynergy(int linkId, int now, bool fromKeyboard);
    void Emit(JudgmentEvent event);
    void MissKeyboard(KeyboardNote& note);
    void MissMouse(MouseNote& note);

    ChartData* m_chart = nullptr;
    Judge m_judge;
    ScoreCalculator m_score;
    std::vector<HoldState> m_holds;
    std::vector<SliderState> m_sliders;
    std::vector<SessionInput> m_recording;
    std::vector<LinkHitRecord> m_recentLinks;
    std::function<void(const JudgmentEvent&)> m_onJudge;
    std::array<bool, kTotalKeyboardLanes> m_lanes{};
    std::array<int, 6> m_modifierDownMs{-100000, -100000, -100000, -100000, -100000, -100000};
    float m_x = 0.5f;
    float m_y = 0.5f;
    float m_scaleX = 1, m_scaleY = 1;
    bool m_mouseDown = false;
    bool m_rightMouseDown = false;
    bool m_autoPlay = false;
    int m_time = -10000;
    int m_lastAdvanceMs = -100000;
    int m_total = 0;
    size_t m_keyboardCursor = 0;
    size_t m_mouseCursor = 0;
};

} // namespace sakura::game
