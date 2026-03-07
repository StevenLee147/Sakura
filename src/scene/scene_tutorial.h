#pragma once

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "game/tutorial_data.h"
#include "ui/button.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace sakura::scene
{

class SceneTutorial final : public Scene
{
public:
    explicit SceneTutorial(SceneManager& mgr, bool showFirstRunPrompt = false);

    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    enum class Phase
    {
        Prompt,
        Intro,
        Playing,
        Failed,
        Success,
        Finished,
    };

    struct RuntimeNote
    {
        sakura::game::TutorialLessonNote note;
        bool started   = false;
        bool completed = false;
        std::vector<int> dragPathLanes;
        int dragNextLaneIndex = 1;
    };

    SceneManager& m_manager;
    bool          m_showFirstRunPrompt = false;
    Phase         m_phase              = Phase::Intro;

    std::vector<sakura::game::TutorialLesson> m_lessons;
    std::vector<RuntimeNote>                  m_runtimeNotes;
    int                                       m_currentLesson = 0;
    float                                     m_lessonTimerMs = 0.0f;
    std::string                               m_statusText;

    sakura::core::FontHandle m_fontTitle = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontText  = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSmall = sakura::core::INVALID_HANDLE;

    std::unique_ptr<sakura::ui::Button> m_btnPromptYes;
    std::unique_ptr<sakura::ui::Button> m_btnPromptNo;

    std::array<SDL_Scancode, 4> m_laneKeys = {
        SDL_SCANCODE_A,
        SDL_SCANCODE_S,
        SDL_SCANCODE_D,
        SDL_SCANCODE_F,
    };

    static constexpr float TRACK_X      = 0.08f;
    static constexpr float TRACK_Y      = 0.24f;
    static constexpr float TRACK_W      = 0.34f;
    static constexpr float TRACK_H      = 0.56f;
    static constexpr float JUDGE_LINE_Y = 0.78f;
    static constexpr float MOUSE_X      = 0.54f;
    static constexpr float MOUSE_Y      = 0.24f;
    static constexpr float MOUSE_W      = 0.34f;
    static constexpr float MOUSE_H      = 0.56f;
    static constexpr float APPROACH_MS  = 2200.0f;

    void SetupPromptButtons();
    void BeginLesson(int lessonIndex);
    void StartCurrentLesson();
    void ResolvePrompt(bool enterTutorial);
    void ReturnToMenu();
    void MarkPromptShown();
    void MarkTutorialCompleted();
    void FailCurrentLesson(const std::string& reason);
    void CompleteCurrentLesson();

    const sakura::game::TutorialLesson& CurrentLesson() const;
    float CalcKeyboardY(int noteTimeMs, float lessonTimeMs) const;
    float CalcCircleScale(int noteTimeMs, float lessonTimeMs) const;
    int   LaneFromScancode(SDL_Scancode scancode) const;
    const char* LaneLabel(int lane) const;

    void HandleKeyboardInput(SDL_Scancode scancode);
    void HandleMouseInput(float screenX, float screenY);
    void UpdatePlayingState();

    void RenderHeader(sakura::core::Renderer& renderer) const;
    void RenderLessonOverlay(sakura::core::Renderer& renderer) const;
    void RenderKeyboardArea(sakura::core::Renderer& renderer) const;
    void RenderMouseArea(sakura::core::Renderer& renderer) const;
    void RenderPrompt(sakura::core::Renderer& renderer);
};

} // namespace sakura::scene