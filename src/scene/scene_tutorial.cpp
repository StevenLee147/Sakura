// scene_tutorial.cpp — 新手教程场景

#include "scene_tutorial.h"

#include "scene_menu.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/input.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sakura::scene
{

namespace
{

constexpr sakura::core::Color kBgTop    = { 20, 16, 36, 255 };
constexpr sakura::core::Color kBgBottom = { 10, 8, 20, 255 };

} // namespace

SceneTutorial::SceneTutorial(SceneManager& mgr, bool showFirstRunPrompt)
    : m_manager(mgr)
    , m_showFirstRunPrompt(showFirstRunPrompt)
{
}

void SceneTutorial::OnEnter()
{
    LOG_INFO("[SceneTutorial] 进入教程场景");

    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontTitle = rm.GetDefaultFontHandle();
    m_fontText  = rm.GetDefaultFontHandle();
    m_fontSmall = rm.GetDefaultFontHandle();

    m_lessons = sakura::game::BuildTutorialLessons();
    SetupPromptButtons();

    if (m_showFirstRunPrompt
        && !sakura::core::Config::GetInstance().Get<bool>(
            std::string(sakura::core::ConfigKeys::kTutorialPromptShown), false))
    {
        m_phase = Phase::Prompt;
        m_statusText = "首次运行，是否进入教程？";
    }
    else
    {
        BeginLesson(0);
    }
}

void SceneTutorial::OnExit()
{
    LOG_INFO("[SceneTutorial] 退出教程场景");
}

void SceneTutorial::SetupPromptButtons()
{
    sakura::ui::ButtonColors yesColors;
    yesColors.normal  = { 70, 120, 85, 220 };
    yesColors.hover   = { 95, 155, 110, 235 };
    yesColors.pressed = { 55, 90, 65, 245 };
    yesColors.text    = sakura::core::Color::White;

    sakura::ui::ButtonColors noColors;
    noColors.normal  = { 75, 60, 110, 220 };
    noColors.hover   = { 105, 85, 145, 235 };
    noColors.pressed = { 55, 40, 80, 245 };
    noColors.text    = sakura::core::Color::White;

    m_btnPromptYes = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.34f, 0.56f, 0.14f, 0.055f },
        "进入教程", m_fontText, 0.024f, 0.010f);
    m_btnPromptYes->SetColors(yesColors);
    m_btnPromptYes->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnPromptYes->SetOnClick([this]() { ResolvePrompt(true); });

    m_btnPromptNo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.52f, 0.56f, 0.14f, 0.055f },
        "暂时跳过", m_fontText, 0.024f, 0.010f);
    m_btnPromptNo->SetColors(noColors);
    m_btnPromptNo->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnPromptNo->SetOnClick([this]() { ResolvePrompt(false); });
}

void SceneTutorial::BeginLesson(int lessonIndex)
{
    if (lessonIndex < 0 || lessonIndex >= static_cast<int>(m_lessons.size()))
    {
        ReturnToMenu();
        return;
    }

    m_currentLesson = lessonIndex;
    m_runtimeNotes.clear();
    for (const auto& note : m_lessons[lessonIndex].notes)
    {
        m_runtimeNotes.push_back(RuntimeNote{ note, false, false });
    }

    m_lessonTimerMs = 0.0f;
    m_phase = Phase::Intro;
    m_statusText.clear();
}

void SceneTutorial::StartCurrentLesson()
{
    for (auto& note : m_runtimeNotes)
    {
        note.started = false;
        note.completed = false;
    }

    m_lessonTimerMs = 0.0f;
    m_phase = Phase::Playing;
    m_statusText.clear();
}

void SceneTutorial::ResolvePrompt(bool enterTutorial)
{
    MarkPromptShown();
    if (enterTutorial)
        BeginLesson(0);
    else
        ReturnToMenu();
}

void SceneTutorial::ReturnToMenu()
{
    m_manager.SwitchScene(
        std::make_unique<SceneMenu>(m_manager),
        TransitionType::Fade,
        0.35f);
}

void SceneTutorial::MarkPromptShown()
{
    auto& cfg = sakura::core::Config::GetInstance();
    cfg.Set(std::string(sakura::core::ConfigKeys::kTutorialPromptShown), true);
    cfg.Save();
}

