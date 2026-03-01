// scene_pause.cpp — 暂停菜单场景

#include "scene_pause.h"
#include "scene_select.h"
#include "core/input.h"
#include "core/resource_manager.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"
#include "effects/shader_manager.h"

#include <memory>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

ScenePause::ScenePause(SceneManager& mgr,
                       sakura::game::GameState& gameState)
    : m_manager(mgr)
    , m_gameState(gameState)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void ScenePause::OnEnter()
{
    LOG_INFO("[ScenePause] 游戏已暂停");

    auto& rm  = sakura::core::ResourceManager::GetInstance();
    m_fontUI  = rm.GetDefaultFontHandle();

    auto& audio = sakura::audio::AudioManager::GetInstance();
    audio.PauseMusic();

    // ── 按钮布局 ─────────────────────────────────────────────────────────────
    // 面板区域: x=0.30, y=0.25, w=0.40, h=0.50

    constexpr float BX = 0.35f;
    constexpr float BW = 0.30f;
    constexpr float BH = 0.055f;

    m_btnResume = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{BX, 0.43f, BW, BH}, "继 续", m_fontUI);
    m_btnRestart = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{BX, 0.53f, BW, BH}, "重新开始", m_fontUI);
    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{BX, 0.63f, BW, BH}, "返回选歌", m_fontUI);

    m_btnResume ->SetOnClick([this]() { Resume(); });
    m_btnRestart->SetOnClick([this]()
    {
        // 恢复音乐，切回同一张谱面（或重新构造 SceneGame）
        sakura::audio::AudioManager::GetInstance().StopMusic();
        m_manager.PopScene(sakura::scene::TransitionType::Fade, 0.3f);
        // SceneGame 的 OnExit 会停止游戏；调用方（SceneGame）需要在
        // PopScene 后由外部再推一个新的 SceneGame 实例。
        // 目前实现：直接 Pop 回 SceneGame，SceneSelect 的"开始"按钮
        // 会创建新 SceneGame；这里只 Pop 到 SceneGame，然后 SceneGame
        // 检测到 IsFinished()==true 时会走到结算。
        // 更简单：Pop → SceneGame 仍然在栈上，它的 OnEnter 里重新调用
        // m_gameState.Start()，但目前 SceneGame 不支持 restart。
        // 暂时实现：跨过 SceneGame 直接返回 SceneSelect。
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::Fade, 0.4f);
    });
    m_btnBack->SetOnClick([this]()
    {
        sakura::audio::AudioManager::GetInstance().StopMusic();
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::Fade, 0.4f);
    });
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void ScenePause::OnExit()
{
    LOG_INFO("[ScenePause] 退出暂停菜单");
}

// ── Resume ───────────────────────────────────────────────────────────────────

void ScenePause::Resume()
{
    m_gameState.Resume();
    sakura::audio::AudioManager::GetInstance().ResumeMusic();
    m_manager.PopScene(sakura::scene::TransitionType::Fade, 0.3f);
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void ScenePause::OnUpdate(float /*dt*/)
{
    // 无动画
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void ScenePause::OnRender(sakura::core::Renderer& renderer)
{
    // ── 半透明黑遮罩 ─────────────────────────────────────────────────────────
    renderer.DrawFilledRect({0.0f, 0.0f, 1.0f, 1.0f}, {0, 0, 0, 160});

    // 暂停时额外晕影强化沉浸感
    sakura::effects::ShaderManager::GetInstance().DrawVignette(0.35f);

    // ── 圆角面板填充 ─────────────────────────────────────────────────────────
    renderer.DrawRoundedRect({0.30f, 0.25f, 0.40f, 0.50f},
                             0.015f,
                             sakura::core::Color{30, 30, 50, 240}, true);
    // 面板边框
    renderer.DrawRoundedRect({0.30f, 0.25f, 0.40f, 0.50f},
                             0.015f,
                             sakura::core::Color{120, 120, 180, 200}, false);

    // ── 标题 "PAUSED" ────────────────────────────────────────────────────────
    renderer.DrawText(m_fontUI, "PAUSED",
                      0.50f, 0.32f, 0.04f,
                      sakura::core::Color{255, 255, 255, 255},
                      sakura::core::TextAlign::Center);

    // ── 按钮 ─────────────────────────────────────────────────────────────────
    m_btnResume ->Render(renderer);
    m_btnRestart->Render(renderer);
    m_btnBack   ->Render(renderer);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void ScenePause::OnEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Resume();
        return;
    }

    if (m_btnResume)  m_btnResume ->HandleEvent(event);
    if (m_btnRestart) m_btnRestart->HandleEvent(event);
    if (m_btnBack)    m_btnBack   ->HandleEvent(event);
}

} // namespace sakura::scene
