// pp_calculator.cpp — Performance Point 计算器实现

#include "pp_calculator.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_map>

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
    if (result.synergyCount > 0)
        pp *= (1.0 + std::min(0.03, static_cast<double>(result.synergyCount) * 0.0015));

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

double PPCalculator::CalculateChartRating(const GameResult& result, float level)
{
    if (result.assisted || !std::isfinite(level) || level <= 0.0f || result.score <= 0)
        return 0.0;

    const double L = static_cast<double>(level);
    const double rawAcc = std::isfinite(result.accuracy) ? static_cast<double>(result.accuracy) : 0.0;
    const double acc = std::clamp(rawAcc, 0.0, 100.0);

    double base = 0.0;
    if (acc >= 100.0)
        base = L + 2.00;
    else if (acc >= 99.5)
        base = L + 1.50 + (acc - 99.5) * 1.00;
    else if (acc >= 98.0)
        base = L + 1.00 + (acc - 98.0) / 3.0;
    else if (acc >= 95.0)
        base = L + (acc - 95.0) / 3.0;
    else if (acc >= 90.0)
        base = std::max(0.0, L - 1.0 + (acc - 90.0) / 5.0);
    else
        base = std::max(0.0, (L - 1.0) * std::pow(acc / 90.0, 2.0));

    double bonus = 0.0;
    if (result.isAllPerfect)
        bonus += 0.12;
    else if (result.isFullCombo)
        bonus += 0.06;

    if (result.synergyCount > 0)
        bonus += std::min(0.06, static_cast<double>(result.synergyCount) * 0.002);

    return std::max(0.0, std::round((base + bonus) * 1000.0) / 1000.0);
}

const char* PPCalculator::GetRatingTierName(double sakuraRating)
{
    if (!std::isfinite(sakuraRating) || sakuraRating < 4.0)
        return "青樱 · Bud";
    if (sakuraRating < 8.0)
        return "银月 · Crescent";
    if (sakuraRating < 11.5)
        return "鎏金 · Gilded";
    if (sakuraRating < 14.0)
        return "绯虹 · Crimson";
    return "极星 · Astral";
}

std::string PPCalculator::FormatRating(double rating)
{
    const double safeRating = std::isfinite(rating) ? std::max(0.0, rating) : 0.0;
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(2) << safeRating;
    return oss.str();
}

RatingSummary PPCalculator::CalculateSakuraRating(const std::vector<GameResult>& allBestScores,
                                                  const std::vector<GameResult>& recentScores)
{
    RatingSummary summary;

    std::vector<RatingEntry> dedupEntries;
    dedupEntries.reserve(allBestScores.size());
    std::unordered_map<std::string, std::size_t> keyToIndex;
    keyToIndex.reserve(allBestScores.size());

    for (const auto& s : allBestScores)
    {
        if (s.assisted || s.chartId.empty())
            continue;

        const std::string key = s.chartId + "::" + s.difficulty;
        const double rating = CalculateChartRating(s, s.difficultyLevel);

        const auto it = keyToIndex.find(key);
        if (it == keyToIndex.end())
        {
            keyToIndex.emplace(key, dedupEntries.size());
            dedupEntries.push_back({ s, rating });
        }
        else
        {
            auto& existing = dedupEntries[it->second];
            if (rating > existing.rating ||
                (rating == existing.rating && s.score > existing.result.score))
            {
                existing = { s, rating };
            }
        }
    }

    auto ratingDescComparator = [](const RatingEntry& lhs, const RatingEntry& rhs)
    {
        if (lhs.rating != rhs.rating)
            return lhs.rating > rhs.rating;
        return lhs.result.score > rhs.result.score;
    };

    std::stable_sort(dedupEntries.begin(), dedupEntries.end(), ratingDescComparator);

    const std::size_t b30Count = std::min<std::size_t>(30, dedupEntries.size());
    summary.best30.assign(dedupEntries.begin(), dedupEntries.begin() + b30Count);

    if (summary.best30.empty())
    {
        summary.sakuraRating = 0.0;
        summary.tierName     = GetRatingTierName(0.0);
        return summary;
    }

    double b30Sum = 0.0;
    for (const auto& entry : summary.best30)
        b30Sum += entry.rating;
    summary.b30Avg = b30Sum / static_cast<double>(summary.best30.size());

    std::vector<RatingEntry> recentCandidates;
    recentCandidates.reserve(recentScores.size());
    for (const auto& s : recentScores)
    {
        if (s.assisted)
            continue;
        recentCandidates.push_back({ s, CalculateChartRating(s, s.difficultyLevel) });
    }

    std::stable_sort(recentCandidates.begin(), recentCandidates.end(), ratingDescComparator);

    const std::size_t r10Count = std::min<std::size_t>(10, recentCandidates.size());
    summary.recent10.assign(recentCandidates.begin(), recentCandidates.begin() + r10Count);

    double r10Sum = 0.0;
    for (const auto& entry : summary.recent10)
        r10Sum += entry.rating;

    const double rawR10 = summary.recent10.empty()
        ? summary.b30Avg
        : (r10Sum / static_cast<double>(summary.recent10.size()));

    summary.r10Avg       = std::max(rawR10, summary.b30Avg * 0.85);
    summary.sakuraRating = std::round((0.75 * summary.b30Avg + 0.25 * summary.r10Avg) * 100.0) / 100.0;
    summary.tierName     = GetRatingTierName(summary.sakuraRating);
    return summary;
}

std::vector<PPPlay> PPCalculator::GetBestPlays(int limit) const
{
    if (limit <= 0 || m_bestPlays.empty())
        return {};

    const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(limit), m_bestPlays.size());
    return std::vector<PPPlay>(m_bestPlays.begin(), m_bestPlays.begin() + count);
}

} // namespace sakura::game