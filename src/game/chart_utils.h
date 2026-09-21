#pragma once

#include "chart.h"

namespace sakura::game
{

// A hold has a head and a tail; a slider has a head and one judgment per waypoint.
int JudgmentCount(const ChartData& chart);
int ChartEndTime(const ChartData& chart);
// Integrate SV over the interval so notes remain continuous at speed changes.
float ScrollDistance(const ChartData& chart, int fromMs, int toMs);

} // namespace sakura::game
