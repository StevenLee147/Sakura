#pragma once

#include <SDL3/SDL.h>
#include "timer.h"
#include "window.h"
#include "renderer.h"
#include "input.h"
#include "resource_manager.h"
#include "scene/scene_manager.h"

namespace sakura::core
{

// 应用程序主类，管理生命周期与主循环
class App
{
public:
    App();
    ~App();

    // 不允许拷贝
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // 初始化所有子系统，成功返回 true
    bool Initialize();

    // 启动主循环（阻塞直到退出）
    void Run();

    // 资源清理
    void Shutdown();

    // 请求退出（线程安全）
    void RequestQuit() { m_running = false; }

    // 访问器
    Window&        GetWindow()    { return m_window; }
    const Window&  GetWindow()    const { return m_window; }
    Renderer&      GetRenderer()  { return m_renderer; }
    const Renderer& GetRenderer() const { return m_renderer; }
    const Timer&   GetTimer()     const { return m_timer; }
    sakura::scene::SceneManager& GetSceneManager() { return m_sceneManager; }

protected:
    // 子类可覆盖以扩展逻辑
    virtual void OnUpdate(float dt);
    virtual void OnRender();
    virtual void OnEvent(const SDL_Event& event);

private:
    void ProcessEvents();
    void Update(float dt);
    void Render();

    Window   m_window;
    Renderer m_renderer;
    Timer    m_timer;
    bool     m_running = false;

    // 场景管理器
    sakura::scene::SceneManager m_sceneManager;

    // 固定时间步长（60Hz）
    static constexpr double FIXED_TIMESTEP = 1.0 / 60.0;
    // 最大累计步数（防止 spiral of death）
    static constexpr int    MAX_STEPS      = 5;

    double m_accumulator = 0.0;

    // FPS 日志间隔
    float m_fpsLogTimer = 0.0f;
    static constexpr float FPS_LOG_INTERVAL = 3.0f;
};

} // namespace sakura::core
