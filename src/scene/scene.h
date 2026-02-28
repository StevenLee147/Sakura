#pragma once

#include <SDL3/SDL.h>

// 前向声明，避免循环包含
namespace sakura::core { class Renderer; }

namespace sakura::scene
{

// Scene — 所有场景的纯虚基类
// 场景生命周期：OnEnter → (OnUpdate + OnRender + OnEvent 循环) → OnExit
class Scene
{
public:
    virtual ~Scene() = default;

    // ── 生命周期虚函数 ────────────────────────────────────────────────────────

    // 场景进入时调用（场景切换完成后）
    virtual void OnEnter() = 0;

    // 场景退出时调用（开始切换到下一场景时）
    virtual void OnExit() = 0;

    // 游戏逻辑更新（固定时间步长）
    // dt: 本次更新的时间步长（秒）
    virtual void OnUpdate(float dt) = 0;

    // 渲染本帧
    // renderer: 当前帧的渲染器
    virtual void OnRender(sakura::core::Renderer& renderer) = 0;

    // 事件处理
    // event: SDL 事件
    virtual void OnEvent(const SDL_Event& event) = 0;

    // ── 可选覆盖 ──────────────────────────────────────────────────────────────

    // 场景是否透明（Push 时下方场景仍然渲染）
    virtual bool IsTransparent() const { return false; }

    // 场景是否暂停更新（Push 时下方场景逻辑仍然运行）
    virtual bool IsPaused() const { return false; }
};

} // namespace sakura::scene