void SceneTutorial::MarkTutorialCompleted()
{
    auto& cfg = sakura::core::Config::GetInstance();
    cfg.Set(std::string(sakura::core::ConfigKeys::kTutorialPromptShown), true);
    cfg.Set(std::string(sakura::core::ConfigKeys::kTutorialCompleted), true);
    cfg.Save();
}

void SceneTutorial::FailCurrentLesson(const std::string& reason)
{
    if (m_phase != Phase::Playing) return;
    m_phase = Phase::Failed;
    m_statusText = reason;
}

void SceneTutorial::CompleteCurrentLesson()
{
    if (m_currentLesson + 1 >= static_cast<int>(m_lessons.size()))
    {
        MarkTutorialCompleted();
        m_phase = Phase::Finished;
        m_statusText = CurrentLesson().completionText;
        return;
    }

    m_phase = Phase::Success;
    m_statusText = CurrentLesson().completionText;
}

const sakura::game::TutorialLesson& SceneTutorial::CurrentLesson() const
{
    return m_lessons[m_currentLesson];
}

float SceneTutorial::CalcKeyboardY(int noteTimeMs, float lessonTimeMs) const
{
    float remainMs = static_cast<float>(noteTimeMs) - lessonTimeMs;
    float normalized = 1.0f - std::clamp(remainMs / APPROACH_MS, 0.0f, 1.0f);
    return TRACK_Y + normalized * (JUDGE_LINE_Y - TRACK_Y);
}

float SceneTutorial::CalcCircleScale(int noteTimeMs, float lessonTimeMs) const
{
    float remainMs = static_cast<float>(noteTimeMs) - lessonTimeMs;
    float t = std::clamp(remainMs / APPROACH_MS, 0.0f, 1.0f);
    return 1.0f + 1.3f * t;
}

int SceneTutorial::LaneFromScancode(SDL_Scancode scancode) const
{
    for (int lane = 0; lane < static_cast<int>(m_laneKeys.size()); ++lane)
    {
        if (m_laneKeys[lane] == scancode)
            return lane;
    }
    return -1;
}

const char* SceneTutorial::LaneLabel(int lane) const
{
    static constexpr const char* kLabels[4] = { "A", "S", "D", "F" };
    if (lane < 0 || lane >= 4) return "?";
    return kLabels[lane];
}

void SceneTutorial::OnUpdate(float dt)
{
    if (m_phase == Phase::Prompt)
    {
        if (m_btnPromptYes) m_btnPromptYes->Update(dt);
        if (m_btnPromptNo)  m_btnPromptNo->Update(dt);
        return;
    }

    if (m_phase == Phase::Playing)
    {
        m_lessonTimerMs += dt * 1000.0f;
        UpdatePlayingState();
    }
}

void SceneTutorial::UpdatePlayingState()
{
    const auto& lesson = CurrentLesson();
    bool allCompleted = true;

    for (auto& runtimeNote : m_runtimeNotes)
    {
        const auto& note = runtimeNote.note;

        switch (note.type)
        {
        case sakura::game::NoteType::Tap:
        case sakura::game::NoteType::Circle:
            if (!runtimeNote.completed)
            {
                allCompleted = false;
                if (m_lessonTimerMs > static_cast<float>(note.timeMs + lesson.judgeWindowMs))
                {
                    FailCurrentLesson("错过了节奏点，按空格重新开始这一课。");
                    return;
                }
            }
            break;

        case sakura::game::NoteType::Hold:
            if (!runtimeNote.started)
            {
                allCompleted = false;
                if (m_lessonTimerMs > static_cast<float>(note.timeMs + lesson.judgeWindowMs))
                {
                    FailCurrentLesson("Hold 需要在音符到线时按下并持续按住。");
                    return;
                }
            }
            else if (!runtimeNote.completed)
            {
                allCompleted = false;
                bool held = sakura::core::Input::IsKeyHeld(m_laneKeys[note.lane]);
                if (!held && m_lessonTimerMs < static_cast<float>(note.timeMs + note.durationMs))
                {
                    FailCurrentLesson("Hold 中途松开了，再试一次。\n按住直到进度条填满即可。");
                    return;
                }

                if (m_lessonTimerMs >= static_cast<float>(note.timeMs + note.durationMs))
                {
                    if (held)
                        runtimeNote.completed = true;
                    else
                    {
                        FailCurrentLesson("Hold 结束前松开了，再试一次。");
                        return;
                    }
                }
            }
            break;

        case sakura::game::NoteType::Drag:
            if (!runtimeNote.started)
            {
                allCompleted = false;
                if (m_lessonTimerMs > static_cast<float>(note.timeMs + lesson.judgeWindowMs))
                {
                    FailCurrentLesson("Drag 要先按起点，再切到目标轨道。");
                    return;
                }
            }
            else if (!runtimeNote.completed)
            {
                allCompleted = false;
                if (m_lessonTimerMs > static_cast<float>(note.timeMs + note.durationMs + lesson.judgeWindowMs))
                {
                    FailCurrentLesson("方向切换慢了一点，再沿着箭头试一次。");
                    return;
                }
            }
            break;

        default:
            break;
        }
    }

    if (allCompleted)
        CompleteCurrentLesson();
}

