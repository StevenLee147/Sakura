#include "window.h"
#include "utils/logger.h"

namespace sakura::core
{

Window::Window() = default;

Window::~Window()
{
    Destroy();
}

bool Window::Create(const std::string& title, int width, int height)
{
    if (m_window)
    {
        LOG_WARN("Window::Create 调用时窗口已存在，先销毁旧窗口");
        Destroy();
    }

    m_window = SDL_CreateWindow(
        title.c_str(),
        width, height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!m_window)
    {
        LOG_ERROR("SDL_CreateWindow 失败: {}", SDL_GetError());
        return false;
    }

    UpdateSize();
    LOG_INFO("窗口 \"{}\" 创建成功 ({}x{})", title, m_width, m_height);
    return true;
}

void Window::Destroy()
{
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        LOG_DEBUG("窗口已销毁");
    }
}

void Window::ToggleFullscreen()
{
    SetFullscreen(!m_fullscreen);
}

void Window::SetFullscreen(bool fullscreen)
{
    if (!m_window)
    {
        LOG_WARN("Window::SetFullscreen: 窗口无效");
        return;
    }

    if (!SDL_SetWindowFullscreen(m_window, fullscreen))
    {
        LOG_ERROR("SDL_SetWindowFullscreen 失败: {}", SDL_GetError());
        return;
    }

    m_fullscreen = fullscreen;
    UpdateSize();
    LOG_INFO("全屏模式: {}", m_fullscreen ? "开启" : "关闭");
}

bool Window::HandleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        {
            UpdateSize();
            LOG_DEBUG("窗口尺寸变化: {}x{}", m_width, m_height);
            return false; // 不消费，允许其他系统也收到
        }
        case SDL_EVENT_KEY_DOWN:
        {
            // F11 切换全屏
            if (event.key.scancode == SDL_SCANCODE_F11)
            {
                ToggleFullscreen();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

void Window::UpdateSize()
{
    if (m_window)
    {
        SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    }
}

} // namespace sakura::core
