#pragma once

// editor_preview.h — 编辑器内试玩预览
// F5：从当前位置开始试玩；F6：从选中音符 -2s 处开始。
// 在编辑器右侧区域渲染简化版下落游戏视图，ESC 退出回编辑器。

#include "editor_core.h"
#include "core/renderer.h"
#include "game/note.h"

#include <vector>
#include <string>

namespace sakura::editor
{

// ── 预览中的键盘音符状态 ──────────────────────────────────────────────────────
struct PreviewKbNote
{
    sakura::game::KeyboardNote note;   // 副本
    bool  hit     = false;
    bool  missed  = false;
    float hitFlash = 0.0f;   // 命中时高亮计时（秒）
};

// ── EditorPreview ─────────────────────────────────────────────────────────────
// 不依赖 SceneGame；使用 EditorCore 中的谱面数据自行驱动。
// 鼠标音符仅做视觉展示（接近圈缩放到位）。
class EditorPreview
{
public:
    explicit EditorPreview(EditorCore& core);

    void SetFont(sakura::core::FontHandle font) { m_font = font; }

    // ── 控制 ──────────────────────────────────────────────────────────────────

    // 从 fromMs 处进入预览（会启动/跳转音频）
    void Start(int fromMs);

    // 停止预览（停止音频，但不切场景）
    void Stop();

    bool IsActive() const { return m_active; }
    int  GetCurrentMs() const { return m_currentMs; }

    // ── 每帧 ──────────────────────────────────────────────────────────────────
    void Update(float dt);

    // 渲染右侧预览区域 (0.42, 0.06, 0.56, 0.94)
    void Render(sakura::core::Renderer& renderer);

    // 处理事件：ESC 停止预览，返回 true 表示已消费
    bool HandleEvent(const SDL_Event& event);

private:
    EditorCore& m_core;
    sakura::core::FontHandle m_font = sakura::core::INVALID_HANDLE;

    bool  m_active    = false;
    int   m_startMs   = 0;    // 预览起始时间
    int   m_currentMs = 0;    // 当前预览时间（ms，从音频同步）

    std::vector<PreviewKbNote> m_kbNotes;  // 所有键盘音符的工作副本

    // ── 布局常量 ──────────────────────────────────────────────────────────────
    static constexpr float AREA_X      = 0.42f;
    static constexpr float AREA_Y      = 0.06f;
    static constexpr float AREA_W      = 0.56f;
    static constexpr float AREA_H      = 0.94f;

    // 轨道区域（在 AREA 内，归一化于整屏）
    static constexpr float TRACK_X     = AREA_X + AREA_W * 0.1f;
    static constexpr float TRACK_W     = AREA_W * 0.8f;
    static constexpr float LANE_W      = TRACK_W / 4.0f;

    // 判定线位置（归一化整屏 Y）
    static constexpr float JUDGE_Y     = AREA_Y + AREA_H * 0.85f;

    // 音符接近时间（ms）——提前这么多毫秒出现在屏幕顶部
    static constexpr float LEAD_TIME_MS = 3000.0f;

    // 自动命中窗口（ms）——音符到达时自动标记为 hit
    static constexpr float AUTO_HIT_MS  = 30.0f;

    // ── 坐标换算 ──────────────────────────────────────────────────────────────
    float LaneToX(int lane) const;     // 返回该轨道左边缘归一化 X
    float TimeToY(int timeMs) const;   // 返回音符在屏幕上的归一化 Y

    // ── 渲染子方法 ────────────────────────────────────────────────────────────
    void DrawBackground(sakura::core::Renderer& renderer);
    void DrawLaneLines(sakura::core::Renderer& renderer);
    void DrawJudgeLine(sakura::core::Renderer& renderer);
    void DrawNotes(sakura::core::Renderer& renderer);
    void DrawMouseNotes(sakura::core::Renderer& renderer);
    void DrawHUD(sakura::core::Renderer& renderer);
    void DrawOverlay(sakura::core::Renderer& renderer);  // "试玩中" 顶部提示条
};

} // namespace sakura::editor
