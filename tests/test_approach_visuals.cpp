#include "test_framework.h"

#include "game/approach_visuals.h"

using namespace sakura::game;

TEST_CASE("接近圈在 0ms 误差时完全收敛到音符颜色", "[approach][visual]")
{
    const GuidanceColor note{ 200, 150, 255, 180 };
    const GuidanceColor startAccent{ 90, 225, 255, 180 };
    const GuidanceColor endAccent{ 255, 200, 120, 180 };

    auto gradient = BuildApproachGradient(1000, 1000, 2000, note, startAccent, endAccent);

    REQUIRE(gradient.startColor.r == note.r);
    REQUIRE(gradient.startColor.g == note.g);
    REQUIRE(gradient.startColor.b == note.b);
    REQUIRE(gradient.endColor.r == note.r);
    REQUIRE(gradient.endColor.g == note.g);
    REQUIRE(gradient.endColor.b == note.b);
}

TEST_CASE("非 0ms 误差时接近圈仍保留引导渐变", "[approach][visual]")
{
    const GuidanceColor note{ 100, 220, 140, 200 };
    const GuidanceColor startAccent{ 255, 215, 120, 200 };
    const GuidanceColor endAccent{ 110, 245, 225, 200 };

    auto early = BuildApproachGradient(1000, 250, 2000, note, startAccent, endAccent);
    auto nearHit = BuildApproachGradient(1000, 990, 2000, note, startAccent, endAccent);

    REQUIRE(early.startColor.r != note.r || early.startColor.g != note.g || early.startColor.b != note.b);
    REQUIRE(early.endColor.r != note.r || early.endColor.g != note.g || early.endColor.b != note.b);

    REQUIRE(std::abs(static_cast<int>(nearHit.startColor.r) - static_cast<int>(note.r))
            < std::abs(static_cast<int>(early.startColor.r) - static_cast<int>(note.r)));
    REQUIRE(std::abs(static_cast<int>(nearHit.endColor.g) - static_cast<int>(note.g))
            < std::abs(static_cast<int>(early.endColor.g) - static_cast<int>(note.g)));
}

TEST_CASE("非零误差的颜色混合因子不会提前达到完全同色", "[approach][visual]")
{
    REQUIRE_THAT(ComputeApproachColorBlend(1, 2000), sakura::tests::Matchers::WithinAbs(0.92, 0.0001));
    REQUIRE(ComputeApproachColorBlend(1, 2000) < 1.0f);
    REQUIRE(ComputeApproachColorBlend(0, 2000) == 1.0f);
}