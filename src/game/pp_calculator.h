#pragma once

// pp_calculator.h — Performance Point 计算器

#include "chart.h"

#include <string>
#include <vector>

namespace sakura::game
{

struct PPPlay
{
    GameResult result;
    double     pp = 0.0;
};

struct RatingEntry
{
    GameResult result;
    double     rating = 0.0;
};

struct RatingSummary
{
    double                   sakuraRating = 0.0; // 0.75 * B30 + 0.25 * R10 (anti-degradation protected)
    double                   b30Avg       = 0.0;
    double                   r10Avg       = 0.0;
    std::string              tierName     = "青樱 · Bud";
    std::vector<RatingEntry> best30;
    std::vector<RatingEntry> recent10;
};

class PPCalculator
{
public:
    static double CalculatePP(const GameResult& result, float level);
    static double CalculateChartRating(const GameResult& result, float level);
    static const char* GetRatingTierName(double sakuraRating);
    static std::string FormatRating(double rating);
    static RatingSummary CalculateSakuraRating(const std::vector<GameResult>& allBestScores,
                                               const std::vector<GameResult>& recentScores = {});

    void RecalculateTotal(const std::vector<GameResult>& bestScores);
    double GetTotalPP() const { return m_totalPP; }
    std::vector<PPPlay> GetBestPlays(int limit = 20) const;

private:
    std::vector<PPPlay> m_bestPlays;
    double              m_totalPP = 0.0;
};

} // namespace sakura::game