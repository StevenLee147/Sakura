#pragma once

// scene_select.h — 选歌场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "game/chart.h"
#include "ui/button.h"
#include "ui/scroll_list.h"

#include <memory>
#include <vector>

namespace sakura::scene
{

// SceneSelect — 选歌界面
// 布局（归一化）：
//   标题   "SELECT SONG"           (0.5, 0.04, 居中)
//   左侧   ScrollList              (0.02, 0.10, 0.45, 0.80)
//   右侧   详情面板                 (0.50, 0.10, 0.48, 0.80)
//   底部   "返回" / "开始"按钮      y=0.93
//
//   歌曲预览：选中 0.5s 后播放 previewTime 位置音乐（淡入淡出）
class SceneSelect final : public Scene
{
public:
    explicit SceneSelect(SceneManager& mgr);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    // 谱面列表（OnEnter 扫描填充）
    std::vector<sakura::game::ChartInfo> m_charts;
    int m_selectedChart    = -1;  // 当前选中曲目下标
    int m_selectedDifficulty = 0; // 当前选中难度下标

    // UI 组件
    std::unique_ptr<sakura::ui::ScrollList> m_songList;
    std::unique_ptr<sakura::ui::Button>     m_btnBack;
    std::unique_ptr<sakura::ui::Button>     m_btnStart;

    // 难度选择按钮（最多 8 个）
    static constexpr int MAX_DIFF_BUTTONS = 8;
    std::vector<std::unique_ptr<sakura::ui::Button>> m_diffButtons;

    // 字体
    sakura::core::FontHandle m_fontUI    = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSmall = sakura::core::INVALID_HANDLE;

    // 音乐预览
    float m_previewTimer  = 0.0f;   // 选中后计时，超过 0.5s 开始预览
    bool  m_previewPlaying = false;  // 当前是否正在预览
    int   m_lastPreviewChart = -1;   // 上次预览的曲目，避免重复触发
    static constexpr float PREVIEW_DELAY = 0.5f;

    // 封面纹理（当前选中曲目）
    sakura::core::TextureHandle m_coverTexture = sakura::core::INVALID_HANDLE;

    // 内部工具
    void SetupUI();
    void RefreshDifficultyButtons();
    void UpdateSongList();
    void OnSongSelected(int index);
    void StartPreview();
    void StopPreview();
    void RenderDetailPanel(sakura::core::Renderer& renderer);

    std::string FormatListItem(const sakura::game::ChartInfo& info) const;
};

} // namespace sakura::scene