void SceneTutorial::HandleKeyboardInput(SDL_Scancode scancode)
{
    const auto& lesson = CurrentLesson();
    int lane = LaneFromScancode(scancode);
    if (lane < 0) return;

    int bestIndex = -1;
    int bestDistance = std::numeric_limits<int>::max();

    for (int i = 0; i < static_cast<int>(m_runtimeNotes.size()); ++i)
    {
        auto& runtimeNote = m_runtimeNotes[i];
        const auto& note = runtimeNote.note;

        if (note.type == sakura::game::NoteType::Drag
            && runtimeNote.started
            && !runtimeNote.completed
            && note.targetLane == lane)
        {
            int targetTime = note.timeMs + note.durationMs;
            int distance = std::abs(static_cast<int>(m_lessonTimerMs) - targetTime);
            if (distance <= lesson.judgeWindowMs && distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }
    }

    if (bestIndex >= 0)
    {
        m_runtimeNotes[bestIndex].completed = true;
        sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::Tap);
        return;
    }

    bestIndex = -1;
    bestDistance = std::numeric_limits<int>::max();

    for (int i = 0; i < static_cast<int>(m_runtimeNotes.size()); ++i)
    {
        auto& runtimeNote = m_runtimeNotes[i];
        const auto& note = runtimeNote.note;

        if (runtimeNote.completed || runtimeNote.started) continue;
        if (note.type != sakura::game::NoteType::Tap
            && note.type != sakura::game::NoteType::Hold
            && note.type != sakura::game::NoteType::Drag)
            continue;
        if (note.lane != lane) continue;

        int distance = std::abs(static_cast<int>(m_lessonTimerMs) - note.timeMs);
        if (distance <= lesson.judgeWindowMs && distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    if (bestIndex < 0) return;

    auto& runtimeNote = m_runtimeNotes[bestIndex];
    switch (runtimeNote.note.type)
    {
    case sakura::game::NoteType::Tap:
        runtimeNote.completed = true;
        break;
    case sakura::game::NoteType::Hold:
    case sakura::game::NoteType::Drag:
        runtimeNote.started = true;
        break;
    default:
        break;
    }

    sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::Tap);
}

void SceneTutorial::HandleMouseInput(float screenX, float screenY)
{
    const auto& lesson = CurrentLesson();
    float localX = (screenX - MOUSE_X) / MOUSE_W;
    float localY = (screenY - MOUSE_Y) / MOUSE_H;
    if (localX < 0.0f || localX > 1.0f || localY < 0.0f || localY > 1.0f)
        return;

    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i < static_cast<int>(m_runtimeNotes.size()); ++i)
    {
        auto& runtimeNote = m_runtimeNotes[i];
        const auto& note = runtimeNote.note;
        if (runtimeNote.completed || note.type != sakura::game::NoteType::Circle)
            continue;

        int timeDistance = std::abs(static_cast<int>(m_lessonTimerMs) - note.timeMs);
        if (timeDistance > lesson.judgeWindowMs)
            continue;

        float dx = localX - note.x;
        float dy = localY - note.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= lesson.mouseTolerance && distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0)
    {
        m_runtimeNotes[bestIndex].completed = true;
        sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::Circle);
    }
}

