// scene_game.cpp — 游戏核心场景实现

#include "scene_game.h"
#include "scene_arena.h"
#include "scene_course.h"
#include "scene_select.h"
#include "scene_pause.h"
#include "scene_result.h"
#include "game/course.h"
#include "game/chart_loader.h"
#include "game/achievement_manager.h"
#include "data/database.h"
#include "net/api_client.h"
#include "core/input.h"
#include "core/config.h"
#include "core/paths.h"
#include "ui/toast.h"
#include "ui/visual_style.h"
#include "game/chart_utils.h"
#include "core/skin_manager.h"
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

sakura::core::Color ToCoreColor(const sakura::core::SkinColor& color)
{
    return { color.r, color.g, color.b, color.a };
}

sakura::core::Color ToCoreColor(const sakura::core::SkinColor& color, uint8_t alpha)
{
    return { color.r, color.g, color.b, alpha };
}

sakura::core::Color ScaleSkinAlpha(const sakura::core::SkinColor& color, float alphaScale)
{
    return {
        color.r,
        color.g,
        color.b,
        static_cast<uint8_t>(std::clamp(static_cast<float>(color.a) * alphaScale, 0.0f, 255.0f))
    };
}

sakura::core::Color LerpSkinColor(const sakura::core::SkinColor& a,
                                  const sakura::core::SkinColor& b,
                                  float t,
                                  uint8_t alpha)
{
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const auto lerpCh = [clamped](uint8_t c0, uint8_t c1) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(
            std::round(static_cast<float>(c0) + (static_cast<float>(c1) - static_cast<float>(c0)) * clamped),
            0.0f, 255.0f));
    };
    return { lerpCh(a.r, b.r), lerpCh(a.g, b.g), lerpCh(a.b, b.b), alpha };
}

const char* GradeToArenaString(sakura::game::Grade grade)
{
    switch (grade)
    {
    case sakura::game::Grade::SS: return "SS";
    case sakura::game::Grade::S:  return "S";
    case sakura::game::Grade::A:  return "A";
    case sakura::game::Grade::B:  return "B";
    case sakura::game::Grade::C:  return "C";
    default:                      return "D";
    }
}

}

std::vector<sakura::net::ArenaPlayerState> SceneGame::BuildLiveArenaStandings() const
{
    const auto currentUser = sakura::net::APIClient::GetInstance().GetCurrentUser();
    const auto& sc         = m_session.Score();
    std::vector<sakura::net::ArenaPlayerState> standings = m_arenaPlayers;

    bool foundSelf = false;
    for (auto& p : standings)
    {
        const bool isSelf = (!currentUser.id.empty() && p.userId == currentUser.id) ||
            (currentUser.id.empty() && !currentUser.username.empty() && p.username == currentUser.username);
        if (isSelf)
        {
            foundSelf   = true;
            p.score     = sc.GetScore();
            p.accuracy  = sc.GetAccuracy();
            p.combo     = sc.GetCombo();
            p.maxCombo  = std::max(p.maxCombo, sc.GetMaxCombo());
            p.grade     = GradeToArenaString(sc.GetGrade());
            p.connected = true;
            break;
        }
    }

    if (!foundSelf)
    {
        sakura::net::ArenaPlayerState selfState;
        selfState.userId    = currentUser.id;
        selfState.username  = currentUser.username.empty() ? "Player" : currentUser.username;
        selfState.danBadge  = currentUser.danBadge;
        selfState.score     = sc.GetScore();
        selfState.accuracy  = sc.GetAccuracy();
        selfState.combo     = sc.GetCombo();
        selfState.maxCombo  = sc.GetMaxCombo();
        selfState.grade     = GradeToArenaString(sc.GetGrade());
        selfState.connected = true;
        standings.push_back(std::move(selfState));
    }

    std::stable_sort(standings.begin(), standings.end(),
        [](const sakura::net::ArenaPlayerState& a, const sakura::net::ArenaPlayerState& b)
        {
            if (a.finished && b.finished && a.placement > 0 && b.placement > 0 && a.placement != b.placement)
                return a.placement < b.placement;
            if (a.score != b.score)
                return a.score > b.score;
            if (std::abs(a.accuracy - b.accuracy) > 0.001f)
                return a.accuracy > b.accuracy;
            return a.maxCombo > b.maxCombo;
        });

    return standings;
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

    *m_alive = true;
    m_arenaSyncTimer = 0.0f;
    m_arenaPlayers.clear();
    m_arenaHudAnim.clear();
    m_ghostRunner.Reset();

    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontHUD   = rm.GetDefaultFontHandle();
    m_fontSmall = rm.GetDefaultFontHandle();

    sakura::core::SkinManager::GetInstance().ScanSkins();
    sakura::core::SkinManager::GetInstance().LoadActiveFromConfig();
    sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(
        sakura::core::Config::GetInstance().Get<std::string>(
            "audio.hitsound", sakura::core::SkinManager::GetInstance().GetActiveSkin().hitsoundSet));

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
        const bool bgaEnabled = cfg.Get<bool>("visual.bga_enabled", true);
        if (!m_chartInfo.videoFile.empty() && bgaEnabled)
        {
            m_videoRenderer.Open(m_chartInfo.folderPath + "/" + m_chartInfo.videoFile, true);
        }
        else
        {
            m_videoRenderer.Close();
            sakura::effects::VideoDecoder::PublishGlobalStageMode(
                bgaEnabled ? sakura::effects::VideoStageMode::Fallback : sakura::effects::VideoStageMode::Disabled);
        }
    }

    // 从 Config 读取键位绑定（含第 5 键樱枝横梁）
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
        m_laneKeys[4] = static_cast<SDL_Scancode>(
            cfg.Get<int>(std::string(sakura::core::ConfigKeys::kKeyThumb), SDL_SCANCODE_SPACE));
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
    if (m_options.ghostMode != sakura::game::GhostMode::Off && !m_options.ghostReplayFile.empty())
    {
        m_ghostRunner.InitializeFromFile(
            m_gameState.GetChartData(), m_options.ghostReplayFile, m_options.ghostRivalName);
    }
    m_session.SetOnJudge([this](const sakura::game::JudgmentEvent& event)
    {
        if(event.keyboard && event.lane >= 0 && event.lane < KEY_BIND_COUNT) m_lanePulse[event.lane]=1.0f;
        AddJudgeFlash(event.result, event.keyboard, event.lane,
            MOUSE_X + event.x * MOUSE_W, MOUSE_Y + event.y * MOUSE_H, event.error, event.timed, event.synergy, event.noteKind);
    });
    m_judgeFlashes.clear();
    m_cursorTrail.clear();

    // ── 特效初始化 ────────────────────────────────────────────────────────────
    m_particles.Clear();
    m_judgePulsePhase = 0.0f;
    m_chromaTimer     = 0.0f;
    m_comboPopTimer   = 0.0f;
    m_stagePetalAccum = 0.0f;
    m_lastCheckedCombo = 0;
    m_lastFeverCount   = 0;
    m_lanePulse.fill(0.0f);
    m_lanePressed.fill(false);
}

// ── OnExit ─────────────────────────────────────────────────────────────────────

void SceneGame::OnExit()
{
    LOG_INFO("[SceneGame] 退出游戏场景");
    *m_alive = false;
    sakura::audio::AudioManager::GetInstance().StopMusic();
    m_judgeFlashes.clear();
    m_particles.Clear();
    m_videoRenderer.Close();
    m_bgRenderer.UnloadImage();
    sakura::audio::AudioClockSync::PublishGlobalDiagnostics(0.0, sakura::audio::ClockSyncMode::Idle);
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

const char* SceneGame::ModifierBadgeLabel(int requireLane) const
{
    if (requireLane >= 0 && requireLane < KEY_BIND_COUNT)
        return SDL_GetScancodeName(m_laneKeys[requireLane]);
    if (requireLane == sakura::game::kSynergyRightClick)
        return "RMB";
    return "";
}

// ── HandleKeyPress ────────────────────────────────────────────────────────────

void SceneGame::HandleKeyPress(SDL_Scancode key)
{
    if (!m_gameState.IsPlaying() || m_options.mode == sakura::game::PlayMode::AutoPlay ||
        m_options.mode == sakura::game::PlayMode::Replay) return;
    for (int lane = 0; lane < KEY_BIND_COUNT; ++lane)
        if (m_laneKeys[lane] == key)
            m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::LaneDown, lane});
}

void SceneGame::HandleMouseClick(float normX, float normY, bool rightButton)
{
    if (!m_gameState.IsPlaying() || m_options.mode == sakura::game::PlayMode::AutoPlay ||
        m_options.mode == sakura::game::PlayMode::Replay) return;
    const auto kind = rightButton ? sakura::game::InputKind::RightMouseDown : sakura::game::InputKind::MouseDown;
    m_session.Submit({GetInputTimeMs(), kind, 0,
        (normX - MOUSE_X) / MOUSE_W, (normY - MOUSE_Y) / MOUSE_H});
}

// ── AddJudgeFlash ─────────────────────────────────────────────────────────────

