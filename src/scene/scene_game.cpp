// scene_game.cpp — 游戏核心场景实现

#include "scene_game.h"
#include "scene_select.h"
#include "scene_pause.h"
#include "scene_result.h"
#include "core/input.h"
#include "core/config.h"
#include "audio/audio_visualizer.h"
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

// 将判定结果映射为点击选 note 时的优先级：值越小表示时间判定越好。
int JudgePriority(sakura::game::JudgeResult result)
{
    switch (result)
    {
    case sakura::game::JudgeResult::Perfect: return 0;
    case sakura::game::JudgeResult::Great:   return 1;
    case sakura::game::JudgeResult::Good:    return 2;
    case sakura::game::JudgeResult::Bad:     return 3;
    case sakura::game::JudgeResult::Miss:    return 4;
    default:                                  return 5;
    }
}

std::vector<int> BuildDragPathLanes(int startLane, int endLane)
{
    std::vector<int> lanes;
    lanes.push_back(startLane);

    if (startLane == endLane)
        return lanes;

    int step = (endLane > startLane) ? 1 : -1;
    for (int lane = startLane + step;; lane += step)
    {
        lanes.push_back(lane);
        if (lane == endLane)
            break;
    }

    return lanes;
}

int CalcDragStepTime(const sakura::game::KeyboardNote& note,
                     const sakura::game::DragState& state,
                     int pathIndex)
{
    int totalPathPoints = static_cast<int>(state.pathLanes.size());
    if (totalPathPoints <= 1) return note.time + note.duration;

    float ratio = static_cast<float>(pathIndex)
        / static_cast<float>(totalPathPoints - 1);
    return note.time + static_cast<int>(std::lround(
        static_cast<float>(note.duration) * ratio));
}

}

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneGame::SceneGame(SceneManager& mgr,
                     const sakura::game::ChartInfo& chartInfo,
                     int difficultyIndex)
    : m_manager(mgr)
    , m_chartInfo(chartInfo)
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

    // 背景系统初始化
    {
        float bgDim = sakura::core::Config::GetInstance().Get("background_dimming", 0.5f);
        m_useDefaultBg = true;
        if (!m_chartInfo.backgroundFile.empty())
        {
            std::string bgPath = m_chartInfo.folderPath + "/" + m_chartInfo.backgroundFile;
            if (m_bgRenderer.LoadImage(bgPath))
            {
                m_bgRenderer.SetDimming(bgDim);
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

    // 初始化判定系统
    m_judge.Initialize();

    // 启动 GameState（加载谱面 + 开始倒计时）
    if (!m_gameState.Start(m_chartInfo, m_difficultyIndex))
    {
        LOG_ERROR("[SceneGame] GameState::Start 失败，返回选歌界面");
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            TransitionType::Fade, 0.4f);
        return;
    }

    // 初始化计分器
    int totalNotes = m_gameState.GetTotalNoteCount();
    m_score.Initialize(totalNotes);

    // 清空状态
    m_holdStates.clear();
    m_dragStates.clear();
    m_sliderStates.clear();
    m_judgeFlashes.clear();

    // ── 特效初始化 ────────────────────────────────────────────────────────────
    m_particles.Clear();
    m_judgePulsePhase = 0.0f;
    m_chromaTimer     = 0.0f;
    m_lastCheckedCombo = 0;
    m_lanePressed.fill(false);
}

// ── OnExit ─────────────────────────────────────────────────────────────────────

void SceneGame::OnExit()
{
    LOG_INFO("[SceneGame] 退出游戏场景");
    sakura::audio::AudioManager::GetInstance().StopMusic();
    m_holdStates.clear();
    m_dragStates.clear();
    m_sliderStates.clear();
    m_judgeFlashes.clear();
    m_particles.Clear();
    m_bgRenderer.UnloadImage();
}

// ── CalcNoteRenderY ───────────────────────────────────────────────────────────

float SceneGame::CalcNoteRenderY(int noteTimeMs, int currentTimeMs,
                                  float svSpeed) const
{
    auto& cfg      = sakura::core::Config::GetInstance();
    float noteSpeed = cfg.Get<float>("gameplay.note_speed", 1.0f);

    float dtMs      = static_cast<float>(noteTimeMs - currentTimeMs);
    float fallRate  = noteSpeed * svSpeed * JUDGE_LINE_Y / BASE_APPROACH_RANGE;
    return JUDGE_LINE_Y - dtMs * fallRate;
}

// ── CalcApproachScale ─────────────────────────────────────────────────────────

float SceneGame::CalcApproachScale(int noteTimeMs, int currentTimeMs) const
{
    float dtMs  = static_cast<float>(noteTimeMs - currentTimeMs);
    float t     = std::max(0.0f, std::min(1.0f, dtMs / BASE_APPROACH_RANGE));
    return 1.0f + 1.5f * t;   // 2.5 → 1.0
}

// ── HandleKeyPress ────────────────────────────────────────────────────────────

void SceneGame::HandleKeyPress(SDL_Scancode key)
{
    if (!m_gameState.IsPlaying()) return;

    // 找出按键对应轨道
    int lane = -1;
    for (int i = 0; i < LANE_COUNT; ++i)
    {
        if (m_laneKeys[i] == key) { lane = i; break; }
    }
    if (lane < 0) return;

    int now = m_gameState.GetCurrentTime();
    auto& kbNotes = m_gameState.GetKeyboardNotes();

    int bestDragStateIdx = -1;
    int bestDragDist = INT_MAX;
    for (int i = 0; i < static_cast<int>(m_dragStates.size()); ++i)
    {
        auto& ds = m_dragStates[i];
        if (ds.noteIndex < 0 || ds.noteIndex >= static_cast<int>(kbNotes.size())) continue;

        auto& dragNote = kbNotes[ds.noteIndex];
        if (dragNote.type != sakura::game::NoteType::Drag) continue;
        if (ds.nextLaneIndex < 0 || ds.nextLaneIndex >= static_cast<int>(ds.pathLanes.size()))
            continue;
        if (ds.pathLanes[ds.nextLaneIndex] != lane) continue;

        bool previousLanesHeld = true;
        for (int pathIndex = 0; pathIndex < ds.nextLaneIndex; ++pathIndex)
        {
            int requiredLane = ds.pathLanes[pathIndex];
            bool isHeld = sakura::core::Input::IsKeyHeld(m_laneKeys[requiredLane]);
            bool withinGapTolerance = ds.lastHeldTimeMs[requiredLane] >= 0 &&
                now - ds.lastHeldTimeMs[requiredLane] <= sakura::game::DragState::INPUT_GAP_TOLERANCE_MS;
            if (!isHeld && !withinGapTolerance)
            {
                previousLanesHeld = false;
                break;
            }
        }
        if (!previousLanesHeld) continue;

        int dist = std::abs(CalcDragStepTime(dragNote, ds, ds.nextLaneIndex) - now);
        if (dist < bestDragDist)
        {
            bestDragDist = dist;
            bestDragStateIdx = i;
        }
    }

    if (bestDragStateIdx >= 0)
    {
        auto& ds = m_dragStates[bestDragStateIdx];
        auto& dragNote = kbNotes[ds.noteIndex];
        bool isFinalStep = false;
        auto result = m_judge.JudgeDragStep(ds, dragNote, now, lane, isFinalStep);
        if (result != sakura::game::JudgeResult::None)
        {
            ds.lastHeldTimeMs[lane] = now;
            ds.releaseTimeMs[lane] = -1;

            if (isFinalStep || result == sakura::game::JudgeResult::Miss)
            {
                m_score.OnJudge(result, sakura::game::Judge::GetHitError(
                    CalcDragStepTime(dragNote, ds,
                        std::min(ds.nextLaneIndex, static_cast<int>(ds.pathLanes.size()) - 1)), now));
            }
            AddJudgeFlash(result, true, lane);
            if (ds.finalized)
                m_dragStates.erase(m_dragStates.begin() + bestDragStateIdx);
            return;
        }
    }

    // 在活跃键盘音符中找同轨道最近未判定的音符
    auto activeKb = m_gameState.GetActiveKeyboardNotes();
    int  bestIdx  = -1;
    int  bestDist = INT_MAX;

    for (auto& note : activeKb)
    {
        if (note.isJudged) continue;
        if (note.lane != lane) continue;
        int dist = std::abs(note.time - now);
        if (dist < bestDist)
        {
            bestDist = dist;
            // 找到索引（需要与 kbNotes 对齐）
            for (int i = 0; i < static_cast<int>(kbNotes.size()); ++i)
            {
                if (&kbNotes[i] == &note) { bestIdx = i; break; }
            }
        }
    }

    if (bestIdx < 0) return;

    auto& note = kbNotes[bestIdx];

    // Hold 起始
    if (note.type == sakura::game::NoteType::Hold)
    {
        auto result = m_judge.JudgeKeyboardNote(note, now);
        if (result != sakura::game::JudgeResult::Miss &&
            result != sakura::game::JudgeResult::None)
        {
            // 立即标记 isJudged=true（防 CheckMisses 误判），result 留 None 等 Hold 结束
            note.isJudged = true;
            note.result   = sakura::game::JudgeResult::None;

            // 开始 Hold 状态追踪
            sakura::game::HoldState hs;
            hs.noteIndex  = bestIdx;
            hs.isHeld     = true;
            hs.headJudged = true;
            hs.headResult = result;
            hs.lastHeldTimeMs = now;
            m_holdStates.push_back(hs);
        }
        // 头部判定结果反馈（闪光 + 计入头 Hit / Miss）
        m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
        AddJudgeFlash(result, true, lane);
    }
    // Drag 起始
    else if (note.type == sakura::game::NoteType::Drag)
    {
        auto result = m_judge.JudgeKeyboardNote(note, now);
        if (result != sakura::game::JudgeResult::Miss &&
            result != sakura::game::JudgeResult::None)
        {
            note.isJudged = true;
            note.result   = sakura::game::JudgeResult::None;

            sakura::game::DragState ds;
            ds.noteIndex            = bestIdx;
            ds.headJudged           = true;
            ds.headResult           = result;
            int targetLane = (note.dragToLane >= 0 && note.dragToLane < LANE_COUNT)
                ? note.dragToLane
                : note.lane;
            ds.pathLanes            = BuildDragPathLanes(note.lane, targetLane);
            ds.lastHeldTimeMs[note.lane] = now;
            m_dragStates.push_back(ds);
        }

        m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
        AddJudgeFlash(result, true, lane);
    }
    // Tap（普通）
    else
    {
        auto result = m_judge.JudgeKeyboardNote(note, now);
        m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
        AddJudgeFlash(result, true, lane);
    }
}

// ── HandleMouseClick ──────────────────────────────────────────────────────────

void SceneGame::HandleMouseClick(float normX, float normY)
{
    if (!m_gameState.IsPlaying()) return;

    // 将屏幕归一化坐标转换为鼠标区域内归一化坐标
    float mouseX = (normX - MOUSE_X) / MOUSE_W;
    float mouseY = (normY - MOUSE_Y) / MOUSE_H;

    // 超出鼠标区域
    if (mouseX < 0.0f || mouseX > 1.0f ||
        mouseY < 0.0f || mouseY > 1.0f) return;

    int now = m_gameState.GetCurrentTime();
    auto& msNotes = m_gameState.GetMouseNotes();
    auto  activeMs= m_gameState.GetActiveMouseNotes();

    // 优先选取空间距离最近且在时间窗口内的未判定音符
    // （鼠标区音符分布在二维空间，空间优先比时间优先更准确）
    int   bestIdx      = -1;
    int   bestPriority = INT_MAX;
    float bestDist     = FLT_MAX;
    int   bestTDist    = INT_MAX;
    for (auto& n : activeMs)
    {
        if (n.isJudged) continue;
        // 排除时间上完全超前（还未进入判定窗口）或已经彻底过期的音符，
        // 避免点击被尚未可打/已经 Miss 的鼠标音符抢走。
        int timeDiff = now - n.time;
        if (timeDiff < -m_judge.GetWindows().miss ||
            timeDiff > m_judge.GetWindows().miss) continue;

        float dx = mouseX - n.x;
        float dy = mouseY - n.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        float tolerance = sakura::game::Judge::GetMouseHitTolerance(n);
        if (dist > tolerance) continue;

        int   absT = std::abs(timeDiff);
        int   priority = JudgePriority(m_judge.GetResultByTimeDiff(absT));

        // 仅在点击真正落入音符头部范围后再比较优先级：
        // 先取时间判定更好的目标，再用空间距离/时间差打破平局。
        if (priority < bestPriority ||
            (priority == bestPriority &&
             (dist < bestDist - 1e-4f ||
              (dist < bestDist + 1e-4f && absT < bestTDist))))
        {
            bestPriority = priority;
            bestDist     = dist;
            bestTDist    = absT;
            bestIdx      = static_cast<int>(&n - msNotes.data());
        }
    }
    if (bestIdx < 0) return;

    auto& note   = msNotes[bestIdx];
    auto  result = m_judge.JudgeMouseNote(note, now, mouseX, mouseY);

    if (note.type == sakura::game::NoteType::Slider &&
        result != sakura::game::JudgeResult::Miss &&
        result != sakura::game::JudgeResult::None)
    {
        // 标记音符已判定（防止 CheckMouseMisses 在 Slider 进行中误判为 Miss）
        // result 暂置 None，Slider 所有拐点判定完毕后在 OnUpdate 中填入终判结果
        note.isJudged = true;
        note.result   = sakura::game::JudgeResult::None;

        // 开始 Slider 追踪
        sakura::game::SliderState ss;
        ss.noteIndex   = bestIdx;
        ss.headJudged  = true;
        ss.headResult  = result;
        m_sliderStates.push_back(ss);
    }

    // 仅在有效判定（非 None）时计分并显示判定闪现
    // None 表示点击未命中音符（距离过远或时间太早），不应产生任何反馈
    if (result != sakura::game::JudgeResult::None)
    {
        m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
        // 将鼠标区局部坐标转换为屏幕坐标再存入闪现记录
        float flashSX = MOUSE_X + note.x * MOUSE_W;
        float flashSY = MOUSE_Y + note.y * MOUSE_H;
        AddJudgeFlash(result, false, 0, flashSX, flashSY);
    }
}

// ── AddJudgeFlash ─────────────────────────────────────────────────────────────

void SceneGame::AddJudgeFlash(sakura::game::JudgeResult r, bool isKb,
                               int lane, float px, float py)
{
    JudgeFlash flash;
    flash.result   = r;
    flash.timer    = JudgeFlash::FLASH_DURATION;
    flash.isKeyboard = isKb;
    flash.lane     = lane;
    flash.posX     = px;
    flash.posY     = py;
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
    // 更新 GameState（倒计时、时间推进）
    m_gameState.Update(dt);

    // ── 游戏结束 → 切换到结算场景（在 IsPlaying 守卫之前检查）─────────────────
    if (m_gameState.IsFinished())
    {
        LOG_INFO("[SceneGame] 游戏完成，切换到结算");

        auto& kbNotes = m_gameState.GetKeyboardNotes();
        for (auto& ds : m_dragStates)
        {
            if (ds.noteIndex >= 0 && ds.noteIndex < static_cast<int>(kbNotes.size()))
            {
                auto& dragNote = kbNotes[ds.noteIndex];
                if (dragNote.result == sakura::game::JudgeResult::None)
                {
                    dragNote.result = sakura::game::JudgeResult::Miss;
                    m_score.OnJudge(sakura::game::JudgeResult::Miss, 0);
                }
            }
        }
        m_dragStates.clear();

        // 将活跃 Slider 中未完成的拐点计为 Miss（音乐结束时 Slider 可能仍在进行中）
        auto& msNotes = m_gameState.GetMouseNotes();
        for (auto& ss : m_sliderStates)
        {
            if (ss.noteIndex >= 0 && ss.noteIndex < static_cast<int>(msNotes.size()))
            {
                const auto& sliderNote = msNotes[ss.noteIndex];
                int remaining = static_cast<int>(sliderNote.sliderPath.size())
                                - ss.nextWaypointIndex;
                for (int i = 0; i < remaining; ++i)
                    m_score.OnJudge(sakura::game::JudgeResult::Miss, 0);
            }
        }
        m_sliderStates.clear();

        // 将 CheckFinished 中强制判定的 Miss 计入分数
        int forcedMisses = m_gameState.TakeForcedMisses();
        for (int i = 0; i < forcedMisses; ++i)
            m_score.OnJudge(sakura::game::JudgeResult::Miss, 0);

        auto result = m_score.GetResult(
            m_chartInfo.id,
            m_chartInfo.title,
            m_difficultyIndex < static_cast<int>(m_chartInfo.difficulties.size())
                ? m_chartInfo.difficulties[m_difficultyIndex].name : "Unknown",
            m_difficultyIndex < static_cast<int>(m_chartInfo.difficulties.size())
                ? m_chartInfo.difficulties[m_difficultyIndex].level : 0.0f
        );
        result.playTimeSeconds = std::max(0.0, static_cast<double>(m_gameState.GetCurrentTime()) / 1000.0);
        m_manager.SwitchScene(
            std::make_unique<SceneResult>(m_manager, result, m_chartInfo),
            TransitionType::Fade, 0.5f);
        return;
    }

    if (!m_gameState.IsPlaying()) return;

    int now = m_gameState.GetCurrentTime();

    // ── 自动 Miss 检测 ─────────────────────────────────────────────────────────
    {
        int misses = m_judge.CheckMisses(
            m_gameState.GetKeyboardNotes(), now);
        int mouseMisses = m_judge.CheckMouseMisses(
            m_gameState.GetMouseNotes(), now);

        for (int i = 0; i < misses + mouseMisses; ++i)
            m_score.OnJudge(sakura::game::JudgeResult::Miss, 0);
    }

    // ── Hold 持续判定 ────────────────────────────────────────────────────────
    auto& kbNotes = m_gameState.GetKeyboardNotes();
    for (auto it = m_holdStates.begin(); it != m_holdStates.end(); )
    {
        auto& hs   = *it;
        if (hs.noteIndex < 0 || hs.noteIndex >= static_cast<int>(kbNotes.size()))
        {
            it = m_holdStates.erase(it);
            continue;
        }
        auto& note = kbNotes[hs.noteIndex];

        // 按键短断触滤波：短时间掉键不立即视为松开
        bool keyHeld = false;
        if (note.lane >= 0 && note.lane < LANE_COUNT)
            keyHeld = sakura::core::Input::IsKeyHeld(m_laneKeys[note.lane]);

        if (keyHeld)
        {
            hs.isHeld = true;
            hs.lastHeldTimeMs = now;
            hs.releaseTimeMs = -1;
        }
        else
        {
            if (hs.releaseTimeMs < 0)
            {
                hs.releaseTimeMs = now;
            }

            bool withinGapTolerance = (hs.lastHeldTimeMs >= 0) &&
                (now - hs.lastHeldTimeMs <= sakura::game::HoldState::INPUT_GAP_TOLERANCE_MS);
            hs.isHeld = withinGapTolerance;
            if (withinGapTolerance)
                hs.releaseTimeMs = -1;
        }

        auto tickResult = m_judge.UpdateHoldTick(hs, note, now);
        if (tickResult != sakura::game::JudgeResult::None)
        {
            // 写入终判结果（覆盖占位的 None）
            note.result = tickResult;
            m_score.OnJudge(tickResult, 0);
            AddJudgeFlash(tickResult, true, note.lane);
        }

        // finalized 置位后移除（由 UpdateHoldTick 内部设置）
        if (hs.finalized)
        {
            it = m_holdStates.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // ── Drag 持续判定 ────────────────────────────────────────────────────────
    for (auto it = m_dragStates.begin(); it != m_dragStates.end(); )
    {
        auto& ds = *it;
        if (ds.noteIndex < 0 || ds.noteIndex >= static_cast<int>(kbNotes.size()))
        {
            it = m_dragStates.erase(it);
            continue;
        }

        auto& note = kbNotes[ds.noteIndex];

        int activePathCount = std::min(ds.nextLaneIndex, static_cast<int>(ds.pathLanes.size()));
        for (int pathIndex = 0; pathIndex < activePathCount; ++pathIndex)
        {
            int lane = ds.pathLanes[pathIndex];
            bool keyHeld = sakura::core::Input::IsKeyHeld(m_laneKeys[lane]);

            if (keyHeld)
            {
                ds.lastHeldTimeMs[lane] = now;
                ds.releaseTimeMs[lane] = -1;
            }
            else if (ds.releaseTimeMs[lane] < 0)
            {
                ds.releaseTimeMs[lane] = now;
            }
        }

        auto dragResult = m_judge.UpdateDragTick(ds, note, now);
        if (dragResult != sakura::game::JudgeResult::None)
        {
            note.result = dragResult;
            m_score.OnJudge(dragResult, 0);
            int flashLane = (note.dragToLane >= 0 && note.dragToLane < LANE_COUNT)
                ? note.dragToLane
                : note.lane;
            AddJudgeFlash(dragResult, true, flashLane);
        }

        if (ds.finalized)
            it = m_dragStates.erase(it);
        else
            ++it;
    }

    // ── Slider 路径追踪 ───────────────────────────────────────────────────────
    auto& msNotes = m_gameState.GetMouseNotes();
    auto [mx, my] = sakura::core::Input::GetMousePosition();
    float mouseX = (mx - MOUSE_X) / MOUSE_W;
    float mouseY = (my - MOUSE_Y) / MOUSE_H;
    bool  mouseDown = sakura::core::Input::IsMouseButtonHeld(SDL_BUTTON_LEFT);

    for (auto it = m_sliderStates.begin(); it != m_sliderStates.end(); )
    {
        auto& ss = *it;
        if (ss.noteIndex < 0 || ss.noteIndex >= static_cast<int>(msNotes.size()))
        {
            it = m_sliderStates.erase(it);
            continue;
        }
        auto& note = msNotes[ss.noteIndex];

        if (mouseDown)
        {
            ss.lastDownTimeMs = now;
        }
        bool filteredMouseDown = mouseDown ||
            (ss.lastDownTimeMs >= 0 &&
             now - ss.lastDownTimeMs <= sakura::game::SliderState::INPUT_GAP_TOLERANCE_MS);

        auto sResult = m_judge.UpdateSliderTracking(
            ss, note, now, mouseX, mouseY, filteredMouseDown);

        // 拐点判定结果：计分并发出判定闪现
        if (sResult != sakura::game::JudgeResult::None)
        {
            m_score.OnJudge(sResult, 0);
            // 在刚判定的拐点位置显示判定结果
            // UpdateSliderTracking 在判定后执行了 ++nextWaypointIndex，因此减 1 还原到刚判定的索引
            // 边界检查确保安全（正常情况下 judgedIdx 恒 ≥ 0）
            int judgedIdx = ss.nextWaypointIndex - 1;
            if (judgedIdx >= 0 && judgedIdx < static_cast<int>(note.sliderPath.size()))
            {
                float wpSX = MOUSE_X + note.sliderPath[judgedIdx].first  * MOUSE_W;
                float wpSY = MOUSE_Y + note.sliderPath[judgedIdx].second * MOUSE_H;
                AddJudgeFlash(sResult, false, 0, wpSX, wpSY);
            }
        }

        // 所有拐点判定完毕 → 填入终判结果，移除状态
        if (ss.finalized)
        {
            note.result = ss.isMissed
                ? sakura::game::JudgeResult::Miss
                : ss.headResult;
            it = m_sliderStates.erase(it);
        }
        else
        {
            ++it;
        }
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

    // 连击里程碑检测（50/100/200/500/1000）
    int currCombo = m_score.GetCombo();
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
        m_lanePressed[i] = sakura::core::Input::IsKeyHeld(m_laneKeys[i]);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneGame::OnEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        // ESC → 暂停
        if (event.key.scancode == SDL_SCANCODE_ESCAPE &&
            m_gameState.IsPlaying())
        {
            m_gameState.Pause();
            m_manager.PushScene(
                std::make_unique<ScenePause>(m_manager, m_gameState),
                TransitionType::Fade, 0.3f);
            return;
        }
        // 游戏按键判定
        HandleKeyPress(event.key.scancode);
        break;

    case SDL_EVENT_KEY_UP:
    {
        int now = m_gameState.GetCurrentTime();

        // 检测 Hold 松开 → 记录松开时刻
        for (auto& hs : m_holdStates)
        {
            if (hs.noteIndex < 0) continue;
            auto& kbNotes = m_gameState.GetKeyboardNotes();
            if (hs.noteIndex < static_cast<int>(kbNotes.size()))
            {
                int lane = kbNotes[hs.noteIndex].lane;
                if (event.key.scancode == m_laneKeys[lane] && hs.isHeld)
                {
                    hs.isHeld       = false;
                    hs.releaseTimeMs = m_gameState.GetCurrentTime();
                }
            }
        }

        for (auto& ds : m_dragStates)
        {
            if (ds.noteIndex < 0) continue;
            auto& kbNotes = m_gameState.GetKeyboardNotes();
            if (ds.noteIndex < static_cast<int>(kbNotes.size()))
            {
                int releasedLane = -1;
                for (int lane = 0; lane < LANE_COUNT; ++lane)
                {
                    if (event.key.scancode == m_laneKeys[lane])
                    {
                        releasedLane = lane;
                        break;
                    }
                }

                if (releasedLane < 0) continue;

                int activePathCount = std::min(ds.nextLaneIndex, static_cast<int>(ds.pathLanes.size()));
                for (int pathIndex = 0; pathIndex < activePathCount; ++pathIndex)
                {
                    int requiredLane = ds.pathLanes[pathIndex];
                    if (requiredLane == releasedLane)
                    {
                        ds.releaseTimeMs[releasedLane] = now;
                        break;
                    }
                }
            }
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            auto [mx, my] = sakura::core::Input::GetMousePosition();
            HandleMouseClick(mx, my);
        }
        break;

    default:
        break;
    }
}

// ── 渲染辅助 ──────────────────────────────────────────────────────────────────

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
    case sakura::game::JudgeResult::Perfect: return { 200, 160, 255, 255 };   // 紫
    case sakura::game::JudgeResult::Great:   return { 100, 180, 255, 255 };   // 蓝
    case sakura::game::JudgeResult::Good:    return { 100, 220, 120, 255 };   // 绿
    case sakura::game::JudgeResult::Bad:     return { 255, 220,  80, 255 };   // 黄
    case sakura::game::JudgeResult::Miss:    return { 255,  80,  80, 255 };   // 红
    default:                                  return sakura::core::Color::White;
    }
}

// ── RenderBackground ─────────────────────────────────────────────────────────

void SceneGame::RenderBackground(sakura::core::Renderer& renderer)
{
    if (m_useDefaultBg)
        m_defaultBg.Render(renderer);
    else
        m_bgRenderer.Render(renderer);
}

// ── RenderTrack ───────────────────────────────────────────────────────────────

void SceneGame::RenderTrack(sakura::core::Renderer& renderer)
{
    // 轨道背景
    renderer.DrawFilledRect(
        { TRACK_X, 0.0f, TRACK_W, 1.0f },
        sakura::core::Color{ 20, 15, 40, 180 });

    // 轨道分隔 + 交替明暗
    for (int i = 0; i < LANE_COUNT; ++i)
    {
        float x = GetLaneX(i);
        uint8_t brightness = (i % 2 == 0) ? 12 : 20;
        renderer.DrawFilledRect(
            { x, 0.0f, LANE_W, 1.0f },
            sakura::core::Color{ brightness, brightness, static_cast<uint8_t>(brightness + 20), 100 });

        // 轨道线
        if (i > 0)
        {
            renderer.DrawLine(x, 0.0f, x, 1.0f,
                sakura::core::Color{ 80, 70, 110, 100 }, 0.001f);
        }
    }

    // 判定线（发光白色）
    renderer.DrawLine(TRACK_X, JUDGE_LINE_Y,
                      TRACK_X + TRACK_W, JUDGE_LINE_Y,
                      sakura::core::Color{ 255, 255, 255, 220 }, 0.003f);
    renderer.DrawLine(TRACK_X, JUDGE_LINE_Y,
                      TRACK_X + TRACK_W, JUDGE_LINE_Y,
                      sakura::core::Color{ 200, 200, 255, 80 }, 0.008f);

    // 判定线辉光（脉冲）
    sakura::effects::GlowEffect::DrawGlowBar(
        renderer,
        TRACK_X, JUDGE_LINE_Y - 0.003f,
        TRACK_W, 0.006f,
        sakura::core::Color{ 255, 255, 220, 180 },
        0.008f, 4);

    // 按键按下时的轨道高亮
    for (int i = 0; i < LANE_COUNT; ++i)
    {
        if (m_lanePressed[i])
        {
            float lx = GetLaneX(i);
            renderer.DrawFilledRect(
                { lx, JUDGE_LINE_Y - 0.06f, LANE_W, 0.06f },
                sakura::core::Color{ 180, 180, 255, 30 });
            sakura::effects::GlowEffect::DrawGlow(
                renderer,
                lx + LANE_W * 0.5f, JUDGE_LINE_Y,
                0.018f,
                sakura::core::Color{ 200, 200, 255, 160 },
                0.025f, 3);
        }
    }

    sakura::audio::AudioVisualizer::GetInstance().RenderBars(
        renderer,
        { TRACK_X, JUDGE_LINE_Y + 0.018f, TRACK_W, 0.055f },
        { 90, 210, 255, 90 },
        { 255, 210, 150, 180 },
        0.60f);

    // 鼠标区域边框
    renderer.DrawRectOutline(
        { MOUSE_X, MOUSE_Y, MOUSE_W, MOUSE_H },
        sakura::core::Color{ 100, 80, 150, 100 }, 0.001f);
}

// ── RenderKeyboardNotes ───────────────────────────────────────────────────────

void SceneGame::RenderKeyboardNotes(sakura::core::Renderer& renderer)
{
    if (!m_gameState.IsPlaying() && !m_gameState.IsInCountdown()) return;

    int now = m_gameState.GetCurrentTime();
    auto activeNotes = m_gameState.GetActiveKeyboardNotes();

    for (const auto& note : activeNotes)
    {
        if (note.isJudged && note.alpha <= 0.01f) continue;

        float sv   = m_gameState.GetCurrentSVSpeed(now);
        float ry   = CalcNoteRenderY(note.time, now, sv);
        float lx   = GetLaneX(note.lane);
        float alpha = note.alpha;

        // Hold/Drag：预先计算尾部 Y，用于精确可见性判断
        float tailY = -999.0f;
        if (note.type == sakura::game::NoteType::Hold ||
            note.type == sakura::game::NoteType::Drag)
            tailY = CalcNoteRenderY(note.time + note.duration, now, sv);

        // ── 可见性剔除 ─────────────────────────────────────────────────────────
        // Hold/Drag 头部可能已过判定线（ry > 1.05），但尾部仍在屏幕内，不可跳过
        if (note.type == sakura::game::NoteType::Hold ||
            note.type == sakura::game::NoteType::Drag)
        {
            if (tailY > 1.05f) continue;
            if (ry < -0.05f && tailY < -0.05f) continue;
        }
        else
        {
            if (ry < -0.05f || ry > 1.05f) continue;
        }

        switch (note.type)
        {
        case sakura::game::NoteType::Tap:
        {
            // 白色填充矩形
            sakura::core::Color noteColor = {
                220, 200, 255, static_cast<uint8_t>(200 * alpha) };
            renderer.DrawRoundedRect(
                { lx + 0.003f, ry - NOTE_H * 0.5f,
                  LANE_W - 0.006f, NOTE_H },
                0.004f, noteColor, true);
            // 下边缘高亮
            sakura::core::Color lineColor = {
                255, 230, 255, static_cast<uint8_t>(240 * alpha) };
            renderer.DrawLine(lx + 0.003f, ry,
                              lx + LANE_W - 0.003f, ry,
                              lineColor, 0.003f);
            break;
        }
        case sakura::game::NoteType::Hold:
        {
            // 持按中：头部钳制到判定线，保持视觉稳定
            bool isHolding = note.isJudged &&
                             (note.result == sakura::game::JudgeResult::None);
            float headY = (isHolding && ry > JUDGE_LINE_Y) ? JUDGE_LINE_Y : ry;

            float topY = std::min(headY - NOTE_H * 0.5f, tailY);
            float botY = std::max(headY + NOTE_H * 0.5f, tailY + NOTE_H * 0.5f);

            // 长条（尾→头）
            sakura::core::Color holdColor = {
                140, 100, 200, static_cast<uint8_t>(160 * alpha) };
            renderer.DrawFilledRect(
                { lx + 0.005f, topY, LANE_W - 0.010f, botY - topY },
                holdColor);
            // 头部矩形
            sakura::core::Color headColor = {
                210, 170, 255, static_cast<uint8_t>(220 * alpha) };
            renderer.DrawRoundedRect(
                { lx + 0.003f, headY - NOTE_H * 0.5f,
                  LANE_W - 0.006f, NOTE_H },
                0.004f, headColor, true);
            break;
        }
        case sakura::game::NoteType::Drag:
        {
            float sourceX = lx + LANE_W * 0.5f;
            int targetLane = (note.dragToLane >= 0 && note.dragToLane < LANE_COUNT)
                ? note.dragToLane
                : note.lane;
            float targetX = GetLaneX(targetLane) + LANE_W * 0.5f;
            bool isDragging = note.isJudged &&
                              (note.result == sakura::game::JudgeResult::None);
            float headY = (isDragging && ry > JUDGE_LINE_Y) ? JUDGE_LINE_Y : ry;

            int noteIndex = static_cast<int>(&note - m_gameState.GetKeyboardNotes().data());
            const sakura::game::DragState* dragState = nullptr;
            for (const auto& ds : m_dragStates)
            {
                if (ds.noteIndex == noteIndex)
                {
                    dragState = &ds;
                    break;
                }
            }

            auto pathLanes = BuildDragPathLanes(note.lane, targetLane);

            sakura::core::Color dragColor = {
                150, 230, 255, static_cast<uint8_t>(200 * alpha) };
            sakura::core::Color trailColor = {
                100, 200, 240, static_cast<uint8_t>(170 * alpha) };

            renderer.DrawLine(sourceX, headY, targetX, tailY,
                trailColor, 0.006f);

            float guideY = std::clamp(headY - 0.048f, 0.05f, JUDGE_LINE_Y - 0.02f);
            for (int pathIndex = 0; pathIndex < static_cast<int>(pathLanes.size()); ++pathIndex)
            {
                int pathLane = pathLanes[pathIndex];
                float pathX = GetLaneX(pathLane) + LANE_W * 0.5f;
                if (pathIndex > 0)
                {
                    int prevLane = pathLanes[pathIndex - 1];
                    float prevX = GetLaneX(prevLane) + LANE_W * 0.5f;
                    renderer.DrawLine(prevX, guideY, pathX, guideY,
                        { 120, 205, 240, static_cast<uint8_t>(120 * alpha) }, 0.0025f);
                }

                bool reached = dragState && pathIndex < dragState->nextLaneIndex;
                bool nextStep = dragState && !dragState->finalized
                    && pathIndex == dragState->nextLaneIndex;
                float radius = nextStep ? 0.012f : 0.009f;
                sakura::core::Color markerColor = reached
                    ? sakura::core::Color{ 255, 220, 120, static_cast<uint8_t>(230 * alpha) }
                    : (nextStep
                        ? sakura::core::Color{ 120, 250, 255, static_cast<uint8_t>(235 * alpha) }
                        : sakura::core::Color{ 190, 235, 255, static_cast<uint8_t>(150 * alpha) });
                renderer.DrawCircleFilled(pathX, guideY, radius, markerColor, 18);

                if (nextStep)
                {
                    renderer.DrawCircleOutline(pathX, JUDGE_LINE_Y, 0.020f,
                        { 120, 250, 255, static_cast<uint8_t>(160 * alpha) }, 0.002f, 24);
                }
            }

            renderer.DrawCircleFilled(sourceX, headY, 0.014f, dragColor, 16);
            renderer.DrawCircleFilled(targetX, tailY, 0.012f,
                sakura::core::Color{ 210, 245, 255, static_cast<uint8_t>(190 * alpha) }, 16);
            renderer.DrawLine(targetX, tailY - 0.018f, targetX, tailY + 0.018f,
                sakura::core::Color{ 100, 200, 240, static_cast<uint8_t>(200 * alpha) },
                0.004f);
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
    if (!m_gameState.IsPlaying() && !m_gameState.IsInCountdown()) return;

    int now = m_gameState.GetCurrentTime();
    auto activeNotes = m_gameState.GetActiveMouseNotes();

    for (const auto& note : activeNotes)
    {
        if (note.isJudged && note.alpha <= 0.01f) continue;

        float scale = CalcApproachScale(note.time, now);
        uint8_t alpha = static_cast<uint8_t>(note.alpha * 220.0f);

        switch (note.type)
        {
        case sakura::game::NoteType::Circle:
        {
            // 将鼠标区局部坐标转换为屏幕坐标
            float sx = MOUSE_X + note.x * MOUSE_W;
            float sy = MOUSE_Y + note.y * MOUSE_H;
            // 接近圈（外圈从大到小）
            renderer.DrawCircleOutline(sx, sy, 0.028f * scale,
                sakura::core::Color{ 220, 170, 255, static_cast<uint8_t>(alpha * 0.7f) },
                0.002f, 48);
            // 核心圆
            renderer.DrawCircleFilled(sx, sy, 0.025f,
                sakura::core::Color{ 200, 150, 255, alpha });
            // 边缘
            renderer.DrawCircleOutline(sx, sy, 0.025f,
                sakura::core::Color{ 255, 220, 255, alpha }, 0.002f, 48);
            break;
        }
        case sakura::game::NoteType::Slider:
        {
            // 将起始坐标转换为屏幕坐标
            float sx = MOUSE_X + note.x * MOUSE_W;
            float sy = MOUSE_Y + note.y * MOUSE_H;

            // 查找对应的活跃 SliderState
            auto& msNotes = m_gameState.GetMouseNotes();
            int noteIndex = static_cast<int>(&note - msNotes.data());
            const sakura::game::SliderState* ssPtr = nullptr;
            for (const auto& s : m_sliderStates)
            {
                if (s.noteIndex == noteIndex)
                {
                    ssPtr = &s;
                    break;
                }
            }
            bool isActive = (ssPtr != nullptr && ssPtr->headJudged);

            // 绘制完整路径：起点 → 拐点0 → 拐点1 → …
            {
                float prevPx = sx, prevPy = sy;
                for (const auto& [wpx, wpy] : note.sliderPath)
                {
                    float spx = MOUSE_X + wpx * MOUSE_W;
                    float spy = MOUSE_Y + wpy * MOUSE_H;
                    renderer.DrawLine(prevPx, prevPy, spx, spy,
                        sakura::core::Color{ 80, 200, 120,
                            static_cast<uint8_t>(alpha * 0.5f) },
                        0.003f);
                    prevPx = spx;
                    prevPy = spy;
                }
            }

            // 绘制各拐点标记（已过的暗化）
            for (int wi = 0; wi < static_cast<int>(note.sliderPath.size()); ++wi)
            {
                auto [wpx, wpy] = note.sliderPath[wi];
                float spx = MOUSE_X + wpx * MOUSE_W;
                float spy = MOUSE_Y + wpy * MOUSE_H;
                bool passed = isActive && (wi < ssPtr->nextWaypointIndex);
                bool isNext = isActive && (wi == ssPtr->nextWaypointIndex);
                uint8_t wpAlpha = passed
                    ? static_cast<uint8_t>(alpha * 0.3f)
                    : static_cast<uint8_t>(alpha * 0.7f);
                renderer.DrawCircleOutline(spx, spy, 0.018f,
                    sakura::core::Color{ 150, 255, 180, wpAlpha },
                    0.002f, 24);
                if (isNext)
                {
                    renderer.DrawCircleOutline(spx, spy, 0.024f,
                        sakura::core::Color{ 220, 255, 240,
                            static_cast<uint8_t>(alpha * 0.9f) },
                        0.002f, 24);
                }
            }

            if (!isActive)
            {
                // 未激活：绘制接近圈（由大缩小）和起点核心圆
                renderer.DrawCircleOutline(sx, sy, 0.030f * scale,
                    sakura::core::Color{ 180, 255, 200,
                        static_cast<uint8_t>(alpha * 0.6f) },
                    0.002f, 48);
                renderer.DrawCircleFilled(sx, sy, 0.025f,
                    sakura::core::Color{ 100, 220, 140, alpha });
            }
            else
            {
                // 激活中：
                // 1. 在路径上绘制随时值移动的引导球（提示玩家当前应在何处）
                float t = static_cast<float>(now - note.time)
                        / static_cast<float>(std::max(1, note.sliderDuration));
                t = std::max(0.0f, std::min(1.0f, t));
                auto [hx, hy] = sakura::game::Judge::GetSliderPosition(note, t);
                float shx = MOUSE_X + hx * MOUSE_W;
                float shy = MOUSE_Y + hy * MOUSE_H;
                // 引导球：绿色偏暗，半径略小，表示"应到达的位置"
                renderer.DrawCircleFilled(shx, shy, 0.016f,
                    sakura::core::Color{ 80, 200, 120,
                        static_cast<uint8_t>(alpha * 0.7f) });
                renderer.DrawCircleOutline(shx, shy, 0.016f,
                    sakura::core::Color{ 150, 255, 180,
                        static_cast<uint8_t>(alpha * 0.6f) },
                    0.002f, 24);

                // 2. 在鼠标当前位置绘制玩家光标圆环（随鼠标移动）
                auto [cmx, cmy] = sakura::core::Input::GetMousePosition();
                float localMx = std::max(0.0f, std::min(1.0f,
                    (cmx - MOUSE_X) / MOUSE_W));
                float localMy = std::max(0.0f, std::min(1.0f,
                    (cmy - MOUSE_Y) / MOUSE_H));
                float cursorSX = MOUSE_X + localMx * MOUSE_W;
                float cursorSY = MOUSE_Y + localMy * MOUSE_H;
                renderer.DrawCircleFilled(cursorSX, cursorSY, 0.022f,
                    sakura::core::Color{ 200, 255, 220, alpha });
                renderer.DrawCircleOutline(cursorSX, cursorSY, 0.022f,
                    sakura::core::Color{ 255, 255, 255, alpha },
                    0.002f, 32);
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
    if (m_fontHUD == sakura::core::INVALID_HANDLE) return;

    // 分数（右对齐 0.96, 0.02）
    {
        std::ostringstream ss;
        ss << std::setw(7) << std::setfill('0') << m_score.GetScore();
        renderer.DrawText(m_fontHUD, ss.str(),
            0.96f, 0.02f, 0.040f,
            sakura::core::Color{ 255, 240, 255, 230 },
            sakura::core::TextAlign::Right);
    }

    // 连击（居中 0.225, 0.05，只在 ≥10 时显示）
    {
        int combo = m_score.GetCombo();
        if (combo >= 10)
        {
            renderer.DrawText(m_fontHUD, std::to_string(combo),
                0.225f, 0.05f, 0.050f,
                sakura::core::Color{ 255, 220, 100, 230 },
                sakura::core::TextAlign::Center);
            renderer.DrawText(m_fontSmall, "COMBO",
                0.225f, 0.103f, 0.020f,
                sakura::core::Color{ 220, 200, 100, 180 },
                sakura::core::TextAlign::Center);
        }
    }

    // 准确率（右对齐 0.96, 0.065）
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << m_score.GetAccuracy() << "%";
        renderer.DrawText(m_fontSmall, ss.str(),
            0.96f, 0.065f, 0.025f,
            sakura::core::Color{ 200, 200, 240, 200 },
            sakura::core::TextAlign::Right);
    }

    // 时间（左下 0.02, 0.96）
    {
        int t   = m_gameState.GetCurrentTime();
        int sec = std::abs(t / 1000);
        int ms  = std::abs(t % 1000);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%02d.%03d",
                      sec / 60, sec % 60, ms);
        renderer.DrawText(m_fontSmall, buf,
            0.02f, 0.960f, 0.020f,
            sakura::core::Color{ 160, 155, 180, 160 },
            sakura::core::TextAlign::Left);
    }

    // 进度条（底部 0.0, 0.982, 1.0, 0.012）
    {
        float progress = m_gameState.GetProgress();
        renderer.DrawFilledRect({ 0.0f, 0.982f, 1.0f, 0.012f },
            sakura::core::Color{ 20, 15, 35, 180 });
        renderer.DrawFilledRect({ 0.0f, 0.982f, progress, 0.012f },
            sakura::core::Color{ 150, 100, 220, 200 });
    }
}

// ── RenderCountdown ───────────────────────────────────────────────────────────

void SceneGame::RenderCountdown(sakura::core::Renderer& renderer)
{
    if (!m_gameState.IsInCountdown()) return;
    if (m_fontHUD == sakura::core::INVALID_HANDLE) return;

    int num = m_gameState.GetCountdownNumber();
    if (num <= 0) return;

    float remaining = m_gameState.GetCountdownRemaining();
    float phase     = std::fmod(remaining, 1.0f);   // 0=刚切换, 1=将切换
    float t         = 1.0f - phase;                 // 0→1 从入场到退出
    float scale     = 1.0f + 0.6f * (1.0f - sakura::utils::EaseOutBack(t));
    uint8_t alpha   = static_cast<uint8_t>((1.0f - sakura::utils::EaseInCubic(t)) * 255.0f);

    std::string numStr = std::to_string(num);
    float fontSize = 0.14f * scale;

    renderer.DrawText(m_fontHUD, numStr,
        0.225f, 0.40f, fontSize,
        sakura::core::Color{ 255, 230, 255, alpha },
        sakura::core::TextAlign::Center);
}

// ── RenderJudgeFlashes ────────────────────────────────────────────────────────

void SceneGame::RenderJudgeFlashes(sakura::core::Renderer& renderer)
{
    if (m_fontSmall == sakura::core::INVALID_HANDLE) return;

    for (const auto& flash : m_judgeFlashes)
    {
        float progress = 1.0f - flash.timer / JudgeFlash::FLASH_DURATION;
        uint8_t alpha  = static_cast<uint8_t>(
            (1.0f - sakura::utils::EaseInQuad(progress)) * 255.0f);

        float posY = flash.isKeyboard
            ? JUDGE_LINE_Y - 0.06f - progress * 0.04f
            : flash.posY - 0.08f - progress * 0.04f;
        float posX = flash.isKeyboard
            ? GetLaneX(flash.lane) + LANE_W * 0.5f
            : flash.posX;

        auto color = JudgeResultColor(flash.result);
        color.a = alpha;

        renderer.DrawText(m_fontSmall, JudgeResultText(flash.result),
            posX, posY, 0.028f,
            color, sakura::core::TextAlign::Center);
    }
}

// ── OnRender ─────────────────────────────────────────────────────────────────

void SceneGame::OnRender(sakura::core::Renderer& renderer)
{
    RenderBackground(renderer);
    RenderTrack(renderer);

    if (m_gameState.IsPlaying() || m_gameState.IsInCountdown())
    {
        RenderKeyboardNotes(renderer);
        RenderMouseNotes(renderer);
    }

    RenderHUD(renderer);
    RenderCountdown(renderer);
    RenderJudgeFlashes(renderer);

    // 粒子渲染（最上层）
    m_particles.Render(renderer);

    // 连击里程碑色差特效
    if (m_chromaTimer > 0.0f)
    {
        float intensity = (m_chromaTimer / 0.4f) * 0.006f;
        sakura::effects::ShaderManager::GetInstance().DrawChromaticAberration(
            nullptr, intensity);
    }
}

} // namespace sakura::scene
