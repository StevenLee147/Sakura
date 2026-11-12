// scene_pause.cpp — 暂停菜单场景

#include "scene_pause.h"
#include "scene_select.h"
#include "scene_game.h"
#include "core/input.h"
#include "core/resource_manager.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"
#include "effects/shader_manager.h"
#include "ui/visual_style.h"

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
    m_animTimer = 0.0f;

    constexpr float BX = 0.355f;
    constexpr float BW = 0.29f;
    constexpr float BH = 0.054f;

    m_btnResume = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{BX, 0.455f, BW, BH}, "继续演奏  (Esc)", m_fontUI, 0.022f);
    m_btnRestart = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{BX, 0.548f, BW, BH}, "重新开始", m_fontUI, 0.022f);
    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{BX, 0.641f, BW, BH}, "返回曲库", m_fontUI, 0.022f);

    m_btnResume ->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnRestart->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnBack   ->SetTextAlign(sakura::core::TextAlign::Center);

    sakura::ui::VisualStyle::ApplyButton(m_btnResume.get(), sakura::ui::ButtonVariant::Primary);
    sakura::ui::VisualStyle::ApplyButton(m_btnRestart.get(), sakura::ui::ButtonVariant::Accent);
    sakura::ui::VisualStyle::ApplyButton(m_btnBack.get(), sakura::ui::ButtonVariant::Secondary);

    m_btnResume ->SetOnClick([this]() { Resume(); });
    m_btnRestart->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneGame>(
                m_manager,
                m_gameState.GetChartInfo(),
                m_gameState.GetDifficultyIndex(), m_gameState.GetOptions()),
            sakura::scene::TransitionType::SakuraSweep, 0.34f);
    });
    m_btnBack->SetOnClick([this]()
    {
        sakura::audio::AudioManager::GetInstance().StopMusic();
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::SilkShutter, 0.38f);
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
    m_manager.PopScene(sakura::scene::TransitionType::Fade, 0.24f);
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────
void ScenePause::OnUpdate(float dt)
{
    m_animTimer += dt;
    m_btnResume->Update(dt);
    m_btnRestart->Update(dt);
    m_btnBack->Update(dt);
}

// ── OnRender ──────────────────────────────────────────────────────────────────
void ScenePause::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawScrim(renderer, 0.68f);
    renderer.Flush();
    sakura::effects::ShaderManager::GetInstance().DrawVignette(0.42f);

    const float aspect = static_cast<float>(renderer.GetScreenHeight()) / std::max(1, renderer.GetScreenWidth());
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer, { 0.305f, 0.215f, 0.39f, 0.54f },
        { 26, 32, 58, 236 }, { 14, 18, 36, 244 },
        { 250, 216, 157, 195 }, { 246, 178, 206, 92 },
        0.020f, 0.65f);

    const auto crest = sakura::ui::VisualStyle::GetSakuraCrestTexture();
    if (crest != sakura::core::INVALID_HANDLE)
    {
        const float s = 0.056f;
        const float w = s * aspect;
        renderer.DrawSprite(crest, { 0.50f - w * 0.5f, 0.236f, w, s }, m_animTimer * 14.0f, sakura::core::Color::White, 0.90f);
    }

    renderer.DrawText(m_fontUI, "PAUSED · 演奏暂停",
                      0.50f, 0.302f, 0.032f,
                      sakura::core::Color{ 250, 244, 248, 255 },
                      sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::DrawTextFit(
        renderer, m_fontUI, m_gameState.GetChartInfo().title,
        0.34f, 0.350f, 0.019f, 0.32f, { 250, 216, 157, 235 });
    renderer.DrawText(m_fontUI, "放松片刻 · 继续后将倒数三秒",
                      0.50f, 0.382f, 0.017f,
                      { 170, 178, 204, 235 }, sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.345f, 0.655f, 0.422f, 0.72f);

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
