// scene_game.cpp — 游戏核心场景实现

#include "scene_game.h"
#include "scene_select.h"
#include "scene_pause.h"
#include "scene_result.h"
#include "core/input.h"
#include "core/config.h"
#include "core/paths.h"
#include "ui/toast.h"
#include "ui/visual_style.h"
#include "game/chart_utils.h"
#include "core/theme.h"
#include "audio/audio_visualizer.h"
#include "game/approach_visuals.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "audio/audio_manager.h"
#include "effects/particle_system.h"
#include "effects/glow.h"
#include "effects/screen_shake.h"
#include "effects/shader_manager.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace sakura::scene
{

namespace
{

sakura::core::Color ToCoreColor(const sakura::game::GuidanceColor& color)
{
    return { color.r, color.g, color.b, color.a };
}
}
// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneGame::SceneGame(SceneManager& mgr,
                     const sakura::game::ChartInfo& chartInfo,
                     int difficultyIndex, sakura::game::PlayOptions options)
    : m_manager(mgr)
    , m_chartInfo(chartInfo)
    , m_options(std::move(options))
    , m_difficultyIndex(difficultyIndex)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneGame::OnEnter()
{
    LOG_INFO("[SceneGame] 开始游戏: {} [diff={}]",
             m_chartInfo.title, m_difficultyIndex);

    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontHUD   = rm.GetDefaultFontHandle();
    m_fontSmall = rm.GetDefaultFontHandle();

    // 背景系统初始化（优先谱面背景，其次使用封面高斯模糊背景）
    {
        auto& cfg = sakura::core::Config::GetInstance();
        float bgDim = cfg.Get("gameplay.background_dim", 0.75f);
        m_useDefaultBg = true;
        const std::string artFile = !m_chartInfo.backgroundFile.empty()
            ? m_chartInfo.backgroundFile
            : m_chartInfo.coverFile;
        if (!artFile.empty())
        {
            std::string bgPath = m_chartInfo.folderPath + "/" + artFile;
            if (m_bgRenderer.LoadImage(bgPath))
            {
                const bool fromCover = m_chartInfo.backgroundFile.empty();
                m_bgRenderer.SetDimming(0.0f);
                m_bgRenderer.SetBlurEnabled(cfg.Get<bool>("graphics.background_blur", true));
                m_bgRenderer.SetBlurStrength(fromCover ? 0.80f : -1.0f);
                m_useDefaultBg = false;
            }
        }
        if (m_useDefaultBg)
        {
            m_defaultBg.Initialize(bgDim * 0.5f);
        }
    }

    // 从 Config 读取键位绑定
    {
        auto& cfg = sakura::core::Config::GetInstance();
        m_laneKeys[0] = static_cast<SDL_Scancode>(
            cfg.Get<int>("input.key_lane_0", SDL_SCANCODE_A));
        m_laneKeys[1] = static_cast<SDL_Scancode>(
            cfg.Get<int>("input.key_lane_1", SDL_SCANCODE_S));
        m_laneKeys[2] = static_cast<SDL_Scancode>(
            cfg.Get<int>("input.key_lane_2", SDL_SCANCODE_D));
        m_laneKeys[3] = static_cast<SDL_Scancode>(
            cfg.Get<int>("input.key_lane_3", SDL_SCANCODE_F));
    }

    auto& cfg = sakura::core::Config::GetInstance();
    m_noteSpeed = cfg.Get<float>("gameplay.note_speed", 5.0f);
    m_approachMs = cfg.Get<int>("gameplay.mouse_approach_ms", 1000);
    m_finishing = false;
    m_replayCursor = 0;
    if (m_options.mode == sakura::game::PlayMode::Replay)
    {
        m_replay = sakura::game::Replay::Load(m_options.replayFile);
        if (m_replay)
        {
            m_options.rate = m_replay->rate;
            m_options.startMs = m_replay->startMs;
        }
    }
    if (!m_gameState.Start(m_chartInfo, m_difficultyIndex, m_options))
    {
        sakura::ui::ToastManager::Instance().Show(m_gameState.GetError(), sakura::ui::ToastType::Error, 6.0f);
        if(m_options.returnToEditor){m_manager.PopScene();return;}
        m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager), TransitionType::Fade, 0.2f);
        return;
    }
    if (m_options.mode == sakura::game::PlayMode::Replay && (!m_replay ||
        m_replay->chartId != m_chartInfo.id || m_replay->difficulty != m_difficultyIndex ||
        m_replay->chartHash != sakura::game::Replay::Hash(m_gameState.GetChartData(),m_chartInfo.offset)))
    {
        sakura::ui::ToastManager::Instance().Show("回放损坏或谱面已变更，无法播放", sakura::ui::ToastType::Error);
        m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager), TransitionType::Fade, 0.2f);
        return;
    }
    m_options=m_gameState.GetOptions();
    m_session.Initialize(m_gameState.GetChartData(), m_options.mode == sakura::game::PlayMode::AutoPlay,
        m_options.startMs, m_options.endMs);
    int viewW=1600, viewH=900; if(auto* window=SDL_GetKeyboardFocus()) SDL_GetWindowSize(window,&viewW,&viewH);
    const float edge=std::min(viewW*MOUSE_W,viewH*MOUSE_H);
    m_pointerScaleX=viewW*MOUSE_W/edge; m_pointerScaleY=viewH*MOUSE_H/edge;
    if(m_replay){m_pointerScaleX=m_replay->pointerScaleX;m_pointerScaleY=m_replay->pointerScaleY;}
    m_session.SetPointerScale(m_pointerScaleX,m_pointerScaleY);
    if(!m_replay)m_session.Submit({-10000,sakura::game::InputKind::PointerScale,0,m_pointerScaleX,m_pointerScaleY});
    m_options = m_gameState.GetOptions();
    if(m_session.Total()==0){sakura::ui::ToastManager::Instance().Show("这个练习区间内没有完整音符",sakura::ui::ToastType::Warning);m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager));return;}
    m_session.SetOnJudge([this](const sakura::game::JudgmentEvent& event)
    {
        if(event.keyboard) m_lanePulse[event.lane]=1.0f;
        AddJudgeFlash(event.result, event.keyboard, event.lane,
            MOUSE_X + event.x * MOUSE_W, MOUSE_Y + event.y * MOUSE_H, event.error, event.timed);
    });
    m_judgeFlashes.clear();
    m_cursorTrail.clear();

    // ── 特效初始化 ────────────────────────────────────────────────────────────
    m_particles.Clear();
    m_judgePulsePhase = 0.0f;
    m_chromaTimer     = 0.0f;
    m_comboPopTimer   = 0.0f;
    m_lastCheckedCombo = 0;
    m_lanePressed.fill(false);
}

// ── OnExit ─────────────────────────────────────────────────────────────────────

void SceneGame::OnExit()
{
    LOG_INFO("[SceneGame] 退出游戏场景");
    sakura::audio::AudioManager::GetInstance().StopMusic();
    m_judgeFlashes.clear();
    m_particles.Clear();
    m_bgRenderer.UnloadImage();
    sakura::audio::AudioManager::GetInstance().SetPlaybackSpeed(1.0f);
}

// ── CalcNoteRenderY ───────────────────────────────────────────────────────────

float SceneGame::CalcNoteRenderY(int noteTimeMs, int currentTimeMs,
                                  float svSpeed) const
{
    (void)svSpeed;
    const float distance = sakura::game::ScrollDistance(m_gameState.GetChartData(), currentTimeMs, noteTimeMs);
    return JUDGE_LINE_Y - distance * m_noteSpeed * JUDGE_LINE_Y / BASE_APPROACH_RANGE;
}

// ── CalcApproachScale ─────────────────────────────────────────────────────────

float SceneGame::CalcApproachScale(int noteTimeMs, int currentTimeMs) const
{
    float dtMs  = static_cast<float>(noteTimeMs - currentTimeMs);
    float t     = std::max(0.0f, std::min(1.0f, dtMs / static_cast<float>(m_approachMs)));
    return 1.0f + 1.5f * t;   // 2.5 → 1.0
}

int SceneGame::GetInputTimeMs() const
{
    auto& audio = sakura::audio::AudioManager::GetInstance();
    if (!m_gameState.IsPlaying() || !audio.IsPlaying())
        return m_gameState.GetCurrentTime();

    const int globalOffset = sakura::core::Config::GetInstance().Get<int>(
        std::string(sakura::core::ConfigKeys::kAudioOffset), 0);
    return static_cast<int>(audio.GetMusicPosition() * 1000.0)
        - m_chartInfo.offset - globalOffset - m_inputLagMs;
}

