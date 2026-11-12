// editor_mouse_area.cpp — 鼠标端音符编辑区实现

#include "editor_mouse_area.h"
#include "core/theme.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawGlassCard(renderer, { AREA_X, AREA_Y, AREA_W, AREA_H },
        { 14, 18, 34, 218 }, { 9, 12, 24, 238 },
        palette.gildedHairline, palette.surfaceBorder, 0.010f, 0.36f);

    // 4×4 辅助构图网格与中心十字准星
    for (int g = 1; g <= 3; ++g)
    {
        const float gx = AREA_X + AREA_W * (static_cast<float>(g) * 0.25f);
        const float gy = AREA_Y + AREA_H * (static_cast<float>(g) * 0.25f);
        const uint8_t alpha = (g == 2) ? 48 : 24;
        renderer.DrawLine(gx, AREA_Y + 0.008f, gx, AREA_Y + AREA_H - 0.008f, { 155, 148, 198, alpha }, 0.001f);
        renderer.DrawLine(AREA_X + 0.008f, gy, AREA_X + AREA_W - 0.008f, gy, { 155, 148, 198, alpha }, 0.001f);
    }

    // 区域标题文字与工具胶囊
    if (m_font != sakura::core::INVALID_HANDLE)
    {
        const NoteToolType tool = m_core.GetNoteTool();
        const bool mouseActive = (tool == NoteToolType::Circle || tool == NoteToolType::Slider);
        const char* hint =
            (tool == NoteToolType::Circle) ? "左键放置 Circle  ·  点击已有音符选中" :
            (tool == NoteToolType::Slider) ? "左键添加折点  ·  右键 / Enter 完成 Slider" :
            "按 3 / 4 切换 Circle / Slider 工具在此编辑";

        sakura::ui::VisualStyle::DrawBadgePill(renderer,
            { AREA_X + AREA_W * 0.5f - 0.075f, AREA_Y + 0.012f, 0.150f, 0.028f },
            "2D 鼠标音符画布", m_font, 0.0145f,
            mouseActive ? sakura::core::Color{ 44, 30, 62, 205 } : sakura::core::Color{ 22, 26, 46, 185 },
            mouseActive ? palette.primary : palette.surfaceBorder,
            mouseActive ? palette.accent : palette.textDim);
        renderer.DrawText(m_font, hint,
            AREA_X + AREA_W * 0.5f, AREA_Y + 0.045f, 0.0145f,
            mouseActive ? palette.text : palette.textMuted,
            sakura::core::TextAlign::Center);
    }
}