void SceneTutorial::OnEvent(const SDL_Event& event)
{
    if (m_phase == Phase::Prompt)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE)
            {
                ResolvePrompt(false);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_SPACE)
            {
                ResolvePrompt(true);
                return;
            }
        }

        if (m_btnPromptYes) m_btnPromptYes->HandleEvent(event);
        if (m_btnPromptNo)  m_btnPromptNo->HandleEvent(event);
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            ReturnToMenu();
            return;
        }

        if (m_phase == Phase::Intro)
        {
            if (event.key.scancode == SDL_SCANCODE_SPACE)
                StartCurrentLesson();
            return;
        }

        if (m_phase == Phase::Failed)
        {
            if (event.key.scancode == SDL_SCANCODE_SPACE)
                StartCurrentLesson();
            return;
        }

        if (m_phase == Phase::Success)
        {
            if (event.key.scancode == SDL_SCANCODE_SPACE)
                BeginLesson(m_currentLesson + 1);
            return;
        }

        if (m_phase == Phase::Finished)
        {
            if (event.key.scancode == SDL_SCANCODE_SPACE)
                ReturnToMenu();
            return;
        }

        if (m_phase == Phase::Playing)
            HandleKeyboardInput(event.key.scancode);
    }

    if (m_phase == Phase::Playing
        && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && event.button.button == SDL_BUTTON_LEFT)
    {
        auto [x, y] = sakura::core::Input::GetMousePosition();
        HandleMouseInput(x, y);
    }
}

void SceneTutorial::RenderHeader(sakura::core::Renderer& renderer) const
{
    renderer.DrawText(m_fontTitle, "新手教程",
        0.08f, 0.06f, 0.050f,
        { 245, 230, 255, 245 },
        sakura::core::TextAlign::Left);

    if (m_phase != Phase::Prompt)
    {
        renderer.DrawText(m_fontText, CurrentLesson().title,
            0.08f, 0.125f, 0.030f,
            { 220, 205, 240, 230 },
            sakura::core::TextAlign::Left);

        renderer.DrawText(m_fontSmall,
            std::to_string(m_currentLesson + 1) + "/" + std::to_string(m_lessons.size()),
            0.92f, 0.07f, 0.024f,
            { 210, 200, 230, 210 },
            sakura::core::TextAlign::Right);
    }

    renderer.DrawText(m_fontSmall, "Esc 返回主菜单",
        0.92f, 0.11f, 0.020f,
        { 170, 165, 190, 180 },
        sakura::core::TextAlign::Right);
}

void SceneTutorial::RenderLessonOverlay(sakura::core::Renderer& renderer) const
{
    if (m_phase == Phase::Prompt) return;

    renderer.DrawRoundedRect({ 0.08f, 0.15f, 0.80f, 0.07f },
        0.010f, { 28, 24, 48, 220 }, true);
    renderer.DrawRoundedRect({ 0.08f, 0.15f, 0.80f, 0.07f },
        0.010f, { 105, 90, 145, 120 }, false);

    renderer.DrawText(m_fontSmall, CurrentLesson().instruction,
        0.10f, 0.176f, 0.021f,
        { 225, 220, 235, 220 },
        sakura::core::TextAlign::Left);

    std::string footer;
    switch (m_phase)
    {
    case Phase::Intro:
        footer = "按空格开始这一课";
        break;
    case Phase::Playing:
        footer = "跟着提示完成所有目标";
        break;
    case Phase::Failed:
        footer = m_statusText + "  按空格重试";
        break;
    case Phase::Success:
        footer = m_statusText + "  按空格进入下一课";
        break;
    case Phase::Finished:
        footer = m_statusText + "  按空格回到主菜单";
        break;
    default:
        break;
    }

    renderer.DrawText(m_fontSmall, footer,
        0.50f, 0.86f, 0.022f,
        { 255, 235, 210, 220 },
        sakura::core::TextAlign::Center);
}