// ── HandleKeyPress ────────────────────────────────────────────────────────────

void SceneGame::HandleKeyPress(SDL_Scancode key)
{
    if (!m_gameState.IsPlaying() || m_options.mode == sakura::game::PlayMode::AutoPlay ||
        m_options.mode == sakura::game::PlayMode::Replay) return;
    for (int lane = 0; lane < LANE_COUNT; ++lane)
        if (m_laneKeys[lane] == key)
            m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::LaneDown, lane});
}

void SceneGame::HandleMouseClick(float normX, float normY)
{
    if (!m_gameState.IsPlaying() || m_options.mode == sakura::game::PlayMode::AutoPlay ||
        m_options.mode == sakura::game::PlayMode::Replay) return;
    m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::MouseDown, 0,
        (normX - MOUSE_X) / MOUSE_W, (normY - MOUSE_Y) / MOUSE_H});
}

// ── AddJudgeFlash ─────────────────────────────────────────────────────────────

void SceneGame::AddJudgeFlash(sakura::game::JudgeResult r, bool isKb,
                               int lane, float px, float py,
                               int hitErrorMs, bool showHitError)
{
    JudgeFlash flash;
    flash.result   = r;
    flash.timer    = JudgeFlash::FLASH_DURATION;
    flash.isKeyboard = isKb;
    flash.lane     = lane;
    flash.posX     = px;
    flash.posY     = py;
    flash.hitErrorMs = hitErrorMs;
    flash.showHitError = showHitError;
    m_judgeFlashes.push_back(flash);

    // ── 粒子特效：根据判定结果发射对应颜色粒子 ────────────────────────────────
    sakura::core::Color judgeColor = JudgeResultColor(r);

    float emitX, emitY;
    if (isKb)
    {
        // 键盘音符：在轨道判定线附近发射
        emitX = GetLaneX(lane) + LANE_W * 0.5f;
        emitY = JUDGE_LINE_Y;
    }
    else
    {
        emitX = px;
        emitY = py;
    }

    if (r == sakura::game::JudgeResult::Miss)
    {
        // Miss: 触发轻微屏幕震动
        sakura::effects::ScreenShake::GetInstance().Trigger(0.003f, 0.15f, 8.0f);
        // 少量红色火花
        m_particles.Emit(emitX, emitY, 6,
            sakura::effects::ParticlePresets::JudgeSpark({ 229, 115, 115, 200 }));
    }
    else
    {
        // 其他判定：发射对应颜色的命中火花
        m_particles.Emit(emitX, emitY, 12,
            sakura::effects::ParticlePresets::HitBurst(judgeColor));
    }

    // ── 判定音效 ──────────────────────────────────────────────────────────────
    if(r==sakura::game::JudgeResult::Miss || sakura::core::Config::GetInstance().Get<bool>("audio.judgment_sounds",false))
        sakura::audio::AudioManager::GetInstance().PlayJudgeSFX(r);

    // ── Hitsound（按音符类型）────────────────────────────────────────────────
    auto hsType = isKb
        ? sakura::audio::HitsoundType::Tap
        : sakura::audio::HitsoundType::Circle;
    if (r != sakura::game::JudgeResult::Miss)
        sakura::audio::AudioManager::GetInstance().PlayHitsound(hsType);
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneGame::OnUpdate(float dt)
{
    if (m_finishing) return;
    m_gameState.Update(dt);
    const int now = m_gameState.GetCurrentTime();
    if (m_gameState.IsPlaying() || m_gameState.IsFinished())
    {
        if (m_gameState.TakeResumed() && m_options.mode != sakura::game::PlayMode::Replay)
            m_session.Submit({now, sakura::game::InputKind::Resume});
        if (m_options.mode == sakura::game::PlayMode::Replay && m_replay)
        {
            while (m_replayCursor < m_replay->inputs.size() && m_replay->inputs[m_replayCursor].time <= now)
                m_session.Submit(m_replay->inputs[m_replayCursor++]);
        }
        else
        {
            const auto mouse = sakura::core::Input::GetMousePosition();
            if(m_options.mode!=sakura::game::PlayMode::AutoPlay) m_session.Submit({now, sakura::game::InputKind::Pointer, 0,
                (mouse.x - MOUSE_X) / MOUSE_W, (mouse.y - MOUSE_Y) / MOUSE_H});
            m_session.Submit({now, sakura::game::InputKind::Tick});
        }
    }
    if (m_gameState.IsFinished())
    {
        m_finishing = true;
        m_session.Finish();
        if(m_options.returnToEditor){
            sakura::ui::ToastManager::Instance().Show("试玩结束 · "+std::to_string(m_session.Score().GetScore())+" 分",sakura::ui::ToastType::Success);
            m_manager.PopScene();return;
        }
        if (m_options.mode == sakura::game::PlayMode::Practice && m_options.loop)
        {
            Retry();
            return;
        }
        const auto& difficulty = m_chartInfo.difficulties[m_difficultyIndex];
        auto result = m_session.Score().GetResult(m_chartInfo.id, m_chartInfo.title,
            difficulty.name, m_difficultyIndex, difficulty.level,
            std::max(0.0, (now - m_options.startMs) / (1000.0 * m_options.rate)));
        result.assisted = m_options.mode != sakura::game::PlayMode::Standard;
        result.chartHash = sakura::game::Replay::Hash(m_gameState.GetChartData(),m_chartInfo.offset);
        result.playbackRate = m_options.rate;
        if (!result.assisted && sakura::core::Config::GetInstance().Get<bool>("gameplay.save_replays", true))
        {
            sakura::game::Replay replay;
            replay.chartId = m_chartInfo.id;
            replay.chartHash = sakura::game::Replay::Hash(m_gameState.GetChartData(),m_chartInfo.offset);
            replay.difficulty = m_difficultyIndex;
            replay.inputs = m_session.Recording();
            replay.pointerScaleX=m_pointerScaleX;replay.pointerScaleY=m_pointerScaleY;
            const auto path = sakura::core::Paths::User("replays/" + std::to_string(result.playedAt) +
                "-" + std::to_string(SDL_GetTicksNS()) + ".skr");
            if (replay.Save(path))
            {
                result.replayFile = path;
                auto& cfg = sakura::core::Config::GetInstance();
                cfg.Set("library.last_replay", path);
                cfg.Set("library.replay_"+m_chartInfo.id+"_"+std::to_string(m_difficultyIndex), path);
                cfg.Save();
            }
            else sakura::ui::ToastManager::Instance().Show("回放保存失败，请检查存储空间", sakura::ui::ToastType::Error);
        }
        m_manager.SwitchScene(std::make_unique<SceneResult>(m_manager, result, m_chartInfo, m_options), TransitionType::SakuraSweep, 0.44f);
        return;
    }
    for (auto& note : m_gameState.GetActiveKeyboardNotes())
        if (note.isJudged && note.result != sakura::game::JudgeResult::None)
            note.alpha = std::max(0.0f, note.alpha - dt * 8.0f);
    for (auto& note : m_gameState.GetActiveMouseNotes())
        if (note.isJudged && note.result != sakura::game::JudgeResult::None)
            note.alpha = std::max(0.0f, note.alpha - dt * 8.0f);

    // ── 更新判定闪现计时器 ────────────────────────────────────────────────────
    for (auto it = m_judgeFlashes.begin(); it != m_judgeFlashes.end(); )
    {
        it->timer -= dt;
        if (it->timer <= 0.0f)
            it = m_judgeFlashes.erase(it);
        else
            ++it;
    }

    // ── 粒子 + 特效更新 ───────────────────────────────────────────────────────
    if (m_useDefaultBg)
        m_defaultBg.Update(dt);
    else
        m_bgRenderer.Update(dt);
    m_particles.Update(dt);
    m_judgePulsePhase += dt;

    // 色差计时倒计
    if (m_chromaTimer > 0.0f)
    {
        m_chromaTimer -= dt;
        if (m_chromaTimer < 0.0f) m_chromaTimer = 0.0f;
    }

    m_comboPopTimer = std::max(0.0f, m_comboPopTimer - dt * 5.5f);
    // 连击里程碑检测（50/100/200/500/1000）
    int currCombo = m_session.Score().GetCombo();
    if (currCombo > m_lastCheckedCombo && currCombo > 1)
        m_comboPopTimer = 1.0f;
    static constexpr int MILESTONES[] = { 50, 100, 200, 500, 1000 };
    for (int ms : MILESTONES)
    {
        if (m_lastCheckedCombo < ms && currCombo >= ms)
        {
            // 触发里程碑特效
            float cx = TRACK_X + TRACK_W * 0.5f;
            float cy = JUDGE_LINE_Y;
            int count = (ms >= 500) ? 50 : 25;
            m_particles.Emit(cx, cy, count,
                sakura::effects::ParticlePresets::ComboMilestone());
            m_chromaTimer = 0.4f;  // 0.4s 色差效果
            LOG_DEBUG("[SceneGame] 连击里程碑: {}", ms);
        }
    }
    m_lastCheckedCombo = currCombo;

    // 更新轨道按键状态（用于发光）
    for (int i = 0; i < LANE_COUNT; ++i)
    {
        m_lanePulse[i]=std::max(0.0f,m_lanePulse[i]-dt*6);
        m_lanePressed[i]=m_session.Lanes()[i] || m_lanePulse[i]>0.2f;
    }
    for(auto& point:m_cursorTrail)point.age+=dt;
    std::erase_if(m_cursorTrail,[](const CursorPoint& p){return p.age>0.14f;});
    m_trailSample+=dt;
    if(m_trailSample>=0.008f){m_trailSample=0; m_cursorTrail.push_back({MOUSE_X+m_session.MouseX()*MOUSE_W,MOUSE_Y+m_session.MouseY()*MOUSE_H,0});}
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneGame::Pause()
{
    if(m_options.returnToEditor){m_gameState.Pause();m_manager.PopScene();return;}
    if (m_gameState.GetPhase() == sakura::game::GamePhase::Idle) return;
    const int now = m_gameState.GetCurrentTime();
    if(m_options.mode!=sakura::game::PlayMode::Replay){
        for (int lane = 0; lane < 4; ++lane) m_session.Submit({now, sakura::game::InputKind::LaneUp, lane});
        m_session.Submit({now, sakura::game::InputKind::MouseUp});
    }
    m_gameState.Pause();
    m_manager.PushScene(std::make_unique<ScenePause>(m_manager, m_gameState), TransitionType::Fade, 0.16f);
}

void SceneGame::Retry()
{
    if(m_options.returnToEditor){OnExit();OnEnter();return;}
    m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager, m_chartInfo, m_difficultyIndex, m_options),
        TransitionType::Fade, 0.18f);
}

