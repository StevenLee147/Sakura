// pp_calculator.cpp — Performance Point 计算器实现

#include "pp_calculator.h"

#include <algorithm>
#include <cmath>

namespace sakura::game
{

namespace
{
// 该 PP 公式是 Sakura 当前版本的项目内调权模型：
// 难度提供基础量级，准确率权重大于分数权重，FC/AP/99% 为额外乘区奖励。
constexpr double kDifficultyExponent = 2.5;
constexpr double kBaseScale = 10.0;
constexpr double kAccuracyExponent = 4.0;
constexpr double kScoreExponent = 2.0;
constexpr double kFullComboBonus = 1.05;
constexpr double kAllPerfectBonus = 1.10;
constexpr double kAccuracyThreshold = 0.99;
constexpr double kHighAccuracyBonus = 1.02;
}

double PPCalculator::CalculatePP(const GameResult& result, float level)
{
    const double safeLevel = std::max(0.0, static_cast<double>(level));
    const double accuracy = std::clamp(static_cast<double>(result.accuracy) / 100.0, 0.0, 1.0);
    const double normalizedScore = std::max(0.0, static_cast<double>(result.score) / 1000000.0);

    double pp = std::pow(safeLevel, kDifficultyExponent) * kBaseScale
        * std::pow(accuracy, kAccuracyExponent)
        * std::pow(normalizedScore, kScoreExponent);

    if (result.isFullCombo)
        pp *= kFullComboBonus;
    if (result.isAllPerfect)
        pp *= kAllPerfectBonus;
    if (accuracy >= kAccuracyThreshold)
        pp *= kHighAccuracyBonus;

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