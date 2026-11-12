// scene_menu.cpp — 主菜单场景

#include "scene_menu.h"
#include "scene_select.h"
#include "scene_stats.h"
#include "scene_tutorial.h"
#include "scene_settings.h"
#include "scene_editor.h"
#include "scene_chart_wizard.h"
#include "audio/audio_visualizer.h"
#include "core/input.h"
#include "core/config.h"
#include "core/paths.h"
#include "game/chart_loader.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "effects/particle_system.h"
#include "effects/glow.h"
#include "ui/visual_style.h"

// version.h 由 CMake 生成
#if __has_include("version.h")
#  include "version.h"
#else
#  define SAKURA_VERSION_STRING "0.0.0"
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <filesystem>

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
    m_anim.titleOffsetX     = -0.15f;
    m_anim.titleTimer       = 0.0f;
    m_anim.done             = false;
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        m_anim.btnOffsetX[i] = -0.25f;
        m_anim.btnTimers[i]  = 0.0f;
    }

    // 重置状态
    m_showExitConfirm  = false;
    m_showEditorMenu   = false;
    m_selectedChartIdx = -1;
    m_customCharts.clear();

    // ── 特效初始化 ────────────────────────────────────────────────────────────
    m_particles.Clear();
    m_glowPhase = 0.0f;
    m_clickRings.clear();

    // 樱花飘落（伪3D三层）
    auto cfgFG = sakura::effects::ParticlePresets::SakuraPetalForeground();
    m_sakuraPetalEmitterFG = m_particles.EmitContinuous(0.5f, -0.05f, 2.0f, cfgFG);

    auto cfgMG = sakura::effects::ParticlePresets::SakuraPetalMidground();
    m_sakuraPetalEmitterMG = m_particles.EmitContinuous(0.5f, -0.05f, 5.0f, cfgMG);

    auto cfgBG = sakura::effects::ParticlePresets::SakuraPetalBackground();
    m_sakuraPetalEmitterBG = m_particles.EmitContinuous(0.5f, -0.05f, 10.0f, cfgBG);

    // 背景微粒：全屏随机漂浮（3/s，低透明度）
    auto bgCfg = sakura::effects::ParticlePresets::BackgroundFloat();
    m_bgFloatEmitter = m_particles.EmitContinuous(0.5f, 0.5f, 3.0f, bgCfg);

    // 创建按钮
    SetupButtons();
    SetupConfirmButtons();
    SetupEditorMenuButtons();
}

// ── SetupButtons ──────────────────────────────────────────────────────────────

void SceneMenu::SetupButtons()
{
    const char* labels[BUTTON_COUNT] = {
        "开始旅程   /   PLAY", "初次见面   /   教程", "我的记录   /   统计", "谱面工房   /   编辑器", "偏好设置   /   SETTINGS", "离开游戏"
    };

    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        float y = BTN_Y0 + i * BTN_GAP;
        sakura::core::NormRect bounds = { BTN_X, y, BTN_W, BTN_H };
        m_buttons[i] = std::make_unique<sakura::ui::Button>(
            bounds, labels[i], m_fontButton, 0.028f, 0.012f);
        sakura::ui::VisualStyle::ApplyButton(
            m_buttons[i].get(),
            i == 0 ? sakura::ui::ButtonVariant::Primary
                   : (i == BUTTON_COUNT - 1 ? sakura::ui::ButtonVariant::Danger
                                            : sakura::ui::ButtonVariant::Secondary));
        m_buttons[i]->SetTextAlign(sakura::core::TextAlign::Left);
        m_buttons[i]->SetTextPadding(0.02f);
    }

    // 开始游戏 → 切换到选歌场景
    m_buttons[0]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：开始游戏");
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            TransitionType::SakuraSweep, 0.45f);
    });

    // 教程 → 新手教程场景
    m_buttons[1]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：教程");
        m_manager.SwitchScene(
            std::make_unique<SceneTutorial>(m_manager, false),
            TransitionType::SilkShutter, 0.42f);
    });

    // 统计 → 进入统计场景
    m_buttons[2]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：统计");
        m_manager.SwitchScene(
            std::make_unique<SceneStats>(m_manager),
            TransitionType::SilkShutter, 0.42f);
    });

    // 谱面编辑器 → 显示编辑器子菜单
    m_buttons[3]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：谱面编辑器");
        ScanCustomCharts();
        m_showEditorMenu = true;
    });

    // 设置 → 切换到设置场景
    m_buttons[4]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：设置");
        m_manager.SwitchScene(
            std::make_unique<SceneSettings>(m_manager),
            TransitionType::SilkShutter, 0.42f);
    });

    // 退出 → 弹出确认对话框
    m_buttons[5]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：退出（显示确认框）");
        m_showExitConfirm = true;
    });
}