void SceneGame::OnEvent(const SDL_Event& event)
{
    const Uint64 nowNS = SDL_GetTicksNS();
    m_inputLagMs = event.common.timestamp > 0 && event.common.timestamp <= nowNS
        ? static_cast<int>(std::min<Uint64>((nowNS - event.common.timestamp) / 1000000, 100)) : 0;
    m_inputLagMs = static_cast<int>(m_inputLagMs * m_options.rate);
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED)
    {
        Pause();
        return;
    }
    if(event.type==SDL_EVENT_WINDOW_RESIZED && m_options.mode!=sakura::game::PlayMode::Replay) {
        const float edge=std::min(event.window.data1*MOUSE_W,event.window.data2*MOUSE_H);
        if(edge>0){
            m_pointerScaleX=event.window.data1*MOUSE_W/edge;m_pointerScaleY=event.window.data2*MOUSE_H/edge;
            m_session.Submit({m_gameState.GetCurrentTime(),sakura::game::InputKind::PointerScale,0,m_pointerScaleX,m_pointerScaleY});
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const auto& cfg = sakura::core::Config::GetInstance();
        if (event.key.scancode == SDL_SCANCODE_ESCAPE ||
            event.key.scancode == cfg.Get<int>(sakura::core::ConfigKeys::kKeyPause, SDL_SCANCODE_ESCAPE))
        { Pause(); return; }
        if (event.key.scancode == cfg.Get<int>(sakura::core::ConfigKeys::kKeyRetry, SDL_SCANCODE_R))
        { Retry(); return; }
        HandleKeyPress(event.key.scancode);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        const auto mouse = sakura::core::Input::GetMousePosition();
        if (mouse.x >= 0.955f && mouse.y <= 0.06f) { Pause(); return; }
        HandleMouseClick(mouse.x, mouse.y);
    }
    if (m_options.mode == sakura::game::PlayMode::Replay || m_options.mode == sakura::game::PlayMode::AutoPlay) return;
    if (event.type == SDL_EVENT_KEY_UP)
        for (int lane = 0; lane < 4; ++lane)
            if (event.key.scancode == m_laneKeys[lane])
                m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::LaneUp, lane});
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
        m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::MouseUp});
    if (event.type == SDL_EVENT_MOUSE_MOTION && m_gameState.IsPlaying())
    {
        const auto mouse = sakura::core::Input::GetMousePosition();
        m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::Pointer, 0,
            (mouse.x - MOUSE_X) / MOUSE_W, (mouse.y - MOUSE_Y) / MOUSE_H});
    }
}

const char* SceneGame::JudgeResultText(sakura::game::JudgeResult r)
{
    switch (r)
    {
    case sakura::game::JudgeResult::Perfect: return "Perfect";
    case sakura::game::JudgeResult::Great:   return "Great";
    case sakura::game::JudgeResult::Good:    return "Good";
    case sakura::game::JudgeResult::Bad:     return "Bad";
    case sakura::game::JudgeResult::Miss:    return "Miss";
    default:                                  return "";
    }
}

sakura::core::Color SceneGame::JudgeResultColor(sakura::game::JudgeResult r)
{
    switch (r)
    {
    case sakura::game::JudgeResult::Perfect: return { 252, 220, 154, 255 };   // 香槟鎏金
    case sakura::game::JudgeResult::Great:   return { 138, 214, 248, 255 };   // 天青冰蓝
    case sakura::game::JudgeResult::Good:    return { 154, 228, 188, 255 };   // 翠玉薄荷
    case sakura::game::JudgeResult::Bad:     return { 244, 184, 132, 255 };   // 暖珀
    case sakura::game::JudgeResult::Miss:    return { 238, 118, 142, 255 };   // 绯红
    default:                                  return sakura::core::Color::White;
    }
}

// ── RenderBackground ─────────────────────────────────────────────────────────
void SceneGame::RenderBackground(sakura::core::Renderer& renderer)
{
    const auto& cfg = sakura::core::Config::GetInstance();
    const float dimRatio = std::clamp(cfg.Get<float>("gameplay.background_dim", 0.75f), 0.0f, 1.0f);

    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    if (!m_useDefaultBg)
    {
        m_bgRenderer.Render(renderer);
        sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, m_judgePulsePhase * 0.42f, 0.22f);
    }
    else
    {
        m_defaultBg.Render(renderer);
        sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, m_judgePulsePhase * 0.42f, 0.34f);
    }

    const uint8_t scrimA = static_cast<uint8_t>(dimRatio * 210.0f);
    const uint8_t scrimBot = static_cast<uint8_t>(std::min(246.0f, dimRatio * 210.0f + 22.0f));
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 6, 9, 20, scrimA },
        { 10, 14, 28, static_cast<uint8_t>(scrimA * 0.92f) },
        { 6, 8, 18, scrimBot },
        { 8, 11, 24, scrimBot });

    if (!cfg.Get<bool>("graphics.reduced_motion", false))
    {
        sakura::audio::AudioVisualizer::GetInstance().RenderWave(
            renderer, { 0.0f, 0.805f, 1.0f, 0.105f }, { 246, 178, 206, 42 }, 0.42f);
    }
}

