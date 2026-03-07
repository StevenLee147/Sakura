// pp_calculator.cpp — Performance Point 计算器实现

#include "pp_calculator.h"

#include <algorithm>
#include <cmath>

namespace sakura::game
{

double PPCalculator::CalculatePP(const GameResult& result, float level)
{
    const double safeLevel = std::max(0.0, static_cast<double>(level));
    const double accuracy = std::clamp(static_cast<double>(result.accuracy) / 100.0, 0.0, 1.0);
    const double normalizedScore = std::max(0.0, static_cast<double>(result.score) / 1000000.0);

    double pp = std::pow(safeLevel, 2.5) * 10.0
        * std::pow(accuracy, 4.0)
        * std::pow(normalizedScore, 2.0);

    if (result.isFullCombo)
        pp *= 1.05;
    if (result.isAllPerfect)
        pp *= 1.10;
    if (result.accuracy >= 99.0f)
        pp *= 1.02;

    return pp;
}

void PPCalculator::RecalculateTotal(const std::vector<GameResult>& bestScores)
{
    m_bestPlays.clear();
    m_bestPlays.reserve(bestScores.size());

    for (const auto& score : bestScores)
        m_bestPlays.push_back({ score, CalculatePP(score, score.difficultyLevel) });

    std::sort(m_bestPlays.begin(), m_bestPlays.end(),
        [](const PPPlay& lhs, const PPPlay& rhs)
        {
            if (lhs.pp != rhs.pp)
                return lhs.pp > rhs.pp;
            return lhs.result.score > rhs.result.score;
        });

    m_totalPP = 0.0;
    for (std::size_t index = 0; index < m_bestPlays.size(); ++index)
        m_totalPP += m_bestPlays[index].pp * std::pow(0.95, static_cast<double>(index));
}

std::vector<PPPlay> PPCalculator::GetBestPlays(int limit) const
{
    if (limit <= 0 || m_bestPlays.empty())
        return {};

    const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(limit), m_bestPlays.size());
    return std::vector<PPPlay>(m_bestPlays.begin(), m_bestPlays.begin() + count);
}

} // namespace sakura::game