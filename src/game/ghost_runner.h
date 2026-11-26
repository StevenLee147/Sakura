#pragma once

// ghost_runner.h — 单人幽灵对手同屏确定性伴跑引擎 (v1.6.0)
// 纯 C++20 跨平台实现（零 SDL 依赖，纳入 sakura-game-logic）

#include "game/chart.h"
#include "game/play_session.h"
#include "game/replay.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sakura::game
{

struct GhostTrailPoint
{
    float x     = 0.5f;
    float y     = 0.5f;
    int   timeMs = 0;
};

struct GhostSnapshot
{
    bool        active      = false;
    std::string rivalLabel;
    int         ghostScore  = 0;
    int         deltaScore  = 0;    // myCurrentScore - ghostScore
    float       ghostAcc    = 0.0f;
    int         ghostCombo  = 0;
    float       cursorX     = 0.5f; // 鼠标区归一化坐标 0.0 ~ 1.0
    float       cursorY     = 0.5f;
};

class GhostRunner
{
public:
    GhostRunner() = default;

    bool Initialize(const ChartData& chart, const Replay& replay, std::string rivalLabel = "PB");
    bool InitializeFromFile(const ChartData& chart, const std::string& replayFilePath, std::string rivalLabel = "PB");
    void Reset();

    bool IsActive() const { return m_active; }
    const std::string& GetRivalLabel() const { return m_rivalLabel; }

    GhostSnapshot AdvanceTo(int nowMs, int myCurrentScore);
    const GhostSnapshot& GetSnapshot() const { return m_snapshot; }
    const std::vector<GhostTrailPoint>& GetRecentTrail() const { return m_trail; }

private:
    bool                         m_active      = false;
    std::string                  m_rivalLabel;
    ChartData                    m_chartCopy;
    Replay                       m_replay;
    PlaySession                  m_session;
    std::size_t                  m_inputCursor = 0;
    int                          m_lastTimeMs  = -100000;
    GhostSnapshot                m_snapshot;
    std::vector<GhostTrailPoint> m_trail;
};

} // namespace sakura::game