void SceneGame::AddJudgeFlash(sakura::game::JudgeResult r, bool isKb,
                               int lane, float px, float py,
                               int hitErrorMs, bool showHitError,
                               bool isSynergy,
                               sakura::game::HitEventKind noteKind)
{
    if (m_options.isCourseMode && sakura::game::CourseSession::GetInstance().IsActive() &&
        r != sakura::game::JudgeResult::None)
    {
        sakura::game::CourseSession::GetInstance().Gauge().OnJudge(r);
    }

    JudgeFlash flash;
    flash.result   = r;
    flash.timer    = JudgeFlash::FLASH_DURATION;
    flash.isKeyboard = isKb;
    flash.lane     = lane;
    flash.posX     = px;
    flash.posY     = py;
    flash.hitErrorMs = hitErrorMs;
    flash.showHitError = showHitError;
    flash.isSynergy  = isSynergy;
    m_judgeFlashes.push_back(flash);

    // ── 粒子特效：根据判定结果发射对应颜色粒子 ────────────────────────────────
    sakura::core::Color judgeColor = JudgeResultColor(r);

    float emitX, emitY;
    if (isKb)
    {
        emitX = (lane == sakura::game::kThumbLaneIndex)
            ? (TRACK_X + TRACK_W * 0.5f)
            : (GetLaneX(lane) + LANE_W * 0.5f);
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
        if (isKb && lane >= 0 && lane < KEY_BIND_COUNT)
            m_lanePulse[lane] = 1.0f;

        const sakura::core::Color burstCol = isSynergy
            ? sakura::core::Color{ 255, 224, 156, 255 }
            : judgeColor;
        m_particles.Emit(emitX, emitY, isSynergy ? 36 : 24,
            sakura::effects::ParticlePresets::HitBurst(burstCol));
        m_particles.Emit(emitX, emitY, 1,
            sakura::effects::ParticlePresets::ShockwaveRipple(burstCol, isKb ? 0.022f : 0.028f, isSynergy ? 3.8f : 2.9f));
        if (r == sakura::game::JudgeResult::Perfect || isSynergy)
        {
            m_particles.Emit(emitX, emitY, isSynergy ? 10 : 6,
                sakura::effects::ParticlePresets::ClickSpark());
            if (!sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false))
            {
                sakura::effects::ScreenShake::GetInstance().Trigger(
                    isSynergy ? 0.0022f : 0.0011f, 0.055f, 18.0f);
            }
        }
        m_hitShockwaves.push_back({ emitX, emitY, 0.22f, 0.22f, burstCol, isKb, lane, isSynergy });
    }

    // ── 判定音效与断连警示 ────────────────────────────────────────────────────
    if (r == sakura::game::JudgeResult::Miss && m_lastCheckedCombo >= 20)
        sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::ComboBreak);
    else if (r == sakura::game::JudgeResult::Miss || sakura::core::Config::GetInstance().Get<bool>("audio.judgment_sounds", false))
        sakura::audio::AudioManager::GetInstance().PlayJudgeSFX(r);

    // ── 10 类双区分层打击音精准派发 (v2.4.0) ─────────────────────────────────
    if (r != sakura::game::JudgeResult::Miss)
    {
        using sakura::audio::HitsoundType;
        using sakura::game::HitEventKind;
        HitsoundType hsType = HitsoundType::Tap;
        switch (noteKind)
        {
            case HitEventKind::HoldHead:     hsType = HitsoundType::HoldStart;    break;
            case HitEventKind::HoldTail:     hsType = HitsoundType::HoldEnd;      break;
            case HitEventKind::ThumbTap:     hsType = HitsoundType::ThumbBar;     break;
            case HitEventKind::Circle:       hsType = HitsoundType::Circle;       break;
            case HitEventKind::SliderHead:   hsType = HitsoundType::SliderStart;  break;
            case HitEventKind::SliderTick:   hsType = HitsoundType::SliderTick;   break;
            case HitEventKind::LinkChord:    hsType = HitsoundType::LinkChord;    break;
            case HitEventKind::SynergyBurst: hsType = HitsoundType::SynergyBurst; break;
            case HitEventKind::Tap:
            default:
                hsType = isKb
                    ? (lane == sakura::game::kThumbLaneIndex ? HitsoundType::ThumbBar : HitsoundType::Tap)
                    : HitsoundType::Circle;
                break;
        }
        sakura::audio::AudioManager::GetInstance().PlayHitsound(hsType);
    }
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneGame::OnUpdate(float dt)
{
    if (m_finishing) return;
    m_retryTracker.Update(dt);
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
    if (m_ghostRunner.IsActive() && (m_gameState.IsPlaying() || m_gameState.IsFinished()))
    {
        m_ghostRunner.AdvanceTo(now, m_session.Score().GetScore());
    }
    if (m_options.isArenaMatch)
    {
        if (!m_options.arenaRoomCode.empty() && m_gameState.IsPlaying())
        {
            m_arenaSyncTimer += dt;
            if (m_arenaSyncTimer >= 0.80f)
            {
                m_arenaSyncTimer = std::fmod(m_arenaSyncTimer, 0.80f);
                const auto& sc = m_session.Score();
                auto alive = m_alive;
                sakura::net::APIClient::GetInstance().ReportArenaProgressAsync(
                    m_options.arenaRoomCode,
                    sc.GetScore(),
                    sc.GetAccuracy(),
                    sc.GetCombo(),
                    sc.GetMaxCombo(),
                    GradeToArenaString(sc.GetGrade()),
                    false,
                    [this, alive](sakura::net::ArenaRoomResult res)
                    {
                        if (!*alive || !res.ok) return;
                        if (res.unchanged) return;
                        m_arenaPlayers = std::move(res.room.players);
                    });
            }
        }

        const auto standings = BuildLiveArenaStandings();
        const auto currentUser = sakura::net::APIClient::GetInstance().GetCurrentUser();
        const bool reducedMotion = sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false);
        const float slotLerp = reducedMotion ? 1.0f : (1.0f - std::exp(-dt * 11.5f));
        const float scoreLerp = reducedMotion ? 1.0f : (1.0f - std::exp(-dt * 16.0f));

        int selfIdx = -1;
        for (int i = 0; i < static_cast<int>(standings.size()); ++i)
        {
            const auto& p = standings[static_cast<std::size_t>(i)];
            if ((!currentUser.id.empty() && p.userId == currentUser.id) ||
                (currentUser.id.empty() && !currentUser.username.empty() && p.username == currentUser.username))
            {
                selfIdx = i;
                break;
            }
        }

        std::vector<int> visIndices;
        const int topN = std::min(4, static_cast<int>(standings.size()));
        visIndices.reserve(5);
        for (int i = 0; i < topN; ++i)
            visIndices.push_back(i);
        if (selfIdx >= 4)
            visIndices.push_back(selfIdx);

        for (int slot = 0; slot < static_cast<int>(visIndices.size()); ++slot)
        {
            const int sIdx = visIndices[static_cast<std::size_t>(slot)];
            const auto& p = standings[static_cast<std::size_t>(sIdx)];
            const std::string key = !p.userId.empty() ? p.userId : ("u:" + p.username);
            const int liveRank = sIdx + 1;
            auto it = std::find_if(m_arenaHudAnim.begin(), m_arenaHudAnim.end(),
                [&](const ArenaHudTrackEntry& e) { return e.key == key; });
            if (it == m_arenaHudAnim.end())
            {
                ArenaHudTrackEntry entry;
                entry.key          = key;
                entry.visualSlot   = static_cast<float>(slot);
                entry.targetSlot   = static_cast<float>(slot);
                entry.displayScore = static_cast<float>(p.score);
                entry.lastRank     = liveRank;
                entry.rankFlash    = 0.0f;
                m_arenaHudAnim.push_back(std::move(entry));
            }
            else
            {
                it->targetSlot = static_cast<float>(slot);
                if (it->lastRank != liveRank)
                {
                    it->rankFlash = 1.0f;
                    it->lastRank  = liveRank;
                }
                else
                {
                    it->rankFlash = std::max(0.0f, it->rankFlash - dt * 2.5f);
                }
                it->visualSlot   += (it->targetSlot - it->visualSlot) * slotLerp;
                it->displayScore += (static_cast<float>(p.score) - it->displayScore) * scoreLerp;
            }
        }
    }
    if (m_options.isCourseMode && sakura::game::CourseSession::GetInstance().IsActive())
    {
        if (sakura::game::CourseSession::GetInstance().Gauge().IsFailed())
        {
            m_finishing = true;
            sakura::audio::AudioManager::GetInstance().StopMusic();
            auto& cs = sakura::game::CourseSession::GetInstance();
            const auto& diff = m_chartInfo.difficulties[m_difficultyIndex];
            auto partial = m_session.Score().GetResult(
                m_chartInfo.id, m_chartInfo.title, diff.name, m_difficultyIndex, diff.level,
                std::max(0.0, (now - m_options.startMs) / 1000.0));
            cs.RecordStageResult(partial);
            auto rec = cs.BuildFinalRecord();
            sakura::data::Database::GetInstance().SaveCourseRecord(rec);
            cs.Abort();
            sakura::ui::ToastManager::Instance().Show(
                "段位血槽归零 · 考核未通过", sakura::ui::ToastType::Warning, 3.5f);
            m_manager.SwitchScene(
                std::make_unique<SceneCourse>(m_manager, rec), TransitionType::SilkShutter, 0.42f);
            return;
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
        if (m_options.isCourseMode && sakura::game::CourseSession::GetInstance().IsActive())
        {
            auto& cs = sakura::game::CourseSession::GetInstance();
            if (cs.Gauge().IsFailed())
            {
                cs.RecordStageResult(result);
                auto rec = cs.BuildFinalRecord();
                sakura::data::Database::GetInstance().SaveCourseRecord(rec);
                cs.Abort();
                sakura::ui::ToastManager::Instance().Show(
                    "段位血槽归零 · 考核未通过", sakura::ui::ToastType::Warning, 3.5f);
                m_manager.SwitchScene(
                    std::make_unique<SceneCourse>(m_manager, rec), TransitionType::SilkShutter, 0.42f);
                return;
            }
            if (!result.assisted)
            {
                sakura::data::Database::GetInstance().SaveScore(result);
                sakura::game::AchievementManager::GetInstance().CheckAndUnlock(result);
                if (sakura::net::APIClient::GetInstance().IsAuthenticated())
                    sakura::net::APIClient::GetInstance().SubmitScoreAsync(result, {});
            }
            cs.RecordStageResult(result);
            if (cs.HasNextStage() && cs.AdvanceStage())
            {
                sakura::game::ChartInfo nextChart;
                int nextDiff = 0;
                if (cs.GetCurrentStageChart(nextChart, nextDiff))
                {
                    sakura::ui::ToastManager::Instance().Show(
                        "第 " + std::to_string(cs.GetCurrentStageIndex()) + " 曲通过 · 进入下一首",
                        sakura::ui::ToastType::Success, 2.5f);
                    m_manager.SwitchScene(
                        std::make_unique<SceneGame>(m_manager, nextChart, nextDiff, m_options),
                        TransitionType::SakuraSweep, 0.38f);
                    return;
                }
            }
            auto rec = cs.BuildFinalRecord();
            sakura::data::Database::GetInstance().SaveCourseRecord(rec);
            if (sakura::core::Config::GetInstance().Get<bool>("online.auto_cloud_sync", true) &&
                sakura::net::APIClient::GetInstance().IsAuthenticated())
            {
                sakura::net::APIClient::GetInstance().SyncCloudSaveAsync();
            }
            cs.Abort();
            sakura::ui::ToastManager::Instance().Show(
                "✦ 樱华道场认定合格：[" + rec.badgeText + "]",
                sakura::ui::ToastType::Achievement, 4.5f);
            m_manager.SwitchScene(
                std::make_unique<SceneCourse>(m_manager, rec), TransitionType::SakuraSweep, 0.44f);
            return;
        }
        if (m_options.isArenaMatch && !m_options.arenaRoomCode.empty())
        {
            if (!result.assisted)
            {
                sakura::data::Database::GetInstance().SaveScore(result);
                for (const auto& achievement : sakura::game::AchievementManager::GetInstance().CheckAndUnlock(result))
                {
                    sakura::ui::ToastManager::Instance().Show(
                        "成就解锁: " + achievement.definition.title,
                        sakura::ui::ToastType::Achievement,
                        4.0f);
                }
                sakura::net::APIClient::GetInstance().SubmitScoreAsync(result, {});
            }
            sakura::net::APIClient::GetInstance().ReportArenaProgressAsync(
                m_options.arenaRoomCode,
                result.score,
                result.accuracy,
                m_session.Score().GetCombo(),
                result.maxCombo,
                GradeToArenaString(result.grade),
                true,
                {});
            m_manager.SwitchScene(
                std::make_unique<SceneArena>(m_manager, m_options.arenaRoomCode),
                TransitionType::SakuraSweep,
                0.44f);
            return;
        }
        m_manager.SwitchScene(std::make_unique<SceneResult>(m_manager, result, m_chartInfo, m_options), TransitionType::SakuraSweep, 0.44f);
        return;
    }
    for (auto& note : m_gameState.GetActiveKeyboardNotes())
    {
        if (note.isJudged && note.result != sakura::game::JudgeResult::None)
        {
            const float fadeRate = (note.result == sakura::game::JudgeResult::Miss) ? 5.5f : 14.0f;
            note.alpha = std::max(0.0f, note.alpha - dt * fadeRate);
        }
    }
    for (auto& note : m_gameState.GetActiveMouseNotes())
    {
        if (note.isJudged && note.result != sakura::game::JudgeResult::None)
        {
            const float fadeRate = (note.result == sakura::game::JudgeResult::Miss) ? 6.0f : 12.5f;
            note.alpha = std::max(0.0f, note.alpha - dt * fadeRate);
        }
    }

    for (auto it = m_hitShockwaves.begin(); it != m_hitShockwaves.end(); )
    {
        it->timer -= dt;
        if (it->timer <= 0.0f) it = m_hitShockwaves.erase(it);
        else ++it;
    }

    // ── 更新判定闪现计时器 ────────────────────────────────────────────────────
    for (auto it = m_judgeFlashes.begin(); it != m_judgeFlashes.end(); )
    {
        it->timer -= dt;
        if (it->timer <= 0.0f)
            it = m_judgeFlashes.erase(it);
        else
            ++it;
    }

    // ── 背景视频 + 粒子 + 特效更新 ────────────────────────────────────────────
    if (m_gameState.IsPlaying())
    {
        const auto stageVis = sakura::game::EvaluateStageVisuals(
            m_gameState.GetChartData(),
            now,
            sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false));
        if (stageVis.petalRateMult > 1.05f &&
            sakura::core::Config::GetInstance().Get<bool>("graphics.particles", true))
        {
            m_stagePetalAccum += dt * (stageVis.petalRateMult - 1.0f) * 6.0f;
            int spawned = 0;
            while (m_stagePetalAccum >= 1.0f && spawned < 12)
            {
                m_stagePetalAccum -= 1.0f;
                ++spawned;
                const float emitX = 0.05f + 0.90f * std::fmod(
                    m_judgePulsePhase * 0.618034f + static_cast<float>(spawned) * 0.273f, 1.0f);
                const bool useFg = ((spawned + (now / 97)) % 3 == 0);
                m_particles.Emit(
                    emitX,
                    -0.02f,
                    1,
                    useFg ? sakura::effects::ParticlePresets::SakuraPetalForeground()
                          : sakura::effects::ParticlePresets::SakuraPetalMidground());
            }
            if (m_stagePetalAccum > 4.0f)
                m_stagePetalAccum = 0.0f;
        }
    }

    m_videoRenderer.Update(
        std::max(0.0, static_cast<double>(m_gameState.GetCurrentTime()) / 1000.0),
        sakura::core::Config::GetInstance().Get<bool>("visual.bga_enabled", true));
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

    const int feverCount = m_session.Score().GetFeverCount();
    if (feverCount > m_lastFeverCount)
    {
        m_particles.Emit(0.50f, JUDGE_LINE_Y, 36,
            sakura::effects::ParticlePresets::ComboMilestone());
        m_chromaTimer = std::max(m_chromaTimer, 0.36f);
        sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::FeverTrigger);
        m_lastFeverCount = feverCount;
    }

    // 更新轨道按键状态（含第 5 键樱枝横梁发光）
    for (int i = 0; i < KEY_BIND_COUNT; ++i)
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
        for (int lane = 0; lane < KEY_BIND_COUNT; ++lane) m_session.Submit({now, sakura::game::InputKind::LaneUp, lane});
        m_session.Submit({now, sakura::game::InputKind::MouseUp});
        m_session.Submit({now, sakura::game::InputKind::RightMouseUp});
    }
    m_gameState.Pause();
    m_manager.PushScene(std::make_unique<ScenePause>(m_manager, m_gameState), TransitionType::Fade, 0.16f);
}

