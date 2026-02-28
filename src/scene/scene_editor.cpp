// scene_editor.cpp — 谱面编辑器场景实现

#include "scene_editor.h"
#include "scene_menu.h"
#include "core/resource_manager.h"
#include "audio/audio_manager.h"
#include "ui/toast.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneEditor::SceneEditor(SceneManager& mgr,
                         const std::string& folderPath,
                         const std::string& difficultyFile)
    : m_manager(mgr)
    , m_timeline(m_core)
    , m_mouseArea(m_core)
    , m_initFolderPath(folderPath)
    , m_initDiffFile(difficultyFile)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneEditor::OnEnter()
{
    LOG_INFO("[SceneEditor] 进入编辑器场景");

    auto& rm      = sakura::core::ResourceManager::GetInstance();
    m_fontUI      = rm.GetDefaultFontHandle();
    m_fontSmall   = rm.GetDefaultFontHandle();

    m_ctrlHeld    = false;

    // 加载或新建谱面
    if (!m_initFolderPath.empty())
    {
        if (!m_core.LoadChart(m_initFolderPath, m_initDiffFile))
        {
            LOG_WARN("[SceneEditor] 谱面加载失败，改为新建");
            m_core.NewChart("new-chart", "新谱面", 120.0f);
        }
    }
    else
    {
        m_core.NewChart("new-chart", "新谱面", 120.0f);
    }

    // 音频停止（回到编辑器时停止游戏音乐）
    sakura::audio::AudioManager::GetInstance().StopMusic();

    // 初始化时间轴字体
    m_timeline.SetFont(m_fontSmall);
    m_mouseArea.SetFont(m_fontSmall);

    // 加载音频波形（若谱面目录中有音乐文件）
    {
        const auto& info = m_core.GetChartInfo();
        if (!info.musicFile.empty() && !info.folderPath.empty())
        {
            std::string musicPath = info.folderPath + "/" + info.musicFile;
            m_timeline.LoadWaveform(musicPath);
        }
    }

    // 初始滚动：让 t=0 在时间轴靠下位置
    m_timeline.CenterOnTime(0);

    SetupToolbar();
    UpdateToolButtons();
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneEditor::OnExit()
{
    LOG_INFO("[SceneEditor] 退出编辑器场景");
    m_core.StopPlayback();
}

// ── SetupToolbar ──────────────────────────────────────────────────────────────

void SceneEditor::SetupToolbar()
{
    const char* toolLabels[TOOL_COUNT] = { "Tap", "Hold", "Drag", "Circle", "Slider" };

    sakura::ui::ButtonColors toolColors;
    toolColors.normal   = { 30, 25, 60, 200 };
    toolColors.hover    = { 60, 50, 110, 230 };
    toolColors.pressed  = { 20, 15, 45, 240 };
    toolColors.text     = sakura::core::Color::White;

    for (int i = 0; i < TOOL_COUNT; ++i)
    {
        float x = 0.01f + i * 0.075f;
        m_toolBtns[i] = std::make_unique<sakura::ui::Button>(
            sakura::core::NormRect{ x, 0.005f, 0.068f, 0.048f },
            toolLabels[i], m_fontUI, 0.020f, 0.008f);
        m_toolBtns[i]->SetColors(toolColors);

        int toolIdx = i;
        m_toolBtns[i]->SetOnClick([this, toolIdx]()
        {
            m_core.SetNoteTool(static_cast<sakura::editor::NoteToolType>(toolIdx));
            UpdateToolButtons();
        });
    }

    // 播放/暂停按钮
    m_btnPlay = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.40f, 0.005f, 0.08f, 0.048f },
        "▶ 播放", m_fontUI, 0.020f, 0.008f);
    m_btnPlay->SetOnClick([this]()
    {
        // 如果有音乐文件，先加载
        const std::string& musicFile = m_core.GetChartInfo().musicFile;
        if (!musicFile.empty() && !m_core.GetChartInfo().folderPath.empty())
        {
            if (!m_core.IsPlaying())
            {
                std::string path = m_core.GetChartInfo().folderPath
                                 + "/" + musicFile;
                sakura::audio::AudioManager::GetInstance().PlayMusic(path, 0);
                sakura::audio::AudioManager::GetInstance().SetMusicPosition(
                    static_cast<double>(m_core.GetCurrentTimeMs()) / 1000.0);
            }
        }
        m_core.TogglePlayback();
        m_btnPlay->SetText(m_core.IsPlaying() ? "⏸ 暂停" : "▶ 播放");
    });

    // 撤销按钮
    m_btnUndo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.635f, 0.005f, 0.072f, 0.048f },
        "↩ 撤销", m_fontUI, 0.020f, 0.008f);
    m_btnUndo->SetOnClick([this]()
    {
        m_core.Undo();
        UpdateUndoRedoButtons();
        sakura::ui::ToastManager::Instance().Show(
            "撤销: " + (m_core.CanRedo() ? m_core.GetRedoDescription() : ""),
            sakura::ui::ToastType::Info);
    });

    // 重做按钮
    m_btnRedo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.712f, 0.005f, 0.072f, 0.048f },
        "↪ 重做", m_fontUI, 0.020f, 0.008f);
    m_btnRedo->SetOnClick([this]()
    {
        m_core.Redo();
        UpdateUndoRedoButtons();
        sakura::ui::ToastManager::Instance().Show(
            "重做: " + (m_core.CanUndo() ? m_core.GetUndoDescription() : ""),
            sakura::ui::ToastType::Info);
    });

    // 保存按钮
    m_btnSave = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.82f, 0.005f, 0.08f, 0.048f },
        "💾 保存", m_fontUI, 0.020f, 0.008f);
    m_btnSave->SetOnClick([this]() { DoSave(); });

    // 退出
    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.91f, 0.005f, 0.08f, 0.048f },
        "← 退出", m_fontUI, 0.020f, 0.008f);
    m_btnBack->SetOnClick([this]()
    {
        if (m_core.IsDirty())
        {
            // 简单提示——后续可做弹窗确认，目前直接保存后退出
            DoSave();
        }
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
    });

    // BeatSnap 调整 ↑↓
    m_btnSnapDec = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.54f, 0.005f, 0.040f, 0.048f },
        "1/↓", m_fontUI, 0.018f, 0.006f);
    m_btnSnapDec->SetOnClick([this]()
    {
        int cur = m_core.GetBeatSnap();
        const int vals[] = { 1, 2, 4, 8, 16 };
        for (int k = static_cast<int>(std::size(vals)) - 1; k >= 0; --k)
        {
            if (vals[k] < cur) { m_core.SetBeatSnap(vals[k]); break; }
        }
    });

    m_btnSnapInc = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.585f, 0.005f, 0.040f, 0.048f },
        "1/↑", m_fontUI, 0.018f, 0.006f);
    m_btnSnapInc->SetOnClick([this]()
    {
        int cur = m_core.GetBeatSnap();
        const int vals[] = { 1, 2, 4, 8, 16 };
        for (int v : vals)
        {
            if (v > cur) { m_core.SetBeatSnap(v); break; }
        }
    });
}

