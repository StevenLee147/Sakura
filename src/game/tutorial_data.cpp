#include "tutorial_data.h"

namespace sakura::game
{

std::vector<TutorialLesson> BuildTutorialLessons()
{
    std::vector<TutorialLesson> lessons;
    lessons.reserve(5);

    lessons.push_back(TutorialLesson{
        TutorialLessonType::KeyboardTap,
        "第 1 课 - 键盘 Tap",
        "音符到达判定线时，按下轨道下方显示的按键。先跟随节奏，教程的判定更宽松。",
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
        "接近圈逐渐缩小，与目标重合时点击圆心。先感受节奏，再熟悉鼠标落点。",
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
        TutorialLessonType::MouseSlide,
        "第 4 课 - 鼠标 Slide",
        "先在起点按下鼠标左键，再按住不放，沿着路径依次滑过每个发光节点。",
        "很好，鼠标 Slide 的按住与跟踪也掌握了。",
        200,
        0.1f,
        false,
        true,
        {
            { NoteType::Slider, 1400, 0, 1800, 0.18f, 0.24f,
                { { 0.40f, 0.30f }, { 0.68f, 0.48f }, { 0.80f, 0.72f } } },
            { NoteType::Slider, 4700, 0, 1700, 0.78f, 0.26f,
                { { 0.64f, 0.40f }, { 0.40f, 0.60f }, { 0.22f, 0.78f } } },
        }
    });

    lessons.push_back(TutorialLesson{
        TutorialLessonType::Mixed,
        "第 5 课 - 综合配合",
        "左侧 Tap 与右侧 Circle 交替出现，照着节奏切换注意力即可。",
        "很好，交替配合也掌握了。进入最后一课学习组合技吧！",
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

    lessons.push_back(TutorialLesson{
        TutorialLessonType::Synergy,
        "第 6 课 - 樱枝横梁与组合技",
        "大拇指按空格敲击横跨全轨的鎏金樱枝横梁；金丝连携需左右同拍击打，樱绽圈需按住对应键再点击。",
        "恭喜，六课全部完成！双域协同与组合技已就绪，开始正式演奏吧。",
        220,
        0.12f,
        true,
        true,
        {
            { NoteType::Tap,    1400, 4, 0, 0.0f,  0.0f,  {}, 0, -1 },
            { NoteType::Tap,    2800, 4, 0, 0.0f,  0.0f,  {}, 0, -1 },
            { NoteType::Tap,    4400, 1, 0, 0.0f,  0.0f,  {}, 1, -1 },
            { NoteType::Circle, 4400, 0, 0, 0.45f, 0.42f, {}, 1, -1 },
            { NoteType::Circle, 6200, 0, 0, 0.56f, 0.58f, {}, 0,  4 },
        }
    });

    return lessons;
}

} // namespace sakura::game