// ── RenderTrack ───────────────────────────────────────────────────────────────
void SceneGame::RenderTrack(sakura::core::Renderer& renderer)
{
    const auto& cfg = sakura::core::Config::GetInstance();
    const float opacity = std::clamp(cfg.Get<float>("gameplay.lane_opacity", 0.9f), 0.1f, 1.0f);
    const bool glowOn = cfg.Get<bool>("graphics.glow", true);
    const bool reducedMotion = cfg.Get<bool>("graphics.reduced_motion", false);
    const float pulse = reducedMotion ? 0.5f : (0.5f + 0.5f * std::sin(m_judgePulsePhase * 5.2f));

    // ── 4K 烟熏琉璃轨道主体 ──────────────────────────────────────────────────
    const sakura::core::NormRect trackCard{ TRACK_X - 0.006f, 0.112f, TRACK_W + 0.012f, 0.812f };
    renderer.DrawSoftShadow(trackCard, 0.014f, 0.018f, { 2, 4, 12, static_cast<uint8_t>(175 * opacity) }, 0.006f, 10);
    renderer.DrawRoundedGradientRect(
        trackCard, 0.014f,
        { 16, 20, 40, static_cast<uint8_t>(228 * opacity) },
        { 18, 22, 44, static_cast<uint8_t>(228 * opacity) },
        { 10, 13, 28, static_cast<uint8_t>(244 * opacity) },
        { 12, 15, 32, static_cast<uint8_t>(244 * opacity) }, 10, true);

    for (int i = 0; i < LANE_COUNT; ++i)
    {
        const float x = GetLaneX(i);
        const bool outerLane = (i == 0 || i == LANE_COUNT - 1);
        const sakura::core::Color topTint = outerLane
            ? sakura::core::Color{ 44, 34, 62, static_cast<uint8_t>(52 * opacity) }
            : sakura::core::Color{ 30, 40, 66, static_cast<uint8_t>(46 * opacity) };
        const sakura::core::Color botTint = outerLane
            ? sakura::core::Color{ 76, 46, 74, static_cast<uint8_t>(96 * opacity) }
            : sakura::core::Color{ 46, 56, 86, static_cast<uint8_t>(84 * opacity) };
        renderer.DrawGradientRect({ x, 0.118f, LANE_W, JUDGE_LINE_Y - 0.118f }, topTint, topTint, botTint, botTint);
        if (i > 0)
        {
            renderer.DrawLine(x, 0.122f, x, 0.916f, { 196, 184, 222, static_cast<uint8_t>(44 * opacity) }, 0.0010f);
        }
    }

    // ── 动态节拍 / 小节刻度线 ────────────────────────────────────────────────
    if (m_gameState.GetPhase() != sakura::game::GamePhase::Idle)
    {
        const int now = m_gameState.GetCurrentTime();
        const float bpm = std::clamp(m_gameState.GetCurrentBPM(now), 30.0f, 400.0f);
        const float beatMs = 60000.0f / bpm;
        const int firstBeat = std::max(0, static_cast<int>(std::floor((now - 120.0f - m_chartInfo.offset) / beatMs)));
        const int lastBeat = std::min(firstBeat + 28, static_cast<int>(std::ceil((now + 2050.0f - m_chartInfo.offset) / beatMs)));
        for (int b = firstBeat; b <= lastBeat; ++b)
        {
            const int bt = static_cast<int>(std::round(b * beatMs)) + m_chartInfo.offset;
            const float by = CalcNoteRenderY(bt, now, 1.0f);
            if (by < 0.122f || by > JUDGE_LINE_Y - 0.004f) continue;
            const bool isBar = (b % 4 == 0);
            renderer.DrawLine(
                TRACK_X + 0.003f, by, TRACK_X + TRACK_W - 0.003f, by,
                isBar ? sakura::core::Color{ 250, 216, 157, static_cast<uint8_t>(68 * opacity) }
                      : sakura::core::Color{ 176, 186, 218, static_cast<uint8_t>(26 * opacity) },
                isBar ? 0.0015f : 0.0009f);
        }
    }

    // ── 轨道外框鎏金边轨 ─────────────────────────────────────────────────────
    renderer.DrawRoundedRectOutlineGradient(
        trackCard, 0.014f,
        { 250, 216, 157, static_cast<uint8_t>(165 * opacity) },
        { 246, 178, 206, static_cast<uint8_t>(95 * opacity) }, 0.0018f, 10);

    // ── 按键丝绸光柱（Additive）与键帽底座 ──────────────────────────────────
    for (int i = 0; i < LANE_COUNT; ++i)
    {
        const float x = GetLaneX(i);
        const float cx = x + LANE_W * 0.5f;
        const bool outerLane = (i == 0 || i == LANE_COUNT - 1);
        const sakura::core::Color accent = outerLane
            ? sakura::core::Color{ 248, 176, 208, 255 }
            : sakura::core::Color{ 148, 216, 250, 255 };
        const float energy = std::max(m_lanePressed[i] ? 0.88f : 0.0f, m_lanePulse[i]);

        if (energy > 0.02f)
        {
            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            const uint8_t bAlpha = static_cast<uint8_t>(energy * 115.0f * opacity);
            renderer.DrawQuad(
                { x + 0.008f, 0.34f }, { x + LANE_W - 0.008f, 0.34f },
                { x + LANE_W - 0.001f, JUDGE_LINE_Y }, { x + 0.001f, JUDGE_LINE_Y },
                accent.WithAlpha(0), accent.WithAlpha(0),
                accent.WithAlpha(bAlpha), accent.WithAlpha(bAlpha));
            if (glowOn)
            {
                renderer.DrawRadialGradient(cx, JUDGE_LINE_Y, LANE_W * 0.62f, 0.036f,
                    { 250, 224, 172, static_cast<uint8_t>(energy * 140.0f) }, accent.WithAlpha(0), 32);
            }
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        }

        // 判定线槽位胶囊与底部键帽
        const sakura::core::NormRect receptorRect{ x + 0.006f, JUDGE_LINE_Y - 0.0065f, LANE_W - 0.012f, 0.013f };
        renderer.DrawRoundedRect(receptorRect, 0.005f,
            m_lanePressed[i] ? accent.WithAlpha(185) : sakura::core::Color{ 42, 48, 76, 150 }, true, 6);
        renderer.DrawRoundedRect(receptorRect, 0.005f,
            m_lanePressed[i] ? sakura::core::Color{ 252, 232, 184, 240 } : sakura::core::Color{ 196, 188, 224, 95 },
            false, 6, 0.0012f);

        const sakura::core::NormRect keyCap{ x + 0.012f, 0.870f, LANE_W - 0.024f, 0.040f };
        renderer.DrawRoundedGradientRect(keyCap, 0.008f,
            m_lanePressed[i] ? accent.WithAlpha(165) : sakura::core::Color{ 28, 34, 60, 195 },
            m_lanePressed[i] ? accent.WithAlpha(135) : sakura::core::Color{ 22, 28, 50, 195 },
            m_lanePressed[i] ? sakura::core::Color{ 86, 56, 92, 210 } : sakura::core::Color{ 18, 22, 42, 215 },
            m_lanePressed[i] ? sakura::core::Color{ 86, 56, 92, 210 } : sakura::core::Color{ 18, 22, 42, 215 }, 6, true);
        renderer.DrawRoundedRectOutlineGradient(keyCap, 0.008f,
            m_lanePressed[i] ? sakura::core::Color{ 252, 226, 174, 235 } : sakura::core::Color{ 176, 184, 218, 82 },
            { 120, 130, 174, 42 }, 0.0012f, 6);
        renderer.DrawText(m_fontSmall, SDL_GetScancodeName(m_laneKeys[i]),
            cx, 0.877f, 0.022f,
            m_lanePressed[i] ? sakura::core::Color{ 255, 248, 236, 255 } : sakura::core::Color{ 182, 190, 218, 225 },
            sakura::core::TextAlign::Center);
    }

    // ── 鎏金樱华判定线与菱形端饰 ────────────────────────────────────────────
    if (glowOn)
    {
        sakura::effects::GlowEffect::DrawGlowBar(
            renderer, TRACK_X, JUDGE_LINE_Y - 0.003f, TRACK_W, 0.006f,
            { 248, 184, 210, static_cast<uint8_t>(135 + pulse * 55.0f) }, 0.010f, 4);
    }
    renderer.DrawLine(TRACK_X - 0.004f, JUDGE_LINE_Y, TRACK_X + TRACK_W + 0.004f, JUDGE_LINE_Y,
                      { 252, 224, 168, 245 }, 0.0028f);
    const float dHalf = 0.0065f;
    for (float endX : { TRACK_X - 0.006f, TRACK_X + TRACK_W + 0.006f })
    {
        renderer.DrawQuad(
            { endX, JUDGE_LINE_Y - dHalf }, { endX + dHalf * 0.65f, JUDGE_LINE_Y },
            { endX, JUDGE_LINE_Y + dHalf }, { endX - dHalf * 0.65f, JUDGE_LINE_Y },
            { 252, 228, 176, 245 }, { 246, 178, 206, 235 },
            { 252, 228, 176, 245 }, { 246, 178, 206, 235 });
    }

    // ── 右侧鼠标旋律琉璃舞台框 ───────────────────────────────────────────────
    const sakura::core::NormRect mouseStage{ MOUSE_X - 0.015f, MOUSE_Y - 0.025f, MOUSE_W + 0.030f, MOUSE_H + 0.055f };
    renderer.DrawRoundedGradientRect(
        mouseStage, 0.018f,
        { 18, 24, 46, static_cast<uint8_t>(68 * opacity) },
        { 22, 26, 52, static_cast<uint8_t>(58 * opacity) },
        { 12, 16, 34, static_cast<uint8_t>(86 * opacity) },
        { 16, 20, 40, static_cast<uint8_t>(78 * opacity) }, 12, true);
    renderer.DrawRoundedRectOutlineGradient(
        mouseStage, 0.018f,
        { 250, 216, 157, 92 }, { 246, 178, 206, 54 }, 0.0014f, 12);

    const float mcx = MOUSE_X + MOUSE_W * 0.5f;
    const float mcy = MOUSE_Y + MOUSE_H * 0.5f;
    renderer.DrawCircleOutline(mcx, mcy, 0.115f, { 250, 216, 157, 24 }, 0.0010f, 64);
    if (!reducedMotion)
    {
        sakura::audio::AudioVisualizer::GetInstance().RenderCircle(
            renderer, mcx, mcy, 0.118f, { 246, 178, 206, 38 }, 0.42f);
    }

    sakura::ui::VisualStyle::DrawBadgePill(
        renderer, { TRACK_X - 0.004f, 0.092f, 0.125f, 0.022f },
        "KEYS · 4K 节奏", m_fontSmall, 0.013f,
        { 36, 30, 58, 185 }, { 250, 216, 157, 125 }, { 250, 220, 168, 235 });
    sakura::ui::VisualStyle::DrawBadgePill(
        renderer, { MOUSE_X - 0.012f, 0.092f, 0.145f, 0.022f },
        "MOUSE · 丝滑旋律", m_fontSmall, 0.013f,
        { 30, 36, 62, 185 }, { 246, 178, 206, 125 }, { 246, 184, 212, 235 });
}

