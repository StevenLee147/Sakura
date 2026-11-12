// editor_timeline.cpp — 键盘轨道编辑区渲染与交互

#include "editor_timeline.h"
#include "core/theme.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace sakura::editor
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

EditorTimeline::EditorTimeline(EditorCore& core)
    : m_core(core)
{
}

// ═════════════════════════════════════════════════════════════════════════════
// 坐标转换
// ═════════════════════════════════════════════════════════════════════════════

float EditorTimeline::TimeToY(int timeMs) const
{
    // 底部（y=1.0）对应 m_scrollTimeMs
    // 顶部（y=AREA_Y）对应 m_scrollTimeMs + m_viewDurationMs
    float t = static_cast<float>(timeMs - m_scrollTimeMs)
            / static_cast<float>(m_viewDurationMs);
    return 1.0f - t * AREA_H;
}

float EditorTimeline::YToTimeRaw(float y) const
{
    // y=1.0 → scrollTimeMs; y=AREA_Y → scrollTimeMs+viewDurationMs
    float t = (1.0f - y) / AREA_H;
    return static_cast<float>(m_scrollTimeMs)
         + t * static_cast<float>(m_viewDurationMs);
}

float EditorTimeline::LaneToX(int lane) const
{
    return TRACK_X + lane * TRACK_LANE_W;
}

float EditorTimeline::LaneWidth() const
{
    return TRACK_LANE_W;
}

int EditorTimeline::XToLane(float x) const
{
    if (x < TRACK_X || x > TRACK_X + TRACK_W)
        return -1;
    int lane = static_cast<int>((x - TRACK_X) / TRACK_LANE_W);
    if (lane < 0 || lane > 3) return -1;
    return lane;
}

bool EditorTimeline::IsInArea(float x, float y) const
{
    return x >= AREA_X && x < AREA_X + AREA_W
        && y >= AREA_Y && y < AREA_Y + AREA_H;
}

// ═════════════════════════════════════════════════════════════════════════════
// 视图控制
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::CenterOnTime(int timeMs)
{
    // 让 timeMs 出现在视图中间略偏下的位置
    m_scrollTimeMs = timeMs - m_viewDurationMs / 4;
}

void EditorTimeline::ZoomIn()
{
    m_viewDurationMs = std::max(500, m_viewDurationMs - 500);
}

void EditorTimeline::ZoomOut()
{
    m_viewDurationMs = std::min(16000, m_viewDurationMs + 500);
}

