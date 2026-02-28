#pragma once

#include "scene.h"
#include "core/renderer.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

namespace sakura::scene
{

// 场景切换过渡动画类型
enum class TransitionType
{
    None,           // 无过渡
    Fade,           // 淡入淡出
    SlideLeft,      // 当前场景向左滑出，新场景从右滑入
    SlideRight,     // 当前场景向右滑出，新场景从左滑入
    SlideUp,        // 当前场景向上滑出，新场景从下滑入
    SlideDown,      // 当前场景向下滑出，新场景从上滑入
    Scale,          // 新场景从中心缩出
    CircleWipe,     // 圆形遮罩展开
};

// SceneManager — 管理场景栈与切换动画
class SceneManager
{
public:
    SceneManager();
    ~SceneManager();

    // ── 场景操作 ──────────────────────────────────────────────────────────────

    // 替换当前场景（如指定了过渡类型，播放切换动画）
    void SwitchScene(std::unique_ptr<Scene> newScene,
                     TransitionType transition = TransitionType::Fade,
                     float durationSec = 0.5f);

    // 压入场景（保留当前场景，新场景在栈顶）
    void PushScene(std::unique_ptr<Scene> newScene,
                   TransitionType transition = TransitionType::Fade,
                   float durationSec = 0.4f);

    // 弹出当前栈顶场景，回到下层场景
    void PopScene(TransitionType transition = TransitionType::Fade,
                  float durationSec = 0.4f);

    // ── 主循环委托 ────────────────────────────────────────────────────────────

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);
    void HandleEvent(const SDL_Event& event);

    // ── 状态查询 ──────────────────────────────────────────────────────────────

    bool IsEmpty() const { return m_sceneStack.empty() && !m_pendingScene; }
    bool IsTransitioning() const { return m_isTransitioning; }

    // 获取当前顶部场景（可能为 nullptr）
    Scene* GetCurrentScene() const;

private:
    // 执行挂起的场景切换
    void ApplyPendingSwitch();

    // 过渡动画渲染
    void RenderTransition(sakura::core::Renderer& renderer);

    // ── 场景栈 ────────────────────────────────────────────────────────────────
    std::vector<std::unique_ptr<Scene>> m_sceneStack;

    // ── 待切换场景 ────────────────────────────────────────────────────────────
    std::unique_ptr<Scene> m_pendingScene;
    bool                   m_pendingIsPush    = false;  // true=push, false=switch
    bool                   m_pendingIsPop     = false;  // true=pop

    // ── 过渡状态 ──────────────────────────────────────────────────────────────
    bool           m_isTransitioning = false;
    TransitionType m_transitionType  = TransitionType::None;
    float          m_transitionTimer    = 0.0f;
    float          m_transitionDuration = 0.5f;

    // 过渡用的离屏纹理（当前、下一场景）
    SDL_Texture*   m_texFrom = nullptr;  // 源场景快照
    SDL_Texture*   m_texTo   = nullptr;  // 目标场景（实时渲染）
};

} // namespace sakura::scene
