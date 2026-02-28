// editor_timeline.cpp — 键盘轨道编辑区渲染与交互

#include "editor_timeline.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>

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
    // 更新 hover 信息（鼠标移动）
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        float mx = event.motion.x;
        float my = event.motion.y;

        // 获取窗口尺寸（需要归一化）
        // 使用 event 中的窗口 ID 获取，但简便起见用静态缓存
        // 此处直接从 SDL_GetWindowFromID 路径较复杂，
        // 考虑到项目其他场景都通过 ResourceManager 获取窗口，
        // 这里通过获取当前 SDL 渲染器窗口来归一化
        SDL_Window* win = SDL_GetWindowFromID(event.motion.windowID);
        if (!win) return false;
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return false;

        float nx = mx / static_cast<float>(ww);
        float ny = my / static_cast<float>(wh);

        if (IsInArea(nx, ny))
        {
            m_lastMouseX = nx;
            m_lastMouseY = ny;
            m_hoverLane  = XToLane(nx);
            if (m_hoverLane >= 0)
            {
                int rawTime  = static_cast<int>(YToTimeRaw(ny));
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
        return false;  // 移动事件不消费（继续传递给 SceneEditor）
    }

    // 鼠标按下：点击放置或选择/删除
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
        if (lane < 0) return false;

        int rawTime = static_cast<int>(YToTimeRaw(ny));
        int snapTime = m_core.QuantizeTime(rawTime);

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            // 先检查是否有已放置音符（±toleranceMs 内）→ 选中
            int found = m_core.FindKeyboardNote(snapTime, lane);
            if (found >= 0)
            {
                m_core.SelectKeyboardNote(found);
            }
            else
            {
                // 放置新音符
                m_core.ClearSelection();
                m_core.PlaceKeyboardNote(snapTime, lane);
            }
            return true;
        }

        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            // 右键删除最近音符
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

    // 滚轮：滚动时间轴
    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        SDL_Window* win = SDL_GetWindowFromID(event.wheel.windowID);
        if (!win) return false;
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);

        // 当前鼠标是否在时间轴区域
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        float nx = mx / static_cast<float>(ww);
        float ny = my / static_cast<float>(wh);
        if (!IsInArea(nx, ny)) return false;

        // Ctrl + 滚轮：缩放（修改 m_viewDurationMs）
        const bool* ks = SDL_GetKeyboardState(nullptr);
        if (ks && (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL]))
        {
            if (event.wheel.y > 0)
                ZoomIn();
            else
                ZoomOut();
        }
        else
        {
            // 普通滚轮：滚动时间
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
    DrawBackground   (renderer);
    DrawGrid         (renderer);
    DrawLaneDividers (renderer);
    DrawNotes        (renderer);
    DrawHoverPreview (renderer);
    DrawPlayhead     (renderer);
    DrawRuler        (renderer);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawBackground
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawBackground(sakura::core::Renderer& renderer)
{
    // 时间轴整体背景
    renderer.DrawFilledRect(
        { AREA_X, AREA_Y, AREA_W, AREA_H },
        sakura::core::Color{ 8, 6, 20, 255 });

    // 标尺背景（稍浅）
    renderer.DrawFilledRect(
        { AREA_X, AREA_Y, RULER_W, AREA_H },
        sakura::core::Color{ 12, 10, 28, 255 });
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawGrid — 拍子线与细分线
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawGrid(sakura::core::Renderer& renderer)
{
    float beatMs   = m_core.GetBeatIntervalMs(m_core.GetCurrentTimeMs());
    float snapMs   = beatMs / static_cast<float>(m_core.GetBeatSnap());

    // 从第一个可见时间开始（向下取整到 snapMs 边界）
    int startMs = m_scrollTimeMs;
    // align to nearest subdiv
    if (snapMs > 0.0f)
        startMs = static_cast<int>(std::floor(startMs / snapMs) * snapMs);
    int endMs = m_scrollTimeMs + m_viewDurationMs + static_cast<int>(snapMs) + 1;

    // 只在轨道区域画线（从 TRACK_X 到 TRACK_X+TRACK_W）
    float x1 = TRACK_X;
    float x2 = TRACK_X + TRACK_W;

    for (int t = startMs; t <= endMs; t += static_cast<int>(snapMs + 0.5f))
    {
        float y = TimeToY(t);
        if (y < AREA_Y || y > 1.0f) continue;

        // 判断是否是整拍（t mod beatMs ≈ 0）
        bool isBeat      = (std::fmod(std::abs(static_cast<float>(t)), beatMs) < snapMs * 0.5f);
        bool isMeasure   = false;
        if (isBeat)
        {
            // 判断是否是小节线（beatSnap subdivisions * timeSigNumerator beats）
            float measureMs = beatMs * 4.0f;  // 假设 4/4
            isMeasure = (std::fmod(std::abs(static_cast<float>(t)), measureMs) < snapMs * 0.5f);
        }

        if (isMeasure)
        {
            // 小节线：亮白
            renderer.DrawLine(x1, y, x2, y,
                sakura::core::Color{ 180, 160, 220, 180 }, 0.002f);
        }
        else if (isBeat)
        {
            // 拍线：中等
            renderer.DrawLine(x1, y, x2, y,
                sakura::core::Color{ 100, 90, 140, 120 }, 0.001f);
        }
        else
        {
            // 细分线：暗
            renderer.DrawLine(x1, y, x2, y,
                sakura::core::Color{ 55, 45, 80, 80 }, 0.001f);
        }
    }

    // t=0 处特殊标记（谱面起始线）
    float y0 = TimeToY(0);
    if (y0 >= AREA_Y && y0 <= 1.0f)
    {
        renderer.DrawLine(x1, y0, x2, y0,
            sakura::core::Color{ 255, 200, 50, 200 }, 0.002f);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawLaneDividers
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawLaneDividers(sakura::core::Renderer& renderer)
{
    for (int i = 0; i <= 4; ++i)
    {
        float x = TRACK_X + i * TRACK_LANE_W;
        renderer.DrawLine(x, AREA_Y, x, 1.0f,
            sakura::core::Color{ 70, 55, 110, 160 }, 0.001f);
    }

    // 标尺右边框
    renderer.DrawLine(RULER_W, AREA_Y, RULER_W, 1.0f,
        sakura::core::Color{ 70, 55, 110, 100 }, 0.001f);

    // 时间轴整体右边框
    renderer.DrawLine(AREA_X + AREA_W, AREA_Y, AREA_X + AREA_W, 1.0f,
        sakura::core::Color{ 60, 50, 90, 120 }, 0.001f);
    renderer.DrawLine(AREA_X, AREA_Y, AREA_X + AREA_W, AREA_Y,
        sakura::core::Color{ 60, 50, 90, 100 }, 0.001f);
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawRuler — 左侧时间标尺
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawRuler(sakura::core::Renderer& renderer)
{
    if (m_font == sakura::core::INVALID_HANDLE) return;

    float beatMs = m_core.GetBeatIntervalMs(m_core.GetCurrentTimeMs());
    // 每隔整拍绘制一个时间标签
    float snapMs = beatMs;
    if (snapMs < 1.0f) snapMs = 1.0f;

    int startMs = static_cast<int>(std::floor(m_scrollTimeMs / snapMs) * snapMs);
    int endMs   = m_scrollTimeMs + m_viewDurationMs + static_cast<int>(snapMs);

    for (int t = startMs; t <= endMs; t += static_cast<int>(snapMs + 0.5f))
    {
        float y = TimeToY(t);
        if (y < AREA_Y + 0.01f || y > 0.99f) continue;

        // 时间标签（秒:毫秒）
        float sec      = static_cast<float>(t) / 1000.0f;
        int   secInt   = static_cast<int>(sec);
        int   msRema   = std::abs(t - secInt * 1000);
        std::ostringstream oss;
        if (t < 0) oss << "-";
        oss << std::abs(secInt) << "." << std::setw(3) << std::setfill('0') << msRema;

        renderer.DrawText(m_font, oss.str(),
            RULER_W * 0.5f, y - 0.003f, 0.012f,
            sakura::core::Color{ 150, 140, 180, 180 },
            sakura::core::TextAlign::Center);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawNotes
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawNotes(sakura::core::Renderer& renderer)
{
    const auto& notes = m_core.GetChartData().keyboardNotes;
    int selectedIdx   = m_core.GetSelectedKbNote();

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& n = notes[i];

        // 只绘制在可见范围内的音符
        if (n.time > m_scrollTimeMs + m_viewDurationMs + 500) continue;
        if (n.time + std::max(n.duration, 0) < m_scrollTimeMs - 500) continue;

        float y = TimeToY(n.time);
        if (y < AREA_Y - NOTE_H || y > 1.0f + NOTE_H) continue;

        float x = LaneToX(n.lane);

        // 音符颜色（按类型）
        sakura::core::Color noteColor = { 80, 130, 255, 230 };    // Tap: 蓝
        if (n.type == sakura::game::NoteType::Hold)
            noteColor = { 80, 220, 120, 230 };                     // Hold: 绿
        else if (n.type == sakura::game::NoteType::Drag)
            noteColor = { 255, 170, 60, 230 };                     // Drag: 橙

        // Hold：画一个从 time 到 time+duration 的竖条
        if (n.type == sakura::game::NoteType::Hold && n.duration > 0)
        {
            float yBottom = TimeToY(n.time);
            float yTop    = TimeToY(n.time + n.duration);
            float holdH   = yBottom - yTop;  // 高度（屏幕向上）
            if (holdH > 0.0f)
            {
                sakura::core::Color holdBody = { 60, 180, 90, 130 };
                renderer.DrawFilledRect(
                    { x + TRACK_LANE_W * 0.1f,
                      yTop,
                      TRACK_LANE_W * 0.8f,
                      holdH },
                    holdBody);
            }
        }

        // 选中高亮
        bool isSelected = (i == selectedIdx);
        if (isSelected)
            noteColor = { 255, 255, 100, 255 };  // 金黄高亮

        // 音符主体矩形
        renderer.DrawFilledRect(
            { x + TRACK_LANE_W * 0.05f,
              y - NOTE_H,
              TRACK_LANE_W * 0.9f,
              NOTE_H * 2.0f },
            noteColor);

        // 选中时加边框
        if (isSelected)
        {
            renderer.DrawLine(x + TRACK_LANE_W * 0.05f, y - NOTE_H,
                             x + TRACK_LANE_W * 0.95f, y - NOTE_H,
                             sakura::core::Color{ 255, 255, 255, 200 }, 0.001f);
            renderer.DrawLine(x + TRACK_LANE_W * 0.05f, y + NOTE_H,
                             x + TRACK_LANE_W * 0.95f, y + NOTE_H,
                             sakura::core::Color{ 255, 255, 255, 200 }, 0.001f);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawPlayhead — 播放头（当前时间线）
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawPlayhead(sakura::core::Renderer& renderer)
{
    int   curMs = m_core.GetCurrentTimeMs();
    float y     = TimeToY(curMs);
    if (y < AREA_Y || y > 1.0f) return;

    renderer.DrawLine(TRACK_X, y, TRACK_X + TRACK_W, y,
        sakura::core::Color{ 255, 60, 100, 220 }, 0.003f);

    // 小三角标记（用一个小矩形代替）
    renderer.DrawFilledRect(
        { RULER_W - 0.012f, y - 0.006f, 0.012f, 0.012f },
        sakura::core::Color{ 255, 60, 100, 220 });
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawHoverPreview — 鼠标悬停时的音符预览
// ═════════════════════════════════════════════════════════════════════════════

void EditorTimeline::DrawHoverPreview(sakura::core::Renderer& renderer)
{
    if (m_hoverLane < 0 || m_hoverTimeMs < 0) return;

    float y = TimeToY(m_hoverTimeMs);
    if (y < AREA_Y || y > 1.0f) return;

    float x = LaneToX(m_hoverLane);

    // 半透明预览
    sakura::core::Color previewColor = { 80, 130, 255, 100 };

    NoteToolType tool = m_core.GetNoteTool();
    if      (tool == NoteToolType::Hold)   previewColor = { 80, 220, 120, 100 };
    else if (tool == NoteToolType::Drag)   previewColor = { 255, 170, 60, 100 };
    else if (tool == NoteToolType::Circle) previewColor = { 255, 80, 200, 100 };

    renderer.DrawFilledRect(
        { x + TRACK_LANE_W * 0.05f,
          y - NOTE_H,
          TRACK_LANE_W * 0.9f,
          NOTE_H * 2.0f },
        previewColor);
}

} // namespace sakura::editor
