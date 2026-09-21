#pragma once

#include "play_session.h"

#include <optional>

namespace sakura::game
{
struct Replay
{
    std::string chartId;
    std::string chartHash;
    int difficulty = 0;
    float rate = 1.0f;
    int startMs = 0;
    float pointerScaleX = 1, pointerScaleY = 1;
    std::vector<SessionInput> inputs;

    bool Save(const std::string& path) const;
    static std::optional<Replay> Load(const std::string& path);
    static std::string Hash(const ChartData& chart, int offsetMs = 0);
};
}
