#include "input.h"
#include "utils/logger.h"

#include <cstring>

namespace sakura::core
{

// ── 静态成员定义 ──────────────────────────────────────────────────────────────

bool         Input::s_currKeys[Input::KEY_COUNT]         = {};
bool         Input::s_prevKeys[Input::KEY_COUNT]         = {};
bool         Input::s_currMouse[Input::MOUSE_BUTTON_MAX] = {};
bool         Input::s_prevMouse[Input::MOUSE_BUTTON_MAX] = {};
float        Input::s_mousePixelX   = 0.0f;
float        Input::s_mousePixelY   = 0.0f;
float        Input::s_mouseDeltaX   = 0.0f;
float        Input::s_mouseDeltaY   = 0.0f;
int          Input::s_screenWidth   = 1920;
int          Input::s_screenHeight  = 1080;
float        Input::s_wheelDelta    = 0.0f;
SDL_Scancode Input::s_lastPressedKey = SDL_SCANCODE_UNKNOWN;
std::string  Input::s_textInput;
bool         Input::s_debugLog      = false;

// ── 事件处理 ──────────────────────────────────────────────────────────────────

void Input::ProcessEvent(const SDL_Event& event)
{
    switch (event.type)
    {
        // ── 键盘 ──────────────────────────────────────────────────────────────
        case SDL_EVENT_KEY_DOWN:
        {
            const SDL_Scancode code = event.key.scancode;
            if (code < 0 || code >= KEY_COUNT) break;

            // repeat=true 表示系统级连发事件，不视为新的 Pressed，跳过
            if (!event.key.repeat)
            {
                s_currKeys[code]  = true;
                s_lastPressedKey  = code;

                if (s_debugLog)
                {
                    LOG_DEBUG("[Input] 按键按下: {} (scancode={})",
                        SDL_GetScancodeName(code), static_cast<int>(code));
                }
            }
            break;
        }
        case SDL_EVENT_KEY_UP:
        {
            const SDL_Scancode code = event.key.scancode;
            if (code < 0 || code >= KEY_COUNT) break;

            s_currKeys[code] = false;

            if (s_debugLog)
            {
                LOG_DEBUG("[Input] 按键松开: {} (scancode={})",
                    SDL_GetScancodeName(code), static_cast<int>(code));
            }
            break;
        }

        // ── 鼠标按键 ──────────────────────────────────────────────────────────
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            const int btn = event.button.button;
            if (btn < 1 || btn >= MOUSE_BUTTON_MAX) break;

            s_currMouse[btn] = true;

            if (s_debugLog)
            {
                const float nx = (s_screenWidth  > 0) ? s_mousePixelX / static_cast<float>(s_screenWidth)  : 0.0f;
                const float ny = (s_screenHeight > 0) ? s_mousePixelY / static_cast<float>(s_screenHeight) : 0.0f;
                LOG_DEBUG("[Input] 鼠标键按下: {} @ 像素({},{}) 归一化({:.3f},{:.3f})",
                    btn,
                    static_cast<int>(s_mousePixelX), static_cast<int>(s_mousePixelY),
                    nx, ny);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const int btn = event.button.button;
            if (btn < 1 || btn >= MOUSE_BUTTON_MAX) break;

            s_currMouse[btn] = false;

            if (s_debugLog)
            {
                LOG_DEBUG("[Input] 鼠标键松开: {}", btn);
            }
            break;
        }

        // ── 鼠标移动 ──────────────────────────────────────────────────────────
        case SDL_EVENT_MOUSE_MOTION:
        {
            // 同一帧可能有多次 MOTION 事件，累加相对增量
            s_mouseDeltaX += event.motion.xrel;
            s_mouseDeltaY += event.motion.yrel;
            s_mousePixelX  = event.motion.x;
            s_mousePixelY  = event.motion.y;
            break;
        }

        // ── 滚轮 ──────────────────────────────────────────────────────────────
        case SDL_EVENT_MOUSE_WHEEL:
        {
            // SDL3 wheel.y: 正 = 向上/向前
            s_wheelDelta += event.wheel.y;
            break;
        }

        // ── 文字输入（TextInput 组件使用）────────────────────────────────────
        case SDL_EVENT_TEXT_INPUT:
        {
            s_textInput += event.text.text;
            break;
        }

        default:
            break;
    }
}

// ── 帧末重置 ──────────────────────────────────────────────────────────────────

void Input::Update()
{
    // 当帧状态存入"上帧"
    std::memcpy(s_prevKeys,  s_currKeys,  sizeof(s_currKeys));
    std::memcpy(s_prevMouse, s_currMouse, sizeof(s_currMouse));

    // 清空帧间增量与临时缓冲
    s_mouseDeltaX    = 0.0f;
    s_mouseDeltaY    = 0.0f;
    s_wheelDelta     = 0.0f;
    s_lastPressedKey = SDL_SCANCODE_UNKNOWN;
    s_textInput.clear();
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
    // curr=true 且 prev=false → 本帧首次按下（非 repeat）
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

bool Input::IsAnyKeyPressed()
{
    for (int i = 0; i < KEY_COUNT; ++i)
    {
        if (s_currKeys[i] && !s_prevKeys[i]) return true;
    }
    return false;
}

bool Input::IsAnyKeyHeld()
{
    for (int i = 0; i < KEY_COUNT; ++i)
    {
        if (s_currKeys[i]) return true;
    }
    return false;
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

bool Input::IsAnyMouseButtonPressed()
{
    for (int i = 1; i < MOUSE_BUTTON_MAX; ++i)
    {
        if (s_currMouse[i] && !s_prevMouse[i]) return true;
    }
    return false;
}

// ── 鼠标位置与增量 ────────────────────────────────────────────────────────────

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

MousePos Input::GetMouseDelta()
{
    return {
        s_mouseDeltaX / static_cast<float>(s_screenWidth),
        s_mouseDeltaY / static_cast<float>(s_screenHeight)
    };
}

MousePixelPos Input::GetMousePixelDelta()
{
    return {
        static_cast<int>(s_mouseDeltaX),
        static_cast<int>(s_mouseDeltaY)
    };
}

} // namespace sakura::core