void SceneTutorial::RenderKeyboardArea(sakura::core::Renderer& renderer) const
{
    const auto& lesson = CurrentLesson();
    if (!lesson.usesKeyboard) return;

    renderer.DrawRoundedRect({ TRACK_X, TRACK_Y, TRACK_W, TRACK_H },
        0.012f, { 18, 16, 34, 200 }, true);
    renderer.DrawRoundedRect({ TRACK_X, TRACK_Y, TRACK_W, TRACK_H },
        0.012f, { 95, 82, 138, 120 }, false);

    float laneWidth = TRACK_W / 4.0f;
    float displayTimeMs = (m_phase == Phase::Playing) ? m_lessonTimerMs : 0.0f;

    for (int lane = 0; lane < 4; ++lane)
    {
        float x = TRACK_X + lane * laneWidth;
        renderer.DrawFilledRect({ x, TRACK_Y, laneWidth - 0.001f, TRACK_H },
            { 28, 24, 46, 120 });
        renderer.DrawText(m_fontText, LaneLabel(lane),
            x + laneWidth * 0.5f, JUDGE_LINE_Y + 0.03f, 0.028f,
            { 230, 225, 245, 230 },
            sakura::core::TextAlign::Center);
    }

    renderer.DrawLine(TRACK_X - 0.01f, JUDGE_LINE_Y, TRACK_X + TRACK_W + 0.01f, JUDGE_LINE_Y,
        { 255, 205, 225, 240 }, 0.004f);

    for (const auto& runtimeNote : m_runtimeNotes)
    {
        const auto& note = runtimeNote.note;
        if (runtimeNote.completed) continue;
        if (note.type != sakura::game::NoteType::Tap
            && note.type != sakura::game::NoteType::Hold
            && note.type != sakura::game::NoteType::Drag)
            continue;

        float laneX = TRACK_X + laneWidth * note.lane + laneWidth * 0.12f;
        float noteW = laneWidth * 0.76f;
        float noteY = CalcKeyboardY(note.timeMs, displayTimeMs);
        auto color = sakura::core::Color{ 255, 170, 205, 235 };

        if (note.type == sakura::game::NoteType::Hold)
        {
            float endY = CalcKeyboardY(note.timeMs + note.durationMs, displayTimeMs);
            float bodyY = std::min(noteY, endY);
            float bodyH = std::max(0.022f, std::abs(endY - noteY));
            renderer.DrawRoundedRect({ laneX + noteW * 0.30f, bodyY, noteW * 0.40f, bodyH },
                0.008f, { 120, 205, 255, 190 }, true);

            if (runtimeNote.started && !runtimeNote.completed)
            {
                float progress = std::clamp(
                    (m_lessonTimerMs - static_cast<float>(note.timeMs))
                    / static_cast<float>(std::max(note.durationMs, 1)),
                    0.0f, 1.0f);
                renderer.DrawRoundedRect({ 0.12f, 0.89f, 0.26f, 0.018f },
                    0.006f, { 36, 32, 58, 220 }, true);
                renderer.DrawRoundedRect({ 0.12f, 0.89f, 0.26f * progress, 0.018f },
                    0.006f, { 120, 220, 255, 230 }, true);
                renderer.DrawText(m_fontSmall, "按住进度",
                    0.12f, 0.865f, 0.018f,
                    { 170, 210, 230, 220 },
                    sakura::core::TextAlign::Left);
            }
        }

        renderer.DrawRoundedRect({ laneX, noteY - 0.018f, noteW, 0.036f },
            0.009f, color, true);
        renderer.DrawText(m_fontSmall, LaneLabel(note.lane),
            laneX + noteW * 0.5f, noteY - 0.002f, 0.018f,
            { 35, 20, 45, 255 },
            sakura::core::TextAlign::Center);

        if (note.type == sakura::game::NoteType::Drag)
        {
            float fromX = TRACK_X + laneWidth * note.lane + laneWidth * 0.5f;
            float toX   = TRACK_X + laneWidth * note.targetLane + laneWidth * 0.5f;
            renderer.DrawLine(fromX, noteY, toX, noteY - 0.05f,
                { 140, 220, 255, 220 }, 0.003f);
            renderer.DrawText(m_fontSmall, LaneLabel(note.targetLane),
                toX, noteY - 0.07f, 0.018f,
                { 185, 235, 255, 230 },
                sakura::core::TextAlign::Center);
        }
    }
}

