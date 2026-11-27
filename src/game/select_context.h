#pragma once

// select_context.h — 选歌界面上下文持久化、通关灯筛选与 UTF-8 遮罩工具（零 SDL 依赖）

#include "core/config.h"
#include "game/chart.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sakura::game
{

enum class SelectLamp : int
{
    Unplayed   = 0,
    Clear      = 1,
    FullCombo  = 2,
    AllPerfect = 3,
};

enum class SelectLampFilter : int
{
    All              = 0,
    Unplayed         = 1,
    ClearOrAbove     = 2,
    FullComboOrAbove = 3,
    AllPerfectOnly   = 4,
};

struct SelectMemoryState
{
    std::string      chartId;
    int              difficultyIndex  = 0;
    std::string      difficultyName;
    int              collectionFilter = 0;
    int              sortMode         = 0;
    float            scrollPos        = 0.0f;
    float            minLevel         = 1.0f;
    float            maxLevel         = 15.0f;
    SelectLampFilter lampFilter       = SelectLampFilter::All;
};

struct RestoredSelectTarget
{
    int  chartIndex              = -1;
    int  difficultyIndex         = 0;
    bool clearedObscuringFilters = false;
};

class SelectContextManager
{
public:
    static SelectMemoryState Load(const sakura::core::Config& cfg);
    static void Save(sakura::core::Config& cfg, const SelectMemoryState& state, bool flushToDisk = false);

    static void SetSessionState(const SelectMemoryState& state);
    static std::optional<SelectMemoryState> GetSessionState();
    static void ClearSessionState();

    static RestoredSelectTarget ResolveRestoredSelection(const std::vector<ChartInfo>& allCharts,
                                                         const std::vector<int>& visibleIndices,
                                                         const SelectMemoryState& desired);
};

SelectLamp ComputeChartLamp(const std::optional<GameResult>& best);
bool MatchesLampFilter(SelectLamp lamp, SelectLampFilter filter);
bool ChartMatchesLevelRange(const ChartInfo& chart, int preferredDiffIdx, float minLevel, float maxLevel);
int PickRandomVisibleChart(const std::vector<int>& visibleIndices, int currentChartIdx, std::uint64_t seed);

std::size_t CountUtf8Codepoints(std::string_view utf8);
std::string BuildPasswordMaskUtf8(std::string_view utf8, std::string_view bullet = "\xE2\x80\xA2");

} // namespace sakura::game
