#pragma once

#include "note.h"

#include <string>
#include <vector>

namespace sakura::game
{

enum class TutorialLessonType
{
    KeyboardTap,
    KeyboardHold,
    MouseCircle,
    Mixed,
};

struct TutorialLessonNote
{
    NoteType type       = NoteType::Tap;
    int      timeMs     = 0;
    int      lane       = 0;
    int      durationMs = 0;
    float    x          = 0.5f;
    float    y          = 0.5f;
};

struct TutorialLesson
{
    TutorialLessonType            type = TutorialLessonType::KeyboardTap;
    std::string                   title;
    std::string                   instruction;
    std::string                   completionText;
    int                           judgeWindowMs  = 200;
    float                         mouseTolerance = 0.1f;
    bool                          usesKeyboard   = true;
    bool                          usesMouse      = false;
    std::vector<TutorialLessonNote> notes;
};

std::vector<TutorialLesson> BuildTutorialLessons();

} // namespace sakura::game
