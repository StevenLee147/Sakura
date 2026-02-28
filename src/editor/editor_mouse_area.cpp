// editor_mouse_area.cpp — 鼠标端音符编辑区实现

#include "editor_mouse_area.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <string>

namespace sakura::editor
{

EditorMouseArea::EditorMouseArea(EditorCore& core)
    : m_core(core)
{
}

// ═════════════════════════════════════════════════════════════════════════════
// Render
// ═════════════════════════════════════════════════════════════════════════════

void EditorMouseArea::Render(sakura::core::Renderer& renderer)
{
    DrawBackground  (renderer);
    DrawMouseNotes  (renderer);
    DrawWipSlider   (renderer);
    DrawHoverPreview(renderer);
}

// ── DrawBackground ────────────────────────────────────────────────────────────

void EditorMouseArea::DrawBackground(sakura::core::Renderer& renderer)
{
    // 深色背景
    renderer.DrawFilledRect({ AREA_X, AREA_Y, AREA_W, AREA_H },
        sakura::core::Color{ 10, 8, 24, 210 });

    // 边框
    renderer.DrawLine(AREA_X,          AREA_Y,          AREA_X + AREA_W, AREA_Y,
        sakura::core::Color{ 60, 50, 100, 130 }, 0.001f);
    renderer.DrawLine(AREA_X,          AREA_Y + AREA_H, AREA_X + AREA_W, AREA_Y + AREA_H,
        sakura::core::Color{ 60, 50, 100, 130 }, 0.001f);
    renderer.DrawLine(AREA_X,          AREA_Y,          AREA_X,          AREA_Y + AREA_H,
        sakura::core::Color{ 60, 50, 100, 130 }, 0.001f);
    renderer.DrawLine(AREA_X + AREA_W, AREA_Y,          AREA_X + AREA_W, AREA_Y + AREA_H,
        sakura::core::Color{ 60, 50, 100, 130 }, 0.001f);

    // 区域标题文字
    if (m_font != sakura::core::INVALID_HANDLE)
    {
        NoteToolType tool = m_core.GetNoteTool();
        const char* hint =
            (tool == NoteToolType::Circle) ? "左键: 放置 Circle 音符" :
            (tool == NoteToolType::Slider) ? "左键: 添加路径点  右键: 完成 Slider" :
            "切换工具 4/5 使用此区域";

        renderer.DrawText(m_font, "鼠标编辑区",
            AREA_X + AREA_W * 0.5f, AREA_Y + 0.020f, 0.020f,
            sakura::core::Color{ 160, 150, 200, 200 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_font, hint,
            AREA_X + AREA_W * 0.5f, AREA_Y + 0.044f, 0.015f,
            sakura::core::Color{ 120, 110, 160, 160 },
            sakura::core::TextAlign::Center);
    }
}

// ── DrawMouseNotes ────────────────────────────────────────────────────────────

void EditorMouseArea::DrawMouseNotes(sakura::core::Renderer& renderer)
{
    const auto& notes     = m_core.GetChartData().mouseNotes;
    int         selectedIdx = m_core.GetSelectedMouseNote();
    int         curTimeMs   = m_core.GetCurrentTimeMs();

    // 时间窗口：当前时间 ±3000ms 的音符才显示
    constexpr int TIME_WINDOW = 3000;

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& n = notes[i];
        if (std::abs(n.time - curTimeMs) > TIME_WINDOW) continue;

        float sx = ToScreenX(n.x);
        float sy = ToScreenY(n.y);

        bool isSelected = (i == selectedIdx);

        if (n.type == sakura::game::NoteType::Circle)
        {
            // 接近圈 + 中心点
            sakura::core::Color circleColor = isSelected
                ? sakura::core::Color{ 255, 255, 100, 230 }
                : sakura::core::Color{ 255, 80, 200, 200 };

            // 外圈
            renderer.DrawLine(sx - CIRCLE_R,     sy,
                              sx + CIRCLE_R,     sy,
                              circleColor, 0.001f);
            renderer.DrawLine(sx, sy - CIRCLE_R * (AREA_W / AREA_H),
                              sx, sy + CIRCLE_R * (AREA_W / AREA_H),
                              circleColor, 0.001f);
            // 中心填充小圆（用小矩形模拟）
            float half = CIRCLE_R * 0.25f;
            renderer.DrawFilledRect({ sx - half, sy - half * (AREA_W / AREA_H),
                                      half * 2.0f, half * 2.0f * (AREA_W / AREA_H) },
                circleColor);
            // 选中边框
            if (isSelected)
            {
                renderer.DrawFilledRect({ sx - CIRCLE_R, sy - CIRCLE_R * (AREA_W / AREA_H),
                                          CIRCLE_R * 2.0f, CIRCLE_R * 2.0f * (AREA_W / AREA_H) },
                    sakura::core::Color{ 255, 255, 100, 40 });
            }
        }
        else if (n.type == sakura::game::NoteType::Slider)
        {
            // 绘制路径线段
            if (n.sliderPath.size() >= 2)
            {
                for (int p = 0; p + 1 < static_cast<int>(n.sliderPath.size()); ++p)
                {
                    float ax = ToScreenX(n.sliderPath[p].first);
                    float ay = ToScreenY(n.sliderPath[p].second);
                    float bx = ToScreenX(n.sliderPath[p + 1].first);
                    float by = ToScreenY(n.sliderPath[p + 1].second);

                    sakura::core::Color sliderColor = isSelected
                        ? sakura::core::Color{ 255, 200, 80, 200 }
                        : sakura::core::Color{ 100, 200, 255, 180 };

                    renderer.DrawLine(ax, ay, bx, by, sliderColor, 0.003f);
                }
                // 起点标记
                float startX = ToScreenX(n.sliderPath.front().first);
                float startY = ToScreenY(n.sliderPath.front().second);
                renderer.DrawFilledRect({ startX - CIRCLE_R, startY - CIRCLE_R * (AREA_W / AREA_H),
                                          CIRCLE_R * 2.0f, CIRCLE_R * 2.0f * (AREA_W / AREA_H) },
                    sakura::core::Color{ 100, 200, 255, 130 });
            }
        }
    }
}

// ── DrawWipSlider ─────────────────────────────────────────────────────────────

void EditorMouseArea::DrawWipSlider(sakura::core::Renderer& renderer)
{
    if (!m_core.HasWipSlider()) return;

    const sakura::game::MouseNote* wip = m_core.GetWipSlider();
    if (!wip || wip->sliderPath.empty()) return;

    // 绘制已添加的路径段
    for (int p = 0; p + 1 < static_cast<int>(wip->sliderPath.size()); ++p)
    {
        float ax = ToScreenX(wip->sliderPath[p].first);
        float ay = ToScreenY(wip->sliderPath[p].second);
        float bx = ToScreenX(wip->sliderPath[p + 1].first);
        float by = ToScreenY(wip->sliderPath[p + 1].second);
        renderer.DrawLine(ax, ay, bx, by,
            sakura::core::Color{ 120, 255, 180, 180 }, 0.003f);
    }

    // 最后一个点到鼠标悬停位置的预览线
    if (m_hoverNX >= 0.0f && !wip->sliderPath.empty())
    {
        float lastX = ToScreenX(wip->sliderPath.back().first);
        float lastY = ToScreenY(wip->sliderPath.back().second);
        float hoverSX = ToScreenX(m_hoverNX);
        float hoverSY = ToScreenY(m_hoverNY);
        renderer.DrawLine(lastX, lastY, hoverSX, hoverSY,
            sakura::core::Color{ 120, 255, 180, 100 }, 0.002f);
    }

    // 路径点标记
    for (const auto& pt : wip->sliderPath)
    {
        float px = ToScreenX(pt.first);
        float py = ToScreenY(pt.second);
        float half = CIRCLE_R * 0.3f;
        renderer.DrawFilledRect({ px - half, py - half * (AREA_W / AREA_H),
                                  half * 2.0f, half * 2.0f * (AREA_W / AREA_H) },
            sakura::core::Color{ 120, 255, 180, 220 });
    }

    // 提示信息
    if (m_font != sakura::core::INVALID_HANDLE)
    {
        std::string hint = "已添加 " + std::to_string(wip->sliderPath.size())
                         + " 个路径点 (右键完成)";
        renderer.DrawText(m_font, hint.c_str(),
            AREA_X + AREA_W * 0.5f, AREA_Y + AREA_H - 0.025f, 0.015f,
            sakura::core::Color{ 120, 255, 180, 200 },
            sakura::core::TextAlign::Center);
    }
}

// ── DrawHoverPreview ──────────────────────────────────────────────────────────

void EditorMouseArea::DrawHoverPreview(sakura::core::Renderer& renderer)
{
    if (m_hoverNX < 0.0f) return;

    float sx = ToScreenX(m_hoverNX);
    float sy = ToScreenY(m_hoverNY);

    NoteToolType tool = m_core.GetNoteTool();

    if (tool == NoteToolType::Circle)
    {
        // 接近圈预览（半透明）
        renderer.DrawFilledRect(
            { sx - CIRCLE_R, sy - CIRCLE_R * (AREA_W / AREA_H),
              CIRCLE_R * 2.0f, CIRCLE_R * 2.0f * (AREA_W / AREA_H) },
            sakura::core::Color{ 255, 80, 200, 70 });
    }
    else if (tool == NoteToolType::Slider)
    {
        // Slider 路径点预览
        renderer.DrawFilledRect(
            { sx - CIRCLE_R * 0.35f, sy - CIRCLE_R * 0.35f * (AREA_W / AREA_H),
              CIRCLE_R * 0.7f, CIRCLE_R * 0.7f * (AREA_W / AREA_H) },
            sakura::core::Color{ 120, 255, 180, 100 });
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// HandleEvent
// ═════════════════════════════════════════════════════════════════════════════

bool EditorMouseArea::HandleEvent(const SDL_Event& event)
{
    // 更新鼠标悬停位置
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        SDL_Window* win = SDL_GetWindowFromID(event.motion.windowID);
        if (!win) return false;
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return false;

        float nx = event.motion.x / static_cast<float>(ww);
        float ny = event.motion.y / static_cast<float>(wh);

        if (IsInArea(nx, ny))
        {
            m_hoverNX = ToNX(nx);
            m_hoverNY = ToNY(ny);
        }
        else
        {
            m_hoverNX = -1.0f;
            m_hoverNY = -1.0f;
        }
        return false;
    }

    // 鼠标按下
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        SDL_Window* win = SDL_GetWindowFromID(event.button.windowID);
        if (!win) return false;
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return false;

        float nx = event.button.x / static_cast<float>(ww);
        float ny = event.button.y / static_cast<float>(wh);

        if (!IsInArea(nx, ny)) return false;

        float areaNX = ToNX(nx);
        float areaNY = ToNY(ny);
        int   timeMs = m_core.GetCurrentTimeMs();

        NoteToolType tool = m_core.GetNoteTool();

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            if (tool == NoteToolType::Circle)
            {
                // 先检查是否点击了已有音符
                int found = m_core.FindMouseNote(timeMs, areaNX, areaNY);
                if (found >= 0)
                {
                    m_core.SelectMouseNote(found);
                }
                else
                {
                    m_core.ClearSelection();
                    m_core.PlaceMouseNote(timeMs, areaNX, areaNY,
                        sakura::game::NoteType::Circle);
                }
                return true;
            }
            else if (tool == NoteToolType::Slider)
            {
                if (!m_core.HasWipSlider())
                {
                    // 开始 Slider
                    m_core.StartSlider(timeMs, areaNX, areaNY);
                }
                else
                {
                    // 追加路径点
                    m_core.AddSliderPoint(areaNX, areaNY);
                }
                return true;
            }
        }

        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (tool == NoteToolType::Slider && m_core.HasWipSlider())
            {
                // 右键完成 Slider
                m_core.FinalizeSlider();
                return true;
            }
            else if (tool == NoteToolType::Circle)
            {
                // 右键删除最近的鼠标音符
                int found = m_core.FindMouseNote(timeMs, areaNX, areaNY);
                if (found >= 0)
                {
                    m_core.DeleteMouseNote(found);
                }
                return true;
            }
        }

        // 双击完成 Slider
        if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks == 2)
        {
            if (tool == NoteToolType::Slider && m_core.HasWipSlider())
            {
                m_core.FinalizeSlider();
                return true;
            }
        }
    }

    return false;
}

} // namespace sakura::editor
