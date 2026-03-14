// tests/test_judge.cpp — Judge 判定逻辑单元测试
// 不依赖 SDL3；仅测试纯时间窗口和偏差计算逻辑

#include "test_framework.h"

#include "game/judge.h"

using namespace sakura::game;

// ─────────────────────────────────────────────────────────────────────────────
// 辅助：默认窗口值（与 JudgeWindows{} 一致）
// ─────────────────────────────────────────────────────────────────────────────
// perfect = 25ms, great = 50ms, good = 80ms, bad = 120ms, miss > 150ms

// ─────────────────────────────────────────────────────────────────────────────
// GetResultByTimeDiff — 时间差 → 判定结果
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GetResultByTimeDiff 在各窗口边界内返回正确判定", "[judge][windows]")
{
    Judge judge;  // 使用默认窗口值（不调 Initialize 避免文件 I/O）
    const auto& w = judge.GetWindows();

    // Perfect 中心
    REQUIRE(judge.GetResultByTimeDiff(0)          == JudgeResult::Perfect);
    // Perfect 边界
    REQUIRE(judge.GetResultByTimeDiff(w.perfect)  == JudgeResult::Perfect);
    // Great 区间（刚好越过 perfect）
    REQUIRE(judge.GetResultByTimeDiff(w.perfect+1) == JudgeResult::Great);
    REQUIRE(judge.GetResultByTimeDiff(w.great)     == JudgeResult::Great);
    // Good
    REQUIRE(judge.GetResultByTimeDiff(w.great+1)   == JudgeResult::Good);
    REQUIRE(judge.GetResultByTimeDiff(w.good)       == JudgeResult::Good);
    // Bad
    REQUIRE(judge.GetResultByTimeDiff(w.good+1)    == JudgeResult::Bad);
    REQUIRE(judge.GetResultByTimeDiff(w.bad)        == JudgeResult::Bad);
    // Miss（超过 miss 窗口）
    REQUIRE(judge.GetResultByTimeDiff(w.miss+1)    == JudgeResult::Miss);
    REQUIRE(judge.GetResultByTimeDiff(9999)        == JudgeResult::Miss);
}

TEST_CASE("GetResultByTimeDiff 默认窗口具体数值校验", "[judge][windows]")
{
    Judge judge;
    // 默认值 25/50/80/120/150
    REQUIRE(judge.GetResultByTimeDiff(24)  == JudgeResult::Perfect);
    REQUIRE(judge.GetResultByTimeDiff(25)  == JudgeResult::Perfect);
    REQUIRE(judge.GetResultByTimeDiff(26)  == JudgeResult::Great);
    REQUIRE(judge.GetResultByTimeDiff(50)  == JudgeResult::Great);
    REQUIRE(judge.GetResultByTimeDiff(51)  == JudgeResult::Good);
    REQUIRE(judge.GetResultByTimeDiff(80)  == JudgeResult::Good);
    REQUIRE(judge.GetResultByTimeDiff(81)  == JudgeResult::Bad);
    REQUIRE(judge.GetResultByTimeDiff(120) == JudgeResult::Bad);
    REQUIRE(judge.GetResultByTimeDiff(151) == JudgeResult::Miss);
}