// ── UpdateToolButtons ─────────────────────────────────────────────────────────

void SceneEditor::UpdateToolButtons()
{
    int current = static_cast<int>(m_core.GetNoteTool());
    for (int i = 0; i < TOOL_COUNT; ++i)
    {
        if (!m_toolBtns[i]) continue;
        sakura::ui::ButtonColors c;
        if (i == current)
        {
            c.normal  = { 70, 100, 220, 235 };  // 选中：亮蓝
            c.hover   = { 90, 120, 255, 245 };
            c.pressed = { 50, 80, 180, 255 };
        }
        else
        {
            c.normal  = { 30, 25, 60, 200 };
            c.hover   = { 60, 50, 110, 230 };
            c.pressed = { 20, 15, 45, 240 };
        }
        c.text = sakura::core::Color::White;
        m_toolBtns[i]->SetColors(c);
    }
}

// ── UpdateUndoRedoButtons ─────────────────────────────────────────────────────

void SceneEditor::UpdateUndoRedoButtons()
{
    // 撤销按钮：有历史时亮显，否则暗灰
    if (m_btnUndo)
    {
        sakura::ui::ButtonColors c;
        if (m_core.CanUndo())
        {
            c.normal  = { 40, 60, 120, 210 };
            c.hover   = { 60, 90, 180, 235 };
            c.pressed = { 30, 45, 90,  245 };
        }
        else
        {
            c.normal  = { 20, 18, 40, 120 };
            c.hover   = { 25, 22, 50, 130 };
            c.pressed = { 15, 12, 30, 120 };
        }
        c.text = sakura::core::Color{ 200, 200, 255,
            static_cast<Uint8>(m_core.CanUndo() ? 230 : 100) };
        m_btnUndo->SetColors(c);

        int cnt = m_core.GetUndoCount();
        std::string label = cnt > 0
            ? "↩ 撤销(" + std::to_string(cnt) + ")"
            : "↩ 撤销";
        m_btnUndo->SetText(label);
    }

    // 重做按钮
    if (m_btnRedo)
    {
        sakura::ui::ButtonColors c;
        if (m_core.CanRedo())
        {
            c.normal  = { 40, 60, 120, 210 };
            c.hover   = { 60, 90, 180, 235 };
            c.pressed = { 30, 45, 90,  245 };
        }
        else
        {
            c.normal  = { 20, 18, 40, 120 };
            c.hover   = { 25, 22, 50, 130 };
            c.pressed = { 15, 12, 30, 120 };
        }
        c.text = sakura::core::Color{ 200, 200, 255,
            static_cast<Uint8>(m_core.CanRedo() ? 230 : 100) };
        m_btnRedo->SetColors(c);

        int cnt = m_core.GetRedoCount();
        std::string label = cnt > 0
            ? "↪ 重做(" + std::to_string(cnt) + ")"
            : "↪ 重做";
        m_btnRedo->SetText(label);
    }
}

