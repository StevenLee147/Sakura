#pragma once

// scene_result.h — 结算场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "game/chart.h"
#include "effects/particle_system.h"
#include "effects/glow.h"

#include <memory>

namespace sakura::scene
{

// SceneResult — 游戏结束后的结算界面
// - 评级大字、分数滚动动画（1.5s EaseOutExpo）
// - 判定统计 / 准确率 / 最大连击
// - 偏差分布图（横轴 ±150 ms）
// - "重玩" / "返回"
class SceneResult final : public Scene
{
public:
    SceneResult(SceneManager& mgr,
                sakura::game::GameResult result,
                sakura::game::ChartInfo  chartInfo);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager&            m_manager;
    sakura::game::GameResult m_result;
    sakura::game::ChartInfo  m_chartInfo;

    sakura::core::FontHandle m_fontUI    = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontScore = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontGrade = sakura::core::INVALID_HANDLE;

    // 元素逐步淡入（每个元素间隔 0.08 s）
    static constexpr int   NUM_ELEMENTS     = 12;
    static constexpr float FADE_INTERVAL    = 0.08f;
    static constexpr float FADE_DURATION    = 0.40f;

    // 分数滚动
    static constexpr float SCORE_ANIM_DURATION = 1.5f;
    float m_scoreTimer  = 0.0f;
    int   m_displayScore = 0;

    // 元素淡入计时
    float m_elemTimer = 0.0f;

    // 按钮
    std::unique_ptr<sakura::ui::Button> m_btnRetry;
    std::unique_ptr<sakura::ui::Button> m_btnBack;

    // 粒子系统（FC/AP 庆祝 + 樱花飘落）
    sakura::effects::ParticleSystem m_particles;
    int   m_sakuraPetalEmitter = -1;
    bool  m_particlesBurst     = false;

    // 评级大字弹入动画（EaseOutElastic）
    float m_gradeScale      = 0.0f;
    float m_gradeScaleTimer = 0.0f;
    static constexpr float GRADE_ANIM_DURATION = 0.6f;

    // 帮助函数 ----------------------------------------------------------------
    static sakura::core::Color GradeColor(sakura::game::Grade grade);
    static const char* GradeText(sakura::game::Grade grade);
    float ElemAlpha(int elemIndex) const;   // 0.0~1.0
};

} // namespace sakura::scene
