#pragma once

#include <SDL3/SDL.h>
#include <utility>

namespace sakura::core
{

// 鼠标归一化位置 (0.0~1.0)
struct MousePos
{
    float x = 0.0f;
    float y = 0.0f;
};

// 鼠标像素位置
struct MousePixelPos
{
    int x = 0;
    int y = 0;
};

// Input — 全局静态输入管理器
// 生命周期：每帧由 App 驱动：ProcessEvent() → Update()
class Input
{
public:
    // ── 事件处理（每帧在 PollEvent 循环中调用）──────────────────────────────
    static void ProcessEvent(const SDL_Event& event);

    // ── 帧末重置 pressed/released 状态 ────────────────────────────────────────
    static void Update();

    // ── 屏幕尺寸（用于归一化鼠标坐标）────────────────────────────────────────
    static void SetScreenSize(int width, int height);

    // ── 键盘查询 ──────────────────────────────────────────────────────────────

    // 本帧刚按下（仅一帧为 true）
    static bool IsKeyPressed(SDL_Scancode code);

    // 持续按住中（按下后每帧为 true）
    static bool IsKeyHeld(SDL_Scancode code);

    // 本帧刚松开（仅一帧为 true）
    static bool IsKeyReleased(SDL_Scancode code);

    // ── 鼠标查询（button: 1=左键，2=中键，3=右键，4/5=侧键）────────────────

    static bool IsMouseButtonPressed(int button);
    static bool IsMouseButtonHeld(int button);
    static bool IsMouseButtonReleased(int button);

    // ── 鼠标位置 ──────────────────────────────────────────────────────────────

    // 归一化坐标 (0.0~1.0)
    static MousePos      GetMousePosition();

    // 像素坐标
    static MousePixelPos GetMousePixelPosition();

    // ── 鼠标滚轮 ──────────────────────────────────────────────────────────────

    // 本帧滚轮 Y 轴增量（向上为正）
    static float GetMouseWheelDelta();

private:
    static constexpr int KEY_COUNT    = SDL_SCANCODE_COUNT;
    static constexpr int MOUSE_BUTTON_MAX = 6;  // 支持最多 5 个鼠标按键 (index 1~5)

    // 键盘状态：当帧、上帧
    static bool s_currKeys[KEY_COUNT];
    static bool s_prevKeys[KEY_COUNT];

    // 鼠标按键状态：当帧、上帧
    static bool s_currMouse[MOUSE_BUTTON_MAX];
    static bool s_prevMouse[MOUSE_BUTTON_MAX];

    // 鼠标像素位置
    static float s_mousePixelX;
    static float s_mousePixelY;

    // 屏幕尺寸（由 App 每帧设置）
    static int   s_screenWidth;
    static int   s_screenHeight;

    // 滚轮增量（每帧重置）
    static float s_wheelDelta;
};

} // namespace sakura::core
