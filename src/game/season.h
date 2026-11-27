#pragma once

// season.h — 樱华赛季排位系统跨平台确定性逻辑引擎 (v3.0.0)
// 零 SDL / Win32 依赖，同时编译进 sakura-game-logic 供客户端与 Linux CI 共用。

#include "game/chart.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sakura::game
{

struct SeasonStageSpec
{
    int         slotIndex       = 0;          // 0=vanguard, 1=mid, 2=boss
    std::string slotRole;                     // "vanguard" | "mid" | "boss"
    std::string roleTitle;                    // "先锋课题" | "中坚课题" | "大将课题"
    std::string chartId;
    std::string chartTitle;
    std::string difficulty;
    float       difficultyLevel = 1.0f;
    int         maxStageSP      = 1000;
};

struct SeasonSpec
{
    std::string                    seasonId;   // "YYYY-MM" (UTC+8)
    std::string                    themeTitle; // 当季主题名
    std::array<SeasonStageSpec, 3> stages;
};

struct SeasonTierInfo
{
    int          tierIndex         = 1;        // 1..5
    std::string  tierName          = "青铜樱";
    std::string  nextTierName      = "白银月";
    int          minSP             = 0;
    int          nextTierThreshold = 1200;
    int          spToNextTier      = 1200;
    std::uint8_t colorR            = 214;
    std::uint8_t colorG            = 154;
    std::uint8_t colorB            = 126;
};

struct SeasonStanding
{
    std::string           seasonId;
    std::array<int, 3>    stageScores{ 0, 0, 0 };
    std::array<float, 3>  stageAccuracies{ 0.0f, 0.0f, 0.0f };
    std::array<int, 3>    stageSP{ 0, 0, 0 };
    int                   totalSP = 0;
    SeasonTierInfo        tier;
};

class SeasonEngine
{
public:
    static constexpr int kStageCount = 3;
    static constexpr int kMaxTotalSP = 3500; // 1000 + 1150 + 1350

    static std::string CurrentSeasonIdUtc8();
    static bool IsValidSeasonId(std::string_view seasonId);

    static SeasonSpec ComputeSeasonSpec(std::string_view seasonIdUtc8 = {});
    static int MaxStageSPForSlot(int slotIndex);

    static int MatchSeasonSlot(const SeasonSpec& spec, std::string_view chartId, std::string_view difficulty);

    static int CalculateStageSP(const GameResult& result,
                                float chartLevel,
                                int slotIndex,
                                float playbackRate = 1.0f);

    static SeasonTierInfo EvaluateTier(int totalSP);

    static SeasonStanding ComputeLocalStanding(const SeasonSpec& spec,
                                               const std::vector<GameResult>& bestScores);
};

} // namespace sakura::game
