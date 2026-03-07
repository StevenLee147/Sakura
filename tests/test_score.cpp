// tests/test_score.cpp — ScoreCalculator 单元测试
// 不依赖 SDL3；仅测试纯计分逻辑

#include "test_framework.h"

#include "game/score.h"

using namespace sakura::game;
using sakura::tests::Matchers::WithinAbs;
using sakura::tests::Matchers::WithinRel;

// ─────────────────────────────────────────────────────────────────────────────
// 辅助：快速完成一局全 Perfect
// ─────────────────────────────────────────────────────────────────────────────
static ScoreCalculator makeAllPerfect(int notes)
{
    ScoreCalculator sc;
    sc.Initialize(notes);
    for (int i = 0; i < notes; ++i)
        sc.OnJudge(JudgeResult::Perfect, 0);
    return sc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScoreCalculator::Initialize 重置内部状态", "[score][init]")
{
    ScoreCalculator sc;
    sc.Initialize(100);
    // 先打一些分
    sc.OnJudge(JudgeResult::Perfect, 5);
    sc.OnJudge(JudgeResult::Miss,    0);
    // 重新初始化
    sc.Initialize(50);
    REQUIRE(sc.GetScore()        == 0);
    REQUIRE(sc.GetCombo()        == 0);
    REQUIRE(sc.GetMaxCombo()     == 0);
    REQUIRE(sc.GetPerfectCount() == 0);
    REQUIRE(sc.GetMissCount()    == 0);
}

TEST_CASE("ScoreCalculator::Initialize 接受 0（保护性最小值 1）", "[score][init]")
{
    ScoreCalculator sc;
    REQUIRE_NOTHROW(sc.Initialize(0));
    // 不应崩溃；单音符也会受到连击加成影响，但仍应受 10% 上限约束
    sc.OnJudge(JudgeResult::Perfect, 0);
    REQUIRE(sc.GetScore() >= 1'000'000);
    REQUIRE(sc.GetScore() <= 1'100'000);
}

// ─────────────────────────────────────────────────────────────────────────────
// 判定计数
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("OnJudge 各判定计数正确", "[score][judge-count]")
{
    ScoreCalculator sc;
    sc.Initialize(10);
    sc.OnJudge(JudgeResult::Perfect, 0);
    sc.OnJudge(JudgeResult::Great,   5);
    sc.OnJudge(JudgeResult::Good,   15);
    sc.OnJudge(JudgeResult::Bad,    30);
    sc.OnJudge(JudgeResult::Miss,    0);

    REQUIRE(sc.GetPerfectCount() == 1);
    REQUIRE(sc.GetGreatCount()   == 1);
    REQUIRE(sc.GetGoodCount()    == 1);
    REQUIRE(sc.GetBadCount()     == 1);
    REQUIRE(sc.GetMissCount()    == 1);
    REQUIRE(sc.GetJudgedCount()  == 5);
}

TEST_CASE("OnJudge::None 不影响任何计数", "[score][judge-count]")
{
    ScoreCalculator sc;
    sc.Initialize(5);
    sc.OnJudge(JudgeResult::None, 0);
    REQUIRE(sc.GetJudgedCount() == 0);
    REQUIRE(sc.GetScore()       == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 分数上限 ≈ 1,000,000
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("全 Perfect 最终分数约为 1,000,000 (+连击加成)", "[score][boundary]")
{
    constexpr int N = 1000;
    auto sc = makeAllPerfect(N);
    // 连击加成最大 10%，因此分数在 [1e6, 1.1e6] 区间
    REQUIRE(sc.GetScore() >= 1'000'000);
    REQUIRE(sc.GetScore() <= 1'100'000);
}

TEST_CASE("全 Miss 分数为 0", "[score][boundary]")
{
    ScoreCalculator sc;
    sc.Initialize(100);
    for (int i = 0; i < 100; ++i)
        sc.OnJudge(JudgeResult::Miss, 0);
    REQUIRE(sc.GetScore() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 连击逻辑
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Bad/Miss 断连击，Perfect/Great/Good 维持连击", "[score][combo]")
{
    ScoreCalculator sc;
    sc.Initialize(10);

    sc.OnJudge(JudgeResult::Perfect, 0);
    sc.OnJudge(JudgeResult::Great,   0);
    sc.OnJudge(JudgeResult::Good,    0);
    REQUIRE(sc.GetCombo() == 3);

    sc.OnJudge(JudgeResult::Bad, 0);
    REQUIRE(sc.GetCombo() == 0);

    sc.OnJudge(JudgeResult::Perfect, 0);
    REQUIRE(sc.GetCombo() == 1);

    sc.OnJudge(JudgeResult::Miss, 0);
    REQUIRE(sc.GetCombo() == 0);
}

TEST_CASE("MaxCombo 记录历史最高连击", "[score][combo]")
{
    ScoreCalculator sc;
    sc.Initialize(10);
    for (int i = 0; i < 5; ++i) sc.OnJudge(JudgeResult::Perfect, 0);
    sc.OnJudge(JudgeResult::Miss, 0);
    for (int i = 0; i < 3; ++i) sc.OnJudge(JudgeResult::Perfect, 0);

    REQUIRE(sc.GetMaxCombo() == 5);
    REQUIRE(sc.GetCombo()    == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// 准确率
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("全 Perfect 准确率为 100%", "[score][accuracy]")
{
    auto sc = makeAllPerfect(50);
    REQUIRE_THAT(sc.GetAccuracy(), WithinAbs(100.0f, 0.01f));
}

TEST_CASE("全 Miss 准确率为 0%", "[score][accuracy]")
{
    ScoreCalculator sc;
    sc.Initialize(50);
    for (int i = 0; i < 50; ++i)
        sc.OnJudge(JudgeResult::Miss, 0);
    REQUIRE_THAT(sc.GetAccuracy(), WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Great 准确率低于 Perfect", "[score][accuracy]")
{
    ScoreCalculator p, g;
    p.Initialize(10); g.Initialize(10);
    for (int i = 0; i < 10; ++i)
    {
        p.OnJudge(JudgeResult::Perfect, 0);
        g.OnJudge(JudgeResult::Great,   0);
    }
    REQUIRE(p.GetAccuracy() > g.GetAccuracy());
}

// ─────────────────────────────────────────────────────────────────────────────
// IsFullCombo / IsAllPerfect
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("IsFullCombo：无 Bad/Miss 时为 true", "[score][fc]")
{
    ScoreCalculator sc;
    sc.Initialize(5);
    sc.OnJudge(JudgeResult::Perfect, 0);
    sc.OnJudge(JudgeResult::Great,   5);
    sc.OnJudge(JudgeResult::Good,   10);
    REQUIRE(sc.IsFullCombo()  == true);
    REQUIRE(sc.IsAllPerfect() == false);

    sc.OnJudge(JudgeResult::Bad, 30);
    REQUIRE(sc.IsFullCombo() == false);
}

TEST_CASE("IsAllPerfect：仅全 Perfect 时为 true", "[score][ap]")
{
    auto sc = makeAllPerfect(10);
    REQUIRE(sc.IsAllPerfect() == true);
    REQUIRE(sc.IsFullCombo()  == true);
}

// ─────────────────────────────────────────────────────────────────────────────
// 偏差记录
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("命中偏差被记录，Miss 不被记录", "[score][hiterror]")
{
    ScoreCalculator sc;
    sc.Initialize(5);
    sc.OnJudge(JudgeResult::Perfect,  5);
    sc.OnJudge(JudgeResult::Great,   -8);
    sc.OnJudge(JudgeResult::Miss,     0);
    sc.OnJudge(JudgeResult::Bad,     25);

    const auto& errs = sc.GetHitErrors();
    REQUIRE(errs.size() == 3);  // Perfect + Great + Bad；Miss 不记录
    REQUIRE(errs[0] ==  5);
    REQUIRE(errs[1] == -8);
    REQUIRE(errs[2] == 25);
}
