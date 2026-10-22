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

    // 固定时间步长（秒）：> 0 时每帧以该值驱动全部逻辑并跳过帧率限制睡眠
    // （离线录制使用虚拟时间）；<= 0 恢复为实时帧间隔
    void SetFixedTimestep(float seconds) { m_fixedTimestep = seconds > 0.0f ? seconds : 0.0f; }
    float GetFixedTimestep() const { return m_fixedTimestep; }

    // 访问器
    Window&        GetWindow()    { return m_window; }
    const Window&  GetWindow()    const { return m_window; }
    Renderer&      GetRenderer()  { return m_renderer; }
    const Renderer& GetRenderer() const { return m_renderer; }
    const Timer&   GetTimer()     const { return m_timer; }
    double GetCpuFrameMs() const { return m_updateCpuMs+m_renderCpuMs; }
    sakura::scene::SceneManager& GetSceneManager() { return m_sceneManager; }

protected:
    // 子类可覆盖以扩展逻辑
    virtual void OnUpdate(float dt);
    virtual void OnRender();
    virtual void OnEvent(const SDL_Event& event);

    // 帧钩子：OnFrameBegin 在 Render() 最开始（BeginFrame 之前）调用；
    // OnFrameEnd 在最后一次 Flush() 之后、EndFrame()（Present）之前调用
    virtual void OnFrameBegin();
    virtual void OnFrameEnd();

private:
    void ProcessEvents();
    void Update(float dt);
    void Render();

    Window   m_window;
    Renderer m_renderer;
    Timer    m_timer;
    bool     m_running = false;
    bool m_shutdown = false;
    double m_updateCpuMs=0,m_renderCpuMs=0;
    int m_appliedVSync = -1;
    int m_appliedWidth = 0, m_appliedHeight = 0;
    float m_fixedTimestep = 0.0f;

    // 场景管理器
    sakura::scene::SceneManager m_sceneManager;

    // FPS 日志间隔
    float m_fpsLogTimer = 0.0f;
    static constexpr float FPS_LOG_INTERVAL = 3.0f;
};

} // namespace sakura::core