// ── RenderKeyboardNotes ───────────────────────────────────────────────────────
void SceneGame::RenderKeyboardNotes(sakura::core::Renderer& renderer)
{
    if (m_gameState.GetPhase() == sakura::game::GamePhase::Idle) return;

    const bool glowOn = sakura::core::Config::GetInstance().Get<bool>("graphics.glow", true);
    int now = m_gameState.GetCurrentTime();
    auto activeNotes = m_gameState.GetActiveKeyboardNotes();

    for (const auto& note : activeNotes)
    {
        if (note.isJudged && note.alpha <= 0.01f) continue;

        float sv    = m_gameState.GetCurrentSVSpeed(now);
        float ry    = CalcNoteRenderY(note.time, now, sv);
        float lx    = GetLaneX(note.lane);
        float alpha = note.alpha;

        float tailY = -999.0f;
        if (note.type == sakura::game::NoteType::Hold)
            tailY = CalcNoteRenderY(note.time + note.duration, now, sv);

        if (note.type == sakura::game::NoteType::Hold)
        {
            if (tailY > 1.05f) continue;
            if (ry < -0.05f && tailY < -0.05f) continue;
        }
        else
        {
            if (ry < -0.05f || ry > 1.05f) continue;
        }

        const bool outerLane = (note.lane == 0 || note.lane == LANE_COUNT - 1);
        const sakura::core::Color gemTop = outerLane
            ? sakura::core::Color{ 252, 198, 222, static_cast<uint8_t>(246 * alpha) }
            : sakura::core::Color{ 178, 228, 255, static_cast<uint8_t>(246 * alpha) };
        const sakura::core::Color gemBot = outerLane
            ? sakura::core::Color{ 224, 128, 172, static_cast<uint8_t>(236 * alpha) }
            : sakura::core::Color{ 108, 168, 236, static_cast<uint8_t>(236 * alpha) };
        const sakura::core::Color rimGold{ 252, 228, 174, static_cast<uint8_t>(235 * alpha) };

        switch (note.type)
        {
        case sakura::game::NoteType::Tap:
        {
            const sakura::core::NormRect gemRect{ lx + 0.004f, ry - NOTE_H * 0.54f, LANE_W - 0.008f, NOTE_H * 1.08f };
            if (glowOn)
            {
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(lx + LANE_W * 0.5f, ry, LANE_W * 0.62f, NOTE_H * 1.45f,
                    gemTop.WithAlpha(static_cast<uint8_t>(82 * alpha)), gemTop.WithAlpha(0), 24);
                renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
            }
            renderer.DrawRoundedGradientRect(gemRect, 0.006f, gemTop, gemTop, gemBot, gemBot, 8, true);
            renderer.DrawRoundedRect(
                { gemRect.x + 0.005f, gemRect.y + 0.0025f, gemRect.width - 0.010f, gemRect.height * 0.34f },
                0.003f, { 255, 252, 250, static_cast<uint8_t>(145 * alpha) }, true, 6);
            renderer.DrawRoundedRectOutlineGradient(gemRect, 0.006f, rimGold, gemTop, 0.0015f, 8);
            break;
        }
        case sakura::game::NoteType::Hold:
        {
            bool isHolding = note.isJudged && (note.result == sakura::game::JudgeResult::None);
            float headY = (isHolding && ry > JUDGE_LINE_Y) ? JUDGE_LINE_Y : ry;
            float topY = std::min(headY, tailY);
            float botY = std::max(headY, tailY);
            float bodyH = std::max(0.004f, botY - topY);

            const uint8_t bodyA = static_cast<uint8_t>((isHolding ? 205.0f : 158.0f) * alpha);
            const sakura::core::NormRect auraRect{ lx + 0.011f, topY, LANE_W - 0.022f, bodyH };
            const sakura::core::NormRect coreRect{ lx + 0.020f, topY, LANE_W - 0.040f, bodyH };

            renderer.DrawRoundedGradientRect(
                auraRect, 0.006f,
                gemBot.WithAlpha(static_cast<uint8_t>(bodyA * 0.68f)),
                gemBot.WithAlpha(static_cast<uint8_t>(bodyA * 0.68f)),
                gemTop.WithAlpha(bodyA),
                gemTop.WithAlpha(bodyA), 6, true);
            renderer.DrawRoundedGradientRect(
                coreRect, 0.004f,
                { 255, 246, 236, static_cast<uint8_t>(bodyA * 0.55f) },
                { 255, 246, 236, static_cast<uint8_t>(bodyA * 0.55f) },
                rimGold.WithAlpha(static_cast<uint8_t>(bodyA * 0.92f)),
                rimGold.WithAlpha(static_cast<uint8_t>(bodyA * 0.92f)), 6, true);
            renderer.DrawRoundedRectOutlineGradient(auraRect, 0.006f, rimGold.WithAlpha(static_cast<uint8_t>(165 * alpha)), gemTop.WithAlpha(static_cast<uint8_t>(120 * alpha)), 0.0012f, 6);

            // 尾部封口胶囊
            const sakura::core::NormRect tailCap{ lx + 0.008f, tailY - NOTE_H * 0.34f, LANE_W - 0.016f, NOTE_H * 0.68f };
            renderer.DrawRoundedRect(tailCap, 0.005f, rimGold.WithAlpha(static_cast<uint8_t>(205 * alpha)), true, 6);

            // 头部琉璃晶石
            const sakura::core::NormRect headRect{ lx + 0.004f, headY - NOTE_H * 0.54f, LANE_W - 0.008f, NOTE_H * 1.08f };
            renderer.DrawRoundedGradientRect(headRect, 0.006f, rimGold, rimGold, gemTop, gemTop, 8, true);
            renderer.DrawRoundedRectOutlineGradient(headRect, 0.006f, { 255, 252, 244, static_cast<uint8_t>(245 * alpha) }, rimGold, 0.0016f, 8);

            if (isHolding)
            {
                const float cx = lx + LANE_W * 0.5f;
                const float spin = m_judgePulsePhase * 220.0f;
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(cx, JUDGE_LINE_Y, LANE_W * 0.75f, 0.045f,
                    { 252, 224, 166, 125 }, { 246, 178, 206, 0 }, 32);
                renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
                renderer.DrawArc(cx, JUDGE_LINE_Y, 0.024f, spin, spin + 140.0f, { 252, 228, 172, 230 }, 0.0020f, 24);
                renderer.DrawArc(cx, JUDGE_LINE_Y, 0.024f, spin + 180.0f, spin + 320.0f, { 246, 178, 206, 220 }, 0.0020f, 24);
            }
            break;
        }
        default:
            break;
        }
    }
}