// ── DrawMouseNotes ────────────────────────────────────────────────────────────
void EditorMouseArea::DrawMouseNotes(sakura::core::Renderer& renderer)
{
    const auto& palette     = sakura::core::Theme::GetInstance().Colors();
    const auto& notes       = m_core.GetChartData().mouseNotes;
    const int   selectedIdx = m_core.GetSelectedMouseNote();
    const int   curTimeMs   = m_core.GetCurrentTimeMs();

    // 时间窗口：当前时间 ±3000ms 的音符才显示
    constexpr int TIME_WINDOW = 3000;

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& n = notes[i];
        const int diffMs = n.time - curTimeMs;
        if (std::abs(diffMs) > TIME_WINDOW) continue;

        const float proximity = 1.0f - std::clamp(static_cast<float>(std::abs(diffMs)) / static_cast<float>(TIME_WINDOW), 0.0f, 1.0f);
        const uint8_t baseAlpha = static_cast<uint8_t>(65 + 180 * proximity);
        const float sx = ToScreenX(n.x);
        const float sy = ToScreenY(n.y);
        const bool isSelected = (i == selectedIdx);

        if (n.type == sakura::game::NoteType::Circle)
        {
            const sakura::core::Color coreCol = isSelected
                ? palette.accent
                : sakura::core::Color{ palette.circleColor.r, palette.circleColor.g, palette.circleColor.b, baseAlpha };

            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            renderer.DrawRadialGradient(sx, sy, CIRCLE_R * 1.65f, CIRCLE_R * 1.65f,
                { coreCol.r, coreCol.g, coreCol.b, static_cast<uint8_t>(baseAlpha * 0.42f) },
                { coreCol.r, coreCol.g, coreCol.b, 0 });
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

            // 时间接近缩圈预览
            if (diffMs >= 0)
            {
                const float approachScale = 1.0f + std::clamp(static_cast<float>(diffMs) / 1500.0f, 0.0f, 1.4f);
                const float ar = CIRCLE_R * 0.72f * approachScale;
                renderer.DrawRingGradient(sx, sy, ar - 0.0014f, ar + 0.0018f,
                    { palette.accent.r, palette.accent.g, palette.accent.b, static_cast<uint8_t>(baseAlpha * 0.65f) },
                    { palette.primary.r, palette.primary.g, palette.primary.b, 0 }, 36);
            }

            const float hitR = CIRCLE_R * 0.72f;
            renderer.DrawCircleFilled(sx, sy, hitR, coreCol, 32);
            renderer.DrawCircleFilled(sx, sy, hitR * 0.42f, { 255, 246, 250, baseAlpha }, 20);
            renderer.DrawCircleOutline(sx, sy, hitR,
                isSelected ? sakura::core::Color{ 255, 255, 255, 250 } : sakura::core::Color{ 255, 240, 248, baseAlpha },
                isSelected ? 0.0024f : 0.0016f, 36);

            if (isSelected)
            {
                renderer.DrawRingGradient(sx, sy, hitR + 0.004f, hitR + 0.009f,
                    palette.accent, { palette.accent.r, palette.accent.g, palette.accent.b, 0 }, 36);
            }
        }
        else if (n.type == sakura::game::NoteType::Slider)
        {
            if (n.sliderPath.size() >= 2)
            {
                std::vector<sakura::core::RibbonNode> ribbon;
                ribbon.reserve(n.sliderPath.size());
                const sakura::core::Color spineCol = isSelected
                    ? sakura::core::Color{ palette.accent.r, palette.accent.g, palette.accent.b, std::max<uint8_t>(baseAlpha, 215) }
                    : sakura::core::Color{ 175, 235, 255, baseAlpha };
                const sakura::core::Color edgeCol = isSelected
                    ? sakura::core::Color{ palette.primary.r, palette.primary.g, palette.primary.b, 60 }
                    : sakura::core::Color{ palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 42 };

                for (const auto& pt : n.sliderPath)
                {
                    ribbon.push_back({
                        ToScreenX(pt.first), ToScreenY(pt.second), 0.0095f, spineCol, edgeCol
                    });
                }
                renderer.DrawRibbonStrip(ribbon.data(), static_cast<int>(ribbon.size()), true);

                for (size_t p = 1; p < n.sliderPath.size(); ++p)
                {
                    const float wx = ToScreenX(n.sliderPath[p].first);
                    const float wy = ToScreenY(n.sliderPath[p].second);
                    const bool isTail = (p + 1 == n.sliderPath.size());
                    renderer.DrawCircleFilled(wx, wy, isTail ? 0.0075f : 0.0052f, spineCol, 22);
                    renderer.DrawCircleOutline(wx, wy, isTail ? 0.0092f : 0.0068f,
                        { 255, 255, 255, static_cast<uint8_t>(baseAlpha * 0.8f) }, 0.0012f, 22);
                }

                const float startX = ToScreenX(n.sliderPath.front().first);
                const float startY = ToScreenY(n.sliderPath.front().second);
                const float headR  = CIRCLE_R * 0.68f;
                renderer.DrawCircleFilled(startX, startY, headR,
                    isSelected ? palette.accent : sakura::core::Color{ palette.primary.r, palette.primary.g, palette.primary.b, baseAlpha }, 32);
                renderer.DrawCircleFilled(startX, startY, headR * 0.42f, { 255, 248, 252, baseAlpha }, 20);
                renderer.DrawCircleOutline(startX, startY, headR, { 255, 255, 255, baseAlpha }, 0.0018f, 32);
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
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    std::vector<sakura::core::RibbonNode> ribbon;
    ribbon.reserve(wip->sliderPath.size() + 1);
    for (const auto& pt : wip->sliderPath)
    {
        ribbon.push_back({
            ToScreenX(pt.first), ToScreenY(pt.second), 0.0095f,
            { 140, 255, 205, 225 }, { 80, 220, 175, 45 }
        });
    }
    if (ribbon.size() >= 2)
        renderer.DrawRibbonStrip(ribbon.data(), static_cast<int>(ribbon.size()), true);

    // 最后一个点到鼠标悬停位置的虚实引导线
    if (m_hoverNX >= 0.0f && !wip->sliderPath.empty())
    {
        const float lastX = ToScreenX(wip->sliderPath.back().first);
        const float lastY = ToScreenY(wip->sliderPath.back().second);
        const float hoverSX = ToScreenX(m_hoverNX);
        const float hoverSY = ToScreenY(m_hoverNY);
        renderer.DrawLine(lastX, lastY, hoverSX, hoverSY,
            { 140, 255, 205, 150 }, 0.0022f);
    }

    for (size_t idx = 0; idx < wip->sliderPath.size(); ++idx)
    {
        const float px = ToScreenX(wip->sliderPath[idx].first);
        const float py = ToScreenY(wip->sliderPath[idx].second);
        const float r  = (idx == 0) ? (CIRCLE_R * 0.62f) : 0.0065f;
        renderer.DrawCircleFilled(px, py, r, { 130, 250, 195, 235 }, 24);
        renderer.DrawCircleOutline(px, py, r + 0.002f, { 255, 255, 255, 220 }, 0.0014f, 24);
    }

    if (m_font != sakura::core::INVALID_HANDLE)
    {
        const std::string hint = "正在绘制 Slider · 已添加 " + std::to_string(wip->sliderPath.size())
                               + " 个锚点 (右键 / Enter 完成)";
        sakura::ui::VisualStyle::DrawBadgePill(renderer,
            { AREA_X + 0.035f, AREA_Y + AREA_H - 0.042f, AREA_W - 0.070f, 0.030f },
            hint, m_font, 0.014f, { 18, 42, 38, 220 }, { 120, 245, 190, 210 }, palette.text);
    }
}

// ── DrawHoverPreview ──────────────────────────────────────────────────────────
void EditorMouseArea::DrawHoverPreview(sakura::core::Renderer& renderer)
{
    if (m_hoverNX < 0.0f) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    const float sx = ToScreenX(m_hoverNX);
    const float sy = ToScreenY(m_hoverNY);

    const NoteToolType tool = m_core.GetNoteTool();
    if (tool == NoteToolType::Circle)
    {
        const float r = CIRCLE_R * 0.72f;
        renderer.DrawRingGradient(sx, sy, r - 0.002f, r + 0.003f,
            { palette.circleColor.r, palette.circleColor.g, palette.circleColor.b, 165 },
            { palette.primary.r, palette.primary.g, palette.primary.b, 20 }, 36);
        renderer.DrawCircleFilled(sx, sy, r * 0.35f,
            { palette.accent.r, palette.accent.g, palette.accent.b, 160 }, 20);
    }
    else if (tool == NoteToolType::Slider)
    {
        renderer.DrawRingGradient(sx, sy, 0.006f, 0.010f,
            { 130, 250, 195, 185 }, { 130, 250, 195, 20 }, 28);
        renderer.DrawCircleFilled(sx, sy, 0.0035f, { 240, 255, 248, 200 }, 16);
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
                // 如果有进行中的 Slider，先取消它
                if (m_core.HasWipSlider())
                    m_core.CancelSlider();
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
