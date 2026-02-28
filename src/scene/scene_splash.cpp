// scene_splash.cpp — 启动画面

#include "scene_splash.h"
#include "scene_loading.h"
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

    // 目标场景工厂（Step 1.9 完成后改为 SceneMenu）
    auto& mgr = m_manager;
    auto factory = [&mgr]() -> std::unique_ptr<Scene>
    {
        // 占位：临时回到 Loading（Step 1.9 后将替换为 SceneMenu）
        // 此处返回 nullptr 让 SceneManager 自行处理（空任务 Loading 直接 goto null）
        return nullptr;
    };

    m_manager.SwitchScene(
        std::make_unique<SceneLoading>(m_manager, std::move(tasks), std::move(factory)),
        TransitionType::Fade,
        0.4f
    );
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneSplash::OnRender(sakura::core::Renderer& renderer)
{
    // 深色背景
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 10, 8, 20, 255 });

    if (m_fontTitle == sakura::core::INVALID_HANDLE) return;

    uint8_t alpha = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, m_opacity)) * 255.0f);

    // ── Logo 主标题 ───────────────────────────────────────────────────────────
    renderer.DrawText(m_fontTitle, "Sakura-樱",
        0.5f, 0.38f, 0.12f,
        sakura::core::Color{ 255, 200, 220, alpha },
        sakura::core::TextAlign::Center);

    // ── 副标题 ────────────────────────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "Mixed-Mode Rhythm Game",
        0.5f, 0.525f, 0.025f,
        sakura::core::Color{ 200, 180, 210, static_cast<uint8_t>(alpha * 0.7f) },
        sakura::core::TextAlign::Center);

    // ── 底部 "Loading..." 闪烁 ────────────────────────────────────────────────
    if (m_blinkVisible)
    {
        renderer.DrawText(m_fontSub, "Loading...",
            0.5f, 0.90f, 0.022f,
            sakura::core::Color{ 200, 200, 220, static_cast<uint8_t>(alpha * 0.6f) },
            sakura::core::TextAlign::Center);
    }
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