// ── DoSave ────────────────────────────────────────────────────────────────────

void SceneEditor::DoSave()
{
    bool ok = m_core.SaveChart();
    if (ok)
    {
        sakura::ui::ToastManager::Instance().Show(
            "谱面已保存", sakura::ui::ToastType::Success);
    }
    else
    {
        sakura::ui::ToastManager::Instance().Show(
            "保存失败，请检查路径", sakura::ui::ToastType::Error);
    }
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneEditor::OnUpdate(float dt)
{
    m_core.Update(dt);
    m_timeline.Update(dt);

    // 同步播放按钮标签
    if (m_btnPlay)
        m_btnPlay->SetText(m_core.IsPlaying() ? "⏸ 暂停" : "▶ 播放");

    // 同步撤销/重做按钮状态（每帧更新文本和颜色）
    UpdateUndoRedoButtons();
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneEditor::OnRender(sakura::core::Renderer& renderer)
{
    // 全屏背景
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 8, 6, 18, 255 });

    // 1. 工具栏
    RenderToolbar(renderer);

    // 2. 键盘时间轴（EditorTimeline）
    m_timeline.Render(renderer);

    // 3. 右侧鼠标编辑区（EditorMouseArea）
    m_mouseArea.Render(renderer);

    // 4. 属性面板（占位）
    RenderPropertyPanel(renderer);

    // 5. 底部全曲缩略轴
    RenderOverviewAxis(renderer);

    // Toast 通知
    sakura::ui::ToastManager::Instance().Render(renderer, m_fontSmall);
}

// ── RenderToolbar ─────────────────────────────────────────────────────────────