void SceneGame::Retry()
{
    if(m_options.returnToEditor){OnExit();OnEnter();return;}
    if (m_options.isCourseMode)
    {
        auto& cs = sakura::game::CourseSession::GetInstance();
        if (cs.IsActive())
        {
            const auto courseDef = cs.GetCourse();
            sakura::game::ChartLoader loader;
            auto charts = loader.ScanCharts("resources/charts");
            auto custom = loader.ScanCharts(sakura::core::Paths::User("charts"));
            for (auto& c : custom)
            {
                auto existing = std::find_if(charts.begin(), charts.end(),
                    [&](const sakura::game::ChartInfo& other) { return other.id == c.id; });
                if (existing == charts.end())
                    charts.push_back(std::move(c));
                else
                    *existing = std::move(c);
            }
            if (std::find_if(charts.begin(), charts.end(),
                    [this](const sakura::game::ChartInfo& other) { return other.id == m_chartInfo.id; }) == charts.end())
            {
                charts.push_back(m_chartInfo);
            }
            if (cs.Start(courseDef, charts))
            {
                sakura::game::ChartInfo firstChart;
                int firstDiff = 0;
                if (cs.GetCurrentStageChart(firstChart, firstDiff))
                {
                    sakura::ui::ToastManager::Instance().Show(
                        "段位考核已从第 1 曲重新开始", sakura::ui::ToastType::Info, 2.2f);
                    m_manager.SwitchScene(
                        std::make_unique<SceneGame>(m_manager, firstChart, firstDiff, m_options),
                        TransitionType::Fade, 0.18f);
                    return;
                }
            }
        }
        m_manager.SwitchScene(std::make_unique<SceneCourse>(m_manager), TransitionType::SilkShutter, 0.35f);
        return;
    }
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
        {
            Pause();
            return;
        }
        if (event.key.scancode == cfg.Get<int>(sakura::core::ConfigKeys::kKeyRetry, SDL_SCANCODE_R))
        {
            if (m_retryTracker.OnPress())
                Retry();
            else
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
            return;
        }
        HandleKeyPress(event.key.scancode);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT))
    {
        const auto mouse = sakura::core::Input::GetMousePosition();
        if (event.button.button == SDL_BUTTON_LEFT && mouse.x >= 0.955f && mouse.y <= 0.06f) { Pause(); return; }
        HandleMouseClick(mouse.x, mouse.y, event.button.button == SDL_BUTTON_RIGHT);
    }
    if (m_options.mode == sakura::game::PlayMode::Replay || m_options.mode == sakura::game::PlayMode::AutoPlay) return;
    if (event.type == SDL_EVENT_KEY_UP)
        for (int lane = 0; lane < KEY_BIND_COUNT; ++lane)
            if (event.key.scancode == m_laneKeys[lane])
                m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::LaneUp, lane});
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
        m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::MouseUp});
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
        m_session.Submit({GetInputTimeMs(), sakura::game::InputKind::RightMouseUp});
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
    const auto& skin = sakura::core::SkinManager::GetInstance().GetActiveSkin();
    switch (r)
    {
    case sakura::game::JudgeResult::Perfect: return ToCoreColor(skin.judgeColors[0]);
    case sakura::game::JudgeResult::Great:   return ToCoreColor(skin.judgeColors[1]);
    case sakura::game::JudgeResult::Good:    return ToCoreColor(skin.judgeColors[2]);
    case sakura::game::JudgeResult::Bad:     return ToCoreColor(skin.judgeColors[3]);
    case sakura::game::JudgeResult::Miss:    return ToCoreColor(skin.judgeColors[4]);
    default:                                  return sakura::core::Color::White;
    }
}

// ── RenderBackground ─────────────────────────────────────────────────────────
void SceneGame::RenderBackground(sakura::core::Renderer& renderer)
{
    const auto& cfg = sakura::core::Config::GetInstance();
    const bool reducedMotion = cfg.Get<bool>("graphics.reduced_motion", false);
    const float gameplayDim = std::clamp(cfg.Get<float>("gameplay.background_dim", 0.75f), 0.0f, 1.0f);
    const float visualStageDim = std::clamp(cfg.Get<float>("visual.bg_dim", 0.65f), 0.20f, 0.90f);
    const float dimRatio = std::clamp((gameplayDim + visualStageDim) * 0.5f, 0.20f, 0.92f);

    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    if (!m_videoRenderer.Render(renderer, visualStageDim))
    {
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
    }

    if (m_gameState.GetPhase() != sakura::game::GamePhase::Idle)
    {
        const auto stageVis = sakura::game::EvaluateStageVisuals(
            m_gameState.GetChartData(), m_gameState.GetCurrentTime(), reducedMotion);
        if (stageVis.tintStrength > 0.01f)
        {
            const uint8_t tr = static_cast<uint8_t>(std::clamp(stageVis.tintR, 0, 255));
            const uint8_t tg = static_cast<uint8_t>(std::clamp(stageVis.tintG, 0, 255));
            const uint8_t tb = static_cast<uint8_t>(std::clamp(stageVis.tintB, 0, 255));
            const float strength = std::clamp(stageVis.tintStrength, 0.0f, 1.0f);
            const uint8_t washA = static_cast<uint8_t>(std::min(58.0f, std::round(44.0f * strength)));
            const uint8_t radialA = static_cast<uint8_t>(std::min(58.0f, std::round(56.0f * strength)));

            renderer.DrawGradientRect(
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { tr, tg, tb, washA },
                { tr, tg, tb, static_cast<uint8_t>(washA * 0.78f) },
                { tr, tg, tb, static_cast<uint8_t>(washA * 0.55f) },
                { tr, tg, tb, washA });
            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            renderer.DrawRadialGradient(
                0.52f, 0.48f, 0.56f, 0.46f,
                { tr, tg, tb, radialA }, { tr, tg, tb, 0 }, 48);
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        }
        if (stageVis.vignetteBoost > 0.01f)
        {
            const float vBoost = std::clamp(stageVis.vignetteBoost, 0.0f, 1.0f);
            const uint8_t edgeA = static_cast<uint8_t>(std::clamp(std::round(136.0f * vBoost), 0.0f, 176.0f));
            const sakura::core::Color darkEdge{ 4, 6, 16, edgeA };
            const sakura::core::Color darkZero{ 4, 6, 16, 0 };
            renderer.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 0.18f }, darkEdge, darkEdge, darkZero, darkZero);
            renderer.DrawGradientRect({ 0.0f, 0.82f, 1.0f, 0.18f }, darkZero, darkZero, darkEdge, darkEdge);
            renderer.DrawGradientRect({ 0.0f, 0.0f, 0.12f, 1.0f }, darkEdge, darkZero, darkEdge, darkZero);
            renderer.DrawGradientRect({ 0.88f, 0.0f, 0.12f, 1.0f }, darkZero, darkEdge, darkZero, darkEdge);
        }
    }

    if (!reducedMotion)
    {
        sakura::audio::AudioVisualizer::GetInstance().RenderWave(
            renderer, { 0.0f, 0.805f, 1.0f, 0.105f }, { 246, 178, 206, 42 }, 0.42f);
    }
}

