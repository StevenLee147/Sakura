#pragma once

#include "chart_utils.h"
#include "judge.h"
#include "score.h"

#include <array>
#include <functional>

namespace sakura::game
{

enum class PlayMode { Standard, Practice, AutoPlay, Replay };

struct PlayOptions
{
    PlayMode mode = PlayMode::Standard;
    float rate = 1.0f;
    int startMs = 0;
    int endMs = 0; // 0 = entire song
    bool loop = false;
    bool returnToEditor = false;
    std::string replayFile;
};

enum class InputKind { Tick, LaneDown, LaneUp, Pointer, MouseDown, MouseUp, Resume, PointerScale };

struct SessionInput
{
    int time = 0;
    InputKind kind = InputKind::Tick;
    int lane = 0;
    float x = 0.5f;
    float y = 0.5f;
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
    std::array<bool, 4> Lanes() const { return m_lanes; }
    float MouseX() const { return m_x; }
    float MouseY() const { return m_y; }

private:
    void Advance(int now);
    void PressLane(int lane, int now);
    void PressMouse(int now);
    void Emit(JudgmentEvent event);
    void MissKeyboard(KeyboardNote& note);
    void MissMouse(MouseNote& note);

    ChartData* m_chart = nullptr;
    Judge m_judge;
    ScoreCalculator m_score;
    std::vector<HoldState> m_holds;
    std::vector<SliderState> m_sliders;
    std::vector<SessionInput> m_recording;
    std::function<void(const JudgmentEvent&)> m_onJudge;
    std::array<bool, 4> m_lanes{};
    float m_x = 0.5f;
    float m_y = 0.5f;
    float m_scaleX = 1, m_scaleY = 1;
    bool m_mouseDown = false;
    bool m_autoPlay = false;
    int m_time = -10000;
    int m_total = 0;
    size_t m_keyboardCursor = 0;
    size_t m_mouseCursor = 0;
};

} // namespace sakura::game
