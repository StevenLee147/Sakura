// tests/test_tutorial_data.cpp — 教程脚本数据测试

#include "test_framework.h"

#include "game/tutorial_data.h"

using namespace sakura::game;

TEST_CASE("教程共五课且顺序正确", "[tutorial][data]")
{
    auto lessons = BuildTutorialLessons();
    REQUIRE(lessons.size() == 5);
    REQUIRE(lessons[0].type == TutorialLessonType::KeyboardTap);
    REQUIRE(lessons[1].type == TutorialLessonType::KeyboardHold);
    REQUIRE(lessons[2].type == TutorialLessonType::KeyboardDrag);
    REQUIRE(lessons[3].type == TutorialLessonType::MouseCircle);
    REQUIRE(lessons[4].type == TutorialLessonType::Mixed);
}

TEST_CASE("教程脚本满足需求中的音符数量与容差", "[tutorial][spec]")
{
    auto lessons = BuildTutorialLessons();

    REQUIRE(lessons[0].notes.size() == 4);
    REQUIRE(lessons[0].judgeWindowMs == 200);

    REQUIRE(lessons[1].notes.size() == 3);
    for (const auto& note : lessons[1].notes)
    {
        REQUIRE(note.type == NoteType::Hold);
        REQUIRE(note.durationMs > 0);
    }

    REQUIRE(lessons[2].notes.size() == 2);
    for (const auto& note : lessons[2].notes)
    {
        REQUIRE(note.type == NoteType::Drag);
        REQUIRE(note.targetLane >= 0);
        REQUIRE(note.targetLane != note.lane);
    }

    REQUIRE(lessons[3].notes.size() == 5);
    REQUIRE(lessons[3].usesMouse == true);
    REQUIRE(lessons[3].mouseTolerance == 0.1f);
    for (const auto& note : lessons[3].notes)
    {
        REQUIRE(note.type == NoteType::Circle);
        REQUIRE(note.x >= 0.0f);
        REQUIRE(note.x <= 1.0f);
        REQUIRE(note.y >= 0.0f);
        REQUIRE(note.y <= 1.0f);
    }

    REQUIRE(lessons[4].notes.size() == 6);
    REQUIRE(lessons[4].usesKeyboard == true);
    REQUIRE(lessons[4].usesMouse == true);
}