// ── RenderTrack ───────────────────────────────────────────────────────────────
void SceneGame::RenderTrack(sakura::core::Renderer& renderer)
{
    const auto& cfg = sakura::core::Config::GetInstance();
    const auto& skin = sakura::core::SkinManager::GetInstance().GetActiveSkin();
    const float opacity = std::clamp(cfg.Get<float>("gameplay.lane_opacity", 0.9f), 0.1f, 1.0f);
    const bool glowOn = cfg.Get<bool>("graphics.glow", true);
    const bool reducedMotion = cfg.Get<bool>("graphics.reduced_motion", false);
    const float pulse = reducedMotion ? 0.5f : (0.5f + 0.5f * std::sin(m_judgePulsePhase * 5.2f));
    const sakura::game::StageVisualState stageVis =
        (m_gameState.GetPhase() != sakura::game::GamePhase::Idle)
            ? sakura::game::EvaluateStageVisuals(m_gameState.GetChartData(), m_gameState.GetCurrentTime(), reducedMotion)
            : sakura::game::StageVisualState{};

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
        ToCoreColor(skin.judgeLine, static_cast<uint8_t>(165 * opacity)),
        ToCoreColor(skin.outerGemTop, static_cast<uint8_t>(95 * opacity)), 0.0018f, 10);

    // ── 按键丝绸光柱（Additive）与键帽底座 ──────────────────────────────────
    for (int i = 0; i < LANE_COUNT; ++i)
    {
        const float x = GetLaneX(i);
        const float cx = x + LANE_W * 0.5f;
        const bool outerLane = (i == 0 || i == LANE_COUNT - 1);
        const sakura::core::Color accent = outerLane
            ? ToCoreColor(skin.outerGemTop, 255)
            : ToCoreColor(skin.innerGemTop, 255);
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
                    ToCoreColor(skin.judgeLine, static_cast<uint8_t>(energy * 140.0f)), accent.WithAlpha(0), 32);
            }
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        }

        // 判定线槽位胶囊与底部键帽
        const sakura::core::NormRect receptorRect{ x + 0.006f, JUDGE_LINE_Y - 0.0065f, LANE_W - 0.012f, 0.013f };
        renderer.DrawRoundedRect(receptorRect, 0.005f,
            m_lanePressed[i] ? accent.WithAlpha(185) : sakura::core::Color{ 42, 48, 76, 150 }, true, 6);
        renderer.DrawRoundedRect(receptorRect, 0.005f,
            m_lanePressed[i] ? ToCoreColor(skin.judgeLine, 240) : sakura::core::Color{ 196, 188, 224, 95 },
            false, 6, 0.0012f);

        const sakura::core::Color keyPressBot{
            static_cast<uint8_t>(accent.r * 0.35f),
            static_cast<uint8_t>(accent.g * 0.32f),
            static_cast<uint8_t>(accent.b * 0.44f),
            210 };
        const sakura::core::NormRect keyCap{ x + 0.012f, 0.864f, LANE_W - 0.024f, 0.032f };
        renderer.DrawRoundedGradientRect(keyCap, 0.007f,
            m_lanePressed[i] ? accent.WithAlpha(165) : sakura::core::Color{ 28, 34, 60, 195 },
            m_lanePressed[i] ? accent.WithAlpha(135) : sakura::core::Color{ 22, 28, 50, 195 },
            m_lanePressed[i] ? keyPressBot : sakura::core::Color{ 18, 22, 42, 215 },
            m_lanePressed[i] ? keyPressBot : sakura::core::Color{ 18, 22, 42, 215 }, 6, true);
        renderer.DrawRoundedRectOutlineGradient(keyCap, 0.007f,
            m_lanePressed[i] ? ToCoreColor(skin.judgeLine, 235) : sakura::core::Color{ 176, 184, 218, 82 },
            { 120, 130, 174, 42 }, 0.0012f, 6);
        renderer.DrawText(m_fontSmall, SDL_GetScancodeName(m_laneKeys[i]),
            cx, 0.869f, 0.0195f,
            m_lanePressed[i] ? sakura::core::Color{ 255, 248, 236, 255 } : sakura::core::Color{ 182, 190, 218, 225 },
            sakura::core::TextAlign::Center);
    }

    // ── 第 5 键：底部全宽鎏金樱枝横梁底座 ───────────────────────────────────
    {
        const bool thumbDown = m_lanePressed[sakura::game::kThumbLaneIndex];
        const float thumbEnergy = std::max(thumbDown ? 0.90f : 0.0f, m_lanePulse[sakura::game::kThumbLaneIndex]);
        const sakura::core::Color thumbTop = ToCoreColor(skin.thumbGemTop, 255);
        const sakura::core::Color thumbBot = ToCoreColor(skin.thumbGemBot, 255);
        if (thumbEnergy > 0.02f)
        {
            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            renderer.DrawQuad(
                { TRACK_X + 0.010f, 0.56f }, { TRACK_X + TRACK_W - 0.010f, 0.56f },
                { TRACK_X + TRACK_W - 0.002f, JUDGE_LINE_Y }, { TRACK_X + 0.002f, JUDGE_LINE_Y },
                thumbTop.WithAlpha(0), thumbTop.WithAlpha(0),
                thumbTop.WithAlpha(static_cast<uint8_t>(thumbEnergy * 88.0f * opacity)),
                thumbBot.WithAlpha(static_cast<uint8_t>(thumbEnergy * 88.0f * opacity)));
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        }
        const sakura::core::Color thumbPressDark{
            static_cast<uint8_t>(thumbBot.r * 0.62f),
            static_cast<uint8_t>(thumbBot.g * 0.56f),
            static_cast<uint8_t>(thumbBot.b * 0.60f),
            215 };
        const sakura::core::NormRect thumbBar{ TRACK_X + 0.012f, 0.901f, TRACK_W - 0.024f, 0.018f };
        renderer.DrawRoundedGradientRect(thumbBar, 0.005f,
            thumbDown ? thumbTop.WithAlpha(195) : sakura::core::Color{ 36, 30, 56, 205 },
            thumbDown ? thumbBot.WithAlpha(185) : sakura::core::Color{ 28, 24, 48, 205 },
            thumbDown ? thumbPressDark : sakura::core::Color{ 20, 18, 36, 220 },
            thumbDown ? thumbPressDark : sakura::core::Color{ 20, 18, 36, 220 }, 6, true);
        renderer.DrawRoundedRectOutlineGradient(thumbBar, 0.005f,
            thumbDown ? sakura::core::Color{ 255, 244, 210, 245 } : thumbTop.WithAlpha(110),
            thumbBot.WithAlpha(55), 0.0011f, 6);
        renderer.DrawText(m_fontSmall, std::string("SPACE · ") + SDL_GetScancodeName(m_laneKeys[4]),
            TRACK_X + TRACK_W * 0.5f, 0.902f, 0.013f,
            thumbDown ? sakura::core::Color{ 32, 20, 38, 255 } : sakura::core::Color{ 238, 214, 176, 215 },
            sakura::core::TextAlign::Center);
    }

    // ── 鎏金樱华判定线与菱形端饰 ────────────────────────────────────────────
    const bool feverOn = m_session.Score().IsFeverActive();
    const sakura::core::Color judgeLineCol = ToCoreColor(skin.judgeLine);
    if (glowOn)
    {
        sakura::effects::GlowEffect::DrawGlowBar(
            renderer, TRACK_X, JUDGE_LINE_Y - 0.003f, TRACK_W, 0.006f,
            feverOn ? sakura::core::Color{ 255, 226, 152, static_cast<uint8_t>(175 + pulse * 65.0f) }
                    : judgeLineCol.WithAlpha(static_cast<uint8_t>(135 + pulse * 55.0f)), 0.010f, 4);
    }
    renderer.DrawLine(TRACK_X - 0.004f, JUDGE_LINE_Y, TRACK_X + TRACK_W + 0.004f, JUDGE_LINE_Y,
                      judgeLineCol, 0.0028f);
    const float dHalf = 0.0065f;
    for (float endX : { TRACK_X - 0.006f, TRACK_X + TRACK_W + 0.006f })
    {
        renderer.DrawQuad(
            { endX, JUDGE_LINE_Y - dHalf }, { endX + dHalf * 0.65f, JUDGE_LINE_Y },
            { endX, JUDGE_LINE_Y + dHalf }, { endX - dHalf * 0.65f, JUDGE_LINE_Y },
            judgeLineCol, ToCoreColor(skin.outerGemTop, 235),
            judgeLineCol, ToCoreColor(skin.outerGemTop, 235));
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
        feverOn ? sakura::core::Color{ 255, 228, 158, 185 } : ToCoreColor(skin.judgeLine, 92),
        feverOn ? sakura::core::Color{ 252, 188, 216, 135 } : ToCoreColor(skin.circleColor, 54), 0.0016f, 12);

    if (stageVis.pulseZoom > 0.01f)
    {
        const float pz = std::clamp(stageVis.pulseZoom, 0.0f, 1.0f);
        const float expand = 0.003f + 0.0085f * pz;
        const uint8_t auraA = static_cast<uint8_t>(std::clamp(std::round(158.0f * pz * opacity), 0.0f, 220.0f));
        const uint8_t softA = static_cast<uint8_t>(std::clamp(std::round(78.0f * pz * opacity), 0.0f, 140.0f));
        const sakura::core::Color pulseTop{
            static_cast<uint8_t>(std::clamp(stageVis.tintR, 0, 255)),
            static_cast<uint8_t>(std::clamp(stageVis.tintG, 0, 255)),
            static_cast<uint8_t>(std::clamp(stageVis.tintB, 0, 255)),
            auraA };
        const sakura::core::Color pulseBot = ToCoreColor(skin.judgeLine, static_cast<uint8_t>(auraA * 0.82f));
        const sakura::core::NormRect trackPulseRect{
            trackCard.x - expand, trackCard.y - expand,
            trackCard.width + expand * 2.0f, trackCard.height + expand * 2.0f };
        const sakura::core::NormRect mousePulseRect{
            mouseStage.x - expand, mouseStage.y - expand,
            mouseStage.width + expand * 2.0f, mouseStage.height + expand * 2.0f };

        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        renderer.DrawSoftShadow(trackCard, 0.015f, 0.011f + 0.010f * pz, pulseTop.WithAlpha(softA), 0.0f, 10);
        renderer.DrawRoundedRectOutlineGradient(
            trackPulseRect, 0.014f + expand, pulseTop, pulseBot, 0.0020f, 10);
        renderer.DrawSoftShadow(mouseStage, 0.019f, 0.011f + 0.010f * pz, pulseTop.WithAlpha(softA), 0.0f, 12);
        renderer.DrawRoundedRectOutlineGradient(
            mousePulseRect, 0.018f + expand, pulseTop, pulseBot, 0.0020f, 12);
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    }

    const float mcx = MOUSE_X + MOUSE_W * 0.5f;
    const float mcy = MOUSE_Y + MOUSE_H * 0.5f;
    renderer.DrawCircleOutline(mcx, mcy, 0.115f, { 250, 216, 157, 24 }, 0.0010f, 64);
    if (!reducedMotion)
    {
        sakura::audio::AudioVisualizer::GetInstance().RenderCircle(
            renderer, mcx, mcy, 0.118f, { 246, 178, 206, 38 }, 0.42f);
    }

    sakura::ui::VisualStyle::DrawBadgePill(
        renderer, { TRACK_X - 0.004f, 0.092f, 0.140f, 0.022f },
        "KEYS · 4K + SPACE", m_fontSmall, 0.0125f,
        { 36, 30, 58, 185 }, { 250, 216, 157, 125 }, { 250, 220, 168, 235 });
    const float mouseBadgeX = (m_options.isCourseMode && sakura::game::CourseSession::GetInstance().IsActive())
        ? (MOUSE_X + MOUSE_W - 0.133f)
        : (MOUSE_X - 0.012f);
    sakura::ui::VisualStyle::DrawBadgePill(
        renderer, { mouseBadgeX, 0.092f, 0.145f, 0.022f },
        "MOUSE · 丝滑旋律", m_fontSmall, 0.013f,
        { 30, 36, 62, 185 }, { 246, 178, 206, 125 }, { 246, 184, 212, 235 });
}

