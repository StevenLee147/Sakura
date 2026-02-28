#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace sakura::core
{

// 封装 SDL_Window，提供窗口生命周期管理与全屏切换
class Window
{
public:
    Window();
    ~Window();

    // 不允许拷贝
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 创建窗口，返回是否成功
    bool Create(const std::string& title, int width, int height);

    // 销毁窗口
    void Destroy();

    // 切换全屏/窗口模式
    void ToggleFullscreen();

    // 设置全屏状态
    void SetFullscreen(bool fullscreen);

    // 处理窗口相关事件（尺寸变化、全屏热键等）
    // 返回 true 表示事件被消费
    bool HandleEvent(const SDL_Event& event);

    // 当前窗口像素宽度
    int GetWidth()  const { return m_width; }

    // 当前窗口像素高度
    int GetHeight() const { return m_height; }

    // 是否处于全屏模式
    bool IsFullscreen() const { return m_fullscreen; }

    // 原生 SDL_Window 指针
    SDL_Window* GetSDLWindow() const { return m_window; }

    // 是否有效（已创建）
    bool IsValid() const { return m_window != nullptr; }

private:
    void UpdateSize();

    SDL_Window* m_window     = nullptr;
    int         m_width      = 0;
    int         m_height     = 0;
    bool        m_fullscreen = false;
};

} // namespace sakura::core