// ── SetupEditorMenuButtons ────────────────────────────────────────────────────

void SceneMenu::SetupEditorMenuButtons()
{
    // 面板区域：x=0.30, y=0.28, w=0.40, h=0.44；按钮居中布局
    m_btnEditorOpen = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.355f, 0.455f, 0.29f, 0.055f },
        "打开已有谱面", m_fontButton, 0.026f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnEditorOpen.get(), sakura::ui::ButtonVariant::Primary);
    m_btnEditorOpen->SetOnClick([this]()
    {
        if (m_customCharts.empty())
        {
            LOG_INFO("[SceneMenu] 暂无自制谱面，无法打开");
        }
        else
        {
            int idx = (m_selectedChartIdx >= 0 &&
                       m_selectedChartIdx < static_cast<int>(m_customCharts.size()))
                      ? m_selectedChartIdx : 0;
            OpenEditorForChart(idx);
        }
    });

    m_btnEditorNew = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.355f, 0.525f, 0.29f, 0.055f },
        "新建谱面", m_fontButton, 0.026f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnEditorNew.get(), sakura::ui::ButtonVariant::Accent);
    m_btnEditorNew->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：新建谱面向导");
        m_showEditorMenu = false;
        m_manager.SwitchScene(
            std::make_unique<SceneChartWizard>(m_manager),
            TransitionType::SlideLeft, 0.4f);
    });

    m_btnEditorCancel = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.355f, 0.600f, 0.29f, 0.050f },
        "关 闭", m_fontButton, 0.024f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnEditorCancel.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnEditorCancel->SetOnClick([this]()
    {
        m_showEditorMenu = false;
    });
}

// ── ScanCustomCharts ──────────────────────────────────────────────────────────

void SceneMenu::ScanCustomCharts()
{
    m_customCharts.clear();
    m_selectedChartIdx = -1;

    // 确保谱面目录存在
    std::error_code ec;
    std::filesystem::create_directories(sakura::core::Paths::User("charts"), ec);

    sakura::game::ChartLoader loader;
    auto charts = loader.ScanCharts(sakura::core::Paths::User("charts"));
    auto builtin = loader.ScanCharts(CUSTOM_CHARTS_PATH);
    for(auto& chart:builtin)if(std::none_of(charts.begin(),charts.end(),[&](const auto& other){return other.id==chart.id;}))charts.push_back(std::move(chart));
    for (auto& ci : charts)
    {
        ChartEntry entry;
        entry.folderPath = ci.folderPath;
        entry.title      = ci.title.empty() ? ci.id : ci.title;
        entry.diffFile   = !ci.difficulties.empty() && !ci.difficulties.front().chartFile.empty()
                         ? ci.difficulties.front().chartFile
                         : "normal.json";
        m_customCharts.push_back(std::move(entry));
    }

    if (!m_customCharts.empty())
        m_selectedChartIdx = 0;

    LOG_INFO("[SceneMenu] 扫描自制谱: {} 首", m_customCharts.size());
}

// ── OpenEditorForChart ────────────────────────────────────────────────────────

void SceneMenu::OpenEditorForChart(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_customCharts.size())) return;

    const auto& entry = m_customCharts[idx];
    LOG_INFO("[SceneMenu] 打开自制谱编辑器: {}", entry.folderPath);
    m_showEditorMenu = false;
    m_manager.SwitchScene(
        std::make_unique<SceneEditor>(m_manager,
                                      entry.folderPath,
                                      entry.diffFile),
        TransitionType::SlideLeft, 0.4f);
}

// ── SetupConfirmButtons ───────────────────────────────────────────────────────

