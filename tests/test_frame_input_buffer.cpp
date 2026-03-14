#include "test_framework.h"

#include "core/frame_input_buffer.h"

using namespace sakura::core;

TEST_CASE("FrameInputBuffer 保留同帧全部按键按下顺序", "[input][buffer]")
{
    FrameInputBuffer buffer;

    buffer.PushKeyPress(4);
    buffer.PushKeyPress(7);
    buffer.PushKeyPress(12);

    auto events = buffer.GetKeyPresses();
    REQUIRE(events.size() == 3);
    REQUIRE(events[0].scancode == 4);
    REQUIRE(events[1].scancode == 7);
    REQUIRE(events[2].scancode == 12);
}

TEST_CASE("FrameInputBuffer 同时保留多个鼠标按下事件及坐标", "[input][buffer]")
{
    FrameInputBuffer buffer;

    buffer.PushMouseButtonPress(1, 0.25f, 0.50f, 480.0f, 540.0f);
    buffer.PushMouseButtonPress(3, 0.75f, 0.35f, 1440.0f, 378.0f);

    auto events = buffer.GetMouseButtonPresses();
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].button == 1);
    REQUIRE_THAT(events[0].normX, sakura::tests::Matchers::WithinAbs(0.25, 0.0001));
    REQUIRE_THAT(events[0].normY, sakura::tests::Matchers::WithinAbs(0.50, 0.0001));
    REQUIRE_THAT(events[1].pixelX, sakura::tests::Matchers::WithinAbs(1440.0, 0.0001));
    REQUIRE_THAT(events[1].pixelY, sakura::tests::Matchers::WithinAbs(378.0, 0.0001));
}

TEST_CASE("FrameInputBuffer Clear 会清空全部事件", "[input][buffer]")
{
    FrameInputBuffer buffer;

    buffer.PushKeyPress(8);
    buffer.PushMouseButtonPress(1, 0.4f, 0.6f, 100.0f, 200.0f);
    buffer.Clear();

    REQUIRE(buffer.GetKeyPresses().empty());
    REQUIRE(buffer.GetMouseButtonPresses().empty());
}