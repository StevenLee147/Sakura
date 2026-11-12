// scene_tutorial.cpp — 新手教程场景

#include "scene_tutorial.h"

#include "scene_menu.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/input.h"
#include "core/theme.h"
#include "utils/logger.h"
#include "ui/visual_style.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sakura::scene
{
namespace
{

int CalcSliderWaypointTimeMs(const sakura::game::TutorialLessonNote& note,
                             int waypointCount,
                             int waypointIndex)
{
    if (waypointCount <= 0)
        return note.timeMs;

    float ratio = static_cast<float>(waypointIndex + 1)
        / static_cast<float>(waypointCount);
    return note.timeMs + static_cast<int>(std::lround(
        static_cast<float>(note.durationMs) * ratio));
}
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
    for(int i=0;i<4;++i)m_laneKeys[i]=static_cast<SDL_Scancode>(sakura::core::Config::GetInstance().Get<int>("input.key_lane_"+std::to_string(i),m_laneKeys[i]));
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
    m_btnPromptYes = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.34f, 0.56f, 0.14f, 0.055f },
        "进入教程", m_fontText, 0.024f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnPromptYes.get(), sakura::ui::ButtonVariant::Primary);
    m_btnPromptYes->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnPromptYes->SetOnClick([this]() { ResolvePrompt(true); });

    m_btnPromptNo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.52f, 0.56f, 0.14f, 0.055f },
        "暂时跳过", m_fontText, 0.024f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnPromptNo.get(), sakura::ui::ButtonVariant::Secondary);
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
        m_runtimeNotes.push_back(RuntimeNote{ note });

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
        note.sliderNextWaypointIndex = 0;
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
    if (lane < 0 || lane >= 4) return "?";
    return SDL_GetScancodeName(m_laneKeys[lane]);
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

        case sakura::game::NoteType::Slider:
            if (!runtimeNote.started)
            {
                allCompleted = false;
                if (m_lessonTimerMs > static_cast<float>(note.timeMs + lesson.judgeWindowMs))
                {
                    FailCurrentLesson("Slide 需要先在起点按下鼠标左键。");
                    return;
                }
            }
            else if (!runtimeNote.completed)
            {
                allCompleted = false;

                bool held = sakura::core::Input::IsMouseButtonHeld(SDL_BUTTON_LEFT);
                if (!held && m_lessonTimerMs < static_cast<float>(note.timeMs + note.durationMs))
                {
                    FailCurrentLesson("Slide 中途松开了。\n按住鼠标左键并沿着路径继续滑动。");
                    return;
                }

                if (note.sliderPath.empty())
                {
                    runtimeNote.completed = true;
                    break;
                }

                auto [mouseX, mouseY] = sakura::core::Input::GetMousePosition();
                float localX = (mouseX - MOUSE_X) / MOUSE_W;
                float localY = (mouseY - MOUSE_Y) / MOUSE_H;

                if (runtimeNote.sliderNextWaypointIndex < static_cast<int>(note.sliderPath.size()))
                {
                    int waypointIndex = runtimeNote.sliderNextWaypointIndex;
                    int waypointTime = CalcSliderWaypointTimeMs(note,
                        static_cast<int>(note.sliderPath.size()),
                        waypointIndex);

                    if (held)
                    {
                        const auto& waypoint = note.sliderPath[waypointIndex];
                        float dx = localX - waypoint.first;
                        float dy = localY - waypoint.second;
                        float distance = std::sqrt(dx * dx + dy * dy);
                        if (distance <= lesson.mouseTolerance)
                        {
                            ++runtimeNote.sliderNextWaypointIndex;
                            sakura::audio::AudioManager::GetInstance().PlayHitsound(
                                sakura::audio::HitsoundType::Circle);
                            if (runtimeNote.sliderNextWaypointIndex
                                >= static_cast<int>(note.sliderPath.size()))
                            {
                                runtimeNote.completed = true;
                            }
                        }
                    }

                    if (!runtimeNote.completed
                        && m_lessonTimerMs > static_cast<float>(waypointTime + lesson.judgeWindowMs))
                    {
                        FailCurrentLesson("Slide 要按住鼠标，并按顺序滑过每个发光节点。");
                        return;
                    }
                }

                if (!runtimeNote.completed
                    && m_lessonTimerMs > static_cast<float>(note.timeMs + note.durationMs + lesson.judgeWindowMs))
                {
                    FailCurrentLesson("Slide 终点慢了一点，再沿路径滑一次。");
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

        if (runtimeNote.completed || runtimeNote.started) continue;
        if (note.type != sakura::game::NoteType::Tap
            && note.type != sakura::game::NoteType::Hold)
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
        if (runtimeNote.completed)
            continue;
        if (note.type != sakura::game::NoteType::Circle
            && (note.type != sakura::game::NoteType::Slider || runtimeNote.started))
        {
            continue;
        }

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
        auto& runtimeNote = m_runtimeNotes[bestIndex];
        if (runtimeNote.note.type == sakura::game::NoteType::Circle)
        {
            runtimeNote.completed = true;
            sakura::audio::AudioManager::GetInstance().PlayHitsound(
                sakura::audio::HitsoundType::Circle);
        }
        else if (runtimeNote.note.type == sakura::game::NoteType::Slider)
        {
            runtimeNote.started = true;
            runtimeNote.sliderNextWaypointIndex = 0;
            if (runtimeNote.note.sliderPath.empty())
                runtimeNote.completed = true;
            sakura::audio::AudioManager::GetInstance().PlayHitsound(
                sakura::audio::HitsoundType::SliderStart);
        }
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
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.08f, 0.024f, 0.13f, 0.026f }, "HOW TO PLAY",
        m_fontSmall, 0.013f, { 36, 28, 56, 195 }, palette.gildedHairline, palette.accent);
    renderer.DrawText(m_fontTitle, "双域演奏教程",
        0.08f, 0.056f, 0.042f, palette.text, sakura::core::TextAlign::Left);

    if (m_phase != Phase::Prompt)
    {
        renderer.DrawText(m_fontText, CurrentLesson().title,
            0.08f, 0.106f, 0.026f, palette.accent, sakura::core::TextAlign::Left);
        sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.76f, 0.052f, 0.12f, 0.036f },
            "第 " + std::to_string(m_currentLesson + 1) + " / " + std::to_string(m_lessons.size()) + " 课",
            m_fontSmall, 0.017f, { 26, 30, 52, 210 }, palette.gildedHairline, palette.text);
    }

    renderer.DrawText(m_fontSmall, "ESC 返回主菜单",
        0.88f, 0.106f, 0.0175f, palette.textDim, sakura::core::TextAlign::Right);
}