void SceneMenu::SetupConfirmButtons()
{
    // 确认框内两个按钮，居中布局
    // 面板: x=0.32 y=0.35 w=0.36 h=0.26
    m_btnConfirmYes = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.348f, 0.505f, 0.13f, 0.055f },
        "确认退出", m_fontButton, 0.026f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnConfirmYes.get(), sakura::ui::ButtonVariant::Danger);
    m_btnConfirmYes->SetOnClick([]()
    {
        LOG_INFO("[SceneMenu] 确认退出");
        SDL_Event quitEvent;
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    });

    m_btnConfirmNo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.502f, 0.505f, 0.13f, 0.055f },
        "取 消", m_fontButton, 0.026f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnConfirmNo.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnConfirmNo->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 取消退出");
        m_showExitConfirm = false;
    });
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneMenu::OnExit()
{
    LOG_INFO("[SceneMenu] 退出主菜单");
    m_particles.StopEmitter(m_sakuraPetalEmitterFG);
    m_particles.StopEmitter(m_sakuraPetalEmitterMG);
    m_particles.StopEmitter(m_sakuraPetalEmitterBG);
    m_particles.StopEmitter(m_bgFloatEmitter);
    m_sakuraPetalEmitterFG = -1;
    m_sakuraPetalEmitterMG = -1;
    m_sakuraPetalEmitterBG = -1;
    m_bgFloatEmitter     = -1;
    m_particles.Clear();
    for (auto& btn : m_buttons) btn.reset();
    m_btnConfirmYes.reset();
    m_btnConfirmNo.reset();
    m_btnEditorOpen.reset();
    m_btnEditorNew.reset();
    m_btnEditorCancel.reset();
}

// ── UpdateEnterAnimation ──────────────────────────────────────────────────────

