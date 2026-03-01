#pragma once

// scene_game.h — 游戏核心场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "game/game_state.h"
#include "game/judge.h"
#include "game/score.h"
#include "effects/particle_system.h"
#include "effects/glow.h"
#include "effects/screen_shake.h"
#include "effects/shader_manager.h"

#include <array>
#include <memory>
#include <vector>

namespace sakura::scene
{

// 判定闪现（每次判定在屏幕上短暂显示）
struct JudgeFlash
{
    sakura::game::JudgeResult result = sakura::game::JudgeResult::None;
    float timer       = 0.0f;   // 0.5s 倒计时
    bool  isKeyboard  = true;   // true=键盘区, false=鼠标区
    int   lane        = 0;      // 键盘轨道（isKeyboard=true 时有效）
    float posX        = 0.0f;   // 鼠标区显示位置
    float posY        = 0.0f;
    static constexpr float FLASH_DURATION = 0.5f;
};

// SceneGame — 游戏进行中场景
// 构造时接收 ChartInfo + 难度索引。
// 流程：加载资源 → 倒计时 3-2-1 → 播放音乐 → 音符下落判定 → 结算
class SceneGame final : public Scene
{
public:
    SceneGame(SceneManager& mgr,
              const sakura::game::ChartInfo& chartInfo,
              int difficultyIndex = 0);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    // 游戏核心系统
    sakura::game::GameState        m_gameState;
    sakura::game::Judge            m_judge;
    sakura::game::ScoreCalculator  m_score;

    // 初始化参数（OnEnter 时传给 GameState::Start）
    sakura::game::ChartInfo m_chartInfo;
    int                     m_difficultyIndex;

    // Hold/Slider 活跃状态
    std::vector<sakura::game::HoldState>   m_holdStates;
    std::vector<sakura::game::SliderState> m_sliderStates;

    // 判定闪现
    std::vector<JudgeFlash> m_judgeFlashes;

    // 背景纹理
    sakura::core::TextureHandle m_bgTexture = sakura::core::INVALID_HANDLE;

    // 字体
    sakura::core::FontHandle m_fontHUD   = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSmall = sakura::core::INVALID_HANDLE;

    // 按键 → 轨道 映射（默认 A/S/D/F）
    static constexpr int LANE_COUNT = 4;
    std::array<SDL_Scancode, LANE_COUNT> m_laneKeys = {
        SDL_SCANCODE_A, SDL_SCANCODE_S,
        SDL_SCANCODE_D, SDL_SCANCODE_F
    };

    // ── 布局常量（归一化） ────────────────────────────────────────────────────
    static constexpr float TRACK_X      = 0.05f;
    static constexpr float TRACK_W      = 0.35f;
    static constexpr float LANE_W       = TRACK_W / LANE_COUNT;   // 0.0875
    static constexpr float JUDGE_LINE_Y = 0.85f;
    static constexpr float MOUSE_X      = 0.45f;
    static constexpr float MOUSE_Y      = 0.05f;
    static constexpr float MOUSE_W      = 0.50f;
    static constexpr float MOUSE_H      = 0.90f;

    // 音符渲染参数
    static constexpr float NOTE_H        = 0.022f;  // Tap 音符高度
    static constexpr float BASE_APPROACH_RANGE = 2000.0f;  // ms, 屏幕高度跨度

    // ── 视觉特效 ─────────────────────────────────────────────────────────────
    sakura::effects::ParticleSystem m_particles;  // 判定爆发 + 里程碑粒子
    float m_judgePulsePhase = 0.0f;               // 判定线脉冲相位
    float m_chromaTimer     = 0.0f;               // 色差计时（>0=激活）
    int   m_lastCheckedCombo = 0;                 // 上帧连击数（用于里程碑检测）

    // 轨道按键状态（用于轨道按下发光效果）
    std::array<bool, LANE_COUNT> m_lanePressed = {};

    // ── 内部方法 ──────────────────────────────────────────────────────────────

    // 计算键盘音符的渲染 Y（判定线=0.85，向上为正方向）
    float CalcNoteRenderY(int noteTimeMs, int currentTimeMs, float svSpeed) const;

    // 计算鼠标音符的接近圈缩放倍率（2.5~1.0）
    float CalcApproachScale(int noteTimeMs, int currentTimeMs) const;

    // 获取轨道 X 坐标（左边缘）
    float GetLaneX(int lane) const { return TRACK_X + lane * LANE_W; }

    // 响应按键判定
    void HandleKeyPress(SDL_Scancode key);

    // 响应鼠标点击判定
    void HandleMouseClick(float normX, float normY);

    // 添加判定闪现
    void AddJudgeFlash(sakura::game::JudgeResult r, bool isKb, int lane = 0,
                       float px = 0.f, float py = 0.f);

    // 渲染各部分
    void RenderBackground(sakura::core::Renderer& renderer);
    void RenderTrack(sakura::core::Renderer& renderer);
    void RenderKeyboardNotes(sakura::core::Renderer& renderer);
    void RenderMouseNotes(sakura::core::Renderer& renderer);
    void RenderHUD(sakura::core::Renderer& renderer);
    void RenderCountdown(sakura::core::Renderer& renderer);
    void RenderJudgeFlashes(sakura::core::Renderer& renderer);

    // 判定结果文字和颜色
    static const char*              JudgeResultText(sakura::game::JudgeResult r);
    static sakura::core::Color      JudgeResultColor(sakura::game::JudgeResult r);
};

} // namespace sakura::scene
