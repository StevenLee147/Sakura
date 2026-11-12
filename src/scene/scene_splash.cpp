// scene_splash.cpp — 启动画面

#include "scene_splash.h"
#include "ui/visual_style.h"
#include "scene_loading.h"
#include "scene_menu.h"
#include "scene_tutorial.h"
#include "core/config.h"
#include "core/input.h"
#include "utils/logger.h"
#include "utils/easing.h"

#include <algorithm>
#include <memory>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneSplash::SceneSplash(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneSplash::OnEnter()
{
    LOG_INFO("[SceneSplash] 进入启动画面");
    m_phase      = Phase::FadeIn;
    m_timer      = 0.0f;
    m_opacity    = 0.0f;
    m_blinkTimer = 0.0f;
    m_blinkVisible = true;

    // 获取已加载的默认字体
    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontTitle = rm.GetDefaultFontHandle();   // NotoSansSC 24pt → 用 DrawText 的 normFontSize 控制
    m_fontSub   = rm.GetDefaultFontHandle();

    // 预加载全局资源
    PreloadResources();
}

void SceneSplash::PreloadResources()
{
    // 预加载可能需要的字体尺寸和 UI 纹理
    // 目前所有资源在 ResourceManager 初始化时已加载，此处仅记录日志
    LOG_INFO("[SceneSplash] 预加载全局资源完成");
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneSplash::OnExit()
{
    LOG_INFO("[SceneSplash] 退出启动画面");
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneSplash::OnUpdate(float dt)
{
    // 闪烁计时
    m_blinkTimer += dt;
    if (m_blinkTimer >= BLINK_INTERVAL)
    {
        m_blinkTimer -= BLINK_INTERVAL;
        m_blinkVisible = !m_blinkVisible;
    }

    m_timer += dt;

    switch (m_phase)
    {
    case Phase::FadeIn:
    {
        float t = std::min(1.0f, m_timer / FADE_IN_DURATION);
        m_opacity = sakura::utils::EaseOutCubic(t);
        if (m_timer >= FADE_IN_DURATION)
        {
            m_opacity = 1.0f;
            m_phase   = Phase::Hold;
            m_timer   = 0.0f;
        }
        break;
    }
    case Phase::Hold:
    {
        if (m_timer >= HOLD_DURATION)
        {
            m_phase = Phase::FadeOut;
            m_timer = 0.0f;
        }
        break;
    }
    case Phase::FadeOut:
    {
        float t = std::min(1.0f, m_timer / FADE_OUT_DURATION);
        m_opacity = 1.0f - sakura::utils::EaseInCubic(t);
        if (m_timer >= FADE_OUT_DURATION)
        {
            m_opacity = 0.0f;
            m_phase   = Phase::Done;
            GoToNextScene();
        }
        break;
    }
    case Phase::Done:
        break;
    }
}

// ── GoToNextScene ─────────────────────────────────────────────────────────────

void SceneSplash::GoToNextScene()
{
    LOG_INFO("[SceneSplash] 切换到加载场景");

    // 加载任务：目前仅预加载字体等较快资源，后续 Step 1.9 改为加载主菜单资源
    std::vector<LoadingTask> tasks;
    tasks.push_back({ "扫描谱面", []()
    {
        // 谱面扫描将在 SceneSelect 按需进行
    }});
    tasks.push_back({ "初始化 UI 资源", []()
    {
        // UI 组件字体/纹理已预加载
    }});

    // 目标场景工厂：首次运行先进入教程提示，否则进入主菜单
    auto& mgr = m_manager;
    auto factory = [&mgr]() -> std::unique_ptr<Scene>
    {
        bool shouldPromptTutorial = !sakura::core::Config::GetInstance().Get<bool>(
            std::string(sakura::core::ConfigKeys::kTutorialPromptShown), false);
        if (shouldPromptTutorial)
            return std::make_unique<SceneTutorial>(mgr, true);
        return std::make_unique<SceneMenu>(mgr);
    };

    m_manager.SwitchScene(
        std::make_unique<SceneLoading>(m_manager, std::move(tasks), std::move(factory)),
        TransitionType::SilkShutter,
        0.45f
    );
}

// ── OnRender ──────────────────────────────────────────────────────────────────
void SceneSplash::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    const float clampedOpacity = std::clamp(m_opacity, 0.0f, 1.0f);
    sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, m_timer * 0.6f, clampedOpacity * 0.72f);

    if (m_fontTitle == sakura::core::INVALID_HANDLE) return;

    const uint8_t alpha = static_cast<uint8_t>(clampedOpacity * 255.0f);
    const float aspect = static_cast<float>(renderer.GetScreenHeight()) / std::max(1, renderer.GetScreenWidth());

    // ── 水墨落樱涟漪与月华光晕 ───────────────────────────────────────────────
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        0.5f, 0.36f, 0.24f, 0.24f,
        { 246, 172, 200, static_cast<uint8_t>(alpha * 0.32f) }, { 246, 172, 200, 0 }, 48);
    for (int r = 0; r < 3; ++r)
    {
        const float wave = std::fmod(m_timer * 0.55f + r * 0.33f, 1.0f);
        const float rad  = 0.075f + wave * 0.16f;
        const uint8_t ra = static_cast<uint8_t>((1.0f - wave) * clampedOpacity * 115.0f);
        renderer.DrawCircleOutline(0.5f, 0.31f, rad, { 250, 216, 157, ra }, 0.0014f, 64);
    }
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    // ── 中心樱纹徽章 ──────────────────────────────────────────────────────────
    const auto crestTex = sakura::ui::VisualStyle::GetSakuraCrestTexture();
    if (crestTex != sakura::core::INVALID_HANDLE)
    {
        const float crestSize = 0.135f * (0.92f + 0.08f * clampedOpacity);
        const float crestW = crestSize * aspect;
        renderer.DrawSprite(
            crestTex,
            { 0.5f - crestW * 0.5f, 0.31f - crestSize * 0.5f, crestW, crestSize },
            m_timer * 14.0f,
            sakura::core::Color::White,
            clampedOpacity * 0.95f);
    }

    // ── Logo 主标题与鎏金纹线 ─────────────────────────────────────────────────
    renderer.DrawText(m_fontTitle, "Sakura · 樱",
        0.5f, 0.425f, 0.098f,
        sakura::core::Color{ 250, 242, 246, alpha },
        sakura::core::TextAlign::Center);

    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.34f, 0.66f, 0.548f, clampedOpacity * 0.85f);

    renderer.DrawText(m_fontSub, "LUMINOUS GLASS  ·  MIXED-MODE RHYTHM",
        0.5f, 0.568f, 0.022f,
        sakura::core::Color{ 238, 198, 214, static_cast<uint8_t>(alpha * 0.85f) },
        sakura::core::TextAlign::Center);

    // ── 底部呼吸玻璃胶囊提示 ──────────────────────────────────────────────────
    const float pulse = 0.55f + 0.45f * (m_blinkVisible ? 1.0f : 0.35f);
    sakura::ui::VisualStyle::DrawBadgePill(
        renderer,
        { 0.39f, 0.865f, 0.22f, 0.042f },
        "AWAKENING SAKURA...",
        m_fontSub, 0.018f,
        { 20, 26, 48, static_cast<uint8_t>(alpha * 0.72f) },
        { 250, 216, 157, static_cast<uint8_t>(alpha * 0.55f * pulse) },
        { 236, 228, 244, static_cast<uint8_t>(alpha * 0.80f * pulse) });
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneSplash::OnEvent(const SDL_Event& event)
{
    // 按任意键/鼠标跳过 Splash（Hold 和 FadeIn 后期 → 直接进 FadeOut）
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (m_phase == Phase::FadeIn && m_timer > 0.3f)
        {
            m_phase = Phase::FadeOut;
            m_timer = 0.0f;
        }
        else if (m_phase == Phase::Hold)
        {
            m_phase = Phase::FadeOut;
            m_timer = 0.0f;
        }
    }
}

} // namespace sakura::scene
