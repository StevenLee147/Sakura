#include "test_framework.h"

#include "game/pp_calculator.h"

using namespace sakura::game;

namespace
{

GameResult MakePPResult(float level,
                        float accuracy,
                        int score,
                        bool isFullCombo = false,
                        bool isAllPerfect = false)
{
    GameResult result;
    result.chartId = "pp-test";
    result.chartTitle = "pp-test";
    result.difficulty = "Test";
    result.difficultyLevel = level;
    result.accuracy = accuracy;
    result.score = score;
    result.isFullCombo = isFullCombo;
    result.isAllPerfect = isAllPerfect;
    return result;
}

} // namespace

TEST_CASE("PPCalculator 难度与准确率越高单谱 PP 越高", "[pp]")
{
    auto low = MakePPResult(5.0f, 92.0f, 850000);
    auto highAccuracy = MakePPResult(5.0f, 98.0f, 850000);
    auto highDifficulty = MakePPResult(9.0f, 92.0f, 850000);

    REQUIRE(PPCalculator::CalculatePP(highAccuracy, highAccuracy.difficultyLevel)
        > PPCalculator::CalculatePP(low, low.difficultyLevel));
    REQUIRE(PPCalculator::CalculatePP(highDifficulty, highDifficulty.difficultyLevel)
        > PPCalculator::CalculatePP(low, low.difficultyLevel));
}

TEST_CASE("PPCalculator FC AP 与高准确率加成生效", "[pp]")
{
    auto base = MakePPResult(10.0f, 98.5f, 980000);
    auto fc = MakePPResult(10.0f, 98.5f, 980000, true, false);
    auto ap = MakePPResult(10.0f, 99.7f, 1000000, true, true);

    double basePP = PPCalculator::CalculatePP(base, base.difficultyLevel);
    double fcPP = PPCalculator::CalculatePP(fc, fc.difficultyLevel);
    double apPP = PPCalculator::CalculatePP(ap, ap.difficultyLevel);

    REQUIRE(fcPP > basePP);
    REQUIRE(apPP > fcPP);
}

TEST_CASE("PPCalculator 总 PP 按降序加权", "[pp]")
{
    PPCalculator calculator;
    std::vector<GameResult> bestScores = {
        MakePPResult(12.0f, 99.5f, 995000, true, false),
        MakePPResult(9.0f, 98.0f, 970000, true, false),
        MakePPResult(6.0f, 96.0f, 930000, false, false),
    };

    calculator.RecalculateTotal(bestScores);
    auto plays = calculator.GetBestPlays(3);

    REQUIRE(plays.size() == 3);
    REQUIRE(plays[0].pp >= plays[1].pp);
    REQUIRE(plays[1].pp >= plays[2].pp);

    double expected = plays[0].pp
        + plays[1].pp * 0.95
        + plays[2].pp * 0.95 * 0.95;
    REQUIRE_THAT(calculator.GetTotalPP(), sakura::tests::Matchers::WithinAbs(expected, 0.0001));
}