#pragma once

// editor_mouse_area.h — 鼠标端音符编辑区（Circle/Slider）
// 区域：屏幕归一化坐标 (0.42, 0.06, 0.33, 0.60)
//
// 操作方式：
//   Circle 工具：左键单击放置 Circle 音符
//   Slider 工具：左键逐点添加路径，右键完成 / 双击完成
//
// 鼠标音符用当前编辑器时间轴的 m_core.GetCurrentTimeMs() 作为时间戳。

#include "core/renderer.h"
#include "core/resource_manager.h"
#include "editor_core.h"

#include <SDL3/SDL.h>

namespace sakura::editor
{

class EditorMouseArea
{
public:
    explicit EditorMouseArea(EditorCore& core);
    ~EditorMouseArea() = default;

    void SetFont(sakura::core::FontHandle font) { m_font = font; }

    void Render     (sakura::core::Renderer& renderer);
    bool HandleEvent(const SDL_Event& event);      // returns true if consumed

    // 布局常量（与 scene_editor 保持一致）
    static constexpr float AREA_X = 0.42f;
    static constexpr float AREA_Y = 0.06f;
    static constexpr float AREA_W = 0.33f;
    static constexpr float AREA_H = 0.60f;

private:
    EditorCore& m_core;
    sakura::core::FontHandle m_font = sakura::core::INVALID_HANDLE;

    // 鼠标悬停位置（放置预览）
    float m_hoverNX = -1.0f;   // 归一化到区域内 [0,1]
    float m_hoverNY = -1.0f;

    // 是否在区域内
    bool IsInArea(float screenX, float screenY) const
    {
        return screenX >= AREA_X && screenX < AREA_X + AREA_W
            && screenY >= AREA_Y && screenY < AREA_Y + AREA_H;
    }

    // 屏幕坐标 → 区域内归一化坐标
    float ToNX(float screenX) const { return (screenX - AREA_X) / AREA_W; }
    float ToNY(float screenY) const { return (screenY - AREA_Y) / AREA_H; }

    // 区域内归一化坐标 → 屏幕坐标
    float ToScreenX(float nx) const { return AREA_X + nx * AREA_W; }
    float ToScreenY(float ny) const { return AREA_Y + ny * AREA_H; }

    // 音符接近圈渲染半径（归一化）
    static constexpr float CIRCLE_R = 0.022f;   // 相对于整个屏幕宽度

    void DrawBackground  (sakura::core::Renderer& renderer);
    void DrawMouseNotes  (sakura::core::Renderer& renderer);
    void DrawWipSlider   (sakura::core::Renderer& renderer);
    void DrawHoverPreview(sakura::core::Renderer& renderer);
};

} // namespace sakura::editor