// ═════════════════════════════════════════════════════════════════════════════
// Update
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::Update(float /*dt*/)
{
    // 播放中：自动滚动，保持播放位置在视图下 1/4 处
    if (m_core.IsPlaying())
    {
        int currentMs    = m_core.GetCurrentTimeMs();
        int targetScroll = currentMs - m_viewDurationMs / 4;
        // 平滑追踪：每帧直接更新（因为是编辑器，精确度优先）
        m_scrollTimeMs = targetScroll;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// HandleEvent
// ═════════════════════════════════════════════════════════════════════════════

bool EditorTimeline::HandleEvent(const SDL_Event& event)
{
    // ── 鼠标移动 ─────────────────────────────────────────────────────────────
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
            m_lastMouseX = nx;
            m_lastMouseY = ny;
            m_hoverLane  = XToLane(nx);
            if (m_hoverLane >= 0)
            {
                int rawTime   = static_cast<int>(YToTimeRaw(ny));
                m_hoverTimeMs = m_core.QuantizeTime(rawTime);
            }
            else
            {
                m_hoverTimeMs = -1;
            }
        }
        else
        {
            m_hoverLane   = -1;
            m_hoverTimeMs = -1;
        }

        // Hold 拖拽：追踪当前 Y
        if (m_holdDragging)
            m_holdDragCurrentY = ny;

        return false;
    }

    // ── 鼠标按下 ─────────────────────────────────────────────────────────────
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

        int lane = XToLane(nx);

        int rawTime  = static_cast<int>(YToTimeRaw(ny));
        int snapTime = m_core.QuantizeTime(rawTime);

        if (event.button.button == SDL_BUTTON_LEFT && lane < 0)
        {
            m_core.SetCurrentTimeMs(snapTime);
            return true;
        }

        if (lane < 0) return false;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            NoteToolType tool = m_core.GetNoteTool();

            if (tool == NoteToolType::Hold)
            {
                // Hold 工具：开始拖拽以设置 duration
                int found = m_core.FindKeyboardNote(snapTime, lane);
                if (found >= 0)
                {
                    // 选中已有 Hold 音符
                    m_core.SelectKeyboardNote(found);
                }
                else
                {
                    // 开始 Hold 拖拽
                    m_holdDragging    = true;
                    m_holdDragStartMs = snapTime;
                    m_holdDragLane    = lane;
                    m_holdDragCurrentY = ny;
                }
                return true;
            }
            else
            {
                // Tap/Circle/Slider：单击放置或选中
                int found = m_core.FindKeyboardNote(snapTime, lane);
                if (found >= 0)
                {
                    m_core.SelectKeyboardNote(found);
                }
                else
                {
                    m_core.ClearSelection();
                    m_core.PlaceKeyboardNote(snapTime, lane);
                }
                return true;
            }
        }

        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            // 右键：取消 Hold 拖拽，或删除最近音符
            if (m_holdDragging)
            {
                m_holdDragging = false;
                return true;
            }
            int found = m_core.FindKeyboardNote(snapTime, lane);
            if (found >= 0)
            {
                if (m_core.GetSelectedKbNote() == found)
                    m_core.ClearSelection();
                m_core.DeleteKeyboardNote(found);
            }
            return true;
        }
    }

    // ── 鼠标释放 ─────────────────────────────────────────────────────────────
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT && m_holdDragging)
        {
            SDL_Window* win = SDL_GetWindowFromID(event.button.windowID);
            if (win)
            {
                int ww, wh;
                SDL_GetWindowSize(win, &ww, &wh);
                if (ww > 0 && wh > 0)
                {
                    float ny     = event.button.y / static_cast<float>(wh);
                    int   endMs  = m_core.QuantizeTime(static_cast<int>(YToTimeRaw(ny)));
                    int   durMs  = endMs - m_holdDragStartMs;

                    if (durMs > 0)
                    {
                        // 拖拽上方：duration > 0，放置 Hold 音符
                        m_core.SetNoteTool(NoteToolType::Hold);
                        m_core.PlaceKeyboardNote(m_holdDragStartMs, m_holdDragLane, durMs);
                    }
                    else
                    {
                        // 点击但未拖拽（或向下拖），放置默认 Hold（1拍）
                        m_core.SetNoteTool(NoteToolType::Hold);
                        m_core.PlaceKeyboardNote(m_holdDragStartMs, m_holdDragLane);
                    }
                }
            }
            m_holdDragging = false;
            return true;
        }
    }

    // ── 滚轮 ─────────────────────────────────────────────────────────────────
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        SDL_Window* win = SDL_GetWindowFromID(event.wheel.windowID);
        if (!win) return false;
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);

        float mx, my;
        SDL_GetMouseState(&mx, &my);
        float nx = mx / static_cast<float>(ww);
        float ny = my / static_cast<float>(wh);
        if (!IsInArea(nx, ny)) return false;

        const bool* ks = SDL_GetKeyboardState(nullptr);
        if (ks && (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL]))
        {
            if (event.wheel.y > 0) ZoomIn();
            else                   ZoomOut();
        }
        else
        {
            float beatMs = m_core.GetBeatIntervalMs(m_core.GetCurrentTimeMs());
            int   step   = static_cast<int>(beatMs / m_core.GetBeatSnap());
            if (step < 10) step = 10;
            m_scrollTimeMs -= static_cast<int>(event.wheel.y * step);
        }
        return true;
    }

    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// Render — 总入口
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::Render(sakura::core::Renderer& renderer)
{
    DrawBackground      (renderer);
    DrawWaveform        (renderer);  // 标尺区波形（音乐可视化）
    DrawGrid            (renderer);
    DrawLaneDividers    (renderer);
    DrawNotes           (renderer);
    DrawHoldDragPreview (renderer);  // Hold 拖拽预览
    DrawHoverPreview    (renderer);
    DrawPlayhead        (renderer);
    DrawRuler           (renderer);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawBackground
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawBackground(sakura::core::Renderer& renderer)
{
    // 轨道主背景：深墨夜琉璃渐变
    renderer.DrawRoundedGradientRect(
        { AREA_X, AREA_Y, AREA_W, AREA_H }, 0.0f,
        { 12, 15, 28, 245 }, { 16, 18, 34, 245 },
        { 8, 10, 20, 252 },  { 11, 13, 26, 252 });

    // 左侧波形与时间标尺列
    renderer.DrawRoundedGradientRect(
        { AREA_X, AREA_Y, RULER_W, AREA_H }, 0.0f,
        { 18, 22, 42, 248 }, { 14, 17, 32, 248 },
        { 12, 15, 30, 252 }, { 10, 12, 24, 252 });

    // 交替轨道柔光底带
    for (int lane = 0; lane < 4; ++lane)
    {
        const float lx = TRACK_X + static_cast<float>(lane) * TRACK_LANE_W;
        const sakura::core::Color tint = (lane % 2 == 0)
            ? sakura::core::Color{ 24, 28, 50, 62 }
            : sakura::core::Color{ 16, 20, 38, 34 };
        renderer.DrawFilledRect({ lx, AREA_Y, TRACK_LANE_W, AREA_H }, tint);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawGrid — 拍子线与细分线
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawGrid(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    const float beatMs  = m_core.GetBeatIntervalMs(m_core.GetCurrentTimeMs());
    const float snapMs  = beatMs / static_cast<float>(std::max(1, m_core.GetBeatSnap()));

    int startMs = m_scrollTimeMs;
    if (snapMs > 0.0f)
        startMs = static_cast<int>(std::floor(startMs / snapMs) * snapMs);

    const int endMs   = m_scrollTimeMs + m_viewDurationMs + static_cast<int>(snapMs) + 1;
    const int stepMs  = std::max(1, static_cast<int>(snapMs + 0.5f));
    const float x1    = TRACK_X;
    const float x2    = TRACK_X + TRACK_W;

    for (int t = startMs; t <= endMs; t += stepMs)
    {
        const float y = TimeToY(t);
        if (y < AREA_Y || y > 1.0f) continue;

        const bool isBeat = (std::fmod(std::abs(static_cast<float>(t)), beatMs) < snapMs * 0.5f);
        bool isMeasure = false;
        if (isBeat)
        {
            const float measureMs = beatMs * 4.0f;
            isMeasure = (std::fmod(std::abs(static_cast<float>(t)), measureMs) < snapMs * 0.5f);
        }

        if (isMeasure)
        {
            renderer.DrawLine(x1, y, x2, y,
                { palette.accent.r, palette.accent.g, palette.accent.b, 170 }, 0.0020f);
        }
        else if (isBeat)
        {
            renderer.DrawLine(x1, y, x2, y,
                { 165, 158, 212, 110 }, 0.0012f);
        }
        else
        {
            renderer.DrawLine(x1, y, x2, y,
                { 95, 90, 140, 58 }, 0.0010f);
        }
    }

    // t=0 谱面起始鎏金线
    const float y0 = TimeToY(0);
    if (y0 >= AREA_Y && y0 <= 1.0f)
    {
        renderer.DrawLine(x1, y0, x2, y0, palette.accent, 0.0026f);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawLaneDividers
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawLaneDividers(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    for (int i = 0; i <= 4; ++i)
    {
        const float x = TRACK_X + static_cast<float>(i) * TRACK_LANE_W;
        renderer.DrawLine(x, AREA_Y, x, 1.0f,
            { 140, 135, 185, static_cast<uint8_t>((i == 0 || i == 4) ? 140 : 75) }, 0.0011f);
    }

    renderer.DrawLine(RULER_W, AREA_Y, RULER_W, 1.0f, palette.surfaceBorder, 0.0012f);
    renderer.DrawLine(AREA_X + AREA_W, AREA_Y, AREA_X + AREA_W, 1.0f, palette.gildedHairline, 0.0014f);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawRuler — 左侧时间标尺
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawRuler(sakura::core::Renderer& renderer)
{
    if (m_font == sakura::core::INVALID_HANDLE) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    float snapMs = m_core.GetBeatIntervalMs(m_core.GetCurrentTimeMs());
    if (snapMs < 1.0f) snapMs = 1.0f;

    const int startMs = static_cast<int>(std::floor(m_scrollTimeMs / snapMs) * snapMs);
    const int endMs   = m_scrollTimeMs + m_viewDurationMs + static_cast<int>(snapMs);
    const int stepMs  = std::max(1, static_cast<int>(snapMs + 0.5f));

    for (int t = startMs; t <= endMs; t += stepMs)
    {
        const float y = TimeToY(t);
        if (y < AREA_Y + 0.012f || y > 0.99f) continue;

        const float sec    = static_cast<float>(t) / 1000.0f;
        const int   secInt = static_cast<int>(sec);
        const int   msRema = std::abs(t - secInt * 1000);

        std::ostringstream oss;
        if (t < 0) oss << "-";
        oss << std::abs(secInt) << "." << std::setw(3) << std::setfill('0') << msRema;

        renderer.DrawLine(RULER_W - 0.006f, y, RULER_W, y, { 175, 168, 215, 130 }, 0.001f);
        renderer.DrawText(m_font, oss.str(),
            RULER_W * 0.48f, y - 0.006f, 0.0125f,
            palette.textDim,
            sakura::core::TextAlign::Center);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawNotes
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawNotes(sakura::core::Renderer& renderer)
{
    const auto& palette   = sakura::core::Theme::GetInstance().Colors();
    const auto& notes     = m_core.GetChartData().keyboardNotes;
    const int selectedIdx = m_core.GetSelectedKbNote();

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& n = notes[i];
        if (n.time > m_scrollTimeMs + m_viewDurationMs + 500) continue;
        if (n.time + std::max(n.duration, 0) < m_scrollTimeMs - 500) continue;

        const float y = TimeToY(n.time);
        if (y < AREA_Y - NOTE_H * 2.0f && (n.type != sakura::game::NoteType::Hold))
            continue;

        const float x = LaneToX(n.lane);
        const bool isSelected = (i == selectedIdx);
        const sakura::core::Color laneCol = palette.laneColors[std::clamp(n.lane, 0, 3)];

        if (n.type == sakura::game::NoteType::Hold && n.duration > 0)
        {
            const float yBottom = TimeToY(n.time);
            const float yTop    = TimeToY(n.time + n.duration);
            const float holdH   = yBottom - yTop;
            if (holdH > 0.002f)
            {
                const sakura::core::NormRect bodyRect{
                    x + TRACK_LANE_W * 0.18f, yTop,
                    TRACK_LANE_W * 0.64f, holdH
                };
                renderer.DrawRoundedGradientRect(bodyRect, 0.005f,
                    { 170, 235, 255, 195 }, { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 195 },
                    { palette.primary.r, palette.primary.g, palette.primary.b, 165 },
                    { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 165 });
                renderer.DrawLine(x + TRACK_LANE_W * 0.50f, yTop + 0.003f,
                                  x + TRACK_LANE_W * 0.50f, yBottom - 0.003f,
                                  { 245, 252, 255, 175 }, 0.0016f);
            }
        }

        if (y < AREA_Y - NOTE_H || y > 1.0f + NOTE_H) continue;

        const sakura::core::NormRect gemRect{
            x + TRACK_LANE_W * 0.07f, y - NOTE_H,
            TRACK_LANE_W * 0.86f, NOTE_H * 2.0f
        };
        if (isSelected)
        {
            renderer.DrawSoftShadow(gemRect, 0.006f, 0.007f, { palette.accent.r, palette.accent.g, palette.accent.b, 150 }, 0.0f);
            renderer.DrawRoundedGradientRect(gemRect, 0.006f,
                { 255, 250, 220, 255 }, palette.accent, palette.accent, palette.primary);
            renderer.DrawRoundedRectOutlineGradient(gemRect, 0.006f,
                { 255, 255, 255, 255 }, palette.accent, 0.0020f);
        }
        else
        {
            const sakura::core::Color baseCol = (n.type == sakura::game::NoteType::Hold)
                ? palette.holdColor
                : laneCol;
            renderer.DrawRoundedGradientRect(gemRect, 0.006f,
                { 255, 240, 248, 242 }, baseCol, baseCol, palette.primary);
            renderer.DrawRoundedRectOutlineGradient(gemRect, 0.006f,
                { 255, 255, 255, 210 }, palette.gildedHairline, 0.0013f);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawPlayhead — 播放头（当前时间线）
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawPlayhead(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    const int   curMs = m_core.GetCurrentTimeMs();
    const float y     = TimeToY(curMs);
    if (y < AREA_Y || y > 1.0f) return;

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(TRACK_X + TRACK_W * 0.5f, y, TRACK_W * 0.52f, 0.012f,
        { palette.primary.r, palette.primary.g, palette.primary.b, 85 },
        { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    renderer.DrawLine(TRACK_X, y, TRACK_X + TRACK_W, y, palette.accent, 0.0028f);
    renderer.DrawRoundedGradientRect(
        { RULER_W - 0.014f, y - 0.006f, 0.014f, 0.012f }, 0.004f,
        palette.accent, palette.primary, palette.accent, palette.primary);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawHoverPreview — 鼠标悬停时的音符预览
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawHoverPreview(sakura::core::Renderer& renderer)
{
    if (m_hoverLane < 0 || m_hoverTimeMs < 0) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    const float y = TimeToY(m_hoverTimeMs);
    if (y < AREA_Y || y > 1.0f) return;

    const float x = LaneToX(m_hoverLane);
    sakura::core::Color previewColor = palette.laneColors[std::clamp(m_hoverLane, 0, 3)];
    const NoteToolType tool = m_core.GetNoteTool();
    if      (tool == NoteToolType::Hold)   previewColor = palette.holdColor;
    else if (tool == NoteToolType::Circle) previewColor = palette.circleColor;
    previewColor.a = 115;

    const sakura::core::NormRect previewRect{
        x + TRACK_LANE_W * 0.07f, y - NOTE_H,
        TRACK_LANE_W * 0.86f, NOTE_H * 2.0f
    };
    renderer.DrawRoundedRect(previewRect, 0.006f, previewColor, true);
    renderer.DrawRoundedRectOutlineGradient(previewRect, 0.006f,
        { 255, 255, 255, 165 }, previewColor, 0.0012f);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawHoldDragPreview — Hold 工具拖拽时的实时预览
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawHoldDragPreview(sakura::core::Renderer& renderer)
{
    if (!m_holdDragging || m_holdDragLane < 0) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    const float startY  = TimeToY(m_holdDragStartMs);
    const float endY    = m_holdDragCurrentY;
    const float topY    = std::min(startY, endY);
    const float bottomY = std::max(startY, endY);
    const float holdH   = bottomY - topY;
    if (holdH < 0.002f) return;

    const float x = LaneToX(m_holdDragLane);
    renderer.DrawRoundedGradientRect(
        { x + TRACK_LANE_W * 0.16f, topY, TRACK_LANE_W * 0.68f, holdH }, 0.005f,
        { 170, 240, 255, 145 }, { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 145 },
        { palette.primary.r, palette.primary.g, palette.primary.b, 115 },
        { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 115 });
    renderer.DrawLine(x + TRACK_LANE_W * 0.10f, topY, x + TRACK_LANE_W * 0.90f, topY,
                      palette.accent, 0.0022f);
    renderer.DrawLine(x + TRACK_LANE_W * 0.10f, bottomY, x + TRACK_LANE_W * 0.90f, bottomY,
                      palette.holdColor, 0.0016f);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawWaveform — 标尺区域的音频波形可视化
// ═════════════════════════════════════════════════════════════════════════════
void EditorTimeline::DrawWaveform(sakura::core::Renderer& renderer)
{
    if (m_waveform.empty()) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    const float centerX = RULER_W * 0.50f;
    for (int i = 0; i < static_cast<int>(m_waveform.size()); ++i)
    {
        const int   timeMs = i * m_waveformWindowMs;
        const float y      = TimeToY(timeMs);
        if (y < AREA_Y || y > 1.0f) continue;

        const float amplitude = std::clamp(m_waveform[i], 0.0f, 1.0f);
        const float halfW     = RULER_W * 0.44f * amplitude;
        if (halfW < 0.0004f) continue;

        const sakura::core::Color edgeCol{
            palette.holdColor.r, palette.holdColor.g, palette.holdColor.b,
            static_cast<uint8_t>(55 + 95 * amplitude)
        };
        const sakura::core::Color peakCol{
            palette.primary.r, palette.primary.g, palette.primary.b,
            static_cast<uint8_t>(90 + 120 * amplitude)
        };
        renderer.DrawRoundedGradientRect(
            { centerX - halfW, y - 0.0022f, halfW * 2.0f, 0.0044f }, 0.002f,
            edgeCol, peakCol, edgeCol, peakCol);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// LoadWaveform — 使用 miniaudio 解码音频文件并计算波形数据
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::LoadWaveform(const std::string& audioPath)
{
    m_waveform.clear();
    if (audioPath.empty()) return;

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 44100);
    ma_decoder decoder;
    if (ma_decoder_init_file(audioPath.c_str(), &config, &decoder) != MA_SUCCESS)
    {
        LOG_WARN("[EditorTimeline] 波形解码失败: {}", audioPath);
        return;
    }

    const ma_uint64 windowSamples =
        static_cast<ma_uint64>(44100) * static_cast<ma_uint64>(m_waveformWindowMs) / 1000;

    std::vector<float> buf(static_cast<size_t>(windowSamples));

    while (true)
    {
        ma_uint64 framesRead = 0;
        ma_result res = ma_decoder_read_pcm_frames(&decoder, buf.data(), windowSamples, &framesRead);
        if (framesRead == 0) break;

        float peak = 0.0f;
        for (ma_uint64 s = 0; s < framesRead; ++s)
        {
            float v = std::abs(buf[static_cast<size_t>(s)]);
            if (v > peak) peak = v;
        }
        m_waveform.push_back(peak);

        if (res != MA_SUCCESS) break;
    }

    ma_decoder_uninit(&decoder);
    LOG_INFO("[EditorTimeline] 波形加载完成: {} 个窗口 ({}ms/窗)",
             static_cast<int>(m_waveform.size()), m_waveformWindowMs);
}

} // namespace sakura::editor
