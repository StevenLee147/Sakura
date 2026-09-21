// scene_result.cpp — 结算场景

#include "scene_result.h"
#include "scene_select.h"
#include "scene_game.h"
#include "core/resource_manager.h"
#include "core/config.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "data/database.h"
#include "game/achievement_manager.h"
#include "effects/particle_system.h"
#include "effects/glow.h"
#include "ui/visual_style.h"

#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <memory>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneResult::SceneResult(SceneManager& mgr,
                         sakura::game::GameResult result,
                         sakura::game::ChartInfo  chartInfo, sakura::game::PlayOptions options)
    : m_manager(mgr)
    , m_result(std::move(result))
    , m_chartInfo(std::move(chartInfo))
    , m_options(std::move(options))
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneResult::OnEnter()
{
    LOG_INFO("[SceneResult] 进入结算场景，分数={}, 评级={}",
             m_result.score,
             static_cast<int>(m_result.grade));

    auto& rm      = sakura::core::ResourceManager::GetInstance();
    m_fontUI      = rm.GetDefaultFontHandle();
    m_fontScore   = rm.GetDefaultFontHandle();
    m_fontGrade   = rm.GetDefaultFontHandle();

    m_scoreTimer   = 0.0f;
    m_displayScore = 0;
    m_resultPP     = m_result.assisted ? 0 : sakura::game::PPCalculator::CalculatePP(m_result, m_result.difficultyLevel);
    m_elemTimer    = 0.0f;

    // 评级弹入动画复位
    m_gradeScale      = 0.0f;
    m_gradeScaleTimer = 0.0f;
    m_particlesBurst  = false;

    // ── 按钮 ──────────────────────────────────────────────────────────────────
    m_btnRetry = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{0.77f, 0.913f, 0.19f, 0.054f},
        "再次演奏 / R", m_fontUI, 0.024f);

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{0.04f, 0.913f, 0.16f, 0.054f},
        "返回曲库 / Enter", m_fontUI, 0.022f);

    sakura::ui::VisualStyle::ApplyButton(m_btnRetry.get(), sakura::ui::ButtonVariant::Primary);
    sakura::ui::VisualStyle::ApplyButton(m_btnBack.get(), sakura::ui::ButtonVariant::Secondary);

    m_btnRetry->SetOnClick([this]()
    {
        // 重新构造同一谱面的 SceneGame（使用与本局相同的难度）
        m_manager.SwitchScene(
            std::make_unique<SceneGame>(m_manager, m_chartInfo,
                                        m_result.difficultyIndex, m_options),
            sakura::scene::TransitionType::Fade, 0.4f);
    });

    m_btnBack->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::Fade, 0.4f);
    });

    m_btnRetry->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnBack->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnReplay=std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.57f,0.913f,0.18f,0.054f},"查看回放",m_fontUI,0.023f);
    m_btnReplay->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnReplay->SetEnabled(!m_result.replayFile.empty() || m_options.mode==sakura::game::PlayMode::Replay);
    m_btnReplay->SetOnClick([this]{auto options=m_options;options.mode=sakura::game::PlayMode::Replay;
        if(!m_result.replayFile.empty())options.replayFile=m_result.replayFile;
        m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager,m_chartInfo,m_result.difficultyIndex,options));});
    for(int error:m_result.hitErrors){++m_histogram[std::clamp((150-error)*41/301,0,40)];m_meanError+=error;}
    if(!m_result.hitErrors.empty()){
        m_meanError/=m_result.hitErrors.size();for(int error:m_result.hitErrors)m_deviation+=(error-m_meanError)*(error-m_meanError);
        m_deviation=std::sqrt(m_deviation/m_result.hitErrors.size());
    }
    // 停止可能残留的音乐
    sakura::audio::AudioManager::GetInstance().StopMusic();

    // ── 保存成绩到数据库 ──────────────────────────────────────────────────────
    if (!m_result.assisted) {
        auto& db=sakura::data::Database::GetInstance();const auto best=db.GetBestScore(m_result.chartId,m_result.difficulty,m_result.chartHash);
        m_newBest=!best || m_result.score>best->score; m_scoreDelta=m_result.score-(best?best->score:0);
        m_savedScore=db.SaveScore(m_result);
        if(!m_savedScore)sakura::ui::ToastManager::Instance().Show("成绩未能保存，请检查数据目录和剩余空间",sakura::ui::ToastType::Error,6);
    }

    if (m_savedScore) for (const auto& achievement : sakura::game::AchievementManager::GetInstance().CheckAndUnlock(m_result))
    {
        sakura::ui::ToastManager::Instance().Show(
            "成就解锁: " + achievement.definition.title,
            sakura::ui::ToastType::Achievement,
            4.0f);
        sakura::audio::AudioManager::GetInstance().PlayUISFX(
            sakura::audio::UISFXType::Toast);
    }

    // ── 樱花飘落持续发射器 ────────────────────────────────────────────────────
    m_particles.Clear();
    auto petalCfg = sakura::effects::ParticlePresets::SakuraPetal();
    m_sakuraPetalEmitter = m_particles.EmitContinuous(0.5f, -0.02f, 4.0f, petalCfg);
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneResult::OnExit()
{
    LOG_INFO("[SceneResult] 退出结算场景");
    if (m_sakuraPetalEmitter >= 0)
        m_particles.StopEmitter(m_sakuraPetalEmitter);
    m_particles.Clear();
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneResult::OnUpdate(float dt)
{
    if(sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion",false))dt=10;
    m_elemTimer  += dt;
    m_scoreTimer += dt;

    // 分数滚动动画（EaseOutExpo，1.5 秒）
    if (m_scoreTimer < SCORE_ANIM_DURATION)
    {
        float t       = m_scoreTimer / SCORE_ANIM_DURATION;
        float eased   = sakura::utils::EaseOutExpo(t);
        m_displayScore = static_cast<int>(m_result.score * eased);
    }
    else
    {
        m_displayScore = m_result.score;
    }

    // 评级大字弹入（EaseOutElastic，等待元素1淡入后开始）
    float gradeStartDelay = 1 * FADE_INTERVAL + 0.05f;
    if (m_elemTimer > gradeStartDelay)
    {
        m_gradeScaleTimer += dt;
        float t = std::min(m_gradeScaleTimer / GRADE_ANIM_DURATION, 1.0f);
        m_gradeScale = sakura::utils::EaseOutElastic(t);
    }

    // FC/AP 粒子爆发（首次达到元素2可见时）
    if (!m_particlesBurst && ElemAlpha(2) > 0.5f)
    {
        m_particlesBurst = true;
        if (m_result.isAllPerfect || m_result.isFullCombo)
        {
            // 金色/青色粒子爆发
            auto preset = m_result.isAllPerfect
                ? sakura::effects::ParticlePresets::ComboMilestone()
                : sakura::effects::ParticlePresets::HitBurst(
                    sakura::core::Color{100, 220, 255, 255});
            m_particles.Emit(0.5f, 0.27f, 40, preset);
        }
    }

    // 粒子更新
    m_particles.Update(dt);


    if (m_btnReplay) m_btnReplay->Update(dt);
    if (m_btnRetry) m_btnRetry->Update(dt);
    if (m_btnBack)  m_btnBack ->Update(dt);
}

// ── ElemAlpha ─────────────────────────────────────────────────────────────────

float SceneResult::ElemAlpha(int elemIndex) const
{
    float startTime = elemIndex * FADE_INTERVAL;
    float elapsed   = m_elemTimer - startTime;
    if (elapsed <= 0.0f) return 0.0f;
    if (elapsed >= FADE_DURATION) return 1.0f;
    return elapsed / FADE_DURATION;
}

// ── GradeColor / GradeText ────────────────────────────────────────────────────

sakura::core::Color SceneResult::GradeColor(sakura::game::Grade grade)
{
    using sakura::game::Grade;
    switch (grade)
    {
        case Grade::SS: return {218, 165,  32, 255};  // 金色
        case Grade::S:  return {255, 200,   0, 255};  // 亮金
        case Grade::A:  return { 60, 200,  60, 255};  // 绿
        case Grade::B:  return { 80, 160, 220, 255};  // 蓝
        case Grade::C:  return {160, 160, 160, 255};  // 灰
        default:        return {220,  60,  60, 255};  // 红（D）
    }
}

const char* SceneResult::GradeText(sakura::game::Grade grade)
{
    using sakura::game::Grade;
    switch (grade)
    {
        case Grade::SS: return "SS";
        case Grade::S:  return "S";
        case Grade::A:  return "A";
        case Grade::B:  return "B";
        case Grade::C:  return "C";
        default:        return "D";
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneResult::OnRender(sakura::core::Renderer& r)
{
    using namespace sakura::core;
    using sakura::ui::VisualStyle;
    const Color pink{235,174,194,255},dim{160,169,191,255},white{242,235,241,255};
    auto number=[](double value,int p){std::ostringstream s;s<<std::fixed<<std::setprecision(p)<<value;return s.str();};
    VisualStyle::DrawSceneBackground(r);
    r.DrawText(m_fontUI,"PERFORMANCE / 演奏记录",0.04f,0.045f,0.040f,white);
    VisualStyle::DrawTextFit(r,m_fontUI,m_result.chartTitle+"   /   "+m_result.difficulty+"  Lv."+number(m_result.difficultyLevel,0),0.04f,0.108f,0.024f,0.75f,dim);
    VisualStyle::DrawPanel(r,{0.04f,0.19f,0.40f,0.67f});
    const auto grade=GradeColor(m_result.grade);
    r.DrawCircleOutline(0.24f,0.378f,0.126f,grade.WithAlpha(45),0.001f,96);
    r.DrawArc(0.24f,0.378f,0.136f,-90,-90+360*m_result.accuracy/100,grade.WithAlpha(175),0.003f,96);
    r.DrawText(m_fontGrade,GradeText(m_result.grade),0.24f,0.283f,0.124f,grade,TextAlign::Center);
    const std::string badge=m_result.isAllPerfect?"ALL PERFECT":m_result.isFullCombo?"FULL COMBO":"TRACK COMPLETE";
    r.DrawText(m_fontUI,badge,0.24f,0.53f,0.019f,pink,TextAlign::Center);
    r.DrawText(m_fontScore,std::to_string(m_displayScore),0.24f,0.568f,0.072f,white,TextAlign::Center);
    r.DrawText(m_fontUI,"准确率",0.14f,0.686f,0.018f,dim,TextAlign::Center);
    r.DrawText(m_fontUI,number(m_result.accuracy,2)+"%",0.14f,0.718f,0.034f,white,TextAlign::Center);
    r.DrawText(m_fontUI,"最大连击",0.34f,0.686f,0.018f,dim,TextAlign::Center);
    r.DrawText(m_fontUI,std::to_string(m_result.maxCombo)+"×",0.34f,0.718f,0.034f,white,TextAlign::Center);
    const std::string status=m_result.assisted?"辅助演奏 · 不记录成绩与 PP":!m_savedScore?"保存失败 · 请检查存储空间":m_newBest?"NEW BEST  /  新纪录  +"+std::to_string(m_scoreDelta):"成绩已保存  /  "+number(m_resultPP,2)+" PP";
    r.DrawText(m_fontUI,status,0.24f,0.805f,0.019f,pink,TextAlign::Center);
    VisualStyle::DrawPanel(r,{0.47f,0.19f,0.49f,0.37f});
    r.DrawText(m_fontUI,"JUDGMENTS / 判定",0.495f,0.21f,0.022f,white);
    const char* labels[]={"PERFECT","GREAT","GOOD","BAD","MISS"};
    const int counts[]={m_result.perfectCount,m_result.greatCount,m_result.goodCount,m_result.badCount,m_result.missCount};
    const Color colors[]={{248,211,152,255},{142,209,225,255},{163,215,182,255},{235,166,136,255},{222,125,146,255}};
    const int max=*std::max_element(std::begin(counts),std::end(counts));
    for(int i=0;i<5;++i){float y=0.27f+i*0.052f;
        r.DrawText(m_fontUI,labels[i],0.495f,y,0.019f,colors[i]);
        r.DrawRoundedRect({0.59f,y+0.012f,0.265f,0.009f},0.004f,{66,64,86,100});
        if(counts[i])r.DrawRoundedRect({0.59f,y+0.012f,0.265f*counts[i]/std::max(1,max),0.009f},0.004f,colors[i].WithAlpha(180));
        r.DrawText(m_fontUI,std::to_string(counts[i]),0.93f,y,0.022f,white,TextAlign::Right);
    }
    VisualStyle::DrawPanel(r,{0.47f,0.59f,0.49f,0.27f});
    r.DrawText(m_fontUI,"TIMING / 击打偏差",0.495f,0.612f,0.022f,white);
    r.DrawText(m_fontUI,m_result.hitErrors.empty()?"没有有效击打数据":"平均 "+number(m_meanError,1)+" ms  /  标准差 "+number(m_deviation,1)+" ms",0.932f,0.623f,0.017f,dim,TextAlign::Right);
    const int peak=std::max(1,*std::max_element(m_histogram.begin(),m_histogram.end()));
    for(int i=0;i<41;++i){const float h=0.112f*m_histogram[40-i]/peak;const float x=0.503f+i*0.0104f;
        r.DrawFilledRect({x,0.795f-h,0.0078f,h},std::abs(i-20)<=3?Color{243,210,158,225}:Color{154,187,211,190});}
    r.DrawLine(0.501f,0.796f,0.931f,0.796f,{134,143,168,120},0.001f);
    r.DrawLine(0.715f,0.675f,0.715f,0.80f,{221,201,213,75},0.001f);
    r.DrawText(m_fontUI,"EARLY  +150 ms",0.50f,0.811f,0.015f,dim);
    r.DrawText(m_fontUI,"0",0.715f,0.811f,0.015f,dim,TextAlign::Center);
    r.DrawText(m_fontUI,"−150 ms  LATE",0.932f,0.811f,0.015f,dim,TextAlign::Right);
    m_btnBack->Render(r);m_btnReplay->Render(r);m_btnRetry->Render(r);
    m_particles.Render(r);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneResult::OnEvent(const SDL_Event& event)
{
    // ESC → 返回选歌（与"返回"按钮相同行为）
    if (event.type == SDL_EVENT_KEY_DOWN &&
        (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_RETURN))
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::Fade, 0.4f);
        return;
    }

    if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_R){m_btnRetry->Activate();return;}
    if(m_btnReplay)m_btnReplay->HandleEvent(event);
    if (m_btnRetry) m_btnRetry->HandleEvent(event);
    if (m_btnBack)  m_btnBack ->HandleEvent(event);
}

} // namespace sakura::scene
