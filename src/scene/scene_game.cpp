// scene_game.cpp — 游戏核心场景实现

#include "scene_game.h"
#include "scene_select.h"
#include "scene_pause.h"
#include "scene_result.h"
#include "core/input.h"
#include "core/config.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "audio/audio_manager.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace sakura::scene
{

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

    // 加载背景图
    if (!m_chartInfo.backgroundFile.empty())
    {
        std::string bgPath = m_chartInfo.folderPath + "/" + m_chartInfo.backgroundFile;
        auto hOpt = rm.LoadTexture(bgPath);
        m_bgTexture = hOpt.value_or(sakura::core::INVALID_HANDLE);
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
    m_sliderStates.clear();
    m_judgeFlashes.clear();
}

// ── OnExit ─────────────────────────────────────────────────────────────────────

void SceneGame::OnExit()
{
    LOG_INFO("[SceneGame] 退出游戏场景");
    sakura::audio::AudioManager::GetInstance().StopMusic();
    m_holdStates.clear();
    m_sliderStates.clear();
    m_judgeFlashes.clear();
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
            // 开始 Hold 状态追踪
            sakura::game::HoldState hs;
            hs.noteIndex  = bestIdx;
            hs.isHeld     = true;
            hs.headJudged = true;
            hs.headResult = result;
            hs.nextTickMs = now + sakura::game::HoldState::TICK_INTERVAL_MS;
            m_holdStates.push_back(hs);
        }
        m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
        AddJudgeFlash(result, true, lane);
    }
    // Drag 起始
    else if (note.type == sakura::game::NoteType::Drag)
    {
        auto result = m_judge.JudgeKeyboardNote(note, now);
        m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
        AddJudgeFlash(result, true, lane);

        // Drag 终点判定由目标轨道下次按键触发（此处仅判头部）
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

    int  bestIdx  = -1;
    int  bestDist = INT_MAX;
    for (auto& n : activeMs)
    {
        if (n.isJudged) continue;
        int dist = std::abs(n.time - now);
        if (dist < bestDist)
        {
            bestDist = dist;
            for (int i = 0; i < static_cast<int>(msNotes.size()); ++i)
            {
                if (&msNotes[i] == &n) { bestIdx = i; break; }
            }
        }
    }
    if (bestIdx < 0) return;

    auto& note   = msNotes[bestIdx];
    auto  result = m_judge.JudgeMouseNote(note, now, mouseX, mouseY);

    if (note.type == sakura::game::NoteType::Slider &&
        result != sakura::game::JudgeResult::Miss &&
        result != sakura::game::JudgeResult::None)
    {
        // 开始 Slider 追踪
        sakura::game::SliderState ss;
        ss.noteIndex   = bestIdx;
        ss.headJudged  = true;
        ss.headResult  = result;
        m_sliderStates.push_back(ss);
    }

    m_score.OnJudge(result, sakura::game::Judge::GetHitError(note.time, now));
    // 将鼠标区局部坐标转换为屏幕坐标再存入闪现记录
    float flashSX = MOUSE_X + note.x * MOUSE_W;
    float flashSY = MOUSE_Y + note.y * MOUSE_H;
    AddJudgeFlash(result, false, 0, flashSX, flashSY);
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

    // ── Hold 持续 tick ────────────────────────────────────────────────────────
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

        // 检查对应按键是否还按住
        bool isDown = sakura::core::Input::IsKeyHeld(m_laneKeys[note.lane]);
        hs.isHeld   = isDown;

        auto tickResult = m_judge.UpdateHoldTick(hs, note, now, isDown);
        if (tickResult != sakura::game::JudgeResult::None)
        {
            m_score.OnJudge(tickResult, 0);
        }

        // Hold 结束或全部判定完成
        if (note.isJudged)
        {
            it = m_holdStates.erase(it);
        }
        else
        {
            ++it;
        }
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

        auto sResult = m_judge.UpdateSliderTracking(
            ss, note, now, mouseX, mouseY, mouseDown);

        if (note.isJudged)
        {
            if (sResult != sakura::game::JudgeResult::None)
                m_score.OnJudge(sResult, 0);
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
        // 检测 Hold 松开
        for (auto& hs : m_holdStates)
        {
            if (hs.noteIndex < 0) continue;
            auto& kbNotes = m_gameState.GetKeyboardNotes();
            if (hs.noteIndex < static_cast<int>(kbNotes.size()))
            {
                int lane = kbNotes[hs.noteIndex].lane;
                if (event.key.scancode == m_laneKeys[lane])
                    hs.isHeld = false;
            }
        }
        break;

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
    // 深色底层
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 8, 6, 18, 255 });

    // 背景图（透明度 30%）
    if (m_bgTexture != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawSprite(m_bgTexture,
            { 0.0f, 0.0f, 1.0f, 1.0f },
            0.0f, sakura::core::Color::White, 0.30f);
    }
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

        // 剔除完全不可见的音符
        if (ry < -0.05f || ry > 1.05f) continue;

        float lx   = GetLaneX(note.lane);
        float alpha = note.alpha;

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
            // 计算尾部 Y
            float tailY = CalcNoteRenderY(note.time + note.duration, now, sv);
            float topY  = std::min(ry - NOTE_H * 0.5f, tailY);
            float botY  = std::max(ry + NOTE_H * 0.5f, tailY + NOTE_H * 0.5f);

            // 长条
            sakura::core::Color holdColor = {
                140, 100, 200, static_cast<uint8_t>(160 * alpha) };
            renderer.DrawFilledRect(
                { lx + 0.005f, topY, LANE_W - 0.010f, botY - topY },
                holdColor);
            // 头部矩形
            sakura::core::Color headColor = {
                210, 170, 255, static_cast<uint8_t>(220 * alpha) };
            renderer.DrawRoundedRect(
                { lx + 0.003f, ry - NOTE_H * 0.5f,
                  LANE_W - 0.006f, NOTE_H },
                0.004f, headColor, true);
            break;
        }
        case sakura::game::NoteType::Drag:
        {
            // 箭头（简化为菱形端点 + 中心小矩形）
            float cx = lx + LANE_W * 0.5f;
            sakura::core::Color dragColor = {
                150, 230, 255, static_cast<uint8_t>(200 * alpha) };
            renderer.DrawCircleFilled(cx, ry, 0.014f, dragColor, 16);
            renderer.DrawLine(cx, ry - 0.020f, cx, ry + 0.020f,
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
            // 将鼠标区局部坐标转换为屏幕坐标
            float sx = MOUSE_X + note.x * MOUSE_W;
            float sy = MOUSE_Y + note.y * MOUSE_H;
            // 头部
            renderer.DrawCircleOutline(sx, sy, 0.030f * scale,
                sakura::core::Color{ 180, 255, 200, static_cast<uint8_t>(alpha * 0.6f) },
                0.002f, 48);
            renderer.DrawCircleFilled(sx, sy, 0.025f,
                sakura::core::Color{ 100, 220, 140, alpha });

            // 路径线（路径点同样需要坐标转换）
            if (note.sliderPath.size() >= 2)
            {
                for (size_t pi = 1; pi < note.sliderPath.size(); ++pi)
                {
                    auto [x1, y1] = note.sliderPath[pi - 1];
                    auto [x2, y2] = note.sliderPath[pi];
                    float sx1 = MOUSE_X + x1 * MOUSE_W;
                    float sy1 = MOUSE_Y + y1 * MOUSE_H;
                    float sx2 = MOUSE_X + x2 * MOUSE_W;
                    float sy2 = MOUSE_Y + y2 * MOUSE_H;
                    renderer.DrawLine(sx1, sy1, sx2, sy2,
                        sakura::core::Color{ 80, 200, 120,
                            static_cast<uint8_t>(alpha * 0.5f) },
                        0.003f);
                }
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
}

} // namespace sakura::scene
