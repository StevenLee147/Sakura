// scene_menu.cpp — 主菜单场景

#include "scene_menu.h"
#include "scene_select.h"
#include "scene_settings.h"
#include "scene_editor.h"
#include "scene_chart_wizard.h"
#include "core/input.h"
#include "game/chart_loader.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "effects/particle_system.h"
#include "effects/glow.h"

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
        "开始游戏", "谱面编辑器", "设置", "退出"
    };

    sakura::ui::ButtonColors colors;
    // 使用玻璃质感
    colors.normal   = { 255, 240, 245, 20 };
    colors.hover    = { 255, 255, 255, 45 };
    colors.pressed  = { 255, 255, 255, 15 };
    colors.disabled = { 100, 100, 100, 30 };
    colors.text     = sakura::core::Color::White;
    // 边框留默认的高亮

    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        float y = BTN_Y0 + i * BTN_GAP;
        sakura::core::NormRect bounds = { BTN_X, y, BTN_W, BTN_H };
        m_buttons[i] = std::make_unique<sakura::ui::Button>(
            bounds, labels[i], m_fontButton, 0.028f, 0.012f);
        m_buttons[i]->SetColors(colors);
        m_buttons[i]->SetTextAlign(sakura::core::TextAlign::Left);
        m_buttons[i]->SetTextPadding(0.02f);
    }

    // 开始游戏 → 切换到选歌场景
    m_buttons[0]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：开始游戏");
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            TransitionType::SlideLeft, 0.4f);
    });

    // 谱面编辑器 → 显示编辑器子菜单
    m_buttons[1]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：谱面编辑器");
        ScanCustomCharts();
        m_showEditorMenu = true;
    });

    // 设置 → 切换到设置场景
    m_buttons[2]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：设置");
        m_manager.SwitchScene(
            std::make_unique<SceneSettings>(m_manager),
            TransitionType::SlideLeft, 0.4f);
    });

    // 退出 → 弹出确认对话框
    m_buttons[3]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：退出（显示确认框）");
        m_showExitConfirm = true;
    });
}

// ── SetupEditorMenuButtons ────────────────────────────────────────────────────

void SceneMenu::SetupEditorMenuButtons()
{
    sakura::ui::ButtonColors openColors;
    openColors.normal   = { 40, 70, 110, 220 };
    openColors.hover    = { 60, 100, 150, 235 };
    openColors.pressed  = { 25,  50,  80, 240 };
    openColors.disabled = { 30,  45,  60, 120 };
    openColors.text     = sakura::core::Color::White;

    sakura::ui::ButtonColors newColors;
    newColors.normal   = { 50, 80,  50, 220 };
    newColors.hover    = { 70, 110,  70, 235 };
    newColors.pressed  = { 30,  55,  30, 240 };
    newColors.disabled = { 30,  45,  30, 120 };
    newColors.text     = sakura::core::Color::White;

    sakura::ui::ButtonColors cancelColors;
    cancelColors.normal   = { 45, 45, 70, 220 };
    cancelColors.hover    = { 70, 65, 105, 235 };
    cancelColors.pressed  = { 25, 25,  50, 240 };
    cancelColors.disabled = { 30, 30,  50, 120 };
    cancelColors.text     = sakura::core::Color::White;

    // 面板区域：x=0.30, y=0.28, w=0.40, h=0.44；按钮居中布局
    m_btnEditorOpen = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.355f, 0.455f, 0.29f, 0.055f },
        "打开已有谱面", m_fontButton, 0.026f, 0.010f);
    m_btnEditorOpen->SetColors(openColors);
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
    m_btnEditorNew->SetColors(newColors);
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
    m_btnEditorCancel->SetColors(cancelColors);
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

    // 确保自制谱目录存在
    std::error_code ec;
    std::filesystem::create_directories(CUSTOM_CHARTS_PATH, ec);

    sakura::game::ChartLoader loader;
    auto charts = loader.ScanCharts(CUSTOM_CHARTS_PATH);
    for (auto& ci : charts)
    {
        ChartEntry entry;
        entry.folderPath = ci.folderPath;
        entry.title      = ci.title.empty() ? ci.id : ci.title;
        m_customCharts.push_back(std::move(entry));
    }

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
                                      "normal.json"),
        TransitionType::SlideLeft, 0.4f);
}

