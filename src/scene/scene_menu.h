#pragma once

// scene_menu.h — 主菜单场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"

#include <array>
#include <memory>

namespace sakura::scene
{

// SceneMenu — 主菜单
// 布局（归一化）：
//   标题  "Sakura-樱"           (0.5, 0.22, size=0.08, 居中)
//   副标题 "Mixed-Mode Rhythm…" (0.5, 0.31, size=0.025)
//   按钮区：宽0.22, 高0.055, 从 y=0.48 开始依次排列
//   版本号 (0.5, 0.95)
//
// 入场动画：标题从上滑入(0.3s), 按钮依次从右滑入(间隔0.1s)
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

    // 按钮组（开始/编辑器/设置/退出）
    static constexpr int BUTTON_COUNT = 4;
    std::array<std::unique_ptr<sakura::ui::Button>, BUTTON_COUNT> m_buttons;

    // 按钮中心 X（居中）
    static constexpr float BTN_X     = 0.5f - 0.11f;   // (center - half-width)
    static constexpr float BTN_W     = 0.22f;
    static constexpr float BTN_H     = 0.055f;
    static constexpr float BTN_GAP   = 0.082f;          // 每个按钮的 Y 间距
    static constexpr float BTN_Y0    = 0.455f;          // 第一个按钮 Y

    // ── 入场动画 ──────────────────────────────────────────────────────────────
    struct EnterAnim
    {
        // 标题从上方 -0.15 滑到目标 Y（0.22）
        float titleOffsetY = -0.15f;   // 当前偏移（0 = 到位）
        float titleTimer   = 0.0f;     // 到达时间计时器
        static constexpr float TITLE_DURATION = 0.3f;

        // 每个按钮从右方滑入（初始 X 偏移 +0.4）
        std::array<float, 4> btnOffsetX = { 0.4f, 0.4f, 0.4f, 0.4f };
        std::array<float, 4> btnTimers  = { 0.0f, 0.0f, 0.0f, 0.0f };
        static constexpr float BTN_DURATION     = 0.30f;   // 每个按钮动画时长
        static constexpr float BTN_STAGGER      = 0.10f;   // 按钮启动间隔

        bool done = false;   // 动画是否全部完成
    } m_anim;

    float m_enterTimer = 0.0f;   // 进场总计时

    // 目标 Y 坐标（动画完成后的稳定位置）
    static constexpr float TITLE_Y = 0.22f;

    // 内部工具
    void SetupButtons();
    void UpdateEnterAnimation(float dt);
};

} // namespace sakura::scene