void SceneMenu::UpdateEnterAnimation(float dt)
{
    if (m_anim.done) return;

    m_enterTimer += dt;

    // 标题滑入（0.3s EaseOutBack）
    m_anim.titleTimer += dt;
    float t = std::min(1.0f, m_anim.titleTimer / EnterAnim::TITLE_DURATION);
    m_anim.titleOffsetX = -0.15f * (1.0f - sakura::utils::EaseOutBack(t));

    // 按钮依次从左往右滑入（间隔 0.08s，EaseOutCubic）
    bool allDone = (t >= 1.0f);
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        // 第 i 个按钮在 titleDuration + i*stagger 后开始
        float startTime = EnterAnim::TITLE_DURATION + i * EnterAnim::BTN_STAGGER;
        float elapsed   = m_enterTimer - startTime;

        if (elapsed <= 0.0f)
        {
            m_anim.btnOffsetX[i] = -0.25f;
            allDone = false;
        }
        else
        {
            float bt = std::min(1.0f, elapsed / EnterAnim::BTN_DURATION);
            m_anim.btnOffsetX[i] = -0.25f * (1.0f - sakura::utils::EaseOutCubic(bt));
            if (bt < 1.0f) allDone = false;
        }
    }

    if (allDone) m_anim.done = true;
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneMenu::OnUpdate(float dt)
{
    UpdateEnterAnimation(sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false) ? 10.0f : dt);

    // ── 粒子与特效更新 ───────────────────────────────────────────────────────
    m_particles.Update(dt);
    m_glowPhase += dt;  // 标题脉冲相位

    for (auto it = m_clickRings.begin(); it != m_clickRings.end(); )
    {
        it->timer += dt;
        if (it->timer > 0.4f) { // 存活0.4秒
            it = m_clickRings.erase(it);
        } else {
            ++it;
        }
    }

    // 更新按钮（按带 Update 处理悬停动画）
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (m_buttons[i])
        {
            // 临时移动按钮 bounds 到动画偏移位置（X轴偏移）
            auto origBounds = m_buttons[i]->GetBounds();
            sakura::core::NormRect animBounds = origBounds;
            animBounds.x = origBounds.x + m_anim.btnOffsetX[i] + (i % 2 == 1 ? 0.006f : 0.0f);

            m_buttons[i]->SetBounds(animBounds);

            m_buttons[i]->Update(dt);

            // 恢复原始 bounds
            m_buttons[i]->SetBounds(origBounds);
        }
    }

    // 编辑器子菜单按钮更新
    if (m_showEditorMenu)
    {
        if (m_btnEditorOpen)   m_btnEditorOpen->Update(dt);
        if (m_btnEditorNew)    m_btnEditorNew->Update(dt);
        if (m_btnEditorCancel) m_btnEditorCancel->Update(dt);
    }

    // 确认框按钮更新
    if (m_showExitConfirm)
    {
        if (m_btnConfirmYes) m_btnConfirmYes->Update(dt);
        if (m_btnConfirmNo)  m_btnConfirmNo ->Update(dt);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────
void SceneMenu::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, m_glowPhase);
    if (m_fontTitle == sakura::core::INVALID_HANDLE) return;

    const float aspect = static_cast<float>(renderer.GetScreenHeight()) / std::max(1, renderer.GetScreenWidth());
    const auto [mx, my] = sakura::core::Input::GetMousePosition();
    const float parX = (mx - 0.5f) * 0.014f;
    const float parY = (my - 0.5f) * 0.010f;

    // ── 右侧月夜樱纹星盘与律动光环 ────────────────────────────────────────────
    const float crestCX = 0.72f + parX;
    const float crestCY = 0.45f + parY;
    const float breath  = 0.5f + 0.5f * std::sin(m_glowPhase * 1.8f);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        crestCX, crestCY, 0.24f + breath * 0.02f, 0.24f + breath * 0.02f,
        { 246, 172, 200, static_cast<uint8_t>(48 + breath * 22.0f) }, { 246, 172, 200, 0 }, 48);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    sakura::audio::AudioVisualizer::GetInstance().RenderCircle(
        renderer, crestCX, crestCY, 0.145f, { 246, 178, 206, 210 }, 0.72f);

    renderer.DrawCircleOutline(crestCX, crestCY, 0.185f, { 250, 216, 157, 72 }, 0.0011f, 72);
    renderer.DrawArc(crestCX, crestCY, 0.205f, m_glowPhase * 18.0f, m_glowPhase * 18.0f + 135.0f,
                     { 250, 216, 157, 130 }, 0.0018f, 48);
    renderer.DrawArc(crestCX, crestCY, 0.205f, m_glowPhase * 18.0f + 180.0f, m_glowPhase * 18.0f + 315.0f,
                     { 246, 172, 200, 125 }, 0.0018f, 48);

    const auto crestTex = sakura::ui::VisualStyle::GetSakuraCrestTexture();
    if (crestTex != sakura::core::INVALID_HANDLE)
    {
        const float s = 0.21f * (0.98f + 0.03f * breath);
        const float w = s * aspect;
        renderer.DrawSprite(crestTex, { crestCX - w * 0.5f, crestCY - s * 0.5f, w, s },
                            m_glowPhase * 7.5f, sakura::core::Color::White, 0.94f);
    }

    // 右下角磨砂琉璃双模说明铭牌
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer,
        { 0.585f + parX * 0.5f, 0.735f + parY * 0.5f, 0.27f, 0.125f },
        { 22, 28, 52, 188 }, { 14, 18, 36, 208 },
        { 250, 216, 157, 145 }, { 164, 154, 212, 58 },
        0.015f, 0.42f);

    const auto pink = sakura::core::Color{ 246, 178, 206, 255 };
    const auto gold = sakura::core::Color{ 250, 216, 157, 245 };
    const auto dim  = sakura::core::Color{ 176, 184, 210, 235 };
    renderer.DrawText(m_fontSub, "4 KEYS  +  MOUSE  HYBRID", 0.72f + parX * 0.5f, 0.758f + parY * 0.5f, 0.019f, gold, sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.625f + parX * 0.5f, 0.815f + parX * 0.5f, 0.795f + parY * 0.5f, 0.55f);
    renderer.DrawText(m_fontSub, "左手键盘落键 · 右手丝滑轨迹", 0.72f + parX * 0.5f, 0.810f + parY * 0.5f, 0.021f, dim, sakura::core::TextAlign::Center);

    // ── 左侧导航磨砂底座与主标题 ─────────────────────────────────────────────
    const float titleX = TITLE_X + m_anim.titleOffsetX;
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer,
        { 0.052f, 0.088f, 0.345f, 0.835f },
        { 18, 24, 46, 168 }, { 10, 14, 28, 196 },
        { 250, 216, 157, 128 }, { 154, 162, 206, 48 },
        0.020f, 0.48f);

    sakura::ui::VisualStyle::DrawBadgePill(
        renderer,
        { titleX, 0.115f, 0.215f, 0.032f },
        "A DUAL RHYTHM JOURNEY",
        m_fontSub, 0.015f,
        { 46, 34, 66, 195 }, { 250, 216, 157, 145 }, gold);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        titleX + 0.12f, TITLE_Y + 0.055f, 0.15f, 0.09f,
        { 246, 172, 200, static_cast<uint8_t>(36 + breath * 22.0f) }, { 246, 172, 200, 0 }, 36);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    renderer.DrawText(m_fontTitle, "Sakura", titleX, TITLE_Y - 0.008f, 0.098f, { 250, 244, 248, 255 });
    renderer.DrawText(m_fontTitle, "樱", titleX + 0.236f, TITLE_Y + 0.015f, 0.068f, pink);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, titleX, titleX + 0.29f, 0.308f, 0.78f);
    renderer.DrawText(m_fontSub, "月华琉璃，指尖落樱成诗。", titleX, 0.324f, 0.021f, dim);

    // ── 按钮（错落阶梯感 + 鎏金键盘焦点指示）────────────────────────────────
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i] + (i % 2 == 1 ? 0.006f : 0.0f);

        if (animBounds.x + animBounds.width < 0.0f) continue;

        m_buttons[i]->SetBounds(animBounds);
        m_buttons[i]->Render(renderer);
        if (i == m_keyboardFocus)
        {
            renderer.DrawRoundedGradientRect(
                { animBounds.x - 0.009f, animBounds.y + 0.010f, 0.0034f, animBounds.height - 0.020f },
                0.0017f, gold, pink, gold, pink, 6, true);
        }
        m_buttons[i]->SetBounds(origBounds);
    }

    renderer.DrawText(m_fontSub, "↑ ↓ 选择   ·   Enter 确认   ·   Esc 退出", 0.075f, 0.876f, 0.016f, dim);

    // ── 三层景深樱瓣与点击涟漪 ───────────────────────────────────────────────
    m_particles.Render(renderer);
    for (const auto& ring : m_clickRings)
    {
        const float rt = std::clamp(ring.timer / 0.4f, 0.0f, 1.0f);
        const float rad = 0.008f + sakura::utils::EaseOutCubic(rt) * 0.045f;
        const uint8_t ra = static_cast<uint8_t>((1.0f - rt) * 195.0f);
        renderer.DrawRingGradient(ring.x, ring.y, rad * 0.78f, rad, { 250, 216, 157, ra }, { 246, 172, 200, 0 }, 40);
    }

    // ── 版本号 ────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "v" SAKURA_VERSION_STRING "  ·  LUMINOUS GLASS EDITION",
        0.075f, 0.948f, 0.015f,
        sakura::core::Color{ 208, 212, 232, 168 },
        sakura::core::TextAlign::Left);

    // ── 编辑器子菜单 ──────────────────────────────────────────────────────────
    if (m_showEditorMenu)
    {
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.66f);
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer, { 0.295f, 0.265f, 0.41f, 0.465f },
            { 28, 34, 62, 236 }, { 16, 20, 40, 244 },
            { 250, 216, 157, 190 }, { 246, 172, 200, 92 },
            0.018f, 0.62f);

        sakura::ui::VisualStyle::DrawBadgePill(
            renderer, { 0.425f, 0.285f, 0.15f, 0.028f },
            "CHART ATELIER", m_fontSub, 0.013f,
            { 54, 38, 74, 210 }, { 250, 216, 157, 155 }, gold);
        renderer.DrawText(m_fontSub, "谱面编辑器",
            0.50f, 0.322f, 0.036f,
            sakura::core::Color{ 248, 242, 248, 250 },
            sakura::core::TextAlign::Center);
        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.33f, 0.67f, 0.374f, 0.78f);

        if (m_customCharts.empty())
        {
            renderer.DrawText(m_fontSub, "（暂无自制谱面 · 点击下方新建）",
                0.50f, 0.420f, 0.021f,
                sakura::core::Color{ 176, 182, 210, 185 },
                sakura::core::TextAlign::Center);
        }
        else
        {
            int showCount = std::min(static_cast<int>(m_customCharts.size()), 3);
            for (int i = 0; i < showCount; ++i)
            {
                bool selected = (m_selectedChartIdx == i);
                if (selected)
                {
                    renderer.DrawRoundedGradientRect(
                        { 0.345f, 0.394f + i * 0.029f, 0.31f, 0.026f },
                        0.006f, { 82, 54, 88, 170 }, { 48, 38, 72, 130 },
                        { 82, 54, 88, 170 }, { 48, 38, 72, 130 }, 8, true);
                }
                renderer.DrawText(m_fontSub,
                    (selected ? "◆  " : "   ") + m_customCharts[i].title,
                    0.358f, 0.396f + i * 0.029f, 0.021f,
                    selected ? gold : sakura::core::Color{ 210, 214, 234, 195 },
                    sakura::core::TextAlign::Left);
            }
            if (static_cast<int>(m_customCharts.size()) > 3)
            {
                renderer.DrawText(m_fontSub,
                    "   ...（共 " + std::to_string(m_customCharts.size()) + " 首自制谱面）",
                    0.358f, 0.396f + 3 * 0.029f, 0.018f,
                    sakura::core::Color{ 168, 174, 202, 155 },
                    sakura::core::TextAlign::Left);
            }
        }

        if (m_btnEditorOpen)
        {
            m_btnEditorOpen->SetEnabled(!m_customCharts.empty());
            m_btnEditorOpen->Render(renderer);
        }
        if (m_btnEditorNew)    m_btnEditorNew->Render(renderer);
        if (m_btnEditorCancel) m_btnEditorCancel->Render(renderer);
    }

    // ── 退出确认对话框 ────────────────────────────────────────────────────────
    if (m_showExitConfirm)
    {
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.66f);
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer, { 0.315f, 0.34f, 0.37f, 0.275f },
            { 28, 34, 62, 238 }, { 16, 20, 40, 246 },
            { 250, 216, 157, 190 }, { 246, 172, 200, 92 },
            0.018f, 0.62f);

        renderer.DrawText(m_fontSub, "EXIT SAKURA",
            0.50f, 0.368f, 0.015f, gold, sakura::core::TextAlign::Center);
        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.36f, 0.64f, 0.398f, 0.72f);
        renderer.DrawText(m_fontSub, "确定要退出游戏吗？",
            0.50f, 0.422f, 0.031f,
            sakura::core::Color{ 248, 242, 248, 250 },
            sakura::core::TextAlign::Center);

        if (m_btnConfirmYes) m_btnConfirmYes->Render(renderer);
        if (m_btnConfirmNo)  m_btnConfirmNo ->Render(renderer);
    }
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneMenu::OnEvent(const SDL_Event& event)
{
    // 鼠标点击全局特效
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        auto [mx, my] = sakura::core::Input::GetMousePosition();
        m_clickRings.push_back({mx, my, 0.0f});
        auto cfg = sakura::effects::ParticlePresets::ClickSpark();
        m_particles.Emit(mx, my, 5, cfg);
    }

    // 编辑器子菜单优先
    if (m_showEditorMenu)
    {
        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_showEditorMenu = false;
            return;
        }

        // 方向键上下选择自制谱
        if (event.type == SDL_EVENT_KEY_DOWN && !m_customCharts.empty())
        {
            if (event.key.scancode == SDL_SCANCODE_UP)
            {
                int maxIdx = static_cast<int>(m_customCharts.size()) - 1;
                m_selectedChartIdx = (m_selectedChartIdx <= 0) ? maxIdx : m_selectedChartIdx - 1;
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_DOWN)
            {
                int maxIdx = static_cast<int>(m_customCharts.size()) - 1;
                m_selectedChartIdx = (m_selectedChartIdx >= maxIdx) ? 0 : m_selectedChartIdx + 1;
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_RETURN && m_selectedChartIdx >= 0)
            {
                OpenEditorForChart(m_selectedChartIdx);
                return;
            }
        }

        if (m_btnEditorOpen)   m_btnEditorOpen->HandleEvent(event);
        if (m_btnEditorNew)    m_btnEditorNew->HandleEvent(event);
        if (m_btnEditorCancel) m_btnEditorCancel->HandleEvent(event);
        return;
    }

    // 若退出确认框可见，事件只路由给确认框按钮
    if (m_showExitConfirm)
    {
        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            // ESC 取消退出确认框
            m_showExitConfirm = false;
            return;
        }
        if (m_btnConfirmYes) m_btnConfirmYes->HandleEvent(event);
        if (m_btnConfirmNo)  m_btnConfirmNo ->HandleEvent(event);
        return;
    }

    // ESC → 显示退出确认框
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_UP) { m_keyboardFocus = (m_keyboardFocus + BUTTON_COUNT - 1) % BUTTON_COUNT; return; }
        if (event.key.scancode == SDL_SCANCODE_DOWN || event.key.scancode == SDL_SCANCODE_TAB) { m_keyboardFocus = (m_keyboardFocus + 1) % BUTTON_COUNT; return; }
        if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_SPACE) { m_buttons[m_keyboardFocus]->Activate(); return; }
    }
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        m_showExitConfirm = true;
        return;
    }

    // 分发给按钮（带当前 X 偏移做命中测试）
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        // 临时应用偏移
        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i] + (i % 2 == 1 ? 0.006f : 0.0f);

        m_buttons[i]->SetBounds(animBounds);
        m_buttons[i]->HandleEvent(event);

        m_buttons[i]->SetBounds(origBounds);
    }
}

} // namespace sakura::scene