// ── RenderMouseNotes ──────────────────────────────────────────────────────────
void SceneGame::RenderMouseNotes(sakura::core::Renderer& renderer)
{
    if (m_gameState.GetPhase() == sakura::game::GamePhase::Idle) return;

    const auto& theme = sakura::core::Theme::GetInstance();
    const bool glowOn = sakura::core::Config::GetInstance().Get<bool>("graphics.glow", true);
    int now = m_gameState.GetCurrentTime();
    auto activeNotes = m_gameState.GetActiveMouseNotes();

    auto drawGradientApproachRing = [&](float sx,
                                        float sy,
                                        float radius,
                                        float thickness,
                                        const sakura::core::Color& noteColor,
                                        const sakura::game::GuidanceColor& startAccent,
                                        const sakura::game::GuidanceColor& endAccent,
                                        uint8_t ringAlpha,
                                        int noteTimeMs)
    {
        auto gradient = sakura::game::BuildApproachGradient(
            noteTimeMs,
            now,
            m_approachMs,
            { noteColor.r, noteColor.g, noteColor.b, ringAlpha },
            startAccent,
            endAccent);
        constexpr int kRingSegments = 56;
        for (int seg = 0; seg < kRingSegments; ++seg)
        {
            float t0 = static_cast<float>(seg) / static_cast<float>(kRingSegments);
            float t1 = static_cast<float>(seg + 1) / static_cast<float>(kRingSegments);
            float midT = (t0 + t1) * 0.5f;
            auto segColor = ToCoreColor(
                sakura::game::LerpGuidanceColor(gradient.startColor, gradient.endColor, midT));
            renderer.DrawArc(
                sx,
                sy,
                radius,
                -90.0f + t0 * 360.0f,
                -90.0f + t1 * 360.0f,
                segColor,
                thickness,
                2);
        }
    };

    for (const auto& note : activeNotes)
    {
        if (note.isJudged && note.alpha <= 0.01f) continue;
        if (note.time - now > m_approachMs) continue;

        float scale = CalcApproachScale(note.time, now);
        uint8_t alpha = static_cast<uint8_t>(note.alpha * 235.0f);
        const float edge = std::min(renderer.GetScreenWidth() * MOUSE_W, renderer.GetScreenHeight() * MOUSE_H);
        const float radius = sakura::game::Judge::GetMouseHitTolerance(note) * edge / std::min(renderer.GetScreenWidth(), renderer.GetScreenHeight());

        switch (note.type)
        {
        case sakura::game::NoteType::Circle:
        {
            float sx = MOUSE_X + note.x * MOUSE_W;
            float sy = MOUSE_Y + note.y * MOUSE_H;
            auto noteColor = theme.Colors().circleColor;
            noteColor.a = alpha;

            if (glowOn)
            {
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(sx, sy, radius * 1.48f, radius * 1.48f,
                    { 246, 178, 208, static_cast<uint8_t>(alpha * 0.34f) }, { 246, 178, 208, 0 }, 40);
                renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
            }

            renderer.DrawRadialGradient(sx, sy, radius, radius,
                { 252, 206, 226, static_cast<uint8_t>(alpha * 0.42f) },
                { 48, 34, 68, static_cast<uint8_t>(alpha * 0.30f) }, 48);
            renderer.DrawRingGradient(sx, sy, radius * 0.76f, radius,
                { 250, 216, 157, static_cast<uint8_t>(alpha * 0.22f) },
                { 246, 178, 206, static_cast<uint8_t>(alpha * 0.52f) }, 48);
            renderer.DrawCircleOutline(sx, sy, radius,
                { 252, 226, 174, alpha }, 0.0024f, 56);
            renderer.DrawCircleOutline(sx, sy, radius * 0.72f,
                { 248, 188, 214, static_cast<uint8_t>(alpha * 0.68f) }, 0.0012f, 48);

            const float d = radius * 0.22f;
            renderer.DrawQuad(
                { sx, sy - d }, { sx + d, sy }, { sx, sy + d }, { sx - d, sy },
                { 255, 244, 224, alpha }, { 248, 192, 216, alpha },
                { 255, 244, 224, alpha }, { 248, 192, 216, alpha });

            drawGradientApproachRing(
                sx, sy, radius * scale, 0.0026f, noteColor,
                { 126, 224, 255, static_cast<uint8_t>(alpha * 0.85f) },
                { 252, 216, 152, static_cast<uint8_t>(alpha * 0.90f) },
                static_cast<uint8_t>(alpha * 0.85f), note.time);
            break;
        }
        case sakura::game::NoteType::Slider:
        {
            float sx = MOUSE_X + note.x * MOUSE_W;
            float sy = MOUSE_Y + note.y * MOUSE_H;

            auto& msNotes = m_gameState.GetMouseNotes();
            int noteIndex = static_cast<int>(&note - msNotes.data());
            const sakura::game::SliderState* ssPtr = nullptr;
            for (const auto& s : m_session.Sliders())
            {
                if (s.noteIndex == noteIndex) { ssPtr = &s; break; }
            }
            bool isActive = (ssPtr != nullptr && ssPtr->headJudged);

            // 丝绸样条轨道（起点 → 所有拐点）
            std::vector<sakura::core::NormPoint> pts;
            pts.reserve(note.sliderPath.size() + 1);
            pts.push_back({ sx, sy });
            for (const auto& [wpx, wpy] : note.sliderPath)
                pts.push_back({ MOUSE_X + wpx * MOUSE_W, MOUSE_Y + wpy * MOUSE_H });

            if (pts.size() >= 2)
            {
                const uint8_t ribCoreA = static_cast<uint8_t>(alpha * (isActive ? 0.88f : 0.68f));
                const uint8_t ribAuraA = static_cast<uint8_t>(alpha * (isActive ? 0.42f : 0.28f));
                std::vector<sakura::core::RibbonNode> nodes;
                nodes.reserve(pts.size());
                const int nPts = static_cast<int>(pts.size());
                for (int pi = 0; pi < nPts; ++pi)
                {
                    const float u = static_cast<float>(pi) / static_cast<float>(std::max(1, nPts - 1));
                    const float w = radius * (0.82f - 0.14f * u);
                    const sakura::core::Color cCore{
                        static_cast<uint8_t>(252 + (142 - 252) * u),
                        static_cast<uint8_t>(222 + (228 - 222) * u),
                        static_cast<uint8_t>(164 + (236 - 164) * u),
                        ribCoreA };
                    const sakura::core::Color cEdge{
                        static_cast<uint8_t>(246 + (112 - 246) * u),
                        static_cast<uint8_t>(174 + (198 - 174) * u),
                        static_cast<uint8_t>(206 + (236 - 206) * u),
                        static_cast<uint8_t>(ribAuraA * (1.0f - 0.5f * u)) };
                    nodes.push_back({ pts[pi].x, pts[pi].y, w, cCore, cEdge });
                }
                renderer.DrawRibbonStrip(nodes.data(), static_cast<int>(nodes.size()), true);
            }

            // 拐点与终点星芒锚点
            for (int wi = 0; wi < static_cast<int>(note.sliderPath.size()); ++wi)
            {
                auto [wpx, wpy] = note.sliderPath[wi];
                float spx = MOUSE_X + wpx * MOUSE_W;
                float spy = MOUSE_Y + wpy * MOUSE_H;
                bool passed = isActive && (wi < ssPtr->nextWaypointIndex);
                bool isNext = isActive && (wi == ssPtr->nextWaypointIndex);
                bool isTail = (wi + 1 == static_cast<int>(note.sliderPath.size()));
                uint8_t wpAlpha = passed ? static_cast<uint8_t>(alpha * 0.28f) : static_cast<uint8_t>(alpha * 0.78f);

                renderer.DrawCircleOutline(spx, spy, isTail ? radius * 0.62f : 0.016f,
                    { 250, 222, 168, wpAlpha }, 0.0018f, 32);
                if (isNext)
                {
                    renderer.DrawRingGradient(spx, spy, 0.018f, 0.028f,
                        { 152, 238, 228, static_cast<uint8_t>(alpha * 0.85f) }, { 152, 238, 228, 0 }, 32);
                }
            }

            if (!isActive)
            {
                sakura::core::Color noteColor{ theme.Accent().r, theme.Accent().g, theme.Accent().b, alpha };
                renderer.DrawRadialGradient(sx, sy, radius, radius,
                    { 252, 226, 168, static_cast<uint8_t>(alpha * 0.45f) },
                    { 42, 54, 76, static_cast<uint8_t>(alpha * 0.30f) }, 48);
                renderer.DrawCircleOutline(sx, sy, radius, { 252, 228, 174, alpha }, 0.0024f, 56);
                drawGradientApproachRing(
                    sx, sy, radius * scale, 0.0026f, noteColor,
                    { 255, 220, 142, static_cast<uint8_t>(alpha * 0.84f) },
                    { 126, 240, 228, static_cast<uint8_t>(alpha * 0.88f) },
                    static_cast<uint8_t>(alpha * 0.84f), note.time);
            }
            else
            {
                float t = static_cast<float>(now - note.time)
                        / static_cast<float>(std::max(1, note.sliderDuration));
                t = std::max(0.0f, std::min(1.0f, t));
                auto [hx, hy] = sakura::game::Judge::GetSliderPosition(note, t);
                float shx = MOUSE_X + hx * MOUSE_W;
                float shy = MOUSE_Y + hy * MOUSE_H;

                if (glowOn)
                {
                    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                    renderer.DrawRadialGradient(shx, shy, radius * 1.45f, radius * 1.45f,
                        { 252, 226, 170, static_cast<uint8_t>(alpha * 0.62f) }, { 138, 228, 236, 0 }, 36);
                    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
                }
                renderer.DrawRadialGradient(shx, shy, 0.019f, 0.019f,
                    { 255, 250, 236, alpha }, { 250, 214, 154, static_cast<uint8_t>(alpha * 0.72f) }, 32);
                renderer.DrawCircleOutline(shx, shy, radius * 0.92f,
                    { 252, 226, 172, static_cast<uint8_t>(alpha * 0.85f) }, 0.0020f, 44);

                const float cmx = MOUSE_X + m_session.MouseX() * MOUSE_W;
                const float cmy = MOUSE_Y + m_session.MouseY() * MOUSE_H;
                float cursorSX = std::clamp(cmx, MOUSE_X, MOUSE_X + MOUSE_W);
                float cursorSY = std::clamp(cmy, MOUSE_Y, MOUSE_Y + MOUSE_H);

                renderer.DrawRingGradient(cursorSX, cursorSY, 0.016f, 0.026f,
                    { 152, 238, 228, static_cast<uint8_t>(alpha * 0.80f) }, { 246, 178, 206, 0 }, 36);
                renderer.DrawCircleOutline(cursorSX, cursorSY, 0.022f,
                    { 255, 250, 244, alpha }, 0.0020f, 36);
            }
            break;
        }
        default:
            break;
        }
    }
}

