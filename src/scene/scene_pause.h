#pragma once

// scene_pause.h — 暂停菜单场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "game/game_state.h"

#include <memory>

namespace sakura::scene
{

// ScenePause — 暂停菜单（Push 到场景栈上，下层场景继续渲染）
// - 半透明黑遮罩 + 居中面板
// - "继续" / "重新开始" / "返回选歌"
// - ESC 继续
class ScenePause final : public Scene
{
public:
    ScenePause(SceneManager& mgr,
               sakura::game::GameState& gameState);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

    // 暂停菜单是否透明（下层游戏场景仍然渲染）
    bool IsTransparent() const override { return true; }
    bool IsPaused()      const override { return true; }

private:
    SceneManager&              m_manager;
    sakura::game::GameState&   m_gameState;

    sakura::core::FontHandle   m_fontUI = sakura::core::INVALID_HANDLE;

    std::unique_ptr<sakura::ui::Button> m_btnResume;
    std::unique_ptr<sakura::ui::Button> m_btnRestart;
    std::unique_ptr<sakura::ui::Button> m_btnBack;

    void Resume();
};

} // namespace sakura::scene
