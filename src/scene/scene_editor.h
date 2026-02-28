#pragma once

// scene_editor.h — 谱面编辑器场景
// 包含：工具栏、键盘轨道时间轴、鼠标编辑区（占位）、属性面板（占位）、底部总览轴

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "editor/editor_core.h"
#include "editor/editor_timeline.h"
#include "editor/editor_mouse_area.h"
#include "editor/editor_preview.h"

#include <memory>
#include <array>
#include <string>

namespace sakura::scene
{

// SceneEditor — 编辑器场景
// 快捷键：空格=播放/暂停, 1-5=音符工具, Delete=删除选中, Ctrl+S=保存, ESC=退出
class SceneEditor final : public Scene
{
public:
    // 进入编辑器：可选指定谱面目录和难度文件（空=创建新谱面）
    explicit SceneEditor(SceneManager& mgr,
                         const std::string& folderPath    = "",
                         const std::string& difficultyFile = "normal.json");

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    // ── 核心状态 ──────────────────────────────────────────────────────────────
    sakura::editor::EditorCore      m_core;
    sakura::editor::EditorTimeline  m_timeline;
    sakura::editor::EditorMouseArea m_mouseArea;
    sakura::editor::EditorPreview   m_preview;

    std::string m_initFolderPath;
    std::string m_initDiffFile;

    // ── 字体 ──────────────────────────────────────────────────────────────────
    sakura::core::FontHandle m_fontUI    = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSmall = sakura::core::INVALID_HANDLE;

    // ── 工具栏 UI ─────────────────────────────────────────────────────────────
    // 音符工具按钮（1-5）：Tap/Hold/Drag/Circle/Slider
    static constexpr int TOOL_COUNT = 5;
    std::array<std::unique_ptr<sakura::ui::Button>, TOOL_COUNT> m_toolBtns;

    // 播放/暂停按钮
    std::unique_ptr<sakura::ui::Button> m_btnPlay;

    // 撤销/重做按钮
    std::unique_ptr<sakura::ui::Button> m_btnUndo;
    std::unique_ptr<sakura::ui::Button> m_btnRedo;

    // 保存按钮
    std::unique_ptr<sakura::ui::Button> m_btnSave;

    // 退出编辑器按钮
    std::unique_ptr<sakura::ui::Button> m_btnBack;

    // BeatSnap 按钮（↑ ↓）
    std::unique_ptr<sakura::ui::Button> m_btnSnapInc;
    std::unique_ptr<sakura::ui::Button> m_btnSnapDec;

    // 难度管理（展示在属性面板）
    std::unique_ptr<sakura::ui::Button> m_btnDiffPrev;   // 上一难度
    std::unique_ptr<sakura::ui::Button> m_btnDiffNext;   // 下一难度
    std::unique_ptr<sakura::ui::Button> m_btnDiffAdd;    // 新建难度
    int m_currentDiffIndex = 0;                          // 当前难度索引

    // ── 按键辅助 ──────────────────────────────────────────────────────────────
    bool m_ctrlHeld  = false;
    bool m_shiftHeld = false;

    // ── 内部方法 ──────────────────────────────────────────────────────────────
    void SetupToolbar();
    void UpdateToolButtons();       // 刷新工具按钮的选中状态颜色
    void UpdateUndoRedoButtons();   // 刷新撤销/重做按钮状态
    void DoSave();                  // 保存谱面并显示 Toast
    void SwitchDifficulty(int index);  // 切换到指定难度索引
    void AddNewDifficulty();           // 新建难度（从当前难度复制）

    // ── 渲染分区 ──────────────────────────────────────────────────────────────
    void RenderToolbar       (sakura::core::Renderer& renderer);
    void RenderMouseArea     (sakura::core::Renderer& renderer);  // 现在由 m_mouseArea.Render 代替，保留占位
    void RenderPropertyPanel (sakura::core::Renderer& renderer);
    void RenderOverviewAxis  (sakura::core::Renderer& renderer);
};

} // namespace sakura::scene
