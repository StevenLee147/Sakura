#include "chart_utils.h"

#include <algorithm>

namespace sakura::game
{

int JudgmentCount(const ChartData& chart)
{
    int count = 0;
    for (const auto& note : chart.keyboardNotes)
        count += note.type == NoteType::Hold ? 2 : 1;
    for (const auto& note : chart.mouseNotes)
        count += 1 + (note.type == NoteType::Slider ? static_cast<int>(note.sliderPath.size()) : 0);
    return count;
}

int ChartEndTime(const ChartData& chart)
{
    int end = 0;
    for (const auto& note : chart.keyboardNotes)
        end = std::max(end, note.time + std::max(0, note.duration));
    for (const auto& note : chart.mouseNotes)
        end = std::max(end, note.time + std::max(0, note.sliderDuration));
    return end;
}

float ScrollDistance(const ChartData& chart, int fromMs, int toMs)
{
    if (fromMs > toMs) return -ScrollDistance(chart, toMs, fromMs);
    float speed = 1.0f;
    float distance = 0.0f;
    int cursor = fromMs;
    auto it = std::upper_bound(chart.svPoints.begin(), chart.svPoints.end(), fromMs,
        [](int time, const SVPoint& point) { return time < point.time; });
    if (it != chart.svPoints.begin()) speed = std::prev(it)->speed;
    for (; it != chart.svPoints.end() && it->time < toMs; ++it)
    {
        distance += static_cast<float>(it->time - cursor) * speed;
        cursor = it->time;
        speed = it->speed;
    }
    return distance + static_cast<float>(toMs - cursor) * speed;
}

} // namespace sakura::game
