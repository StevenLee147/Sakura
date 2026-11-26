// ghost_runner.cpp — 单人幽灵对手同屏确定性伴跑引擎实现 (v1.6.0)
// 纯 C++20 跨平台实现（零 SDL / Windows 依赖）

#include "game/ghost_runner.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sakura::game
{

namespace
{

constexpr int   kInitialLastTimeMs   = -100000;
constexpr int   kTrailRetentionMs    = 220;
constexpr int   kTrailSampleStepMs   = 16;
constexpr float kTrailMoveEpsilon    = 1e-4f;

float ClampNormalized(float v)
{
    if (!std::isfinite(v))
        return 0.5f;
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace

bool GhostRunner::Initialize(const ChartData& chart, const Replay& replay, std::string rivalLabel)
{
    Reset();

    m_chartCopy  = chart;
    m_replay     = replay;
    m_rivalLabel = rivalLabel.empty() ? "PB" : std::move(rivalLabel);

    m_session = PlaySession{};
    m_session.Initialize(m_chartCopy, false, m_replay.startMs, 0);
    m_session.SetPointerScale(m_replay.pointerScaleX, m_replay.pointerScaleY);

    m_active      = true;
    m_inputCursor = 0;
    m_lastTimeMs  = kInitialLastTimeMs;
    m_snapshot    = { true, m_rivalLabel, 0, 0, 100.0f, 0, 0.5f, 0.5f };
    return true;
}

bool GhostRunner::InitializeFromFile(const ChartData& chart, const std::string& replayFilePath, std::string rivalLabel)
{
    if (replayFilePath.empty())
    {
        Reset();
        return false;
    }

    auto loaded = Replay::Load(replayFilePath);
    if (!loaded)
    {
        Reset();
        return false;
    }

    return Initialize(chart, *loaded, std::move(rivalLabel));
}

void GhostRunner::Reset()
{
    m_active      = false;
    m_rivalLabel.clear();
    m_chartCopy   = ChartData{};
    m_replay      = Replay{};
    m_session     = PlaySession{};
    m_inputCursor = 0;
    m_lastTimeMs  = kInitialLastTimeMs;
    m_snapshot    = {};
    m_trail.clear();
}

GhostSnapshot GhostRunner::AdvanceTo(int nowMs, int myCurrentScore)
{
    if (!m_active)
    {
        m_snapshot = {};
        return m_snapshot;
    }

    if (nowMs < m_lastTimeMs)
    {
        for (auto& note : m_chartCopy.keyboardNotes)
            note.renderY = 0.0f;
        for (auto& note : m_chartCopy.mouseNotes)
            note.approachScale = 2.0f;

        m_session = PlaySession{};
        m_session.Initialize(m_chartCopy, false, m_replay.startMs, 0);
        m_session.SetPointerScale(m_replay.pointerScaleX, m_replay.pointerScaleY);
        m_inputCursor = 0;
        m_trail.clear();
    }

    while (m_inputCursor < m_replay.inputs.size() && m_replay.inputs[m_inputCursor].time <= nowMs)
    {
        const SessionInput& input = m_replay.inputs[m_inputCursor++];
        m_session.Submit(input);

        if (input.kind == InputKind::Pointer ||
            input.kind == InputKind::MouseDown ||
            input.kind == InputKind::RightMouseDown)
        {
            m_trail.push_back(GhostTrailPoint{
                ClampNormalized(input.x),
                ClampNormalized(input.y),
                input.time
            });
        }
    }

    m_session.Submit(SessionInput{ nowMs, InputKind::Tick });

    const float curX = ClampNormalized(m_session.MouseX());
    const float curY = ClampNormalized(m_session.MouseY());

    if (m_trail.empty() ||
        std::abs(curX - m_trail.back().x) > kTrailMoveEpsilon ||
        std::abs(curY - m_trail.back().y) > kTrailMoveEpsilon ||
        (nowMs - m_trail.back().timeMs) > kTrailSampleStepMs)
    {
        m_trail.push_back(GhostTrailPoint{ curX, curY, nowMs });
    }

    const int trailCutoffMs = nowMs - kTrailRetentionMs;
    std::erase_if(m_trail, [trailCutoffMs](const GhostTrailPoint& pt) {
        return pt.timeMs < trailCutoffMs;
    });

    m_lastTimeMs = nowMs;

    const int ghostScore  = m_session.Score().GetScore();
    m_snapshot.active     = true;
    m_snapshot.rivalLabel = m_rivalLabel;
    m_snapshot.ghostScore = ghostScore;
    m_snapshot.deltaScore = myCurrentScore - ghostScore;
    m_snapshot.ghostAcc   = m_session.Score().GetAccuracy();
    m_snapshot.ghostCombo = m_session.Score().GetCombo();
    m_snapshot.cursorX    = curX;
    m_snapshot.cursorY    = curY;
    return m_snapshot;
}

} // namespace sakura::game
