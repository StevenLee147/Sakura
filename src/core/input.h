#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <string_view>

namespace sakura::core
{

// ── 辅助结构体 ────────────────────────────────────────────────────────────────

// 鼠标归一化位置／增量 (0.0~1.0 / 帧间差值)
struct MousePos
{
    float x = 0.0f;
    float y = 0.0f;
};

// 鼠标像素位置／增量
struct MousePixelPos
{
    int x = 0;
    int y = 0;
};

// ============================================================================
// Input — 全局静态输入管理器
// 生命周期：每帧由 App 驱动：ProcessEvent → (场景/逻辑使用) → Update()
// ============================================================================
class Input
{
public:
    // ── 生命周期 ──────────────────────────────────────────────────────────────

    // 事件处理（每帧在 SDL_PollEvent 循环内调用，可多次）
    static void ProcessEvent(const SDL_Event& event);

    // 帧末重置 pressed/released 状态，清空帧间增量
    // 必须在本帧所有输入查询完成后调用（App::Update 末尾）
    static void Update();

    // 屏幕尺寸（用于归一化鼠标坐标，每帧由 App 同步）
    static void SetScreenSize(int width, int height);

    // ── 日志开关 ──────────────────────────────────────────────────────────────

    // 开启后每次 pressed/released 均输出 LOG_DEBUG（默认关闭，调试用）
    static void SetDebugLogging(bool enabled) { s_debugLog = enabled; }
    static bool IsDebugLogging()              { return s_debugLog; }

    // ── 键盘查询 ──────────────────────────────────────────────────────────────

    // 本帧刚按下（仅一帧 true，repeat 事件不触发）
    static bool IsKeyPressed(SDL_Scancode code);

    // 持续按住中（按下后每帧 true，含 repeat 帧）
    static bool IsKeyHeld(SDL_Scancode code);

    // 本帧刚松开（仅一帧 true）
    static bool IsKeyReleased(SDL_Scancode code);

    // 任意按键本帧刚按下
    static bool IsAnyKeyPressed();

    // 任意按键持续按住
    static bool IsAnyKeyHeld();

    // 本帧最后一次按下的键（无则 SDL_SCANCODE_UNKNOWN）
    static SDL_Scancode GetLastPressedKey()           { return s_lastPressedKey; }

    // SDL 键名（英文）— 用于调试输出 / 按键绑定 UI
    static const char*  GetKeyName(SDL_Scancode code) { return SDL_GetScancodeName(code); }

    // ── 鼠标按键（button: 1=左，2=中，3=右，4/5=侧键）──────────────────────

    static bool IsMouseButtonPressed(int button);
    static bool IsMouseButtonHeld(int button);
    static bool IsMouseButtonReleased(int button);

    // 任意鼠标键本帧刚按下
    static bool IsAnyMouseButtonPressed();

    // ── 鼠标位置与增量 ────────────────────────────────────────────────────────

    // 当前归一化位置 (0.0~1.0)
    static MousePos      GetMousePosition();

    // 当前像素位置
    static MousePixelPos GetMousePixelPosition();

    // 本帧归一化鼠标移动量（拖拽检测 / Slider 跟踪）
    static MousePos      GetMouseDelta();

    // 本帧像素鼠标移动量
    static MousePixelPos GetMousePixelDelta();

    // ── 鼠标滚轮 ──────────────────────────────────────────────────────────────

    // 本帧 Y 轴增量（向上/向前为正），Update() 后清零
    static float GetMouseWheelDelta() { return s_wheelDelta; }

    // ── 文字输入（用于 TextInput 组件）────────────────────────────────────────

    // 本帧通过 SDL_EVENT_TEXT_INPUT 输入的字符串（UTF-8，可能为空）
    static std::string_view GetTextInput()  { return s_textInput; }

    // 清空文字输入缓冲（不希望被其他系统重复消费时手动调用）
    static void ClearTextInput()            { s_textInput.clear(); }

private:
    static constexpr int KEY_COUNT        = SDL_SCANCODE_COUNT;
    static constexpr int MOUSE_BUTTON_MAX = 6;   // 支持最多 5 个按键 (index 1~5)

    // 键盘状态：当帧 / 上帧
    static bool s_currKeys[KEY_COUNT];
    static bool s_prevKeys[KEY_COUNT];

    // 鼠标按键：当帧 / 上帧
    static bool s_currMouse[MOUSE_BUTTON_MAX];
    static bool s_prevMouse[MOUSE_BUTTON_MAX];

    // 鼠标像素位置（当前帧）
    static float s_mousePixelX;
    static float s_mousePixelY;

    // 鼠标帧间像素增量（每帧 MOUSE_MOTION 累加，Update 清零）
    static float s_mouseDeltaX;
    static float s_mouseDeltaY;

    // 屏幕尺寸
    static int   s_screenWidth;
    static int   s_screenHeight;

    // 滚轮 Y 轴增量（每帧累加，Update 清零）
    static float s_wheelDelta;

    // 本帧最后一次按下的键
    static SDL_Scancode s_lastPressedKey;

    // 文字输入缓冲（每帧清空）
    static std::string  s_textInput;

    // 调试日志开关
    static bool         s_debugLog;
};

} // namespace sakura::core