void SceneEditor::RenderToolbar(sakura::core::Renderer& renderer)
{
    // 工具栏背景
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 0.06f },
        sakura::core::Color{ 15, 12, 35, 240 });
    renderer.DrawLine(0.0f, 0.06f, 1.0f, 0.06f,
        sakura::core::Color{ 60, 50, 100, 150 }, 0.001f);

    // 工具按钮
    for (auto& btn : m_toolBtns) if (btn) btn->Render(renderer);

    // 播放/暂停
    if (m_btnPlay) m_btnPlay->Render(renderer);

    // 撤销/重做
    if (m_btnUndo) m_btnUndo->Render(renderer);
    if (m_btnRedo) m_btnRedo->Render(renderer);

    // BeatSnap 显示
    if (m_fontUI != sakura::core::INVALID_HANDLE)
    {
        std::string snapStr = "1/" + std::to_string(m_core.GetBeatSnap());
        renderer.DrawText(m_fontUI, snapStr,
            0.563f, 0.028f, 0.022f,
            sakura::core::Color{ 200, 190, 240, 220 },
            sakura::core::TextAlign::Center);

        // 谱面标题（中央，包含 dirty 标记）
        std::string titleStr = m_core.GetChartInfo().title;
        if (m_core.IsDirty()) titleStr += " *";
        renderer.DrawText(m_fontUI, titleStr,
            0.503f, 0.028f, 0.018f,
            sakura::core::Color{ 200, 180, 255, 200 },
            sakura::core::TextAlign::Center);
    }

    if (m_btnSnapDec) m_btnSnapDec->Render(renderer);
    if (m_btnSnapInc) m_btnSnapInc->Render(renderer);
    if (m_btnSave)    m_btnSave->Render(renderer);
    if (m_btnBack)    m_btnBack->Render(renderer);
}

// ── RenderMouseArea ───────────────────────────────────────────────────────────

void SceneEditor::RenderMouseArea(sakura::core::Renderer& renderer)
{
    // 占位面板：(0.42, 0.06, 0.33, 0.60)
    renderer.DrawFilledRect({ 0.42f, 0.06f, 0.33f, 0.60f },
        sakura::core::Color{ 10, 8, 24, 200 });
    renderer.DrawLine(0.42f, 0.06f, 0.75f, 0.06f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.42f, 0.06f, 0.42f, 0.66f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.42f, 0.66f, 0.75f, 0.66f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.75f, 0.06f, 0.75f, 0.66f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);

    if (m_fontUI != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_fontUI, "鼠标编辑区",
            0.585f, 0.36f, 0.025f,
            sakura::core::Color{ 100, 90, 130, 150 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall, "(Step 2.7 完善)",
            0.585f, 0.40f, 0.018f,
            sakura::core::Color{ 80, 70, 110, 120 },
            sakura::core::TextAlign::Center);
    }
}

// ── RenderPropertyPanel ───────────────────────────────────────────────────────