// ── RenderHUD ─────────────────────────────────────────────────────────────────
void SceneGame::RenderHUD(sakura::core::Renderer& renderer)
{
    using namespace sakura::core;
    if (m_fontHUD == INVALID_HANDLE) return;
    const auto& cfg = Config::GetInstance();
    const Color pink{ 246, 178, 206, 255 }, gold{ 250, 216, 157, 255 }, dim{ 168, 178, 206, 255 }, white{ 248, 242, 248, 255 };

    // 顶部与底部磨砂琉璃通栏
    renderer.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 0.084f }, { 14, 18, 36, 232 }, { 18, 22, 44, 232 }, { 10, 14, 28, 212 }, { 12, 16, 32, 212 });
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.02f, 0.98f, 0.084f, 0.58f);
    renderer.DrawGradientRect({ 0.0f, 0.930f, 1.0f, 0.070f }, { 10, 14, 28, 216 }, { 12, 16, 32, 216 }, { 14, 18, 36, 238 }, { 16, 20, 40, 238 });
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.02f, 0.98f, 0.930f, 0.52f);

    sakura::ui::VisualStyle::DrawTextFit(renderer, m_fontHUD, m_chartInfo.title, 0.036f, 0.011f, 0.027f, 0.36f, white);
    const std::string diffBadge = m_chartInfo.difficulties[m_difficultyIndex].name + "  ·  Lv." +
        std::to_string(static_cast<int>(m_chartInfo.difficulties[m_difficultyIndex].level));
    renderer.DrawText(m_fontSmall, diffBadge, 0.036f, 0.050f, 0.016f, gold);

    const char* modes[] = { "LIVE · 正式演奏", "PRACTICE · 自由练习", "AUTO · 自动演示", "REPLAY · 回放" };
    const char* modeText = m_options.returnToEditor ? "EDITOR · 试玩  (Esc 返回工房)" : modes[static_cast<int>(m_options.mode)];
    sakura::ui::VisualStyle::DrawBadgePill(
        renderer, { 0.435f, 0.024f, 0.19f, 0.034f }, modeText, m_fontSmall, 0.015f,
        { 44, 34, 68, 205 }, { 250, 216, 157, 150 }, pink);

    std::ostringstream score, accuracy;
    score << std::setw(7) << std::setfill('0') << m_session.Score().GetScore();
    accuracy << std::fixed << std::setprecision(2) << m_session.Score().GetAccuracy() << "%";
    renderer.DrawText(m_fontHUD, score.str(), 0.938f, 0.008f, 0.034f, white, TextAlign::Right);
    renderer.DrawText(m_fontSmall, accuracy.str(), 0.938f, 0.049f, 0.020f, gold, TextAlign::Right);

    // 右上角琉璃暂停按钮（保留 0.955f..1.0f, 0..0.06f 命中区域）
    const NormRect pauseRect{ 0.955f, 0.018f, 0.032f, 0.046f };
    renderer.DrawRoundedGradientRect(pauseRect, 0.008f, { 54, 42, 76, 195 }, { 38, 32, 60, 195 }, { 32, 26, 52, 215 }, { 26, 22, 44, 215 }, 6, true);
    renderer.DrawRoundedRectOutlineGradient(pauseRect, 0.008f, { 250, 216, 157, 165 }, { 246, 178, 206, 85 }, 0.0013f, 6);
    renderer.DrawRoundedRect({ 0.965f, 0.029f, 0.0032f, 0.024f }, 0.001f, white, true, 4);
    renderer.DrawRoundedRect({ 0.9745f, 0.029f, 0.0032f, 0.024f }, 0.001f, white, true, 4);

    // 连击弹性跳动展示
    const int combo = m_session.Score().GetCombo();
    if (cfg.Get<bool>("gameplay.show_combo", true) && combo > 1)
    {
        const bool reduced = cfg.Get<bool>("graphics.reduced_motion", false);
        const float pop = reduced ? 1.0f : (1.0f + 0.16f * sakura::utils::EaseOutCubic(m_comboPopTimer));
        const Color comboCol = combo >= 100 ? gold : (combo >= 50 ? Color{ 252, 198, 220, 255 } : pink);
        const float cx = TRACK_X + TRACK_W * 0.5f;
        if (!reduced && combo >= 50)
        {
            renderer.SetBlendMode(BlendMode::Additive);
            renderer.DrawRadialGradient(cx, 0.275f, 0.085f, 0.055f, comboCol.WithAlpha(44), comboCol.WithAlpha(0), 28);
            renderer.SetBlendMode(BlendMode::Alpha);
        }
        renderer.DrawText(m_fontHUD, std::to_string(combo), cx, 0.238f - (pop - 1.0f) * 0.02f, 0.058f * pop, comboCol, TextAlign::Center);
        renderer.DrawText(m_fontSmall, "C O M B O", cx, 0.316f, 0.016f, dim, TextAlign::Center);
    }

    // 底部歌曲进度丝带
    const int sec = std::max(0, m_gameState.GetCurrentTime()) / 1000;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", sec / 60, sec % 60);
    renderer.DrawText(m_fontSmall, buffer, 0.036f, 0.950f, 0.019f, dim);

    const float prog = std::clamp(m_gameState.GetProgress(), 0.0f, 1.0f);
    renderer.DrawRoundedRect({ 0.105f, 0.960f, 0.295f, 0.008f }, 0.004f, { 36, 42, 68, 210 }, true, 6);
    if (prog > 0.002f)
    {
        const float pw = std::max(0.008f, 0.295f * prog);
        renderer.DrawRoundedGradientRect({ 0.105f, 0.960f, pw, 0.008f }, 0.004f, pink, gold, pink, gold, 6, true);
        renderer.DrawCircleFilled(0.105f + pw, 0.964f, 0.0065f, gold, 20);
    }

    // 实时击打误差尺（含滑动均值游标）
    if (cfg.Get<bool>("gameplay.show_hit_error", true))
    {
        const float center = 0.69f, width = 0.25f, y = 0.954f;
        renderer.DrawRoundedRect({ center - width * 0.5f, y, width, 0.011f }, 0.0055f, { 88, 44, 58, 155 }, true, 6);
        renderer.DrawRoundedRect({ center - width * 80.0f / 300.0f, y, width * 160.0f / 300.0f, 0.011f }, 0.0055f, { 68, 126, 166, 175 }, true, 6);
        renderer.DrawRoundedRect({ center - width * 25.0f / 300.0f, y, width * 50.0f / 300.0f, 0.011f }, 0.0055f, { 236, 198, 134, 225 }, true, 6);
        renderer.DrawLine(center, y - 0.006f, center, y + 0.017f, white, 0.0014f);

        const auto& errors = m_session.Score().GetHitErrors();
        const int begin = std::max(0, static_cast<int>(errors.size()) - 20);
        float sumErr = 0.0f;
        int countErr = 0;
        for (int i = begin; i < static_cast<int>(errors.size()); ++i)
        {
            const float clamped = static_cast<float>(std::clamp(errors[i], -150, 150));
            sumErr += clamped;
            ++countErr;
            const float x = center - clamped / 300.0f * width;
            const uint8_t tickA = static_cast<uint8_t>(60 + 185.0f * (i - begin + 1) / std::max(1, static_cast<int>(errors.size()) - begin));
            const Color tickCol = std::abs(clamped) <= 25.0f ? gold.WithAlpha(tickA)
                : (std::abs(clamped) <= 80.0f ? Color{ 148, 220, 250, tickA } : Color{ 244, 146, 166, tickA });
            renderer.DrawLine(x, y - 0.004f, x, y + 0.015f, tickCol, 0.0015f);
        }
        if (countErr > 0)
        {
            const float meanX = center - (sumErr / countErr) / 300.0f * width;
            renderer.DrawQuad(
                { meanX - 0.004f, y - 0.009f }, { meanX + 0.004f, y - 0.009f },
                { meanX, y - 0.002f }, { meanX, y - 0.002f },
                gold, gold, white, white);
        }
        renderer.DrawText(m_fontSmall, "EARLY", 0.556f, 0.949f, 0.013f, dim, TextAlign::Right);
        renderer.DrawText(m_fontSmall, "LATE", 0.824f, 0.949f, 0.013f, dim);
    }
    renderer.DrawText(m_fontSmall, "Esc 暂停  ·  R 重开", 0.966f, 0.950f, 0.015f, dim, TextAlign::Right);
}

