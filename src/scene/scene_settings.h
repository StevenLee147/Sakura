#pragma once

// scene_settings.h — 设置场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "ui/slider.h"
#include "ui/toggle.h"
#include "ui/dropdown.h"
#include "ui/tab_bar.h"
#include "ui/label.h"
#include "ui/toast.h"

#include <array>
#include <memory>
#include <functional>

namespace sakura::scene
{

// SceneSettings : Scene
// 设置界面：TabBar（通用/音频/按键/显示）+ 对应内容区
// 所有设置实时生效，自动保存到 config/settings.json
class SceneSettings final : public Scene
{
public:
    explicit SceneSettings(SceneManager& mgr);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    sakura::core::FontHandle m_font      = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSmall = sakura::core::INVALID_HANDLE;

    // ── TabBar ────────────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::TabBar> m_tabBar;
    int m_currentTab = 0;

    // Tab 索引
    static constexpr int TAB_GENERAL = 0;
    static constexpr int TAB_AUDIO   = 1;
    static constexpr int TAB_KEYS    = 2;
    static constexpr int TAB_DISPLAY = 3;

    // ── 通用设置控件 ──────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::Slider> m_sliderNoteSpeed;
    std::unique_ptr<sakura::ui::Slider> m_sliderOffset;
    std::unique_ptr<sakura::ui::Button> m_btnCalibrate;

    // ── 音频设置控件 ──────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::Slider>   m_sliderMaster;
    std::unique_ptr<sakura::ui::Slider>   m_sliderMusic;
    std::unique_ptr<sakura::ui::Slider>   m_sliderSFX;
    std::unique_ptr<sakura::ui::Dropdown> m_dropHitsound;

    // ── 按键绑定控件 ──────────────────────────────────────────────────────────
    static constexpr int KEY_BIND_COUNT = 6;  // lane0~3 + pause + retry
    std::array<std::unique_ptr<sakura::ui::Button>, KEY_BIND_COUNT> m_keyButtons;
    std::unique_ptr<sakura::ui::Button> m_btnResetKeys;

    // 按键绑定状态
    int  m_listeningKeyIndex = -1;    // 正在等待输入的按键索引(-1=无)
    float m_listenTimer      = 0.0f;  // 监听超时计时
    static constexpr float LISTEN_TIMEOUT = 5.0f;

    // 当前绑定扫描码（每次 Init 时从 Config 读取）
    std::array<SDL_Scancode, KEY_BIND_COUNT> m_keyCodes{};

    void UpdateKeyButtonLabels();
    bool HasKeyConflict(int index, SDL_Scancode code) const;
    const char* GetKeyBindName(int index) const;

    // ── 显示设置控件 ──────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::Toggle>   m_toggleFullscreen;
    std::unique_ptr<sakura::ui::Dropdown> m_dropFpsLimit;
    std::unique_ptr<sakura::ui::Toggle>   m_toggleVSync;

    // ── 底部返回按钮 ──────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::Button> m_btnBack;

    // ── 布局常量 ──────────────────────────────────────────────────────────────
    // 左侧 TabBar
    static constexpr float TAB_X = 0.03f;
    static constexpr float TAB_Y = 0.12f;
    static constexpr float TAB_W = 0.15f;
    static constexpr float TAB_H = 0.75f;

    // 右侧内容区起始位置
    static constexpr float CONTENT_X = 0.22f;
    static constexpr float CONTENT_Y = 0.12f;
    static constexpr float CONTENT_W = 0.73f;
    static constexpr float CONTENT_H = 0.75f;

    // ── 初始化各 Tab 控件 ─────────────────────────────────────────────────────
    void SetupGeneralTab();
    void SetupAudioTab();
    void SetupKeysTab();
    void SetupDisplayTab();
    void SetupBackButton();

    // ── 从 Config 加载所有值到控件 ────────────────────────────────────────────
    void LoadFromConfig();

    // ── 保存按键绑定到 Config ─────────────────────────────────────────────────
    void SaveKeyBindings();

    // ── 渲染各 Tab 内容区 ─────────────────────────────────────────────────────
    void RenderGeneralTab(sakura::core::Renderer& renderer);
    void RenderAudioTab(sakura::core::Renderer& renderer);
    void RenderKeysTab(sakura::core::Renderer& renderer);
    void RenderDisplayTab(sakura::core::Renderer& renderer);

    // 绘制分节标题
    void DrawSectionTitle(sakura::core::Renderer& renderer,
                          const char* title, float y);

    // 标签中心 Y
    float SlotY(int row) const
    {
        return CONTENT_Y + 0.05f + static_cast<float>(row) * 0.085f;
    }
};

} // namespace sakura::scene
