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
        m_manager.SwitchScene(
            std::make_unique<SceneGame>(m_manager, m_chartInfo,
                                        m_result.difficultyIndex, m_options),
            sakura::scene::TransitionType::SakuraSweep, 0.40f);
    });

    m_btnBack->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::SilkShutter, 0.40f);
    });
    m_btnRetry->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnBack->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnReplay=std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.57f,0.913f,0.18f,0.054f},"查看回放",m_fontUI,0.023f);
    sakura::ui::VisualStyle::ApplyButton(m_btnReplay.get(), sakura::ui::ButtonVariant::Accent);
    m_btnReplay->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnReplay->SetEnabled(!m_result.replayFile.empty() || m_options.mode==sakura::game::PlayMode::Replay);
    m_btnReplay->SetOnClick([this]{auto options=m_options;options.mode=sakura::game::PlayMode::Replay;
        if(!m_result.replayFile.empty())options.replayFile=m_result.replayFile;
        m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager,m_chartInfo,m_result.difficultyIndex,options),sakura::scene::TransitionType::SakuraSweep,0.40f);});

    m_bgCover.UnloadImage();
    const std::string artRel = !m_chartInfo.coverFile.empty() ? m_chartInfo.coverFile : m_chartInfo.backgroundFile;
    if (!artRel.empty() && m_bgCover.LoadImage(m_chartInfo.folderPath + "/" + artRel))
    {
        m_bgCover.SetDimming(0.70f);
        m_bgCover.SetBlurEnabled(sakura::core::Config::GetInstance().Get<bool>("graphics.background_blur", true));
        m_bgCover.SetBlurStrength(0.88f);
    }
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
    m_bgCover.UnloadImage();
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

    // FC/AP 或高评级印章落定粒子爆发
    if (!m_particlesBurst && ElemAlpha(2) > 0.5f)
    {
        m_particlesBurst = true;
        if (m_result.isAllPerfect || m_result.isFullCombo ||
            m_result.grade == sakura::game::Grade::SS || m_result.grade == sakura::game::Grade::S)
        {
            auto preset = (m_result.isAllPerfect || m_result.grade == sakura::game::Grade::SS)
                ? sakura::effects::ParticlePresets::ComboMilestone()
                : sakura::effects::ParticlePresets::HitBurst(
                    sakura::core::Color{ 246, 178, 206, 255 });
            m_particles.Emit(0.24f, 0.365f, 36, preset);
        }
    }

    m_bgCover.Update(dt);
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
        case Grade::SS: return { 252, 222, 152, 255 };
        case Grade::S:  return { 248, 198, 126, 255 };
        case Grade::A:  return { 246, 172, 204, 255 };
        case Grade::B:  return { 136, 206, 246, 255 };
        case Grade::C:  return { 184, 178, 224, 255 };
        default:        return { 236, 124, 142, 255 };
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
    const Color pink{246,178,206,255}, gold{250,216,157,255}, dim{168,178,204,255}, white{248,242,248,255};
    auto number=[](double value,int p){std::ostringstream s;s<<std::fixed<<std::setprecision(p)<<value;return s.str();};
    const float aspect=static_cast<float>(r.GetScreenHeight())/std::max(1,r.GetScreenWidth());

    VisualStyle::DrawSceneBackground(r);
    if (m_bgCover.HasImage()) {
        m_bgCover.Render(r);
        VisualStyle::DrawSakuraLandscape(r, m_elemTimer * 0.45f, 0.25f);
    } else {
        VisualStyle::DrawSakuraLandscape(r, m_elemTimer * 0.45f, 0.70f);
    }
    r.DrawGradientRect({0.0f,0.0f,1.0f,1.0f},{8,12,26,130},{12,16,34,105},{8,12,26,180},{10,14,30,165});

    const float a0 = ElemAlpha(0);
    VisualStyle::DrawBadgePill(r,{0.04f,0.032f,0.165f,0.026f},"PERFORMANCE RECORD",m_fontUI,0.013f,
        Color{48,36,70,static_cast<uint8_t>(205*a0)},Color{250,216,157,static_cast<uint8_t>(160*a0)},gold.WithAlpha(static_cast<uint8_t>(255*a0)));
    r.DrawText(m_fontUI,"PERFORMANCE / 演奏结算",0.04f,0.062f,0.038f,white.WithAlpha(static_cast<uint8_t>(255*a0)));
    VisualStyle::DrawTextFit(r,m_fontUI,m_result.chartTitle+"   ·   "+m_result.difficulty+"  Lv."+number(m_result.difficultyLevel,0),0.04f,0.114f,0.023f,0.82f,dim.WithAlpha(static_cast<uint8_t>(240*a0)));

    // ── 左侧评级印章与总分琉璃卡片 ───────────────────────────────────────────
    VisualStyle::DrawGlassCard(r,{0.04f,0.185f,0.40f,0.68f},
        {24,30,56,222},{14,18,36,236},{250,216,157,185},{246,178,206,82},0.018f,0.58f);

    const auto grade = GradeColor(m_result.grade);
    const float stampScale = std::max(0.0f, m_gradeScale);
    const float ringCX = 0.24f, ringCY = 0.358f;

    r.SetBlendMode(BlendMode::Additive);
    r.DrawRadialGradient(ringCX, ringCY, 0.16f * std::min(1.1f, stampScale), 0.16f * std::min(1.1f, stampScale),
        grade.WithAlpha(static_cast<uint8_t>(58 * ElemAlpha(1))), grade.WithAlpha(0), 48);
    r.SetBlendMode(BlendMode::Alpha);

    r.DrawRingGradient(ringCX, ringCY, 0.118f, 0.138f, grade.WithAlpha(48), grade.WithAlpha(0), 72);
    r.DrawCircleOutline(ringCX, ringCY, 0.124f, grade.WithAlpha(72), 0.0013f, 80);
    const float accSweep = std::clamp(static_cast<float>(m_result.accuracy), 0.0f, 100.0f) * 3.6f * ElemAlpha(2);
    if (accSweep > 1.0f) {
        r.DrawArc(ringCX, ringCY, 0.134f, -90.0f, -90.0f + accSweep, grade.WithAlpha(220), 0.0038f, 96);
        r.DrawArc(ringCX, ringCY, 0.114f, -90.0f, -90.0f + accSweep, gold.WithAlpha(135), 0.0016f, 72);
    }

    const auto rankTex = VisualStyle::GetRankEmblemTexture(GradeText(m_result.grade));
    if (rankTex != INVALID_HANDLE && stampScale > 0.01f) {
        const float eh = 0.205f * stampScale;
        const float ew = eh * aspect;
        r.DrawSprite(rankTex, { ringCX - ew * 0.5f, ringCY - eh * 0.5f, ew, eh }, 0.0f, Color::White, std::min(1.0f, ElemAlpha(1) * 1.15f));
    } else if (stampScale > 0.01f) {
        r.DrawText(m_fontGrade, GradeText(m_result.grade), ringCX, ringCY - 0.068f * stampScale,
                   0.122f * stampScale, grade, TextAlign::Center);
    }

    const std::string badge = m_result.isAllPerfect ? "✦  ALL PERFECT  ✦" : (m_result.isFullCombo ? "◆  FULL COMBO  ◆" : "TRACK COMPLETE");
    const Color badgeBorder = m_result.isAllPerfect ? gold : (m_result.isFullCombo ? Color{136,216,248,230} : pink.WithAlpha(165));
    VisualStyle::DrawBadgePill(r, {0.135f, 0.506f, 0.21f, 0.032f}, badge, m_fontUI, 0.015f,
        {48, 36, 72, 215}, badgeBorder, m_result.isAllPerfect ? gold : white);

    std::ostringstream scoreStr;
    scoreStr << std::setw(7) << std::setfill('0') << m_displayScore;
    r.DrawText(m_fontScore, scoreStr.str(), 0.24f, 0.552f, 0.068f, white, TextAlign::Center);
    VisualStyle::DrawGildedDivider(r, 0.08f, 0.40f, 0.652f, 0.68f);

    r.DrawRoundedGradientRect({0.065f, 0.672f, 0.165f, 0.095f}, 0.010f, {32,38,68,185}, {22,28,50,195}, {24,30,54,195}, {18,22,42,205}, 8, true);
    r.DrawRoundedRectOutlineGradient({0.065f, 0.672f, 0.165f, 0.095f}, 0.010f, {250,216,157,95}, {154,164,206,35}, 0.0012f, 8);
    r.DrawText(m_fontUI, "ACCURACY / 准确率", 0.1475f, 0.684f, 0.014f, dim, TextAlign::Center);
    r.DrawText(m_fontUI, number(m_result.accuracy, 2) + "%", 0.1475f, 0.714f, 0.032f, gold, TextAlign::Center);

    r.DrawRoundedGradientRect({0.250f, 0.672f, 0.165f, 0.095f}, 0.010f, {32,38,68,185}, {22,28,50,195}, {24,30,54,195}, {18,22,42,205}, 8, true);
    r.DrawRoundedRectOutlineGradient({0.250f, 0.672f, 0.165f, 0.095f}, 0.010f, {246,178,206,95}, {154,164,206,35}, 0.0012f, 8);
    r.DrawText(m_fontUI, "MAX COMBO / 最大连击", 0.3325f, 0.684f, 0.014f, dim, TextAlign::Center);
    r.DrawText(m_fontUI, std::to_string(m_result.maxCombo) + "×", 0.3325f, 0.714f, 0.032f, pink, TextAlign::Center);

    const std::string status = m_result.assisted ? "辅助演奏 · 不记录成绩与 PP"
        : !m_savedScore ? "保存失败 · 请检查存储空间"
        : m_newBest ? "✦  NEW BEST  /  新纪录  +" + std::to_string(m_scoreDelta)
        : "成绩已保存  ·  " + number(m_resultPP, 2) + " PP";
    VisualStyle::DrawBadgePill(r, {0.068f, 0.794f, 0.344f, 0.038f}, status, m_fontUI, 0.017f,
        m_newBest ? Color{78, 52, 86, 225} : Color{28, 34, 62, 200},
        m_newBest ? gold : Color{186, 174, 218, 110},
        m_newBest ? gold : pink);

    // ── 右上判定明细琉璃卡片 ─────────────────────────────────────────────────
    VisualStyle::DrawGlassCard(r,{0.47f,0.185f,0.49f,0.375f},
        {24,30,56,220},{14,18,36,234},{250,216,157,168},{168,158,214,68},0.018f,0.52f);
    r.DrawText(m_fontUI,"JUDGMENTS / 判定明细",0.495f,0.206f,0.021f,white);
    VisualStyle::DrawGildedDivider(r, 0.495f, 0.935f, 0.246f, 0.58f);

    const char* labels[]={"PERFECT","GREAT","GOOD","BAD","MISS"};
    const int counts[]={m_result.perfectCount,m_result.greatCount,m_result.goodCount,m_result.badCount,m_result.missCount};
    const Color colors[]={{250,218,154,255},{138,212,246,255},{154,226,188,255},{244,184,132,255},{238,124,146,255}};
    const int maxC = std::max(1, *std::max_element(std::begin(counts), std::end(counts)));
    for(int i=0;i<5;++i){
        const float ea = ElemAlpha(3 + i);
        const float y = 0.268f + i * 0.054f;
        r.DrawText(m_fontUI,labels[i],0.495f,y,0.018f,colors[i].WithAlpha(static_cast<uint8_t>(255*ea)));
        r.DrawRoundedRect({0.60f,y+0.010f,0.255f,0.011f},0.0055f,{42,46,74,static_cast<uint8_t>(145*ea)},true,8);
        if(counts[i] > 0){
            const float fillW = std::max(0.011f, 0.255f * (static_cast<float>(counts[i]) / maxC) * ea);
            const Color cL = colors[i].WithAlpha(static_cast<uint8_t>(235*ea));
            const Color cR = Color{ static_cast<uint8_t>(std::min(255, colors[i].r + 24)),
                                    static_cast<uint8_t>(std::min(255, colors[i].g + 24)),
                                    static_cast<uint8_t>(std::min(255, colors[i].b + 24)),
                                    static_cast<uint8_t>(245*ea) };
            r.DrawRoundedGradientRect({0.60f,y+0.010f,fillW,0.011f},0.0055f,cL,cR,cL,cR,8,true);
        }
        r.DrawText(m_fontUI,std::to_string(counts[i]),0.935f,y-0.002f,0.022f,white.WithAlpha(static_cast<uint8_t>(255*ea)),TextAlign::Right);
    }

    // ── 右下击打偏差直方图琉璃卡片 ───────────────────────────────────────────
    VisualStyle::DrawGlassCard(r,{0.47f,0.585f,0.49f,0.28f},
        {24,30,56,220},{14,18,36,234},{250,216,157,168},{168,158,214,68},0.018f,0.52f);
    r.DrawText(m_fontUI,"TIMING DISTRIBUTION / 击打偏差",0.495f,0.606f,0.020f,white);
    r.DrawText(m_fontUI,m_result.hitErrors.empty()?"没有有效击打数据":"均值 "+number(m_meanError,1)+" ms  ·  标准差 "+number(m_deviation,1)+" ms",0.935f,0.610f,0.016f,gold,TextAlign::Right);
    VisualStyle::DrawGildedDivider(r, 0.495f, 0.935f, 0.644f, 0.55f);

    // Perfect / Great 背景窗口色带
    r.DrawRoundedRect({0.501f,0.662f,0.430f,0.136f},0.008f,{16,20,38,145},true,8);
    r.DrawFilledRect({0.716f-0.114f,0.662f,0.228f,0.136f},{68,112,148,36});
    r.DrawFilledRect({0.716f-0.036f,0.662f,0.072f,0.136f},{250,216,157,38});

    const int peak=std::max(1,*std::max_element(m_histogram.begin(),m_histogram.end()));
    const float histAlpha = ElemAlpha(8);
    for(int i=0;i<41;++i){
        const int val = m_histogram[40-i];
        if (val <= 0) continue;
        const float h = std::max(0.004f, 0.116f * (static_cast<float>(val) / peak) * histAlpha);
        const float x = 0.503f + i * 0.0104f;
        const int dist = std::abs(i - 20);
        const Color topC = dist <= 3 ? Color{252, 224, 162, 240} : (dist <= 11 ? Color{144, 214, 246, 225} : Color{236, 152, 178, 210});
        const Color botC = dist <= 3 ? Color{216, 168, 108, 175} : (dist <= 11 ? Color{88, 148, 198, 165} : Color{168, 92, 124, 155});
        r.DrawRoundedGradientRect({x, 0.796f - h, 0.0080f, h}, 0.003f, topC, topC, botC, botC, 4, true);
    }
    r.DrawLine(0.501f,0.797f,0.931f,0.797f,{168,178,210,130},0.0012f);
    r.DrawLine(0.716f,0.662f,0.716f,0.802f,{250,216,157,165},0.0015f);
    r.DrawText(m_fontUI,"EARLY  +150 ms",0.501f,0.812f,0.015f,dim);
    r.DrawText(m_fontUI,"0 ms",0.716f,0.812f,0.015f,gold,TextAlign::Center);
    r.DrawText(m_fontUI,"−150 ms  LATE",0.931f,0.812f,0.015f,dim,TextAlign::Right);

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
            sakura::scene::TransitionType::SilkShutter, 0.38f);
        return;
    }

    if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_R){m_btnRetry->Activate();return;}
    if(m_btnReplay)m_btnReplay->HandleEvent(event);
    if (m_btnRetry) m_btnRetry->HandleEvent(event);
    if (m_btnBack)  m_btnBack ->HandleEvent(event);
}

} // namespace sakura::scene