// ─────────────────────────────────────────────────────────────────────────────
// JudgeWindows 结构体默认值
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JudgeWindows 默认值符合设计规范", "[judge][windows-defaults]")
{
    JudgeWindows w;
    REQUIRE(w.perfect == 25);
    REQUIRE(w.great   == 50);
    REQUIRE(w.good    == 80);
    REQUIRE(w.bad     == 120);
    REQUIRE(w.miss    == 150);
    // 窗口必须递增
    REQUIRE(w.perfect < w.great);
    REQUIRE(w.great   < w.good);
    REQUIRE(w.good    < w.bad);
    REQUIRE(w.bad     < w.miss);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetHitError — 偏差方向
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GetHitError 按早/准/晚返回正确符号", "[judge][hiterror]")
{
    // 偏早：hit(990) < note(1000) → 正值 +10
    REQUIRE(Judge::GetHitError(1000, 990)  == +10);
    // 偏晚：hit(1010) > note(1000) → 负值 -10
    REQUIRE(Judge::GetHitError(1000, 1010) == -10);
    // 精确命中
    REQUIRE(Judge::GetHitError(500, 500) == 0);
}

TEST_CASE("GetHitError 偏差绝对值反映时间差", "[judge][hiterror]")
{
    int err = Judge::GetHitError(2000, 1975);
    // |err| == 25
    int absErr = (err < 0) ? -err : err;
    REQUIRE(absErr == 25);
}

// ─────────────────────────────────────────────────────────────────────────────
// HoldState 默认值
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("HoldState 默认构造值符合预期", "[judge][holdstate]")
{
    HoldState hs;
    REQUIRE(hs.noteIndex    == -1);
    REQUIRE(hs.isHeld       == false);
    REQUIRE(hs.headJudged   == false);
    REQUIRE(hs.finalized    == false);
    REQUIRE(hs.releaseTimeMs == -1);
    // 容错断触窗口 > 0（设计保证）
    REQUIRE(HoldState::INPUT_GAP_TOLERANCE_MS > 0);
}

TEST_CASE("SliderState 容错窗口大于 HoldState", "[judge][sliderstate]")
{
    // Slider 鼠标路径更敏感，设计上容错更宽
    REQUIRE(SliderState::INPUT_GAP_TOLERANCE_MS >= HoldState::INPUT_GAP_TOLERANCE_MS);
}

TEST_CASE("JudgeMouseNote 对 Slider 头部使用更宽容的点击范围", "[judge][mouse]")
{
    Judge judge;

    MouseNote circle;
    circle.time = 1000;
    circle.x    = 0.50f;
    circle.y    = 0.50f;
    circle.type = NoteType::Circle;

    MouseNote slider = circle;
    slider.type = NoteType::Slider;

    REQUIRE(Judge::GetMouseHitTolerance(slider) >= Judge::GetMouseHitTolerance(circle));
    REQUIRE(Judge::GetMouseHitTolerance(slider) == 0.10f);

    // 取接近 0.10f Slider 容差边界的点，验证正式游戏已与教程的 Slide 手感对齐。
    const float hitX = slider.x + 0.095f;
    const float hitY = slider.y;

    REQUIRE(judge.JudgeMouseNote(circle, 1000, hitX, hitY) == JudgeResult::None);
    REQUIRE(judge.JudgeMouseNote(slider, 1000, hitX, hitY) == JudgeResult::Perfect);
}

TEST_CASE("UpdateSliderTracking 使用与教程一致的路径容差", "[judge][slider]")
{
    Judge judge;

    MouseNote slider;
    slider.time           = 1000;
    slider.type           = NoteType::Slider;
    slider.sliderDuration = 300;
    slider.sliderPath     = { { 0.60f, 0.50f } };

    SliderState state;
    state.headJudged = true;

    REQUIRE(judge.UpdateSliderTracking(state, slider, 1300, 0.695f, 0.50f, true)
            == JudgeResult::Perfect);
    REQUIRE(state.nextWaypointIndex == 1);
    REQUIRE(state.finalized == true);
    REQUIRE(state.isMissed == false);

    SliderState missState;
    missState.headJudged = true;

    REQUIRE(judge.UpdateSliderTracking(missState, slider, 1381, 0.705f, 0.50f, true)
            == JudgeResult::Miss);
    REQUIRE(missState.nextWaypointIndex == 1);
    REQUIRE(missState.finalized == true);
    REQUIRE(missState.isMissed == true);
}

TEST_CASE("UpdateSliderTracking 允许在拐点后短时间内修正轨迹", "[judge][slider]")
{
    Judge judge;

    MouseNote slider;
    slider.time           = 1000;
    slider.type           = NoteType::Slider;
    slider.sliderDuration = 300;
    slider.sliderPath     = { { 0.60f, 0.50f } };

    SliderState state;
    state.headJudged = true;

    REQUIRE(judge.UpdateSliderTracking(state, slider, 1300, 0.75f, 0.50f, true)
            == JudgeResult::None);
    REQUIRE(state.nextWaypointIndex == 0);
    REQUIRE(state.finalized == false);
    REQUIRE(state.isMissed == false);

    REQUIRE(judge.UpdateSliderTracking(state, slider, 1360, 0.60f, 0.50f, true)
            == JudgeResult::Perfect);
    REQUIRE(state.nextWaypointIndex == 1);
    REQUIRE(state.finalized == true);
    REQUIRE(state.isMissed == false);
}

TEST_CASE("UpdateSliderTracking 在宽限结束后仍未到位时判 Miss", "[judge][slider]")
{
    Judge judge;

    MouseNote slider;
    slider.time           = 1000;
    slider.type           = NoteType::Slider;
    slider.sliderDuration = 300;
    slider.sliderPath     = { { 0.60f, 0.50f } };

    SliderState state;
    state.headJudged = true;

    REQUIRE(judge.UpdateSliderTracking(state, slider, 1381, 0.75f, 0.50f, true)
            == JudgeResult::Miss);
    REQUIRE(state.nextWaypointIndex == 1);
    REQUIRE(state.finalized == true);
    REQUIRE(state.isMissed == true);
}