void SceneEditor::RenderPropertyPanel(sakura::core::Renderer& renderer)
{
    // 属性面板：(0.42, 0.68, 0.33, 0.32)
    renderer.DrawFilledRect({ 0.42f, 0.68f, 0.33f, 0.32f },
        sakura::core::Color{ 10, 8, 24, 200 });
    renderer.DrawLine(0.42f, 0.68f, 0.75f, 0.68f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.42f, 0.68f, 0.42f, 1.00f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.75f, 0.68f, 0.75f, 1.00f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);

    if (m_fontSmall == sakura::core::INVALID_HANDLE) return;

    float px = 0.585f;
    renderer.DrawText(m_fontSmall, "属性面板",
        px, 0.70f, 0.020f,
        sakura::core::Color{ 160, 150, 200, 200 },
        sakura::core::TextAlign::Center);

    // 选中音符信息
    int selIdx = m_core.GetSelectedKbNote();
    if (selIdx >= 0 && selIdx < static_cast<int>(
            m_core.GetChartData().keyboardNotes.size()))
    {
        const auto& n = m_core.GetChartData().keyboardNotes[selIdx];
        renderer.DrawText(m_fontSmall,
            "选中: KB音符 #" + std::to_string(selIdx),
            px, 0.735f, 0.018f,
            sakura::core::Color{ 200, 200, 100, 210 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            "时间: " + std::to_string(n.time) + " ms",
            px, 0.762f, 0.018f,
            sakura::core::Color{ 180, 170, 220, 200 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            "轨道: " + std::to_string(n.lane) + "  时长: " + std::to_string(n.duration) + " ms",
            px, 0.789f, 0.018f,
            sakura::core::Color{ 180, 170, 220, 200 },
            sakura::core::TextAlign::Center);
    }
    else
    {
        // 总谱面信息
        const auto& info = m_core.GetChartInfo();
        renderer.DrawText(m_fontSmall,
            "曲名: " + info.title,
            px, 0.735f, 0.018f,
            sakura::core::Color{ 180, 170, 210, 180 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            "BPM: " + std::to_string(static_cast<int>(info.bpm)),
            px, 0.762f, 0.018f,
            sakura::core::Color{ 180, 170, 210, 180 },
            sakura::core::TextAlign::Center);
        int noteCount = static_cast<int>(m_core.GetChartData().keyboardNotes.size());
        renderer.DrawText(m_fontSmall,
            "KB音符总数: " + std::to_string(noteCount),
            px, 0.789f, 0.018f,
            sakura::core::Color{ 180, 170, 210, 180 },
            sakura::core::TextAlign::Center);

        // 当前时间 & BPM
        int curMs = m_core.GetCurrentTimeMs();
        float bpm = m_core.GetBpmAt(curMs);
        std::ostringstream timeSS;
        float sec = static_cast<float>(curMs) / 1000.0f;
        timeSS << (curMs < 0 ? "-" : "")
               << static_cast<int>(std::abs(sec)) << "."
               << std::setw(3) << std::setfill('0') << (std::abs(curMs) % 1000)
               << "s  BPM:" << static_cast<int>(bpm);
        renderer.DrawText(m_fontSmall, timeSS.str(),
            px, 0.816f, 0.018f,
            sakura::core::Color{ 160, 220, 160, 200 },
            sakura::core::TextAlign::Center);
    }

    // 快捷键提示
    renderer.DrawText(m_fontSmall, "1-5: 工具  Space: 播放  Del: 删除",
        px, 0.875f, 0.016f,
        sakura::core::Color{ 120, 110, 160, 150 },
        sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall, "Ctrl+Z: 撤销  Ctrl+Y: 重做  Ctrl+S: 保存",
        px, 0.898f, 0.016f,
        sakura::core::Color{ 120, 110, 160, 150 },
        sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall, "Ctrl+滚轮: 缩放  ESC: 退出",
        px, 0.921f, 0.016f,
        sakura::core::Color{ 120, 110, 160, 150 },
        sakura::core::TextAlign::Center);
}

// ── RenderOverviewAxis ────────────────────────────────────────────────────────

void SceneEditor::RenderOverviewAxis(sakura::core::Renderer& renderer)
{
    // 底部全曲缩略轴：(0.77, 0.06, 0.21, 0.94)
    constexpr float OVW_X = 0.77f;
    constexpr float OVW_Y = 0.06f;
    constexpr float OVW_W = 0.21f;
    constexpr float OVW_H = 0.94f;

    renderer.DrawFilledRect({ OVW_X, OVW_Y, OVW_W, OVW_H },
        sakura::core::Color{ 8, 6, 20, 200 });
    // 边框
    renderer.DrawLine(OVW_X, OVW_Y, OVW_X, OVW_Y + OVW_H,
        sakura::core::Color{ 55, 45, 85, 120 }, 0.001f);
    renderer.DrawLine(OVW_X + OVW_W, OVW_Y, OVW_X + OVW_W, OVW_Y + OVW_H,
        sakura::core::Color{ 55, 45, 85, 120 }, 0.001f);

    int totalMs = m_core.GetTotalDurationMs();
    if (totalMs <= 0) totalMs = 10000;

    // 背景中绘制所有音符缩略点
    const auto& notes = m_core.GetChartData().keyboardNotes;
    for (const auto& n : notes)
    {
        float progress = static_cast<float>(n.time) / static_cast<float>(totalMs);
        float noteY    = OVW_Y + OVW_H - progress * OVW_H;
        if (noteY < OVW_Y || noteY > OVW_Y + OVW_H) continue;

        float noteX = OVW_X + (n.lane + 0.5f) / 4.0f * OVW_W;
        renderer.DrawFilledRect(
            { noteX - 0.003f, noteY - 0.002f, 0.006f, 0.004f },
            sakura::core::Color{ 80, 130, 255, 180 });
    }

    // 播放头标记
    int curMs = m_core.GetCurrentTimeMs();
    if (curMs >= 0)
    {
        float progress = static_cast<float>(curMs) / static_cast<float>(totalMs);
        if (progress > 1.0f) progress = 1.0f;
        float headY = OVW_Y + OVW_H - progress * OVW_H;
        renderer.DrawLine(OVW_X, headY, OVW_X + OVW_W, headY,
            sakura::core::Color{ 255, 60, 100, 200 }, 0.002f);
    }

    // 可视区域指示器
    if (totalMs > 0)
    {
        float scrollProgress = static_cast<float>(m_timeline.GetScrollTimeMs())
                             / static_cast<float>(totalMs);
        float viewProgress   = static_cast<float>(
            m_timeline.GetScrollTimeMs()
          + static_cast<int>(4000)) / static_cast<float>(totalMs);

        float viewTop    = OVW_Y + OVW_H - std::clamp(viewProgress,   0.0f, 1.0f) * OVW_H;
        float viewBottom = OVW_Y + OVW_H - std::clamp(scrollProgress, 0.0f, 1.0f) * OVW_H;
        float viewHeight = viewBottom - viewTop;

        if (viewHeight > 0.005f)
        {
            renderer.DrawFilledRect(
                { OVW_X, viewTop, OVW_W, viewHeight },
                sakura::core::Color{ 100, 90, 160, 50 });
            renderer.DrawLine(OVW_X, viewTop, OVW_X + OVW_W, viewTop,
                sakura::core::Color{ 130, 120, 200, 120 }, 0.001f);
            renderer.DrawLine(OVW_X, viewBottom, OVW_X + OVW_W, viewBottom,
                sakura::core::Color{ 130, 120, 200, 120 }, 0.001f);
        }
    }

    if (m_fontSmall != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_fontSmall, "全曲轴",
            OVW_X + OVW_W * 0.5f, OVW_Y + 0.015f, 0.016f,
            sakura::core::Color{ 140, 130, 180, 180 },
            sakura::core::TextAlign::Center);
    }
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneEditor::OnEvent(const SDL_Event& event)
{
    // 追踪 Ctrl 键状态
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
        if (event.key.scancode == SDL_SCANCODE_LCTRL
         || event.key.scancode == SDL_SCANCODE_RCTRL)
        {
            m_ctrlHeld = pressed;
        }
    }

    // ── 键盘快捷键 ───────────────────────────────────────────────────────────
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const SDL_Scancode sc = event.key.scancode;

        // ESC → 返回主菜单
        if (sc == SDL_SCANCODE_ESCAPE)
        {
            if (m_core.IsDirty()) DoSave();
            m_manager.SwitchScene(
                std::make_unique<SceneMenu>(m_manager),
                TransitionType::SlideRight, 0.4f);
            return;
        }

        // 空格 → 播放/暂停
        if (sc == SDL_SCANCODE_SPACE)
        {
            // 直接调用播放逻辑
            if (m_btnPlay) m_btnPlay->HandleEvent(event);  // 触发 click
            return;
        }

        // Ctrl+S → 保存
        if (m_ctrlHeld && sc == SDL_SCANCODE_S)
        {
            DoSave();
            return;
        }

        // Ctrl+Z → 撤销
        if (m_ctrlHeld && sc == SDL_SCANCODE_Z)
        {
            m_core.Undo();
            UpdateUndoRedoButtons();
            return;
        }

        // Ctrl+Y → 重做
        if (m_ctrlHeld && sc == SDL_SCANCODE_Y)
        {
            m_core.Redo();
            UpdateUndoRedoButtons();
            return;
        }

        // Ctrl+A → 全选键盘音符（显示数量）
        if (m_ctrlHeld && sc == SDL_SCANCODE_A)
        {
            int cnt = static_cast<int>(m_core.GetChartData().keyboardNotes.size());
            sakura::ui::ToastManager::Instance().Show(
                "已选中 " + std::to_string(cnt) + " 个键盘音符",
                sakura::ui::ToastType::Info);
            return;
        }

        // Ctrl+M → 水平镜像所有键盘音符（道 0↔3，1↔2）
        if (m_ctrlHeld && sc == SDL_SCANCODE_M)
        {
            const auto& notes = m_core.GetChartData().keyboardNotes;
            if (!notes.empty())
            {
                auto batch = std::make_unique<sakura::editor::BatchCommand>("镜像音符");
                for (int i = 0; i < static_cast<int>(notes.size()); ++i)
                {
                    auto newNote = notes[i];
                    newNote.lane = 3 - newNote.lane;
                    if (newNote.dragToLane >= 0)
                        newNote.dragToLane = 3 - newNote.dragToLane;
                    batch->Add(std::make_unique<sakura::editor::ModifyNoteCommand>(
                        i, notes[i], newNote));
                }
                m_core.ExecuteCommand(std::move(batch));
                sakura::ui::ToastManager::Instance().Show(
                    "所有键盘音符已镜像", sakura::ui::ToastType::Info);
            }
            return;
        }

        // 1-5 → 切换音符工具
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_5)
        {
            int toolIdx = sc - SDL_SCANCODE_1;
            m_core.SetNoteTool(
                static_cast<sakura::editor::NoteToolType>(toolIdx));
            UpdateToolButtons();
            return;
        }

        // Delete → 删除选中音符
        if (sc == SDL_SCANCODE_DELETE)
        {
            int sel = m_core.GetSelectedKbNote();
            if (sel >= 0)
            {
                m_core.DeleteKeyboardNote(sel);
                m_core.ClearSelection();
                sakura::ui::ToastManager::Instance().Show(
                    "已删除选中音符", sakura::ui::ToastType::Info);
                return;
            }
            int msel = m_core.GetSelectedMouseNote();
            if (msel >= 0)
            {
                m_core.DeleteMouseNote(msel);
                m_core.ClearSelection();
                sakura::ui::ToastManager::Instance().Show(
                    "已删除鼠标音符", sakura::ui::ToastType::Info);
            }
            return;
        }

        // Ctrl+滚轮由 timeline 处理，但 Ctrl 单独按下时不做其他事
    }

    // ── 工具栏事件 ───────────────────────────────────────────────────────────
    for (auto& btn : m_toolBtns) if (btn) btn->HandleEvent(event);
    if (m_btnPlay)    m_btnPlay->HandleEvent(event);
    if (m_btnUndo)    m_btnUndo->HandleEvent(event);
    if (m_btnRedo)    m_btnRedo->HandleEvent(event);
    if (m_btnSave)    m_btnSave->HandleEvent(event);
    if (m_btnBack)    m_btnBack->HandleEvent(event);
    if (m_btnSnapDec) m_btnSnapDec->HandleEvent(event);
    if (m_btnSnapInc) m_btnSnapInc->HandleEvent(event);

    // ── 时间轴事件（可能消费事件） ───────────────────────────────────────────
    m_mouseArea.HandleEvent(event);
    m_timeline.HandleEvent(event);
}

} // namespace sakura::scene