void SceneTutorial::RenderLessonOverlay(sakura::core::Renderer& renderer) const
{
    if (m_phase == Phase::Prompt) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    sakura::ui::VisualStyle::DrawPanel(renderer, { 0.08f, 0.148f, 0.80f, 0.068f }, false, true);
    renderer.DrawText(m_fontSmall, CurrentLesson().instruction,
        0.105f, 0.172f, 0.0205f, palette.text, sakura::core::TextAlign::Left);

    std::string footer;
    sakura::core::Color pillBorder = palette.gildedHairline;
    sakura::core::Color pillText   = palette.accent;
    switch (m_phase)
    {
    case Phase::Intro:
        footer = "按 [Space] 开始本课演练";
        break;
    case Phase::Playing:
        footer = "跟随节拍与缩圈完成全部音符目标";
        pillBorder = palette.primary;
        pillText   = palette.text;
        break;
    case Phase::Failed:
        footer = m_statusText + "  ·  按 [Space] 重新尝试";
        pillBorder = { 255, 115, 130, 225 };
        pillText   = { 255, 195, 205, 245 };
        break;
    case Phase::Success:
        footer = m_statusText + "  ·  按 [Space] 进入下一课";
        pillBorder = { 120, 235, 180, 225 };
        pillText   = { 205, 255, 230, 245 };
        break;
    case Phase::Finished:
        footer = m_statusText + "  ·  按 [Space] 返回主菜单";
        break;
    default:
        break;
    }

    sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.28f, 0.922f, 0.44f, 0.044f },
        footer, m_fontSmall, 0.0185f, { 22, 26, 46, 215 }, pillBorder, pillText);
}

