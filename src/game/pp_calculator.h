#pragma once

// pp_calculator.h — Performance Point 计算器

#include "chart.h"

#include <vector>

namespace sakura::game
{

struct PPPlay
{
    GameResult result;
    double     pp = 0.0;
};

class PPCalculator
{
public:
    static double CalculatePP(const GameResult& result, float level);

    void RecalculateTotal(const std::vector<GameResult>& bestScores);
    double GetTotalPP() const { return m_totalPP; }
    std::vector<PPPlay> GetBestPlays(int limit = 20) const;

private:
    std::vector<PPPlay> m_bestPlays;
    double              m_totalPP = 0.0;
};

} // namespace sakura::game