// ── SetupConfirmButtons ───────────────────────────────────────────────────────

void SceneMenu::SetupConfirmButtons()
{
    sakura::ui::ButtonColors yesColors;
    yesColors.normal   = { 160,  40,  60, 220 };
    yesColors.hover    = { 200,  60,  80, 235 };
    yesColors.pressed  = { 120,  25,  40, 240 };
    yesColors.disabled = {  60,  20,  30, 120 };
    yesColors.text     = sakura::core::Color::White;

    sakura::ui::ButtonColors noColors;
    noColors.normal   = {  40,  40,  80, 220 };
    noColors.hover    = {  70,  60, 120, 235 };
    noColors.pressed  = {  25,  25,  55, 240 };
    noColors.disabled = {  30,  30,  50, 120 };
    noColors.text     = sakura::core::Color::White;

    // 确认框内两个按钮，居中布局
    // 面板: x=0.32 y=0.35 w=0.36 h=0.26
    m_btnConfirmYes = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.348f, 0.505f, 0.13f, 0.055f },
        "确认退出", m_fontButton, 0.026f, 0.010f);
    m_btnConfirmYes->SetColors(yesColors);
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
    m_btnConfirmNo->SetColors(noColors);
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
    UpdateEnterAnimation(dt);

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
            animBounds.x = origBounds.x + m_anim.btnOffsetX[i];
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
    // ── 背景图案 ──────────────────────────────────────────────────────────────
    // 假定有全屏背景底图，先绘制底层颜色
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 15, 12, 35, 255 });
    
    // (此处应有插画渲染...) 略

    // ── 半透明渐变遮罩 (保证左边文字可读性) ───────────────────────────────────
    // 渐变占据 x: 0 ~ 0.5
    renderer.DrawGradientRect({ 0.0f, 0.0f, 0.6f, 1.0f },
        sakura::core::Color{ 10, 10, 20, 240 },   // TopLeft
        sakura::core::Color{ 10, 10, 20, 0 },     // TopRight
        sakura::core::Color{ 10, 10, 20, 240 },   // BottomLeft
        sakura::core::Color{ 10, 10, 20, 0 });    // BottomRight

    // ── 背景微粒 + 樱花飘落 ────────────────────────────────────────────────────
    m_particles.Render(renderer);

    // ── 鼠标点击光环特效 ────────────────────────────────────────────────────────
    for (const auto& ring : m_clickRings)
    {
        float t = ring.timer / 0.4f; // 0~1
        float radius = sakura::utils::EaseOutCubic(t) * 0.05f; // 最大半径 5% 屏幕短边
        uint8_t alpha = static_cast<uint8_t>(255 * (1.0f - t));
        renderer.DrawCircleOutline(ring.x, ring.y, radius, sakura::core::Color{ 255, 255, 255, alpha }, 0.002f, 32);
    }

    if (m_fontTitle == sakura::core::INVALID_HANDLE) return;

    float titleX = TITLE_X + m_anim.titleOffsetX;

    // ── 标题发光脉冲效果（在文字后面画glow） ───────────────────────────────────
    sakura::effects::GlowEffect::PulseGlow(renderer,
        titleX + 0.15f, TITLE_Y + 0.04f,           // 中心（左右对齐）
        0.04f, 0.07f,                    // 大小 min~max
        sakura::core::Color{ 255, 140, 180, 120 },  // 粉色发光
        m_glowPhase, 0.8f, 5);           // 频率 0.8Hz

    // ── 标题 ──────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontTitle, "Sakura-\xe6\xa8\xb1",
        titleX, TITLE_Y, 0.08f,
        sakura::core::Color{ 255, 255, 255, 255 },
        sakura::core::TextAlign::Left);

    // ── 副标题 ────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "Mixed-Mode Rhythm Game",
        titleX, TITLE_Y + 0.09f, 0.025f,
        sakura::core::Color{ 220, 220, 230, 200 },
        sakura::core::TextAlign::Left);

    // ── 按钮（应用 X 动画偏移，同时做溢出裁剪）──────────────────────────────
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i];

        // 若完全在左侧屏幕外则跳过渲染
        if (animBounds.x + animBounds.width < 0.0f) continue;

        m_buttons[i]->SetBounds(animBounds);
        m_buttons[i]->Render(renderer);
        m_buttons[i]->SetBounds(origBounds);   // 还原
    }

    // ── 版本号 ────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "v" SAKURA_VERSION_STRING,
        0.02f, 0.96f, 0.018f,
        sakura::core::Color{ 200, 200, 200, 160 },
        sakura::core::TextAlign::Left);

    // ── 编辑器子菜单 ──────────────────────────────────────────────────────────
    if (m_showEditorMenu)
    {
        // 半透明遮罩
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
            sakura::core::Color{ 0, 0, 0, 140 });

        // 子菜单面板 x=0.30, y=0.28, w=0.40, h=0.44
        renderer.DrawRoundedRect({ 0.30f, 0.28f, 0.40f, 0.44f },
            0.012f, sakura::core::Color{ 22, 18, 45, 248 }, true);
        renderer.DrawRoundedRect({ 0.30f, 0.28f, 0.40f, 0.44f },
            0.012f, sakura::core::Color{ 120, 90, 180, 200 }, false);

        // 标题
        renderer.DrawText(m_fontSub, "谱面编辑器",
            0.50f, 0.325f, 0.038f,
            sakura::core::Color{ 230, 210, 255, 240 },
            sakura::core::TextAlign::Center);

        // 分隔线
        renderer.DrawLine(0.32f, 0.375f, 0.68f, 0.375f,
            sakura::core::Color{ 100, 80, 150, 120 }, 0.001f);

        // 自制谱列表信息
        if (m_customCharts.empty())
        {
            renderer.DrawText(m_fontSub, "（暂无自制谱面）",
                0.50f, 0.422f, 0.022f,
                sakura::core::Color{ 160, 150, 190, 160 },
                sakura::core::TextAlign::Center);
        }
        else
        {
            // 显示最多 3 条自制谱名称供参考
            int showCount = std::min(static_cast<int>(m_customCharts.size()), 3);
            for (int i = 0; i < showCount; ++i)
            {
                bool selected = (m_selectedChartIdx == i);
                renderer.DrawText(m_fontSub,
                    (selected ? "> " : "  ") + m_customCharts[i].title,
                    0.36f, 0.398f + i * 0.028f, 0.022f,
                    sakura::core::Color{ selected ? 255u : 200u,
                                        selected ? 220u : 195u,
                                        selected ? 255u : 230u,
                                        selected ? 240u : 180u },
                    sakura::core::TextAlign::Left);
            }
            if (static_cast<int>(m_customCharts.size()) > 3)
            {
                renderer.DrawText(m_fontSub,
                    "  ...（共" + std::to_string(m_customCharts.size()) + "首）",
                    0.36f, 0.398f + 3 * 0.028f, 0.020f,
                    sakura::core::Color{ 160, 155, 185, 140 },
                    sakura::core::TextAlign::Left);
            }
        }

        // 动作按钮（若无自制谱，"打开已有谱面"按钮禁用）
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
        // 半透明遮罩
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
            sakura::core::Color{ 0, 0, 0, 140 });

        // 对话框面板 x=0.32, y=0.35, w=0.36, h=0.26
        renderer.DrawRoundedRect({ 0.32f, 0.35f, 0.36f, 0.26f },
            0.012f, sakura::core::Color{ 28, 24, 52, 245 }, true);
        renderer.DrawRoundedRect({ 0.32f, 0.35f, 0.36f, 0.26f },
            0.012f, sakura::core::Color{ 140, 100, 180, 200 }, false);

        // 提示文字
        renderer.DrawText(m_fontSub, "确定要退出游戏吗？",
            0.50f, 0.425f, 0.032f,
            sakura::core::Color{ 240, 230, 255, 240 },
            sakura::core::TextAlign::Center);

        // 确认 / 取消 按钮
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
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i];
        m_buttons[i]->SetBounds(animBounds);

        m_buttons[i]->HandleEvent(event);

        m_buttons[i]->SetBounds(origBounds);
    }
}

} // namespace sakura::scene
