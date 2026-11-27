#include "game/select_context.h"
#include "game/chart_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>

namespace sakura::game
{

namespace
{
std::mutex                       g_sessionMutex;
std::optional<SelectMemoryState> g_sessionState;

bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

int ResolveDiffForChart(const ChartInfo& chart, int desiredIndex, std::string_view desiredName)
{
    if (chart.difficulties.empty())
        return 0;

    const int count = static_cast<int>(chart.difficulties.size());
    if (desiredIndex >= 0 && desiredIndex < count)
    {
        if (desiredName.empty() || EqualsIgnoreCaseAscii(chart.difficulties[desiredIndex].name, desiredName))
            return desiredIndex;
    }
    if (!desiredName.empty())
    {
        for (int i = 0; i < count; ++i)
        {
            if (EqualsIgnoreCaseAscii(chart.difficulties[i].name, desiredName))
                return i;
        }
    }
    return std::clamp(desiredIndex, 0, count - 1);
}
} // namespace

SelectMemoryState SelectContextManager::Load(const sakura::core::Config& cfg)
{
    SelectMemoryState state;
    std::optional<SelectMemoryState> cached;
    {
        std::lock_guard lock(g_sessionMutex);
        cached = g_sessionState;
    }

    const bool hasCfgChart = cfg.Has("library.last_chart");
    state.chartId = cfg.Get<std::string>("library.last_chart", cached ? cached->chartId : std::string{});
    if (!hasCfgChart && state.chartId.empty() && cached)
        state.chartId = cached->chartId;

    const bool sameAsCached = cached.has_value() && !state.chartId.empty() && cached->chartId == state.chartId;
    state.difficultyIndex  = cfg.Get<int>("library.last_difficulty", sameAsCached ? cached->difficultyIndex : 0);
    state.difficultyName   = cfg.Get<std::string>("library.last_difficulty_name", sameAsCached ? cached->difficultyName : std::string{});
    state.collectionFilter = std::max(0, cfg.Get<int>("library.last_collection_filter", cached ? cached->collectionFilter : 0));
    state.sortMode         = std::clamp(cfg.Get<int>("library.last_sort", cached ? cached->sortMode : 0), 0, 2);
    state.scrollPos        = std::max(0.0f, cfg.Get<float>("library.last_scroll_norm", cached ? cached->scrollPos : 0.0f));
    state.minLevel         = std::clamp(cfg.Get<float>("library.filter_min_level", cached ? cached->minLevel : 1.0f), 1.0f, 15.0f);
    state.maxLevel         = std::clamp(cfg.Get<float>("library.filter_max_level", cached ? cached->maxLevel : 15.0f), state.minLevel, 15.0f);

    const int rawLamp = std::clamp(
        cfg.Get<int>("library.filter_lamp", cached ? static_cast<int>(cached->lampFilter) : 0),
        0, 4);
    state.lampFilter = static_cast<SelectLampFilter>(rawLamp);
    return state;
}

void SelectContextManager::Save(sakura::core::Config& cfg, const SelectMemoryState& state, bool flushToDisk)
{
    SetSessionState(state);
    if (!state.chartId.empty())
        cfg.Set("library.last_chart", state.chartId);
    cfg.Set("library.last_difficulty", std::max(0, state.difficultyIndex));
    cfg.Set("library.last_difficulty_name", state.difficultyName);
    cfg.Set("library.last_collection_filter", std::max(0, state.collectionFilter));
    cfg.Set("library.last_sort", std::clamp(state.sortMode, 0, 2));
    cfg.Set("library.last_scroll_norm", std::max(0.0f, state.scrollPos));
    cfg.Set("library.filter_min_level", std::clamp(state.minLevel, 1.0f, 15.0f));
    cfg.Set("library.filter_max_level", std::clamp(state.maxLevel, std::clamp(state.minLevel, 1.0f, 15.0f), 15.0f));
    cfg.Set("library.filter_lamp", static_cast<int>(state.lampFilter));
    if (flushToDisk)
        cfg.Save();
}

void SelectContextManager::SetSessionState(const SelectMemoryState& state)
{
    std::lock_guard lock(g_sessionMutex);
    g_sessionState = state;
}

std::optional<SelectMemoryState> SelectContextManager::GetSessionState()
{
    std::lock_guard lock(g_sessionMutex);
    return g_sessionState;
}

void SelectContextManager::ClearSessionState()
{
    std::lock_guard lock(g_sessionMutex);
    g_sessionState.reset();
}

RestoredSelectTarget SelectContextManager::ResolveRestoredSelection(const std::vector<ChartInfo>& allCharts,
                                                                    const std::vector<int>& visibleIndices,
                                                                    const SelectMemoryState& desired)
{
    RestoredSelectTarget out;
    if (allCharts.empty())
        return out;

    if (!desired.chartId.empty())
    {
        for (int idx : visibleIndices)
        {
            if (idx >= 0 && idx < static_cast<int>(allCharts.size()) &&
                allCharts[static_cast<std::size_t>(idx)].id == desired.chartId)
            {
                out.chartIndex      = idx;
                out.difficultyIndex = ResolveDiffForChart(
                    allCharts[static_cast<std::size_t>(idx)], desired.difficultyIndex, desired.difficultyName);
                return out;
            }
        }

        for (int idx = 0; idx < static_cast<int>(allCharts.size()); ++idx)
        {
            if (allCharts[static_cast<std::size_t>(idx)].id == desired.chartId)
            {
                out.chartIndex              = idx;
                out.difficultyIndex         = ResolveDiffForChart(
                    allCharts[static_cast<std::size_t>(idx)], desired.difficultyIndex, desired.difficultyName);
                out.clearedObscuringFilters = true;
                return out;
            }
        }
    }

    if (!visibleIndices.empty())
    {
        const int front = visibleIndices.front();
        if (front >= 0 && front < static_cast<int>(allCharts.size()))
        {
            out.chartIndex      = front;
            out.difficultyIndex = 0;
            return out;
        }
    }

    out.chartIndex      = 0;
    out.difficultyIndex = 0;
    out.clearedObscuringFilters = visibleIndices.empty();
    return out;
}

SelectLamp ComputeChartLamp(const std::optional<GameResult>& best)
{
    if (!best.has_value() || best->score <= 0)
        return SelectLamp::Unplayed;
    if (best->isAllPerfect)
        return SelectLamp::AllPerfect;
    if (best->isFullCombo)
        return SelectLamp::FullCombo;
    if (best->grade != Grade::D || best->accuracy >= 60.0f)
        return SelectLamp::Clear;
    return SelectLamp::Unplayed;
}

bool MatchesLampFilter(SelectLamp lamp, SelectLampFilter filter)
{
    switch (filter)
    {
        case SelectLampFilter::All:              return true;
        case SelectLampFilter::Unplayed:         return lamp == SelectLamp::Unplayed;
        case SelectLampFilter::ClearOrAbove:     return static_cast<int>(lamp) >= static_cast<int>(SelectLamp::Clear);
        case SelectLampFilter::FullComboOrAbove: return static_cast<int>(lamp) >= static_cast<int>(SelectLamp::FullCombo);
        case SelectLampFilter::AllPerfectOnly:   return lamp == SelectLamp::AllPerfect;
        default:                                 return true;
    }
}

bool ChartMatchesLevelRange(const ChartInfo& chart, int preferredDiffIdx, float minLevel, float maxLevel)
{
    const float lo = std::min(minLevel, maxLevel) - 0.001f;
    const float hi = std::max(minLevel, maxLevel) + 0.001f;
    if (lo <= 1.001f && hi >= 14.999f)
        return true;
    if (chart.difficulties.empty())
        return true;

    if (preferredDiffIdx >= 0 && preferredDiffIdx < static_cast<int>(chart.difficulties.size()))
    {
        const float lv = chart.difficulties[static_cast<std::size_t>(preferredDiffIdx)].level;
        if (lv >= lo && lv <= hi)
            return true;
    }
    for (const auto& d : chart.difficulties)
    {
        if (d.level >= lo && d.level <= hi)
            return true;
    }
    return false;
}

int PickRandomVisibleChart(const std::vector<int>& visibleIndices, int currentChartIdx, std::uint64_t seed)
{
    if (visibleIndices.empty())
        return -1;
    if (visibleIndices.size() == 1)
        return visibleIndices.front();

    // SplitMix64 mixing step
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31U);

    std::size_t pick = static_cast<std::size_t>(z % visibleIndices.size());
    if (visibleIndices[pick] == currentChartIdx)
    {
        pick = (pick + 1 + static_cast<std::size_t>((z >> 16U) % (visibleIndices.size() - 1))) % visibleIndices.size();
    }
    return visibleIndices[pick];
}

std::size_t CountUtf8Codepoints(std::string_view utf8)
{
    std::size_t count = 0;
    for (char ch : utf8)
    {
        const unsigned char b = static_cast<unsigned char>(ch);
        if ((b & 0xC0u) != 0x80u)
            ++count;
    }
    return count;
}

std::string BuildPasswordMaskUtf8(std::string_view utf8, std::string_view bullet)
{
    const std::size_t n = CountUtf8Codepoints(utf8);
    if (n == 0 || bullet.empty())
        return {};
    std::string out;
    out.reserve(n * bullet.size());
    for (std::size_t i = 0; i < n; ++i)
        out.append(bullet);
    return out;
}

} // namespace sakura::game
