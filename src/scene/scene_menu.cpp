// scene_menu.cpp — 主菜单场景

#include "scene_menu.h"
#include "core/input.h"
#include "utils/logger.h"
#include "utils/easing.h"

// version.h 由 CMake 生成
#if __has_include("version.h")
#  include "version.h"
#else
#  define SAKURA_VERSION_STRING "0.0.0"
#endif

#include <algorithm>
#include <cmath>
#include <memory>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneMenu::SceneMenu(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneMenu::OnEnter()
{
    LOG_INFO("[SceneMenu] 进入主菜单");

    // 获取字体
    auto& rm      = sakura::core::ResourceManager::GetInstance();
    m_fontTitle   = rm.GetDefaultFontHandle();
    m_fontSub     = rm.GetDefaultFontHandle();
    m_fontButton  = rm.GetDefaultFontHandle();

    // 初始化入场动画
    m_enterTimer            = 0.0f;
    m_anim.titleOffsetY     = -0.15f;
    m_anim.titleTimer       = 0.0f;
    m_anim.done             = false;
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        m_anim.btnOffsetX[i] = 0.4f;
        m_anim.btnTimers[i]  = 0.0f;
    }

    // 创建按钮（在最终位置，X 偏移在渲染时动态应用）
    SetupButtons();
}

// ── SetupButtons ──────────────────────────────────────────────────────────────

void SceneMenu::SetupButtons()
{
    const char* labels[BUTTON_COUNT] = {
        "开始游戏", "编辑器", "设置", "退出"
    };

    sakura::ui::ButtonColors colors;
    colors.normal   = { 40, 35, 70, 210 };
    colors.hover    = { 75, 60, 120, 235 };
    colors.pressed  = { 25, 20, 50, 240 };
    colors.disabled = { 30, 30, 50, 120 };
    colors.text     = sakura::core::Color::White;

    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        float y = BTN_Y0 + i * BTN_GAP;
        sakura::core::NormRect bounds = { BTN_X, y, BTN_W, BTN_H };
        m_buttons[i] = std::make_unique<sakura::ui::Button>(
            bounds, labels[i], m_fontButton, 0.028f, 0.012f);
        m_buttons[i]->SetColors(colors);
    }

    // 开始游戏 → 切换到选歌场景
    m_buttons[0]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：开始游戏");
        // Step 1.10 完成后替换为 SceneSelect
        // m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager),
        //                       TransitionType::SlideLeft);
        LOG_INFO("[SceneMenu] (选歌场景 Step 1.10 实现后跳转)");
    });

    // 编辑器（占位）
    m_buttons[1]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：编辑器 (Phase 2 实现)");
    });

    // 设置（占位）
    m_buttons[2]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：设置 (Phase 2 实现)");
    });

    // 退出
    m_buttons[3]->SetOnClick([]()
    {
        LOG_INFO("[SceneMenu] 点击：退出");
        SDL_Event quitEvent;
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    });
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneMenu::OnExit()
{
    LOG_INFO("[SceneMenu] 退出主菜单");
    for (auto& btn : m_buttons) btn.reset();
}

// ── UpdateEnterAnimation ──────────────────────────────────────────────────────

void SceneMenu::UpdateEnterAnimation(float dt)
{
    if (m_anim.done) return;

    m_enterTimer += dt;

    // 标题滑入（0.3s EaseOutBack）
    m_anim.titleTimer += dt;
    float t = std::min(1.0f, m_anim.titleTimer / EnterAnim::TITLE_DURATION);
    m_anim.titleOffsetY = -0.15f * (1.0f - sakura::utils::EaseOutBack(t));

    // 按钮依次滑入（间隔 0.1s）
    bool allDone = (t >= 1.0f);
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        // 第 i 个按钮在 titleDuration + i*stagger 后开始
        float startTime = EnterAnim::TITLE_DURATION + i * EnterAnim::BTN_STAGGER;
        float elapsed   = m_enterTimer - startTime;

        if (elapsed <= 0.0f)
        {
            m_anim.btnOffsetX[i] = 0.4f;
            allDone = false;
        }
        else
        {
            float bt = std::min(1.0f, elapsed / EnterAnim::BTN_DURATION);
            m_anim.btnOffsetX[i] = 0.4f * (1.0f - sakura::utils::EaseOutCubic(bt));
            if (bt < 1.0f) allDone = false;
        }
    }

    if (allDone) m_anim.done = true;
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneMenu::OnUpdate(float dt)
{
    UpdateEnterAnimation(dt);

    // 更新按钮（按钮 Update 处理悬停动画）
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (m_buttons[i])
        {
            // 临时移动按钮 bounds 到动画偏移位置
            auto origBounds = m_buttons[i]->GetBounds();
            sakura::core::NormRect animBounds = origBounds;
            animBounds.x = origBounds.x + m_anim.btnOffsetX[i];
            m_buttons[i]->SetBounds(animBounds);

            m_buttons[i]->Update(dt);

            // 恢复原始 bounds（渲染时再次应用偏移）
            m_buttons[i]->SetBounds(origBounds);
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneMenu::OnRender(sakura::core::Renderer& renderer)
{
    // ── 背景渐变（深蓝/紫渐变近似：画两个叠加矩形）────────────────────────────
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 12, 10, 28, 255 });
    // 中央光晕
    renderer.DrawCircleFilled(0.5f, 0.4f, 0.45f,
        sakura::core::Color{ 30, 15, 55, 80 });

    if (m_fontTitle == sakura::core::INVALID_HANDLE) return;

    float titleY = TITLE_Y + m_anim.titleOffsetY;

    // ── 标题 ──────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontTitle, "Sakura-樱",
        0.5f, titleY, 0.08f,
        sakura::core::Color{ 255, 200, 220, 240 },
        sakura::core::TextAlign::Center);

    // ── 副标题 ────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "Mixed-Mode Rhythm Game",
        0.5f, titleY + 0.10f, 0.025f,
        sakura::core::Color{ 200, 180, 210, 160 },
        sakura::core::TextAlign::Center);

    // ── 按钮（应用 X 动画偏移） ───────────────────────────────────────────────
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i];
        m_buttons[i]->SetBounds(animBounds);

        m_buttons[i]->Render(renderer);

        m_buttons[i]->SetBounds(origBounds);   // 还原
    }

    // ── 版本号 ────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "v" SAKURA_VERSION_STRING,
        0.5f, 0.95f, 0.018f,
        sakura::core::Color{ 140, 130, 160, 120 },
        sakura::core::TextAlign::Center);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneMenu::OnEvent(const SDL_Event& event)
{
    // 分发给按钮（需要用带有当前 X 偏移的坐标）
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        // 临时应用偏移，让按钮能正确做命中测试
        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i];
        m_buttons[i]->SetBounds(animBounds);

        m_buttons[i]->HandleEvent(event);

        m_buttons[i]->SetBounds(origBounds);
    }
}

} // namespace sakura::scene