void SceneTutorial::RenderMouseArea(sakura::core::Renderer& renderer) const
{
    const auto& lesson = CurrentLesson();
    if (!lesson.usesMouse) return;

    renderer.DrawRoundedRect({ MOUSE_X, MOUSE_Y, MOUSE_W, MOUSE_H },
        0.012f, { 18, 16, 34, 200 }, true);
    renderer.DrawRoundedRect({ MOUSE_X, MOUSE_Y, MOUSE_W, MOUSE_H },
        0.012f, { 95, 82, 138, 120 }, false);

    float displayTimeMs = (m_phase == Phase::Playing) ? m_lessonTimerMs : 0.0f;
    for (const auto& runtimeNote : m_runtimeNotes)
    {
        const auto& note = runtimeNote.note;
        if (runtimeNote.completed || note.type != sakura::game::NoteType::Circle)
            continue;

        float cx = MOUSE_X + note.x * MOUSE_W;
        float cy = MOUSE_Y + note.y * MOUSE_H;
        renderer.DrawCircleFilled(cx, cy, 0.020f, { 255, 175, 205, 235 }, 28);
        renderer.DrawCircleOutline(cx, cy,
            0.020f * CalcCircleScale(note.timeMs, displayTimeMs),
            { 255, 230, 245, 160 }, 0.0025f, 48);
    }

    auto [mouseX, mouseY] = sakura::core::Input::GetMousePosition();
    if (mouseX >= MOUSE_X && mouseX <= MOUSE_X + MOUSE_W
        && mouseY >= MOUSE_Y && mouseY <= MOUSE_Y + MOUSE_H)
    {
        renderer.DrawCircleOutline(mouseX, mouseY, 0.012f,
            { 230, 235, 255, 220 }, 0.002f, 24);
    }

    renderer.DrawText(m_fontSmall,
        "Circle 容差：0.10",
        MOUSE_X, MOUSE_Y + MOUSE_H + 0.03f, 0.018f,
        { 175, 190, 215, 210 },
        sakura::core::TextAlign::Left);
}

void SceneTutorial::RenderPrompt(sakura::core::Renderer& renderer)
{
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f }, { 0, 0, 0, 140 });
    renderer.DrawRoundedRect({ 0.24f, 0.30f, 0.52f, 0.38f },
        0.018f, { 24, 20, 42, 248 }, true);
    renderer.DrawRoundedRect({ 0.24f, 0.30f, 0.52f, 0.38f },
        0.018f, { 120, 102, 170, 150 }, false);

    renderer.DrawText(m_fontTitle, "是否进入教程？",
        0.50f, 0.38f, 0.040f,
        { 245, 230, 255, 245 },
        sakura::core::TextAlign::Center);

    renderer.DrawText(m_fontText,
        "教程共 5 课，会依次说明 Tap / Hold / Drag / Circle / 综合配合。",
        0.50f, 0.45f, 0.022f,
        { 220, 215, 230, 220 },
        sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall,
        "也可以按 Esc 跳过，之后仍可从主菜单进入教程。",
        0.50f, 0.49f, 0.019f,
        { 180, 175, 195, 190 },
        sakura::core::TextAlign::Center);

    if (m_btnPromptYes) m_btnPromptYes->Render(renderer);
    if (m_btnPromptNo)  m_btnPromptNo->Render(renderer);
}

void SceneTutorial::OnRender(sakura::core::Renderer& renderer)
{
    renderer.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        kBgTop, kBgTop, kBgBottom, kBgBottom);
    renderer.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 0.45f },
        { 255, 150, 190, 26 }, { 255, 150, 190, 0 },
        { 70, 110, 220, 0 }, { 70, 110, 220, 0 });

    RenderHeader(renderer);
    RenderLessonOverlay(renderer);

    if (m_phase != Phase::Prompt)
    {
        RenderKeyboardArea(renderer);
        RenderMouseArea(renderer);
    }

    if (m_phase == Phase::Prompt)
        RenderPrompt(renderer);
}

} // namespace sakura::scene