// ── RenderKeyboardNotes ───────────────────────────────────────────────────────
void SceneGame::RenderKeyboardNotes(sakura::core::Renderer& renderer)
{
    if (m_gameState.GetPhase() == sakura::game::GamePhase::Idle) return;

    const auto& skin = sakura::core::SkinManager::GetInstance().GetActiveSkin();
    const sakura::core::Color chordTop = ToCoreColor(skin.chordColor);
    const sakura::core::Color chordBot{
        static_cast<uint8_t>(std::max(0, static_cast<int>(chordTop.r) - 10)),
        static_cast<uint8_t>(std::max(0, static_cast<int>(chordTop.g) - 30)),
        static_cast<uint8_t>(std::max(0, static_cast<int>(chordTop.b) - 38)),
        chordTop.a };

    const bool glowOn = sakura::core::Config::GetInstance().Get<bool>("graphics.glow", true);
    const int now = m_gameState.GetCurrentTime();
    const float curSv = m_gameState.GetCurrentSVSpeed(now);
    auto activeNotes = m_gameState.GetActiveKeyboardNotes();

    const auto chordGroups = sakura::game::FindKeyboardChordGroups(activeNotes, 6);
    for (const auto& group : chordGroups)
    {
        if (group.noteCount < 2) continue;
        const float chordY = CalcNoteRenderY(group.timeMs, now, curSv);
        if (chordY < 0.12f || chordY > JUDGE_LINE_Y) continue;

        const float tieX0 = (group.includesThumb || !group.hasStandard)
            ? (TRACK_X + 0.008f)
            : (GetLaneX(group.minLane) + LANE_W * 0.18f);
        const float tieX1 = (group.includesThumb || !group.hasStandard)
            ? (TRACK_X + TRACK_W - 0.008f)
            : (GetLaneX(group.maxLane) + LANE_W * 0.82f);
        const float tieW = std::max(0.010f, tieX1 - tieX0);
        const float vis = std::clamp((chordY - 0.12f) / 0.08f, 0.30f, 1.0f);

        if (glowOn)
        {
            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            renderer.DrawRoundedGradientRect(
                { tieX0 - 0.003f, chordY - 0.0060f, tieW + 0.006f, 0.0120f }, 0.005f,
                chordTop.WithAlpha(static_cast<uint8_t>(92 * vis)),
                chordTop.WithAlpha(static_cast<uint8_t>(92 * vis)),
                chordBot.WithAlpha(static_cast<uint8_t>(64 * vis)),
                chordBot.WithAlpha(static_cast<uint8_t>(64 * vis)), 6, true);
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        }

        renderer.DrawRoundedGradientRect(
            { tieX0, chordY - 0.0026f, tieW, 0.0052f }, 0.0026f,
            chordTop.WithAlpha(static_cast<uint8_t>(228 * vis)),
            chordTop.WithAlpha(static_cast<uint8_t>(228 * vis)),
            chordBot.WithAlpha(static_cast<uint8_t>(216 * vis)),
            chordBot.WithAlpha(static_cast<uint8_t>(216 * vis)), 6, true);
        renderer.DrawLine(
            tieX0 + 0.003f, chordY, tieX1 - 0.003f, chordY,
            { 255, 252, 238, static_cast<uint8_t>(240 * vis) }, 0.0016f);
    }

    for (const auto& note : activeNotes)
    {
        if (note.isJudged && note.alpha <= 0.01f) continue;

        const bool isThumb = (note.lane == sakura::game::kThumbLaneIndex);
        const bool judgedHit = note.isJudged && (note.result != sakura::game::JudgeResult::Miss);
        const bool judgedMiss = note.isJudged && (note.result == sakura::game::JudgeResult::Miss);

        float ry    = CalcNoteRenderY(note.time, now, curSv);
        float lx    = isThumb ? (TRACK_X + 0.006f) : GetLaneX(note.lane);
        float nw    = isThumb ? (TRACK_W - 0.012f) : LANE_W;
        float alpha = note.alpha;

        float tailY = -999.0f;
        if (note.type == sakura::game::NoteType::Hold)
            tailY = CalcNoteRenderY(note.time + note.duration, now, curSv);

        if (note.type == sakura::game::NoteType::Tap)
        {
            if (judgedHit)
                ry = JUDGE_LINE_Y;
            if (ry < -0.05f || ry > 1.05f) continue;
        }
        else if (note.type == sakura::game::NoteType::Hold)
        {
            if (judgedHit)
            {
                ry    = std::min(ry, JUDGE_LINE_Y);
                tailY = std::min(tailY, JUDGE_LINE_Y);
            }
            if (tailY > 1.05f) continue;
            if (ry < -0.05f && tailY < -0.05f) continue;
        }
        else
        {
            if (ry < -0.05f || ry > 1.05f) continue;
        }

        bool isChordNote = false;
        if (!note.isJudged)
        {
            for (const auto& group : chordGroups)
            {
                if (group.noteCount >= 2 && note.time >= group.timeMs && note.time - group.timeMs <= 6)
                {
                    isChordNote = true;
                    break;
                }
            }
        }

        const bool outerLane = (note.lane == 0 || note.lane == LANE_COUNT - 1);
        sakura::core::Color gemTop = isThumb
            ? ScaleSkinAlpha(skin.thumbGemTop, alpha)
            : (outerLane
                ? ScaleSkinAlpha(skin.outerGemTop, alpha)
                : ScaleSkinAlpha(skin.innerGemTop, alpha));
        sakura::core::Color gemBot = isThumb
            ? ScaleSkinAlpha(skin.thumbGemBot, alpha)
            : (outerLane
                ? ScaleSkinAlpha(skin.outerGemBot, alpha)
                : ScaleSkinAlpha(skin.innerGemBot, alpha));
        sakura::core::Color rimGold = ScaleSkinAlpha(skin.chordColor, alpha);

        if (judgedMiss)
        {
            gemTop  = { 138, 76, 86, static_cast<uint8_t>(188 * alpha) };
            gemBot  = { 76, 44, 54, static_cast<uint8_t>(176 * alpha) };
            rimGold = { 156, 92, 102, static_cast<uint8_t>(168 * alpha) };
        }

        switch (note.type)
        {
        case sakura::game::NoteType::Tap:
        {
            const float centerX = lx + nw * 0.5f;
            const float widthScale = judgedHit ? (1.0f + 0.38f * (1.0f - alpha)) : 1.0f;
            const float tapW = (nw - 0.008f) * widthScale;
            const float tapH = judgedHit
                ? (NOTE_H * (0.22f + 0.78f * alpha))
                : (NOTE_H * (isThumb ? 0.88f : 1.08f));
            const sakura::core::NormRect gemRect{ centerX - tapW * 0.5f, ry - tapH * 0.5f, tapW, tapH };

            if (judgedHit)
            {
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(centerX, JUDGE_LINE_Y, tapW * 0.74f, NOTE_H * 2.15f,
                    { 255, 250, 228, static_cast<uint8_t>(215 * alpha) },
                    { 252, 214, 144, 0 }, 28);
                renderer.DrawLine(centerX - tapW * 0.62f, JUDGE_LINE_Y, centerX + tapW * 0.62f, JUDGE_LINE_Y,
                    { 255, 252, 240, static_cast<uint8_t>(230 * alpha) }, 0.0028f);
                renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
            }
            else if (glowOn && !judgedMiss)
            {
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(centerX, ry, nw * 0.55f, NOTE_H * 1.45f,
                    gemTop.WithAlpha(static_cast<uint8_t>(82 * alpha)), gemTop.WithAlpha(0), 24);
                renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
            }

            const sakura::core::Color drawTop = judgedHit
                ? sakura::core::Color{ 255, 250, 234, static_cast<uint8_t>(248 * alpha) }
                : gemTop;
            const sakura::core::Color drawBot = judgedHit
                ? sakura::core::Color{ 252, 222, 162, static_cast<uint8_t>(225 * alpha) }
                : gemBot;
            renderer.DrawRoundedGradientRect(gemRect, 0.006f, drawTop, drawTop, drawBot, drawBot, 8, true);
            if (!judgedMiss)
            {
                renderer.DrawRoundedRect(
                    { gemRect.x + 0.005f, gemRect.y + tapH * 0.12f, std::max(0.004f, gemRect.width - 0.010f), tapH * 0.34f },
                    0.003f, { 255, 252, 250, static_cast<uint8_t>(145 * alpha) }, true, 6);
            }
            renderer.DrawRoundedRectOutlineGradient(gemRect, 0.006f, rimGold, drawTop, 0.0016f, 8);

            if (isChordNote)
            {
                const sakura::core::NormRect chordFrame{
                    gemRect.x - 0.0022f, gemRect.y - 0.0022f,
                    gemRect.width + 0.0044f, gemRect.height + 0.0044f
                };
                renderer.DrawRoundedRectOutlineGradient(
                    chordFrame, 0.0075f,
                    chordTop.WithAlpha(static_cast<uint8_t>(250 * alpha)),
                    chordBot.WithAlpha(static_cast<uint8_t>(235 * alpha)),
                    0.0022f, 8);
            }
            break;
        }
        case sakura::game::NoteType::Hold:
        {
            bool isHolding = note.isJudged && (note.result == sakura::game::JudgeResult::None);
            float headY = judgedHit ? std::min(ry, JUDGE_LINE_Y) : ry;
            float topY = std::min(headY, tailY);
            float botY = std::max(headY, tailY);
            float bodyH = std::max(0.004f, botY - topY);

            const uint8_t bodyA = static_cast<uint8_t>((isHolding ? 205.0f : 158.0f) * alpha);
            const float padA = isThumb ? 0.022f : 0.011f;
            const float padC = isThumb ? 0.048f : 0.020f;
            const sakura::core::NormRect auraRect{ lx + padA, topY, nw - padA * 2.0f, bodyH };
            const sakura::core::NormRect coreRect{ lx + padC, topY, nw - padC * 2.0f, bodyH };

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
            const sakura::core::NormRect tailCap{ lx + 0.008f, tailY - NOTE_H * 0.34f, nw - 0.016f, NOTE_H * 0.68f };
            renderer.DrawRoundedRect(tailCap, 0.005f, rimGold.WithAlpha(static_cast<uint8_t>(205 * alpha)), true, 6);

            // 头部琉璃晶石
            const sakura::core::NormRect headRect{ lx + 0.004f, headY - NOTE_H * 0.54f, nw - 0.008f, NOTE_H * 1.08f };
            renderer.DrawRoundedGradientRect(headRect, 0.006f, rimGold, rimGold, gemTop, gemTop, 8, true);
            renderer.DrawRoundedRectOutlineGradient(headRect, 0.006f, { 255, 252, 244, static_cast<uint8_t>(245 * alpha) }, rimGold, 0.0016f, 8);
            if (isChordNote)
            {
                const sakura::core::NormRect chordFrame{
                    headRect.x - 0.0022f, headRect.y - 0.0022f,
                    headRect.width + 0.0044f, headRect.height + 0.0044f
                };
                renderer.DrawRoundedRectOutlineGradient(
                    chordFrame, 0.0075f,
                    chordTop.WithAlpha(static_cast<uint8_t>(250 * alpha)),
                    chordBot.WithAlpha(static_cast<uint8_t>(235 * alpha)),
                    0.0022f, 8);
            }

            if (isHolding)
            {
                const float cx = lx + nw * 0.5f;
                const float spin = m_judgePulsePhase * 220.0f;
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(cx, JUDGE_LINE_Y, nw * 0.55f, 0.045f,
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

    const auto& skin = sakura::core::SkinManager::GetInstance().GetActiveSkin();
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

    const auto activeKbNotes = m_gameState.GetActiveKeyboardNotes();
    const float curSv = m_gameState.GetCurrentSVSpeed(now);
    const auto syncPairs = sakura::game::FindCrossZoneSyncPairs(activeKbNotes, activeNotes, 8);

    const sakura::game::MouseNote* prevUnjudged = nullptr;
    for (const auto& nextNote : activeNotes)
    {
        if (nextNote.isJudged) continue;
        if (prevUnjudged != nullptr)
        {
            const int deltaMs = nextNote.time - prevUnjudged->time;
            if (deltaMs > 0 && deltaMs <= 900 &&
                prevUnjudged->time - now <= m_approachMs &&
                nextNote.time - now <= std::max(m_approachMs, 900))
            {
                const float fromNormX = (prevUnjudged->type == sakura::game::NoteType::Slider && !prevUnjudged->sliderPath.empty())
                    ? prevUnjudged->sliderPath.back().first
                    : prevUnjudged->x;
                const float fromNormY = (prevUnjudged->type == sakura::game::NoteType::Slider && !prevUnjudged->sliderPath.empty())
                    ? prevUnjudged->sliderPath.back().second
                    : prevUnjudged->y;
                const float x0 = MOUSE_X + fromNormX * MOUSE_W;
                const float y0 = MOUSE_Y + fromNormY * MOUSE_H;
                const float x1 = MOUSE_X + nextNote.x * MOUSE_W;
                const float y1 = MOUSE_Y + nextNote.y * MOUSE_H;
                const float dx = x1 - x0;
                const float dy = y1 - y0;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 0.012f)
                {
                    const float trim = std::min(0.028f, dist * 0.28f);
                    const float ux = dx / dist;
                    const float uy = dy / dist;
                    const sakura::core::NormPoint p0{ x0 + ux * trim, y0 + uy * trim };
                    const sakura::core::NormPoint p1{ x1 - ux * trim, y1 - uy * trim };
                    const sakura::core::NormPoint pm{ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
                    const float timeFade = 1.0f - 0.52f * (static_cast<float>(deltaMs) / 900.0f);
                    const float appearFade = std::clamp(
                        1.0f - static_cast<float>(std::max(0, nextNote.time - now)) /
                            static_cast<float>(std::max(1, std::max(m_approachMs, 900))),
                        0.18f, 1.0f);
                    const float visAlpha = timeFade * appearFade * std::min(prevUnjudged->alpha, nextNote.alpha);
                    const uint8_t startA = static_cast<uint8_t>(96.0f * visAlpha);
                    const uint8_t endA   = static_cast<uint8_t>(42.0f * visAlpha);
                    const std::array<sakura::core::NormPoint, 3> guidePts = { p0, pm, p1 };
                    renderer.DrawCatmullRomRibbon(
                        guidePts.data(), 3, 0.0024f, 0.0015f,
                        { 252, 224, 170, startA },
                        { 246, 182, 210, endA },
                        { 252, 224, 170, static_cast<uint8_t>(startA * 0.30f) },
                        { 246, 182, 210, 0 }, 4, true);
                }
            }
        }
        prevUnjudged = &nextNote;
    }

    for (size_t mi = 0; mi < activeNotes.size(); ++mi)
    {
        const auto& note = activeNotes[mi];
        if (note.isJudged && note.alpha <= 0.01f) continue;
        if (note.time - now > m_approachMs) continue;

        float scale = CalcApproachScale(note.time, now);
        uint8_t alpha = static_cast<uint8_t>(note.alpha * 235.0f);
        const float edge = std::min(renderer.GetScreenWidth() * MOUSE_W, renderer.GetScreenHeight() * MOUSE_H);
        const float radius = sakura::game::Judge::GetMouseHitTolerance(note) * edge / std::min(renderer.GetScreenWidth(), renderer.GetScreenHeight());
        const float noteSX = MOUSE_X + note.x * MOUSE_W;
        const float noteSY = MOUSE_Y + note.y * MOUSE_H;

        if (!note.isJudged)
        {
            for (const auto& sp : syncPairs)
            {
                if (sp.msIndex != mi || sp.kbIndex >= activeKbNotes.size()) continue;
                const auto& kb = activeKbNotes[sp.kbIndex];
                if (kb.isJudged) continue;

                const float kx = (kb.lane == sakura::game::kThumbLaneIndex)
                    ? (TRACK_X + TRACK_W * 0.5f)
                    : (GetLaneX(kb.lane) + LANE_W * 0.5f);
                const float ky = std::clamp(CalcNoteRenderY(kb.time, now, curSv), 0.12f, JUDGE_LINE_Y);
                const std::array<sakura::core::NormPoint, 3> tether = {
                    sakura::core::NormPoint{ kx, ky },
                    sakura::core::NormPoint{ (kx + noteSX) * 0.5f, std::min(ky, noteSY) - 0.035f },
                    sakura::core::NormPoint{ noteSX, noteSY }
                };
                renderer.DrawCatmullRomRibbon(tether.data(), 3, 0.0040f, 0.0040f,
                    { 255, 232, 168, static_cast<uint8_t>(alpha * 0.85f) },
                    { 250, 206, 148, static_cast<uint8_t>(alpha * 0.82f) },
                    { 255, 224, 152, static_cast<uint8_t>(alpha * 0.28f) },
                    { 246, 178, 206, static_cast<uint8_t>(alpha * 0.26f) }, 6, true);

                renderer.DrawCircleOutline(noteSX, noteSY, radius * 1.14f,
                    { 255, 234, 168, static_cast<uint8_t>(alpha * 0.92f) }, 0.0020f, 52);
                renderer.DrawCircleOutline(noteSX, noteSY, radius * 1.24f,
                    { 250, 208, 142, static_cast<uint8_t>(alpha * 0.64f) }, 0.0013f, 52);
                break;
            }
        }

        if (note.requireLane >= 0 && !note.isJudged)
        {
            renderer.DrawCircleOutline(noteSX, noteSY, radius * 1.18f,
                { 255, 228, 158, static_cast<uint8_t>(alpha * 0.90f) }, 0.0018f, 48);
            const std::string badgeText = std::string(ModifierBadgeLabel(note.requireLane)) + " + HIT";
            sakura::ui::VisualStyle::DrawBadgePill(renderer,
                { noteSX - 0.045f, noteSY - radius - 0.030f, 0.090f, 0.020f },
                badgeText, m_fontSmall, 0.012f,
                { 46, 32, 66, static_cast<uint8_t>(alpha * 0.92f) },
                { 255, 226, 158, alpha },
                { 255, 236, 184, alpha });
        }

        switch (note.type)
        {
        case sakura::game::NoteType::Circle:
        {
            float sx = noteSX;
            float sy = noteSY;
            sakura::core::Color noteColor = ToCoreColor(skin.circleColor, alpha);
            const sakura::core::Color circleBright{
                static_cast<uint8_t>(std::min(255, static_cast<int>(skin.circleColor.r) + 18)),
                static_cast<uint8_t>(std::min(255, static_cast<int>(skin.circleColor.g) + 28)),
                static_cast<uint8_t>(std::min(255, static_cast<int>(skin.circleColor.b) + 22)),
                alpha };

            if (glowOn)
            {
                renderer.SetBlendMode(sakura::core::BlendMode::Additive);
                renderer.DrawRadialGradient(sx, sy, radius * 1.48f, radius * 1.48f,
                    noteColor.WithAlpha(static_cast<uint8_t>(alpha * 0.34f)), noteColor.WithAlpha(0), 40);
                renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
            }

            renderer.DrawRadialGradient(sx, sy, radius, radius,
                circleBright.WithAlpha(static_cast<uint8_t>(alpha * 0.42f)),
                { 48, 34, 68, static_cast<uint8_t>(alpha * 0.30f) }, 48);
            renderer.DrawRingGradient(sx, sy, radius * 0.76f, radius,
                ToCoreColor(skin.chordColor, static_cast<uint8_t>(alpha * 0.22f)),
                noteColor.WithAlpha(static_cast<uint8_t>(alpha * 0.52f)), 48);
            renderer.DrawCircleOutline(sx, sy, radius,
                ToCoreColor(skin.chordColor, alpha), 0.0024f, 56);
            renderer.DrawCircleOutline(sx, sy, radius * 0.72f,
                circleBright.WithAlpha(static_cast<uint8_t>(alpha * 0.68f)), 0.0012f, 48);

            const float d = radius * 0.22f;
            renderer.DrawQuad(
                { sx, sy - d }, { sx + d, sy }, { sx, sy + d }, { sx - d, sy },
                { 255, 244, 224, alpha }, circleBright,
                { 255, 244, 224, alpha }, circleBright);

            drawGradientApproachRing(
                sx, sy, radius * scale, 0.0026f, noteColor,
                { skin.approachStart.r, skin.approachStart.g, skin.approachStart.b, static_cast<uint8_t>(alpha * 0.85f) },
                { skin.approachEnd.r, skin.approachEnd.g, skin.approachEnd.b, static_cast<uint8_t>(alpha * 0.90f) },
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
                    const sakura::core::Color cCore = LerpSkinColor(skin.sliderCore, skin.sliderEdge, u, ribCoreA);
                    const sakura::core::Color cEdge = LerpSkinColor(
                        skin.sliderCore, skin.sliderEdge,
                        std::clamp(u * 0.65f + 0.35f, 0.0f, 1.0f),
                        static_cast<uint8_t>(ribAuraA * (1.0f - 0.5f * u)));
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
                    ToCoreColor(skin.sliderCore, wpAlpha), 0.0018f, 32);
                if (isNext)
                {
                    renderer.DrawRingGradient(spx, spy, 0.018f, 0.028f,
                        ToCoreColor(skin.sliderEdge, static_cast<uint8_t>(alpha * 0.85f)),
                        ToCoreColor(skin.sliderEdge, 0), 32);
                }
            }

            if (!isActive)
            {
                sakura::core::Color noteColor = ToCoreColor(skin.circleColor, alpha);
                renderer.DrawRadialGradient(sx, sy, radius, radius,
                    ToCoreColor(skin.sliderCore, static_cast<uint8_t>(alpha * 0.45f)),
                    { 42, 54, 76, static_cast<uint8_t>(alpha * 0.30f) }, 48);
                renderer.DrawCircleOutline(sx, sy, radius, ToCoreColor(skin.sliderCore, alpha), 0.0024f, 56);
                drawGradientApproachRing(
                    sx, sy, radius * scale, 0.0026f, noteColor,
                    { skin.approachStart.r, skin.approachStart.g, skin.approachStart.b, static_cast<uint8_t>(alpha * 0.84f) },
                    { skin.approachEnd.r, skin.approachEnd.g, skin.approachEnd.b, static_cast<uint8_t>(alpha * 0.88f) },
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

    if (m_ghostRunner.IsActive() && m_gameState.IsPlaying())
    {
        const bool reducedMotion = sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false);
        const auto& trail = m_ghostRunner.GetRecentTrail();
        if (trail.size() >= 2)
        {
            std::vector<sakura::core::NormPoint> ghostPts;
            ghostPts.reserve(trail.size());
            for (const auto& pt : trail)
            {
                const float tx = std::clamp(MOUSE_X + pt.x * MOUSE_W, MOUSE_X, MOUSE_X + MOUSE_W);
                const float ty = std::clamp(MOUSE_Y + pt.y * MOUSE_H, MOUSE_Y, MOUSE_Y + MOUSE_H);
                ghostPts.push_back({ tx, ty });
            }
            const uint8_t headCoreA = reducedMotion ? 68 : 108;
            const uint8_t headAuraA = reducedMotion ? 28 : 48;
            renderer.DrawCatmullRomRibbon(
                ghostPts.data(), static_cast<int>(ghostPts.size()),
                0.0016f, 0.0082f,
                { 186, 162, 248, 0 },
                { 126, 228, 246, headCoreA },
                { 186, 162, 248, 0 },
                { 182, 158, 246, headAuraA },
                4, true);
        }

        const auto& snap = m_ghostRunner.GetSnapshot();
        const float gx = std::clamp(MOUSE_X + snap.cursorX * MOUSE_W, MOUSE_X, MOUSE_X + MOUSE_W);
        const float gy = std::clamp(MOUSE_Y + snap.cursorY * MOUSE_H, MOUSE_Y, MOUSE_Y + MOUSE_H);

        const float sw = std::max(1.0f, static_cast<float>(renderer.GetScreenWidth()));
        const float sh = std::max(1.0f, static_cast<float>(renderer.GetScreenHeight()));
        const float shortEdge = std::min(sw, sh);
        const float sxAspect = shortEdge / sw;
        const float syAspect = shortEdge / sh;

        renderer.DrawRingGradient(
            gx, gy, 0.0082f, 0.0152f,
            { 130, 228, 246, 92 },
            { 186, 162, 248, 0 },
            32);
        renderer.DrawCircleOutline(
            gx, gy, 0.0112f,
            { 148, 234, 248, 134 },
            0.0015f, 32);

        const float dR = 0.0046f;
        renderer.DrawQuad(
            { gx, gy - dR * syAspect },
            { gx + dR * sxAspect, gy },
            { gx, gy + dR * syAspect },
            { gx - dR * sxAspect, gy },
            { 222, 250, 255, 164 },
            { 188, 164, 248, 148 },
            { 132, 228, 246, 164 },
            { 188, 164, 248, 148 });

        if (m_fontSmall != sakura::core::INVALID_HANDLE)
        {
            const float labelY = (gy < MOUSE_Y + 0.028f) ? (gy + 0.0135f) : (gy - 0.0235f);
            renderer.DrawText(
                m_fontSmall, "GHOST",
                gx, labelY, 0.0105f,
                { 178, 232, 248, 146 },
                sakura::core::TextAlign::Center);
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
    const std::string diffBadge = (m_difficultyIndex >= 0 && m_difficultyIndex < static_cast<int>(m_chartInfo.difficulties.size()))
        ? sakura::game::FormatDifficultyBadge(
            m_chartInfo.difficulties[m_difficultyIndex].name,
            m_chartInfo.difficulties[m_difficultyIndex].level)
        : std::string{};
    renderer.DrawText(m_fontSmall, diffBadge, 0.036f, 0.050f, 0.016f, gold);

    const bool courseActive = m_options.isCourseMode && sakura::game::CourseSession::GetInstance().IsActive();
    std::string modeLabel;
    NormRect modePillRect{ 0.435f, 0.014f, 0.19f, 0.029f };
    Color modeFill{ 44, 34, 68, 205 };
    Color modeBorder{ 250, 216, 157, 150 };
    Color modeTextCol = pink;

    if (m_options.returnToEditor)
    {
        modeLabel = "EDITOR · 试玩  (Esc 返回工房)";
    }
    else if (courseActive)
    {
        const auto& cs = sakura::game::CourseSession::GetInstance();
        modeLabel = "COURSE " + std::to_string(cs.GetCurrentStageIndex() + 1) + "/" +
                    std::to_string(cs.GetTotalStages()) + " · " + cs.GetCourse().badgeText;
        modePillRect = { 0.415f, 0.014f, 0.23f, 0.029f };
        modeFill = { 52, 36, 74, 220 };
        modeBorder = { 250, 216, 157, 205 };
        modeTextCol = gold;
    }
    else if (m_options.isArenaMatch)
    {
        modeLabel = "ARENA · ROOM " + m_options.arenaRoomCode;
        modePillRect = { 0.420f, 0.014f, 0.22f, 0.029f };
        modeFill = { 38, 34, 72, 220 };
        modeBorder = { 138, 228, 246, 195 };
        modeTextCol = gold;
    }
    else if (m_options.isDailyChallenge)
    {
        modeLabel = "DAILY · 今日挑战";
        modeFill = { 38, 46, 76, 215 };
        modeBorder = { 250, 216, 157, 190 };
        modeTextCol = gold;
    }
    else
    {
        const char* modes[] = { "LIVE · 正式演奏", "PRACTICE · 自由练习", "AUTO · 自动演示", "REPLAY · 回放" };
        modeLabel = modes[static_cast<int>(m_options.mode)];
    }
    sakura::ui::VisualStyle::DrawBadgePill(
        renderer, modePillRect, modeLabel, m_fontSmall, 0.014f,
        modeFill, modeBorder, modeTextCol);

    const float feverEnergy = std::clamp(m_session.Score().GetFeverEnergy(), 0.0f, 1.0f);
    const bool feverActive = m_session.Score().IsFeverActive();
    renderer.DrawRoundedRect({ 0.435f, 0.050f, 0.19f, 0.008f }, 0.004f, { 28, 34, 58, 210 }, true, 6);
    if (feverEnergy > 0.005f)
    {
        const float fw = std::max(0.008f, 0.19f * feverEnergy);
        renderer.DrawRoundedGradientRect({ 0.435f, 0.050f, fw, 0.008f }, 0.004f,
            feverActive ? Color{ 255, 232, 162, 255 } : pink,
            feverActive ? Color{ 252, 184, 214, 255 } : gold,
            feverActive ? Color{ 255, 232, 162, 255 } : pink,
            feverActive ? Color{ 252, 184, 214, 255 } : gold, 6, true);
    }
    renderer.DrawText(m_fontSmall, feverActive ? "✦ SAKURA BLOOM · 满开" : "BLOOM",
        0.530f, 0.060f, 0.0115f, feverActive ? gold : dim, TextAlign::Center);

    if (courseActive)
    {
        const auto& cs = sakura::game::CourseSession::GetInstance();
        const float lifePct = std::clamp(cs.Gauge().GetLife(), 0.0f, 100.0f);
        const float lifeRatio = lifePct / 100.0f;
        const NormRect gaugeRect{ 0.415f, 0.088f, 0.23f, 0.012f };

        Color barLeft, barRight, barTextCol;
        if (lifePct > 50.0f)
        {
            barLeft    = { 86, 224, 172, 255 };
            barRight   = { 250, 216, 152, 255 };
            barTextCol = { 162, 238, 198, 255 };
        }
        else if (lifePct >= 25.0f)
        {
            barLeft    = { 242, 168, 76, 255 };
            barRight   = { 252, 208, 118, 255 };
            barTextCol = { 252, 198, 112, 255 };
        }
        else
        {
            barLeft    = { 224, 64, 92, 255 };
            barRight   = { 248, 112, 134, 255 };
            barTextCol = { 248, 116, 138, 255 };
        }

        renderer.DrawSoftShadow(gaugeRect, 0.006f, 0.008f, { 4, 6, 16, 175 }, 0.002f, 6);
        renderer.DrawRoundedRect(gaugeRect, 0.006f, { 18, 22, 42, 228 }, true, 6);
        if (lifeRatio > 0.002f)
        {
            const float fillW = std::min(gaugeRect.width, std::max(0.008f, gaugeRect.width * lifeRatio));
            renderer.DrawRoundedGradientRect(
                { gaugeRect.x, gaugeRect.y, fillW, gaugeRect.height }, 0.006f,
                barLeft, barRight, barLeft, barRight, 6, true);
        }
        renderer.DrawRoundedRect(gaugeRect, 0.006f, barTextCol.WithAlpha(185), false, 6, 0.0012f);

        std::ostringstream lifeStr;
        lifeStr << std::fixed << std::setprecision(0) << lifePct << "%";
        renderer.DrawText(m_fontSmall, "LIFE", gaugeRect.x - 0.008f, 0.0865f, 0.0125f, dim, TextAlign::Right);
        renderer.DrawText(m_fontSmall, lifeStr.str(), gaugeRect.x + gaugeRect.width + 0.008f, 0.0865f, 0.0135f, barTextCol, TextAlign::Left);
    }

    if (m_retryTracker.GetCount() > 0)
    {
        const NormRect retryRect{ 0.37f, courseActive ? 0.112f : 0.095f, 0.26f, 0.032f };
        const std::string retryText = "再按 " + std::to_string(m_retryTracker.GetRemaining()) +
            " 次 R 确认重开 (" + std::to_string(m_retryTracker.GetCount()) + "/3)";
        renderer.DrawSoftShadow(retryRect, 0.016f, 0.010f, { 250, 216, 157, 82 }, 0.0f, 8);
        sakura::ui::VisualStyle::DrawBadgePill(
            renderer, retryRect, retryText, m_fontSmall, 0.015f,
            { 54, 36, 70, 236 }, { 255, 228, 164, 240 }, { 255, 242, 198, 255 });
    }

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

    if (m_ghostRunner.IsActive())
    {
        const auto& gs = m_ghostRunner.GetSnapshot();
        const int delta = gs.deltaScore;
        const bool ahead = (delta >= 0);
        const NormRect paceRect = m_options.isArenaMatch
            ? NormRect{ 0.408f, 0.088f, 0.220f, 0.026f }
            : NormRect{ 0.73f, 0.088f, 0.23f, 0.026f };

        const Color edgeTop = ahead ? Color{ 108, 236, 188, 185 } : Color{ 236, 86, 122, 185 };
        const Color edgeBot = ahead ? Color{ 250, 216, 157, 120 } : Color{ 246, 162, 196, 120 };
        const Color textCol = ahead ? Color{ 204, 244, 184, 255 } : Color{ 252, 148, 178, 255 };

        renderer.DrawSoftShadow(paceRect, 0.011f, 0.007f, { 4, 6, 16, 155 }, 0.002f, 8);
        renderer.DrawRoundedGradientRect(
            paceRect, 0.011f,
            { 18, 22, 42, 220 }, { 24, 28, 50, 220 },
            { 12, 16, 32, 232 }, { 16, 20, 38, 232 },
            8, true);
        renderer.DrawRoundedRectOutlineGradient(paceRect, 0.011f, edgeTop, edgeBot, 0.0012f, 8);

        const std::string rivalLabel = !gs.rivalLabel.empty()
            ? gs.rivalLabel
            : (!m_ghostRunner.GetRivalLabel().empty() ? m_ghostRunner.GetRivalLabel() : std::string("PB"));
        std::ostringstream deltaOss;
        if (ahead)
            deltaOss << "+" << std::setw(4) << std::setfill('0') << delta;
        else
            deltaOss << "-" << std::setw(4) << std::setfill('0') << std::abs(static_cast<long long>(delta));

        const std::string paceText = "vs " + rivalLabel + "  " + deltaOss.str();
        float paceFont = 0.0125f;
        const float maxPaceW = paceRect.width - 0.020f;
        const float measuredPace = renderer.MeasureTextWidth(m_fontSmall, paceText, paceFont);
        if (measuredPace > maxPaceW && measuredPace > 0.001f)
            paceFont *= (maxPaceW / measuredPace);
        renderer.DrawText(
            m_fontSmall, paceText,
            paceRect.x + paceRect.width * 0.5f, paceRect.y + 0.0022f,
            paceFont, textCol, TextAlign::Center);

        const float barX = paceRect.x + 0.012f;
        const float barY = paceRect.y + 0.0192f;
        const float barW = paceRect.width - 0.024f;
        const float barH = 0.0040f;
        const float midX = barX + barW * 0.5f;
        renderer.DrawRoundedRect({ barX, barY, barW, barH }, 0.002f, { 28, 34, 58, 215 }, true, 4);

        const float mag = std::clamp(std::abs(static_cast<float>(delta)) / 50000.0f, 0.0f, 1.0f);
        if (mag > 0.004f)
        {
            const float fillW = std::max(0.003f, (barW * 0.5f) * mag);
            if (ahead)
            {
                renderer.DrawRoundedGradientRect(
                    { midX, barY, fillW, barH }, 0.002f,
                    { 96, 236, 186, 248 }, { 250, 220, 152, 255 },
                    { 96, 236, 186, 248 }, { 250, 220, 152, 255 },
                    4, true);
            }
            else
            {
                renderer.DrawRoundedGradientRect(
                    { midX - fillW, barY, fillW, barH }, 0.002f,
                    { 226, 66, 102, 255 }, { 248, 148, 186, 248 },
                    { 226, 66, 102, 255 }, { 248, 148, 186, 248 },
                    4, true);
            }
        }
        renderer.DrawLine(midX, barY - 0.0012f, midX, barY + barH + 0.0012f, { 248, 242, 248, 195 }, 0.0012f);
    }

    if (m_options.isArenaMatch)
    {
        // 紧凑实时对战名次浮层（严格位于右上安全条 X=0.635..0.985, Y=0.086..0.156，
        // 零遮挡 4K 轨道 X=0.05..0.40 与右侧鼠标判定区 X=0.45..0.95, Y=0.16..0.82）
        const auto currentUser = sakura::net::APIClient::GetInstance().GetCurrentUser();
        const auto standings   = BuildLiveArenaStandings();

        int selfIdx = 0;
        for (int i = 0; i < static_cast<int>(standings.size()); ++i)
        {
            const auto& p = standings[static_cast<std::size_t>(i)];
            if ((!currentUser.id.empty() && p.userId == currentUser.id) ||
                (currentUser.id.empty() && !currentUser.username.empty() && p.username == currentUser.username))
            {
                selfIdx = i;
                break;
            }
        }

        std::vector<int> visIndices;
        const int topN = std::min(4, static_cast<int>(standings.size()));
        visIndices.reserve(5);
        for (int i = 0; i < topN; ++i)
            visIndices.push_back(i);
        if (selfIdx >= 4)
            visIndices.push_back(selfIdx);

        const NormRect arenaDock{ 0.635f, 0.086f, 0.350f, 0.070f };
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer, arenaDock,
            { 20, 26, 52, 224 }, { 12, 16, 34, 236 },
            { 250, 216, 157, 178 }, { 138, 228, 246, 88 },
            0.010f, 0.38f);

        // 左侧本机实时名次摘要栏
        const int selfRank = selfIdx + 1;
        const Color selfRankCol = (selfRank == 1) ? gold : Color{ 138, 228, 246, 255 };
        renderer.DrawText(m_fontSmall, "ARENA", 0.662f, 0.091f, 0.0098f, dim, TextAlign::Center);
        renderer.DrawText(m_fontHUD,
            (selfRank == 1) ? "♛#1" : ("#" + std::to_string(selfRank)),
            0.662f, 0.107f, 0.0172f, selfRankCol, TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            std::to_string(standings.size()) + "P",
            0.662f, 0.134f, 0.0102f, pink, TextAlign::Center);
        renderer.DrawLine(0.689f, 0.092f, 0.689f, 0.150f, { 250, 216, 157, 86 }, 0.0010f);

        const int visCount  = static_cast<int>(visIndices.size());
        const bool fiveRows = (visCount >= 5);
        const float baseY   = fiveRows ? 0.0885f : 0.0892f;
        const float rowStep = fiveRows ? 0.0128f : 0.0158f;
        const float rowH    = fiveRows ? 0.0116f : 0.0142f;
        const float rowFont = fiveRows ? 0.0098f : 0.0110f;
        const int leaderScore = standings.empty() ? 0 : standings.front().score;

        for (int slot = 0; slot < visCount; ++slot)
        {
            const int sIdx      = visIndices[static_cast<std::size_t>(slot)];
            const auto& p       = standings[static_cast<std::size_t>(sIdx)];
            const int placement = sIdx + 1;
            const bool isSelf   = (sIdx == selfIdx);
            const bool isFirst  = (placement == 1);
            const std::string key = !p.userId.empty() ? p.userId : ("u:" + p.username);

            float animSlot  = static_cast<float>(slot);
            int dispScore   = p.score;
            float rankFlash = 0.0f;
            auto animIt = std::find_if(m_arenaHudAnim.begin(), m_arenaHudAnim.end(),
                [&](const ArenaHudTrackEntry& e) { return e.key == key; });
            if (animIt != m_arenaHudAnim.end())
            {
                animSlot  = animIt->visualSlot;
                dispScore = static_cast<int>(std::lround(animIt->displayScore));
                rankFlash = animIt->rankFlash;
            }

            const float rowY = std::clamp(
                baseY + animSlot * rowStep,
                arenaDock.y + 0.0020f,
                arenaDock.y + arenaDock.height - rowH - 0.0020f);
            const NormRect standRect{ 0.694f, rowY, 0.286f, rowH };

            Color bgLeft  = isSelf ? Color{ 34, 46, 82, 224 } : Color{ 16, 22, 42, 198 };
            Color bgRight = isSelf ? Color{ 46, 36, 72, 224 } : Color{ 22, 26, 50, 198 };
            Color brdTop  = isSelf ? Color{ 138, 228, 246, 195 } : Color{ 170, 180, 214, 76 };
            Color brdBot  = isSelf ? Color{ 246, 178, 206, 132 } : Color{ 116, 126, 164, 42 };
            if (isFirst)
            {
                bgLeft  = { 86, 62, 32, 232 };
                bgRight = { 48, 34, 24, 236 };
                brdTop  = { 255, 226, 138, 242 };
                brdBot  = { 246, 178, 104, 156 };
            }

            renderer.DrawRoundedGradientRect(
                standRect, 0.005f, bgLeft, bgRight, bgLeft, bgRight, 6, true);
            renderer.DrawRoundedRectOutlineGradient(
                standRect, 0.005f, brdTop, brdBot, rankFlash > 0.05f ? 0.0015f : 0.0010f, 6);

            const float textY = standRect.y + (rowH - rowFont) * 0.42f;
            const std::string rankStr = isFirst ? "♛#1" : ("#" + std::to_string(placement));
            const Color rankCol = isFirst ? gold : (isSelf ? Color{ 168, 236, 250, 255 } : white);
            renderer.DrawText(m_fontSmall, rankStr, standRect.x + 0.005f, textY, rowFont, rankCol);

            // 在线 / 断线重连状态点
            const Color connCol = p.connected ? Color{ 142, 238, 194, 255 } : Color{ 255, 124, 144, 255 };
            renderer.DrawCircleFilled(
                standRect.x + 0.032f, standRect.y + rowH * 0.5f, 0.0024f, connCol, 12);

            const std::string rawName = p.username.empty() ? std::string("Player") : p.username;
            const std::string dispName = (!p.connected ? "[离线]" : "") + rawName + (isSelf ? "★" : "");
            const Color nameCol = !p.connected
                ? Color{ 255, 138, 156, 255 }
                : (isFirst ? gold : (isSelf ? Color{ 168, 236, 250, 255 } : white));
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSmall, dispName,
                standRect.x + 0.037f, textY, rowFont, 0.078f, nameCol);

            // 连击徽章 / 断线标记
            const NormRect comboPill{
                standRect.x + 0.118f, standRect.y + 0.0012f, 0.042f, std::max(0.008f, rowH - 0.0024f)
            };
            const std::string comboLabel = !p.connected
                ? std::string("重连")
                : (std::to_string(std::max(0, p.combo)) + "x");
            const Color comboBorder = !p.connected
                ? Color{ 255, 134, 150, 195 }
                : (p.combo >= 100 ? Color{ 250, 216, 157, 185 } : Color{ 246, 178, 206, 145 });
            const Color comboTextCol = !p.connected
                ? Color{ 255, 150, 166, 255 }
                : (p.combo >= 100 ? gold : pink);
            sakura::ui::VisualStyle::DrawBadgePill(
                renderer, comboPill, comboLabel, m_fontSmall, rowFont * 0.88f,
                { 26, 30, 54, 216 }, comboBorder, comboTextCol);

            // 与榜首/第二名分差 (+/-)
            int deltaVal = 0;
            if (isFirst)
                deltaVal = (standings.size() >= 2) ? (p.score - standings[1].score) : 0;
            else
                deltaVal = p.score - leaderScore;

            const std::string deltaStr = (deltaVal >= 0)
                ? ("+" + std::to_string(deltaVal))
                : std::to_string(deltaVal);
            const Color deltaCol = (deltaVal >= 0)
                ? Color{ 168, 242, 198, 255 }
                : Color{ 252, 152, 178, 255 };
            renderer.DrawText(
                m_fontSmall, deltaStr,
                standRect.x + 0.222f, textY, rowFont * 0.94f, deltaCol, TextAlign::Right);

            // 实时滚动总分
            std::ostringstream scoreStr;
            scoreStr << std::setw(7) << std::setfill('0') << std::max(0, dispScore);
            renderer.DrawText(
                m_fontSmall, scoreStr.str(),
                standRect.x + standRect.width - 0.005f, textY,
                rowFont, isFirst ? gold : white, TextAlign::Right);
        }
    }

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

    // Chart v4 双语时间轴同步歌词
    if (cfg.Get<bool>("gameplay.show_lyrics", true))
    {
        const auto lyric = sakura::game::EvaluateActiveLyric(
            m_gameState.GetChartData(), m_gameState.GetCurrentTime());
        if (lyric.active && lyric.opacity > 0.01f && !lyric.text.empty())
        {
            const float lyrOp = std::clamp(lyric.opacity, 0.0f, 1.0f);
            const bool hasSub = !lyric.subText.empty();
            const float pillH = hasSub ? 0.048f : 0.034f;
            const NormRect pillRect{ 0.44f, 0.876f, 0.48f, pillH };
            const float pillCx = pillRect.x + pillRect.width * 0.5f;
            const float maxTextW = pillRect.width - 0.032f;

            renderer.DrawSoftShadow(
                pillRect, pillH * 0.5f, 0.008f,
                { 4, 6, 16, static_cast<uint8_t>(108.0f * lyrOp) }, 0.002f, 10);
            renderer.DrawRoundedGradientRect(
                pillRect, pillH * 0.5f,
                { 20, 22, 44, static_cast<uint8_t>(158.0f * lyrOp) },
                { 28, 24, 52, static_cast<uint8_t>(148.0f * lyrOp) },
                { 14, 16, 34, static_cast<uint8_t>(172.0f * lyrOp) },
                { 18, 20, 40, static_cast<uint8_t>(164.0f * lyrOp) },
                10, true);
            renderer.DrawRoundedRectOutlineGradient(
                pillRect, pillH * 0.5f,
                { 250, 216, 157, static_cast<uint8_t>(128.0f * lyrOp) },
                { 246, 178, 206, static_cast<uint8_t>(78.0f * lyrOp) },
                0.0011f, 10);

            float mainSize = 0.021f;
            const float measuredMain = renderer.MeasureTextWidth(m_fontHUD, lyric.text, mainSize);
            if (measuredMain > maxTextW && measuredMain > 0.001f)
                mainSize *= (maxTextW / measuredMain);

            const Color pearlWhite{ 252, 248, 252, static_cast<uint8_t>(252.0f * lyrOp) };
            if (hasSub)
            {
                float subSize = 0.0145f;
                const float measuredSub = renderer.MeasureTextWidth(m_fontSmall, lyric.subText, subSize);
                if (measuredSub > maxTextW && measuredSub > 0.001f)
                    subSize *= (maxTextW / measuredSub);

                const Color champagnePink{ 250, 206, 184, static_cast<uint8_t>(232.0f * lyrOp) };
                renderer.DrawText(
                    m_fontHUD, lyric.text, pillCx, pillRect.y + 0.0045f,
                    mainSize, pearlWhite, TextAlign::Center);
                renderer.DrawText(
                    m_fontSmall, lyric.subText, pillCx, pillRect.y + 0.0275f,
                    subSize, champagnePink, TextAlign::Center);
            }
            else
            {
                renderer.DrawText(
                    m_fontHUD, lyric.text, pillCx, pillRect.y + (pillH - mainSize) * 0.44f,
                    mainSize, pearlWhite, TextAlign::Center);
            }
        }
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
    renderer.DrawText(m_fontSmall, "Esc 暂停  ·  连按3次 R 重开", 0.966f, 0.950f, 0.015f, dim, TextAlign::Right);
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
    const bool reduced = sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false);

    if (!m_hitShockwaves.empty())
    {
        const float sw = std::max(1.0f, static_cast<float>(renderer.GetScreenWidth()));
        const float sh = std::max(1.0f, static_cast<float>(renderer.GetScreenHeight()));
        const float shortEdge = std::min(sw, sh);
        const float sxAspect = shortEdge / sw;
        const float syAspect = shortEdge / sh;

        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        for (const auto& w : m_hitShockwaves)
        {
            const float maxT = std::max(0.001f, w.maxTime);
            const float p = std::clamp(1.0f - w.timer / maxT, 0.0f, 1.0f);
            const float easeOut3 = sakura::utils::EaseOutCubic(p);
            const float easeOut2 = 1.0f - (1.0f - p) * (1.0f - p);
            const float fade = std::clamp(1.0f - p, 0.0f, 1.0f);
            const float fadeSq = fade * fade;
            const float synScale = w.isSynergy ? 1.25f : 1.0f;

            if (w.isKeyboard)
            {
                const float rOuter = (0.016f + 0.048f * easeOut3) * synScale;
                const float rInner = (0.009f + 0.030f * easeOut2) * synScale;
                renderer.DrawRingGradient(w.x, JUDGE_LINE_Y, rOuter * 0.72f, rOuter,
                    w.color.WithAlpha(static_cast<uint8_t>(185.0f * fade)),
                    w.color.WithAlpha(0), 40);
                renderer.DrawCircleOutline(w.x, JUDGE_LINE_Y, rOuter,
                    { 255, 248, 228, static_cast<uint8_t>(220.0f * fade) }, 0.0020f, 40);
                renderer.DrawCircleOutline(w.x, JUDGE_LINE_Y, rInner,
                    w.color.WithAlpha(static_cast<uint8_t>(195.0f * fadeSq)), 0.0015f, 36);

                const float baseHalfW = ((w.lane == sakura::game::kThumbLaneIndex)
                    ? (TRACK_W * 0.42f)
                    : (LANE_W * 0.36f)) * (0.55f + 0.45f * fade);
                const float pillarH = 0.24f * (0.72f + 0.28f * easeOut3);
                const float topY = std::max(0.0f, JUDGE_LINE_Y - pillarH);
                const sakura::core::Color baseAura = w.color.WithAlpha(static_cast<uint8_t>(165.0f * fadeSq));
                const sakura::core::Color topZero  = w.color.WithAlpha(0);
                renderer.DrawQuad(
                    { w.x - baseHalfW * 0.65f, topY },
                    { w.x + baseHalfW * 0.65f, topY },
                    { w.x + baseHalfW, JUDGE_LINE_Y },
                    { w.x - baseHalfW, JUDGE_LINE_Y },
                    topZero, topZero, baseAura, baseAura);

                const float coreHalfW = std::max(0.0035f, baseHalfW * 0.28f);
                const float coreTopY = std::max(0.0f, JUDGE_LINE_Y - pillarH * 0.90f);
                const sakura::core::Color coreBase{ 255, 250, 236, static_cast<uint8_t>(215.0f * fadeSq) };
                const sakura::core::Color coreZero{ 255, 250, 236, 0 };
                renderer.DrawQuad(
                    { w.x - coreHalfW * 0.40f, coreTopY },
                    { w.x + coreHalfW * 0.40f, coreTopY },
                    { w.x + coreHalfW, JUDGE_LINE_Y },
                    { w.x - coreHalfW, JUDGE_LINE_Y },
                    coreZero, coreZero, coreBase, coreBase);
            }
            else
            {
                const float rOuter = (0.020f + 0.066f * easeOut3) * synScale;
                const float rInner = (0.011f + 0.040f * easeOut2) * synScale;
                renderer.DrawRingGradient(w.x, w.y, rOuter * 0.76f, rOuter,
                    w.color.WithAlpha(static_cast<uint8_t>(190.0f * fade)),
                    w.color.WithAlpha(0), 48);
                renderer.DrawCircleOutline(w.x, w.y, rOuter,
                    { 255, 248, 230, static_cast<uint8_t>(225.0f * fade) }, 0.0022f, 48);
                renderer.DrawCircleOutline(w.x, w.y, rInner,
                    w.color.WithAlpha(static_cast<uint8_t>(205.0f * fadeSq)), 0.0016f, 44);

                const float starLen = (0.038f + 0.028f * easeOut3) * (0.45f + 0.55f * fade) * synScale;
                const float starWaist = 0.0056f * fade * synScale;
                const sakura::core::Color starCore{ 255, 250, 236, static_cast<uint8_t>(235.0f * fadeSq) };
                const sakura::core::Color starTip = w.color.WithAlpha(0);

                renderer.DrawQuad(
                    { w.x - starLen * sxAspect, w.y },
                    { w.x, w.y - starWaist * syAspect },
                    { w.x + starLen * sxAspect, w.y },
                    { w.x, w.y + starWaist * syAspect },
                    starTip, starCore, starTip, starCore);
                renderer.DrawQuad(
                    { w.x, w.y - starLen * syAspect },
                    { w.x + starWaist * sxAspect, w.y },
                    { w.x, w.y + starLen * syAspect },
                    { w.x - starWaist * sxAspect, w.y },
                    starTip, starCore, starTip, starCore);

                const float diamondR = 0.013f * fade * synScale;
                const sakura::core::Color dMid = w.color.WithAlpha(static_cast<uint8_t>(185.0f * fadeSq));
                renderer.DrawQuad(
                    { w.x, w.y - diamondR * syAspect },
                    { w.x + diamondR * sxAspect, w.y },
                    { w.x, w.y + diamondR * syAspect },
                    { w.x - diamondR * sxAspect, w.y },
                    starCore, dMid, starCore, dMid);
            }
        }
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    }

    if (m_fontSmall == sakura::core::INVALID_HANDLE) return;

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
            ? ((flash.lane == sakura::game::kThumbLaneIndex)
                ? (TRACK_X + TRACK_W * 0.5f)
                : (GetLaneX(flash.lane) + LANE_W * 0.5f))
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
        if (flash.isSynergy)
        {
            renderer.DrawText(m_fontSmall, "✦ SYNERGY",
                posX, posY - 0.020f, 0.014f,
                sakura::core::Color{ 255, 228, 162, alpha }, sakura::core::TextAlign::Center);
        }

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
