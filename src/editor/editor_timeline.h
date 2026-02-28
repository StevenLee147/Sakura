#pragma once

// editor_timeline.h — 谱面编辑器键盘轨道区域的渲染与交互
// 区域：(0.0, 0.06, 0.40, 0.94)，时间轴向上流动（底部=早，顶部=晚）

#include "core/renderer.h"
#include "core/resource_manager.h"
#include "editor_core.h"

#include <SDL3/SDL.h>
#include <vector>
#include <string>

namespace sakura::editor
{

// ── EditorTimeline ────────────────────────────────────────────────────────────
// 负责键盘轨道区域的所有渲染和鼠标/滚轮交互。
// 不拥有 EditorCore，仅保持引用。
class EditorTimeline
{
public:
    explicit EditorTimeline(EditorCore& core);
    ~EditorTimeline() = default;

    void SetFont(sakura::core::FontHandle font) { m_font = font; }

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);

    // 处理事件（鼠标点击/右键/滚轮 in 时间轴区域）
    // 返回 true = 事件已被消费，不需要继续传递
    bool HandleEvent(const SDL_Event& event);

    // ── 视图控制 ──────────────────────────────────────────────────────────────

    // 将时间轴滚动到 timeMs 附近（底部显示该时间）
    void CenterOnTime(int timeMs);

    // 缩放时间轴（改变可见时间范围）
    void ZoomIn();
    void ZoomOut();

    int GetScrollTimeMs() const { return m_scrollTimeMs; }

    // 加载并解析音频波形数据（用于时间轴左侧波形可视化）
    void LoadWaveform(const std::string& audioPath);

    // ── 坐标转换 ──────────────────────────────────────────────────────────────

    // 时间（毫秒）→ 归一化 Y 坐标（在整个屏幕坐标系下）
    float TimeToY(int timeMs) const;

    // 归一化 Y 坐标 → 时间（毫秒，未量化）
    float YToTimeRaw(float y) const;

    // 轨道 i → 轨道左边缘 X（归一化）
    float LaneToX(int lane) const;

    // 单轨道宽度（归一化）
    float LaneWidth() const;

    // X 坐标 → 轨道索引（-1 = 不在有效轨道区内）
    int XToLane(float x) const;

    // 是否落在时间轴有效区域内（轨道区+标尺区）
    bool IsInArea(float x, float y) const;

private:
    EditorCore& m_core;

    // ── 布局常量 ──────────────────────────────────────────────────────────────
    // 整个时间轴面板
    static constexpr float AREA_X = 0.0f;
    static constexpr float AREA_Y = 0.06f;
    static constexpr float AREA_W = 0.40f;
    static constexpr float AREA_H = 0.94f;

    // 左侧时间标尺宽度
    static constexpr float RULER_W = 0.04f;

    // 轨道区域（4条轨道均分 AREA_W-RULER_W）
    static constexpr float TRACK_X = AREA_X + RULER_W;        // = 0.04
    static constexpr float TRACK_W = AREA_W - RULER_W;        // = 0.36
    static constexpr float TRACK_LANE_W = TRACK_W / 4.0f;     // = 0.09

    // 音符矩形高度（归一化）
    static constexpr float NOTE_H = 0.008f;

    // ── 视图状态 ──────────────────────────────────────────────────────────────

    // 可见时间范围（毫秒）
    int m_viewDurationMs = 4000;

    // 底部对应的时间（毫秒），可为负数
    int m_scrollTimeMs   = -1000;

    // 上次鼠标位置（用于拖拽检测）
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;

    // 鼠标悬停时间/轨道（用于预览音符位置）
    int   m_hoverTimeMs = -1;
    int   m_hoverLane   = -1;

    // ── Hold 拖拽状态 ─────────────────────────────────────────────────────────
    // 用 Hold 工具时，按下左键后开始拖拽，松开时确定 duration
    bool  m_holdDragging      = false;
    int   m_holdDragStartMs   = 0;    // 按下时量化的时间
    int   m_holdDragLane      = -1;   // 按下的轨道
    float m_holdDragCurrentY  = 0.0f; // 当前鼠标 Y（归一化）

    // ── 波形数据 ──────────────────────────────────────────────────────────────
    // m_waveform[i] = 第 i 个窗口的峰值幅度（归一化到 0~1）
    std::vector<float> m_waveform;
    int m_waveformWindowMs = 20;  // 每个窗口覆盖 20ms 音频

    sakura::core::FontHandle m_font = sakura::core::INVALID_HANDLE;

    // ── 内部渲染 ──────────────────────────────────────────────────────────────
    void DrawBackground   (sakura::core::Renderer& renderer);
    void DrawWaveform     (sakura::core::Renderer& renderer);
    void DrawGrid         (sakura::core::Renderer& renderer);
    void DrawLaneDividers (sakura::core::Renderer& renderer);
    void DrawRuler        (sakura::core::Renderer& renderer);
    void DrawNotes        (sakura::core::Renderer& renderer);
    void DrawPlayhead     (sakura::core::Renderer& renderer);
    void DrawHoverPreview (sakura::core::Renderer& renderer);
    void DrawHoldDragPreview(sakura::core::Renderer& renderer);
};

} // namespace sakura::editor