void SceneTutorial::RenderKeyboardArea(sakura::core::Renderer& renderer) const
{
    const auto& lesson = CurrentLesson();
    if (!lesson.usesKeyboard) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    sakura::ui::VisualStyle::DrawGlassCard(renderer, { TRACK_X, TRACK_Y, TRACK_W, TRACK_H },
        { 14, 18, 34, 216 }, { 10, 12, 24, 236 }, palette.gildedHairline, palette.surfaceBorder, 0.014f, 0.45f);

    const float laneWidth = TRACK_W / 4.0f;
    const float displayTimeMs = (m_phase == Phase::Playing) ? m_lessonTimerMs : 0.0f;

    for (int lane = 0; lane < 4; ++lane)
    {
        const float x = TRACK_X + static_cast<float>(lane) * laneWidth;
        const sakura::core::Color laneTint = (lane % 2 == 0)
            ? sakura::core::Color{ 26, 30, 52, 95 }
            : sakura::core::Color{ 18, 22, 40, 70 };
        renderer.DrawFilledRect({ x + 0.001f, TRACK_Y + 0.004f, laneWidth - 0.002f, TRACK_H - 0.008f }, laneTint);
        if (lane > 0)
            renderer.DrawLine(x, TRACK_Y + 0.006f, x, TRACK_Y + TRACK_H - 0.006f, { 155, 148, 195, 48 }, 0.001f);

        // 键帽徽章
        const sakura::core::NormRect capRect{ x + laneWidth * 0.16f, JUDGE_LINE_Y + 0.026f, laneWidth * 0.68f, 0.038f };
        renderer.DrawRoundedGradientRect(capRect, 0.008f,
            { 34, 38, 66, 220 }, { 34, 38, 66, 220 }, { 22, 24, 44, 235 }, { 22, 24, 44, 235 });
        renderer.DrawRoundedRectOutlineGradient(capRect, 0.008f,
            palette.laneColors[lane], palette.surfaceBorder, 0.0014f);
        renderer.DrawText(m_fontText, LaneLabel(lane),
            x + laneWidth * 0.5f, JUDGE_LINE_Y + 0.032f, 0.023f,
            palette.text, sakura::core::TextAlign::Center);
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(TRACK_X + TRACK_W * 0.5f, JUDGE_LINE_Y, TRACK_W * 0.55f, 0.018f,
        { palette.primary.r, palette.primary.g, palette.primary.b, 85 },
        { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    renderer.DrawLine(TRACK_X - 0.006f, JUDGE_LINE_Y, TRACK_X + TRACK_W + 0.006f, JUDGE_LINE_Y,
        palette.accent, 0.0036f);

    for (const auto& runtimeNote : m_runtimeNotes)
    {
        const auto& note = runtimeNote.note;
        if (runtimeNote.completed) continue;
        if (note.type != sakura::game::NoteType::Tap
            && note.type != sakura::game::NoteType::Hold)
            continue;

        const float laneX = TRACK_X + laneWidth * static_cast<float>(note.lane) + laneWidth * 0.10f;
        const float noteW = laneWidth * 0.80f;
        const float noteY = CalcKeyboardY(note.timeMs, displayTimeMs);
        const sakura::core::Color laneCol = palette.laneColors[std::clamp(note.lane, 0, 3)];

        if (note.type == sakura::game::NoteType::Hold)
        {
            const float endY = CalcKeyboardY(note.timeMs + note.durationMs, displayTimeMs);
            const float bodyY = std::min(noteY, endY);
            const float bodyH = std::max(0.022f, std::abs(endY - noteY));
            renderer.DrawRoundedGradientRect(
                { laneX + noteW * 0.22f, bodyY, noteW * 0.56f, bodyH }, 0.008f,
                { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 205 },
                { 180, 235, 255, 220 },
                { palette.primary.r, palette.primary.g, palette.primary.b, 175 },
                { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 205 });
            renderer.DrawLine(laneX + noteW * 0.50f, bodyY + 0.004f, laneX + noteW * 0.50f, bodyY + bodyH - 0.004f,
                { 245, 252, 255, 185 }, 0.0022f);

            if (runtimeNote.started && !runtimeNote.completed)
            {
                const float progress = std::clamp(
                    (m_lessonTimerMs - static_cast<float>(note.timeMs))
                    / static_cast<float>(std::max(note.durationMs, 1)),
                    0.0f, 1.0f);
                renderer.DrawRoundedRect({ TRACK_X + 0.04f, 0.888f, 0.26f, 0.016f },
                    0.006f, { 20, 24, 42, 225 }, true);
                renderer.DrawRoundedGradientRect({ TRACK_X + 0.04f, 0.888f, 0.26f * progress, 0.016f },
                    0.006f, palette.holdColor, palette.accent, palette.holdColor, palette.accent);
                renderer.DrawText(m_fontSmall, "长按保持进度",
                    TRACK_X + 0.04f, 0.862f, 0.0165f, palette.textDim, sakura::core::TextAlign::Left);
            }
        }

        const sakura::core::NormRect gemRect{ laneX, noteY - 0.016f, noteW, 0.032f };
        renderer.DrawSoftShadow(gemRect, 0.008f, 0.008f, { laneCol.r, laneCol.g, laneCol.b, 95 }, 0.0f);
        renderer.DrawRoundedGradientRect(gemRect, 0.008f,
            { 255, 235, 246, 248 }, laneCol, laneCol, palette.primary);
        renderer.DrawRoundedRectOutlineGradient(gemRect, 0.008f,
            { 255, 255, 255, 225 }, palette.accent, 0.0015f);
        renderer.DrawText(m_fontSmall, LaneLabel(note.lane),
            laneX + noteW * 0.5f, noteY - 0.009f, 0.017f,
            { 24, 16, 36, 255 }, sakura::core::TextAlign::Center);
    }

    renderer.DrawText(m_fontSmall, "左侧 4K 键盘轨道 (A / S / D / F)",
        TRACK_X, TRACK_Y + TRACK_H + 0.074f, 0.0165f, palette.textMuted, sakura::core::TextAlign::Left);
}

void SceneTutorial::RenderMouseArea(sakura::core::Renderer& renderer) const
{
    const auto& lesson = CurrentLesson();
    if (!lesson.usesMouse) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    sakura::ui::VisualStyle::DrawGlassCard(renderer, { MOUSE_X, MOUSE_Y, MOUSE_W, MOUSE_H },
        { 14, 18, 34, 216 }, { 10, 12, 24, 236 }, palette.gildedHairline, palette.surfaceBorder, 0.014f, 0.45f);

    // 柔和定位参考网格
    for (int g = 1; g <= 3; ++g)
    {
        const float gx = MOUSE_X + MOUSE_W * (static_cast<float>(g) * 0.25f);
        const float gy = MOUSE_Y + MOUSE_H * (static_cast<float>(g) * 0.25f);
        renderer.DrawLine(gx, MOUSE_Y + 0.01f, gx, MOUSE_Y + MOUSE_H - 0.01f, { 145, 140, 185, 26 }, 0.001f);
        renderer.DrawLine(MOUSE_X + 0.01f, gy, MOUSE_X + MOUSE_W - 0.01f, gy, { 145, 140, 185, 26 }, 0.001f);
    }

    const float displayTimeMs = (m_phase == Phase::Playing) ? m_lessonTimerMs : 0.0f;
    for (const auto& runtimeNote : m_runtimeNotes)
    {
        const auto& note = runtimeNote.note;
        if (runtimeNote.completed)
            continue;

        const float cx = MOUSE_X + note.x * MOUSE_W;
        const float cy = MOUSE_Y + note.y * MOUSE_H;
        const float approachR = 0.021f * CalcCircleScale(note.timeMs, displayTimeMs);

        if (note.type == sakura::game::NoteType::Circle)
        {
            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            renderer.DrawRadialGradient(cx, cy, 0.038f, 0.038f,
                { palette.circleColor.r, palette.circleColor.g, palette.circleColor.b, 95 },
                { palette.circleColor.r, palette.circleColor.g, palette.circleColor.b, 0 });
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

            renderer.DrawRingGradient(cx, cy, approachR - 0.0018f, approachR + 0.0022f,
                palette.accent, { palette.primary.r, palette.primary.g, palette.primary.b, 35 }, 48);
            renderer.DrawCircleFilled(cx, cy, 0.021f, palette.circleColor, 36);
            renderer.DrawCircleFilled(cx, cy, 0.010f, { 255, 245, 250, 225 }, 24);
            renderer.DrawCircleOutline(cx, cy, 0.021f, { 255, 250, 252, 240 }, 0.0022f, 40);
        }
        else if (note.type == sakura::game::NoteType::Slider)
        {
            // 使用折线丝带 DrawRibbonStrip 保证拐角与线性判定点 1:1 对齐
            std::vector<sakura::core::RibbonNode> ribbon;
            ribbon.reserve(note.sliderPath.size() + 1);
            ribbon.push_back({
                cx, cy, 0.012f,
                { 185, 240, 255, 225 },
                { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 45 }
            });
            for (const auto& pt : note.sliderPath)
            {
                ribbon.push_back({
                    MOUSE_X + pt.first * MOUSE_W,
                    MOUSE_Y + pt.second * MOUSE_H,
                    0.012f,
                    { 185, 240, 255, 225 },
                    { palette.primary.r, palette.primary.g, palette.primary.b, 45 }
                });
            }
            if (ribbon.size() >= 2)
                renderer.DrawRibbonStrip(ribbon.data(), static_cast<int>(ribbon.size()), true);

            for (int pointIndex = 0; pointIndex < static_cast<int>(note.sliderPath.size()); ++pointIndex)
            {
                const auto& point = note.sliderPath[pointIndex];
                const float pointX = MOUSE_X + point.first * MOUSE_W;
                const float pointY = MOUSE_Y + point.second * MOUSE_H;

                const bool reached = runtimeNote.started
                    && pointIndex < runtimeNote.sliderNextWaypointIndex;
                const bool nextPoint = runtimeNote.started && !runtimeNote.completed
                    && pointIndex == runtimeNote.sliderNextWaypointIndex;
                const sakura::core::Color pointColor = reached
                    ? palette.accent
                    : (nextPoint
                        ? sakura::core::Color{ 130, 245, 255, 245 }
                        : sakura::core::Color{ 195, 235, 255, 195 });
                const float pr = nextPoint ? 0.014f : 0.0105f;
                renderer.DrawCircleFilled(pointX, pointY, pr, pointColor, 28);
                renderer.DrawCircleOutline(pointX, pointY, pr + 0.002f, { 255, 255, 255, 210 }, 0.0015f, 28);
            }

            renderer.DrawRingGradient(cx, cy, approachR - 0.0018f, approachR + 0.0022f,
                palette.accent, { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 35 }, 48);
            renderer.DrawCircleFilled(cx, cy, 0.021f, palette.primary, 36);
            renderer.DrawCircleFilled(cx, cy, 0.009f, { 255, 248, 252, 235 }, 24);
            renderer.DrawCircleOutline(cx, cy, 0.021f, palette.accent, 0.0022f, 40);

            if (runtimeNote.started && !runtimeNote.completed)
            {
                renderer.DrawRoundedRect({ MOUSE_X + 0.04f, 0.888f, 0.26f, 0.016f },
                    0.006f, { 20, 24, 42, 225 }, true);
                float progress = 0.0f;
                if (!note.sliderPath.empty())
                {
                    progress = static_cast<float>(runtimeNote.sliderNextWaypointIndex)
                        / static_cast<float>(note.sliderPath.size());
                }
                renderer.DrawRoundedGradientRect(
                    { MOUSE_X + 0.04f, 0.888f, 0.26f * std::clamp(progress, 0.0f, 1.0f), 0.016f },
                    0.006f, palette.holdColor, palette.accent, palette.holdColor, palette.accent);
                renderer.DrawText(m_fontSmall, "丝带滑条引导进度",
                    MOUSE_X + 0.04f, 0.862f, 0.0165f, palette.textDim, sakura::core::TextAlign::Left);
            }
        }
    }

    auto [mouseX, mouseY] = sakura::core::Input::GetMousePosition();
    if (mouseX >= MOUSE_X && mouseX <= MOUSE_X + MOUSE_W
        && mouseY >= MOUSE_Y && mouseY <= MOUSE_Y + MOUSE_H)
    {
        renderer.DrawRingGradient(mouseX, mouseY, 0.009f, 0.014f,
            palette.accent, { palette.primary.r, palette.primary.g, palette.primary.b, 30 }, 32);
        renderer.DrawCircleFilled(mouseX, mouseY, 0.0032f, { 255, 250, 245, 240 }, 16);
    }

    const bool hasSlider = std::any_of(m_runtimeNotes.begin(), m_runtimeNotes.end(),
        [](const RuntimeNote& runtimeNote)
        {
            return runtimeNote.note.type == sakura::game::NoteType::Slider;
        });
    renderer.DrawText(m_fontSmall,
        hasSlider ? "右侧鼠标域：按住左键沿丝带滑向发光节点" : "右侧鼠标域：缩圈与圆心重合时左键点击",
        MOUSE_X, MOUSE_Y + MOUSE_H + 0.032f, 0.017f, palette.textDim, sakura::core::TextAlign::Left);
}

void SceneTutorial::RenderPrompt(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawScrim(renderer, 0.66f);
    sakura::ui::VisualStyle::DrawPanel(renderer, { 0.24f, 0.29f, 0.52f, 0.40f }, true, true);

    sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.43f, 0.320f, 0.14f, 0.028f }, "WELCOME",
        m_fontSmall, 0.0135f, { 36, 28, 56, 200 }, palette.gildedHairline, palette.accent);
    renderer.DrawText(m_fontTitle, "开启新手引导？",
        0.50f, 0.365f, 0.038f, palette.text, sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.31f, 0.69f, 0.420f, 0.72f);
    renderer.DrawText(m_fontText,
        "共 5 节互动课程，带你掌握 Tap、Hold、Circle、Slide 与双域协同。",
        0.50f, 0.445f, 0.021f, palette.text, sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall,
        "按 ESC 可暂时跳过，随时可从主菜单「新手教程」重新进入。",
        0.50f, 0.490f, 0.018f, palette.textDim, sakura::core::TextAlign::Center);

    if (m_btnPromptYes) m_btnPromptYes->Render(renderer);
    if (m_btnPromptNo)  m_btnPromptNo->Render(renderer);
}

void SceneTutorial::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);

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
