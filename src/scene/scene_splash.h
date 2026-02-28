#pragma once

// scene_splash.h — 启动画面场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"

namespace sakura::scene
{

// SceneSplash — 启动动画
// 流程：淡入(0.8s) → 停留(1.5s) → 淡出(0.8s) → 切换到主菜单
class SceneSplash final : public Scene
{
public:
    explicit SceneSplash(SceneManager& mgr);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    // 动画阶段
    enum class Phase { FadeIn, Hold, FadeOut, Done };

    SceneManager& m_manager;

    Phase m_phase   = Phase::FadeIn;
    float m_timer   = 0.0f;          // 当前阶段计时器（秒）
    float m_opacity = 0.0f;          // logo 整体透明度(0~1)

    // "Loading..." 闪烁计时器
    float m_blinkTimer  = 0.0f;
    bool  m_blinkVisible = true;

    // 字体句柄
    sakura::core::FontHandle m_fontTitle = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSub   = sakura::core::INVALID_HANDLE;

    // 各阶段时长（秒）
    static constexpr float FADE_IN_DURATION  = 0.8f;
    static constexpr float HOLD_DURATION     = 1.5f;
    static constexpr float FADE_OUT_DURATION = 0.8f;
    static constexpr float BLINK_INTERVAL    = 0.5f;

    // 预加载全局资源（字体、通用 UI）
    void PreloadResources();

    // 切换到目标场景（Step 1.8 → SceneLoading，Step 1.9 将改为 SceneMenu）
    void GoToNextScene();
};

} // namespace sakura::scene
