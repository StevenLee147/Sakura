#include "tutorial_data.h"

namespace sakura::game
{

std::vector<TutorialLesson> BuildTutorialLessons()
{
    std::vector<TutorialLesson> lessons;
    lessons.reserve(4);

    lessons.push_back(TutorialLesson{
        TutorialLessonType::KeyboardTap,
        "第 1 课 - 键盘 Tap",
        "看准判定线上的箭头提示，按下对应的 A / S / D / F。判定窗口放宽到 ±200ms。",
        "很好，Tap 的基础节奏已经掌握了。",
        200,
        0.1f,
        true,
        false,
        {
            { NoteType::Tap, 1200, 0, 0, 0.0f, 0.0f },
            { NoteType::Tap, 2600, 1, 0, 0.0f, 0.0f },
            { NoteType::Tap, 4000, 2, 0, 0.0f, 0.0f },
            { NoteType::Tap, 5400, 3, 0, 0.0f, 0.0f },
        }
    });

    lessons.push_back(TutorialLesson{
        TutorialLessonType::KeyboardHold,
        "第 2 课 - 键盘 Hold",
        "在音符到线时按下对应按键，并持续按住直到进度条填满。",
        "很好，已经会稳定地按住 Hold 了。",
        200,
        0.1f,
        true,
        false,
        {
            { NoteType::Hold, 1400, 0, 1100, 0.0f, 0.0f },
            { NoteType::Hold, 3400, 1, 1200, 0.0f, 0.0f },
            { NoteType::Hold, 5600, 2, 1400, 0.0f, 0.0f },
        }
    });

    lessons.push_back(TutorialLesson{
        TutorialLessonType::MouseCircle,
        "第 3 课 - 鼠标 Circle",
        "点击右侧区域的圆圈即可。这里距离容差放宽到 0.10，先感受节奏和落点。",
        "不错，鼠标点击也已经稳定了。",
        200,
        0.1f,
        false,
        true,
        {
            { NoteType::Circle, 1200, 0, 0, 0.20f, 0.28f },
            { NoteType::Circle, 2500, 0, 0, 0.74f, 0.30f },
            { NoteType::Circle, 3800, 0, 0, 0.48f, 0.58f },
            { NoteType::Circle, 5100, 0, 0, 0.28f, 0.74f },
            { NoteType::Circle, 6400, 0, 0, 0.78f, 0.70f },
        }
    });

    lessons.push_back(TutorialLesson{
        TutorialLessonType::Mixed,
        "第 4 课 - 综合配合",
        "左侧 Tap 与右侧 Circle 交替出现，照着节奏切换注意力即可。",
        "恭喜，四课全部完成。已经可以开始正式游玩了。",
        200,
        0.1f,
        true,
        true,
        {
            { NoteType::Tap,    1200, 0, 0, 0.0f, 0.0f },
            { NoteType::Circle, 2200, 0, 0, 0.28f, 0.32f },
            { NoteType::Tap,    3300, 2, 0, 0.0f, 0.0f },
            { NoteType::Circle, 4300, 0, 0, 0.72f, 0.38f },
            { NoteType::Tap,    5400, 1, 0, 0.0f, 0.0f },
            { NoteType::Circle, 6500, 0, 0, 0.52f, 0.68f },
        }
    });

    return lessons;
}

} // namespace sakura::game