// ── RenderCountdown ───────────────────────────────────────────────────────────
void SceneGame::RenderCountdown(sakura::core::Renderer& renderer)
{
    if (!m_gameState.IsInCountdown()) return;
    if (m_fontHUD == sakura::core::INVALID_HANDLE) return;

    int num = m_gameState.GetCountdownNumber();
    if (num <= 0) return;

    float remaining = m_gameState.GetCountdownRemaining();
    float phase     = std::fmod(remaining, 1.0f);
    float t         = 1.0f - phase;
    float scale     = 1.0f + 0.45f * (1.0f - sakura::utils::EaseOutBack(t));
    uint8_t alpha   = static_cast<uint8_t>((1.0f - sakura::utils::EaseInCubic(t)) * 255.0f);

    const float cx = 0.50f, cy = 0.46f;
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(cx, cy, 0.14f * scale, 0.14f * scale,
        { 246, 178, 206, static_cast<uint8_t>(alpha * 0.34f) }, { 246, 178, 206, 0 }, 40);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    renderer.DrawCircleOutline(cx, cy, 0.095f * scale, { 250, 216, 157, static_cast<uint8_t>(alpha * 0.65f) }, 0.0018f, 56);
    renderer.DrawArc(cx, cy, 0.108f, -90.0f, -90.0f + (1.0f - t) * 360.0f,
        { 246, 178, 206, static_cast<uint8_t>(alpha * 0.85f) }, 0.0026f, 48);

    std::string numStr = std::to_string(num);
    float fontSize = 0.125f * scale;
    renderer.DrawText(m_fontHUD, numStr, cx, cy - fontSize * 0.56f, fontSize,
        { 252, 240, 246, alpha }, sakura::core::TextAlign::Center);
}

// ── RenderJudgeFlashes ────────────────────────────────────────────────────────
void SceneGame::RenderJudgeFlashes(sakura::core::Renderer& renderer)
{
    if (m_fontSmall == sakura::core::INVALID_HANDLE) return;
    const bool reduced = sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false);

    for (const auto& flash : m_judgeFlashes)
    {
        float progress = 1.0f - flash.timer / JudgeFlash::FLASH_DURATION;
        uint8_t alpha  = static_cast<uint8_t>((1.0f - sakura::utils::EaseInQuad(progress)) * 255.0f);
        const float pop = (!reduced && progress < 0.22f)
            ? (1.0f + 0.24f * (1.0f - progress / 0.22f))
            : 1.0f;

        float posY = flash.isKeyboard
            ? JUDGE_LINE_Y - 0.068f - progress * 0.036f
            : flash.posY - 0.082f - progress * 0.036f;
        float posX = flash.isKeyboard
            ? GetLaneX(flash.lane) + LANE_W * 0.5f
            : flash.posX;

        auto color = JudgeResultColor(flash.result);
        color.a = alpha;

        if (!flash.isKeyboard && flash.result != sakura::game::JudgeResult::Miss)
        {
            const float waveR = 0.024f + sakura::utils::EaseOutCubic(progress) * 0.068f;
            renderer.DrawRingGradient(flash.posX, flash.posY, waveR * 0.76f, waveR,
                color.WithAlpha(static_cast<uint8_t>(alpha * 0.62f)), color.WithAlpha(0), 44);
        }

        renderer.DrawText(m_fontSmall, JudgeResultText(flash.result),
            posX, posY, 0.027f * pop, color, sakura::core::TextAlign::Center);

        if (sakura::core::Config::GetInstance().Get<bool>("gameplay.show_hit_error", true) &&
            flash.showHitError && flash.result != sakura::game::JudgeResult::Miss)
        {
            std::string timingText;
            if (flash.hitErrorMs > 0)
                timingText = "EARLY +" + std::to_string(flash.hitErrorMs) + "ms";
            else if (flash.hitErrorMs < 0)
                timingText = "LATE " + std::to_string(-flash.hitErrorMs) + "ms";
            else
                timingText = "0ms";

            renderer.DrawText(m_fontSmall, timingText,
                posX, posY + 0.027f, 0.015f,
                sakura::core::Color{ color.r, color.g, color.b, static_cast<uint8_t>(alpha * 0.92f) },
                sakura::core::TextAlign::Center);
        }
    }
}

// ── OnRender ─────────────────────────────────────────────────────────────────
void SceneGame::OnRender(sakura::core::Renderer& renderer)
{
    const auto& cfg = sakura::core::Config::GetInstance();
    auto& shader = sakura::effects::ShaderManager::GetInstance();
    const bool glowOn = cfg.Get<bool>("graphics.glow", true);
    const bool reduced = cfg.Get<bool>("graphics.reduced_motion", false);
    const float effectIntensity = std::clamp(cfg.Get<float>("graphics.effect_intensity", 0.7f), 0.0f, 1.0f);

    renderer.Flush();
    const bool capture = m_chromaTimer > 0.0f && glowOn && !reduced && shader.BeginCapture();

    RenderBackground(renderer);
    RenderTrack(renderer);
    if (m_gameState.GetPhase() != sakura::game::GamePhase::Idle)
    {
        RenderKeyboardNotes(renderer);
        RenderMouseNotes(renderer);
    }
    RenderJudgeFlashes(renderer);
    m_particles.Render(renderer);

    // 丝绸光标拖尾与晶石准星
    if (cfg.Get<bool>("gameplay.cursor_trail", true) && !reduced && m_cursorTrail.size() >= 2)
    {
        std::vector<sakura::core::NormPoint> trailPts;
        trailPts.reserve(m_cursorTrail.size());
        for (const auto& pt : m_cursorTrail)
            trailPts.push_back({ pt.x, pt.y });
        const uint8_t headA = static_cast<uint8_t>(185.0f * effectIntensity);
        renderer.DrawCatmullRomRibbon(
            trailPts.data(), static_cast<int>(trailPts.size()),
            0.002f, 0.011f,
            { 246, 178, 206, 0 }, { 252, 224, 168, headA },
            { 246, 178, 206, 0 }, { 246, 178, 206, static_cast<uint8_t>(headA * 0.48f) },
            4, true);
    }
    if (!m_cursorTrail.empty())
    {
        const auto& p = m_cursorTrail.back();
        renderer.DrawRingGradient(p.x, p.y, 0.009f, 0.015f, { 252, 226, 172, 210 }, { 246, 178, 206, 0 }, 32);
        renderer.DrawCircleOutline(p.x, p.y, 0.0115f, { 255, 244, 236, 225 }, 0.0018f, 32);
        renderer.DrawCircleFilled(p.x, p.y, 0.0032f, { 255, 248, 242, 245 }, 16);
    }

    if (capture)
    {
        renderer.Flush();
        auto* texture = shader.EndCapture();
        SDL_RenderTexture(renderer.GetSDLRenderer(), texture, nullptr, nullptr);
        const float strength = (m_chromaTimer / 0.4f) * effectIntensity;
        shader.DrawBloom(texture, 0.28f * strength);
        shader.DrawChromaticAberration(texture, strength);
    }
    if (cfg.Get<bool>("graphics.vignette", true))
    {
        renderer.Flush();
        shader.DrawVignette(0.28f);
    }

    RenderHUD(renderer);
    RenderCountdown(renderer);
}

} // namespace sakura::scene
