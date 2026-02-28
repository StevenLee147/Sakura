#include "input.h"
#include "utils/logger.h"

#include <cstring>

namespace sakura::core
{

// ── 静态成员初始化 ─────────────────────────────────────────────────────────────

bool  Input::s_currKeys[Input::KEY_COUNT]          = {};
bool  Input::s_prevKeys[Input::KEY_COUNT]          = {};
bool  Input::s_currMouse[Input::MOUSE_BUTTON_MAX]  = {};
bool  Input::s_prevMouse[Input::MOUSE_BUTTON_MAX]  = {};
float Input::s_mousePixelX  = 0.0f;
float Input::s_mousePixelY  = 0.0f;
int   Input::s_screenWidth  = 1280;
int   Input::s_screenHeight = 720;
float Input::s_wheelDelta   = 0.0f;

// ── 事件处理 ──────────────────────────────────────────────────────────────────

void Input::ProcessEvent(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_KEY_DOWN:
        {
            SDL_Scancode code = event.key.scancode;
            if (code >= 0 && code < KEY_COUNT)
            {
                s_currKeys[code] = true;
            }
            break;
        }
        case SDL_EVENT_KEY_UP:
        {
            SDL_Scancode code = event.key.scancode;
            if (code >= 0 && code < KEY_COUNT)
            {
                s_currKeys[code] = false;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            int btn = event.button.button;
            if (btn >= 1 && btn < MOUSE_BUTTON_MAX)
            {
                s_currMouse[btn] = true;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            int btn = event.button.button;
            if (btn >= 1 && btn < MOUSE_BUTTON_MAX)
            {
                s_currMouse[btn] = false;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            s_mousePixelX = event.motion.x;
            s_mousePixelY = event.motion.y;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            s_wheelDelta += event.wheel.y;
            break;
        }
        default:
            break;
    }
}

// ── 帧末重置 ──────────────────────────────────────────────────────────────────

void Input::Update()
{
    // 保存当帧状态为"上帧"
    std::memcpy(s_prevKeys,  s_currKeys,  sizeof(s_currKeys));
    std::memcpy(s_prevMouse, s_currMouse, sizeof(s_currMouse));

    // 重置滚轮增量（每帧使用完后清零）
    s_wheelDelta = 0.0f;
}

// ── 屏幕尺寸 ──────────────────────────────────────────────────────────────────

void Input::SetScreenSize(int width, int height)
{
    if (width  > 0) s_screenWidth  = width;
    if (height > 0) s_screenHeight = height;
}

// ── 键盘查询 ──────────────────────────────────────────────────────────────────

bool Input::IsKeyPressed(SDL_Scancode code)
{
    if (code < 0 || code >= KEY_COUNT) return false;
    return s_currKeys[code] && !s_prevKeys[code];
}

bool Input::IsKeyHeld(SDL_Scancode code)
{
    if (code < 0 || code >= KEY_COUNT) return false;
    return s_currKeys[code];
}

bool Input::IsKeyReleased(SDL_Scancode code)
{
    if (code < 0 || code >= KEY_COUNT) return false;
    return !s_currKeys[code] && s_prevKeys[code];
}

// ── 鼠标按键 ──────────────────────────────────────────────────────────────────

bool Input::IsMouseButtonPressed(int button)
{
    if (button < 1 || button >= MOUSE_BUTTON_MAX) return false;
    return s_currMouse[button] && !s_prevMouse[button];
}

bool Input::IsMouseButtonHeld(int button)
{
    if (button < 1 || button >= MOUSE_BUTTON_MAX) return false;
    return s_currMouse[button];
}

bool Input::IsMouseButtonReleased(int button)
{
    if (button < 1 || button >= MOUSE_BUTTON_MAX) return false;
    return !s_currMouse[button] && s_prevMouse[button];
}

// ── 鼠标位置 ──────────────────────────────────────────────────────────────────

MousePos Input::GetMousePosition()
{
    return {
        s_mousePixelX / static_cast<float>(s_screenWidth),
        s_mousePixelY / static_cast<float>(s_screenHeight)
    };
}

MousePixelPos Input::GetMousePixelPosition()
{
    return {
        static_cast<int>(s_mousePixelX),
        static_cast<int>(s_mousePixelY)
    };
}

float Input::GetMouseWheelDelta()
{
    return s_wheelDelta;
}

} // namespace sakura::core
