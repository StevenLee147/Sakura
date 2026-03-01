#pragma once

// scene_menu.h — 主菜单场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "effects/particle_system.h"
#include "effects/glow.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace sakura::scene
{

// SceneMenu — 主菜单
// 布局（归一化）：
//   标题  "Sakura-樱"           (0.5, 0.22, size=0.08, 居中)
//   副标题 "Mixed-Mode Rhythm…" (0.5, 0.31, size=0.025)
//   按钮区：宽0.22, 高0.055, 从 y=0.48 开始依次排列
//   版本号 (0.5, 0.95)
//
// 入场动画：标题从上滑入(0.3s), 按钮依次从下往上滑入(间隔0.1s)
class SceneMenu final : public Scene
{
public:
    explicit SceneMenu(SceneManager& mgr);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    // 字体
    sakura::core::FontHandle m_fontTitle   = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSub     = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontButton  = sakura::core::INVALID_HANDLE;

    // 按钮组（开始/谱面编辑器/设置/退出）
    static constexpr int BUTTON_COUNT = 4;
    std::array<std::unique_ptr<sakura::ui::Button>, BUTTON_COUNT> m_buttons;

    // 按钮中心 X（居中）
    static constexpr float BTN_X     = 0.5f - 0.11f;   // (center - half-width)
    static constexpr float BTN_W     = 0.22f;
    static constexpr float BTN_H     = 0.050f;
    static constexpr float BTN_GAP   = 0.075f;          // 每个按钮的 Y 间距
    static constexpr float BTN_Y0    = 0.43f;           // 第一个按钮 Y（4个按钮居中）

    // ── 入场动画 ──────────────────────────────────────────────────────────────
    struct EnterAnim
    {
        // 标题从上方 -0.15 滑到目标 Y（0.22）
        float titleOffsetY = -0.15f;   // 当前偏移（0 = 到位）
        float titleTimer   = 0.0f;     // 到达时间计时器
        static constexpr float TITLE_DURATION = 0.3f;

        // 每个按钮从下方滑入（初始 Y 偏移 +0.25，向上滑到目标）
        std::array<float, 4> btnOffsetY = { 0.25f, 0.25f, 0.25f, 0.25f };
        std::array<float, 4> btnTimers  = { 0.0f,  0.0f,  0.0f,  0.0f  };
        static constexpr float BTN_DURATION = 0.32f;   // 每个按钮动画时长
        static constexpr float BTN_STAGGER  = 0.08f;   // 按钮启动间隔

        bool done = false;   // 动画是否全部完成
    } m_anim;

    float m_enterTimer = 0.0f;   // 进场总计时

    // 目标 Y 坐标（动画完成后的稳定位置）
    static constexpr float TITLE_Y = 0.22f;

    // ── 谱面编辑器子菜单 ───────────────────────────────────────────────────────
    bool m_showEditorMenu = false;
    std::unique_ptr<sakura::ui::Button> m_btnEditorOpen;   // 打开已有谱面
    std::unique_ptr<sakura::ui::Button> m_btnEditorNew;    // 新建谱面
    std::unique_ptr<sakura::ui::Button> m_btnEditorCancel; // 取消

    // 自制谱文件夹（仅允许在此目录下打开/编辑）
    static constexpr const char* CUSTOM_CHARTS_PATH = "resources/charts/custom/";

    // 自制谱列表（打开面板时扫描）
    struct ChartEntry { std::string folderPath; std::string title; };
    std::vector<ChartEntry> m_customCharts;
    int m_selectedChartIdx = -1;  // 选中的谱面索引

    // ── 退出确认对话框 ─────────────────────────────────────────────────────────
    bool m_showExitConfirm = false;
    std::unique_ptr<sakura::ui::Button> m_btnConfirmYes;
    std::unique_ptr<sakura::ui::Button> m_btnConfirmNo;

    // 内部工具
    void SetupButtons();
    void SetupConfirmButtons();
    void SetupEditorMenuButtons();
    void ScanCustomCharts();
    void OpenEditorForChart(int idx);
    void UpdateEnterAnimation(float dt);

    // ── 视觉特效 ─────────────────────────────────────────────────────────────
    sakura::effects::ParticleSystem m_particles;
    int   m_sakuraPetalEmitter = -1;   // 持续发射器 ID
    int   m_bgFloatEmitter     = -1;   // 背景微粒发射器 ID
    float m_glowPhase          = 0.0f; // 标题发光脉冲相位（随时间递增）
};

} // namespace sakura::scene
