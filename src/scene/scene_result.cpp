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

    m_showDetail = false;
    m_btnDetail = std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.37f,0.913f,0.18f,0.054f},"详细分析 / Tab",m_fontUI,0.022f);
    sakura::ui::VisualStyle::ApplyButton(m_btnDetail.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnDetail->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnDetail->SetOnClick([this]{ m_showDetail = true; });

    m_btnCloseDetail = std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.80f,0.062f,0.14f,0.044f},"关闭报告 / Tab",m_fontUI,0.019f);
    sakura::ui::VisualStyle::ApplyButton(m_btnCloseDetail.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnCloseDetail->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnCloseDetail->SetOnClick([this]{ m_showDetail = false; });

    // 计算 4 分段准确率与最薄弱区间
    int maxSampleMs = static_cast<int>(std::max(4.0, m_result.playTimeSeconds) * 1000.0);
    for (const auto& s : m_result.samples) maxSampleMs = std::max(maxSampleMs, s.timeMs + 500);
    const int qSpan = std::max(1000, maxSampleMs / 4);
    std::array<float, 4> wSum{ 0.f, 0.f, 0.f, 0.f };
    std::array<int, 4>   wCnt{ 0, 0, 0, 0 };
    auto judgeW = [](sakura::game::JudgeResult r) -> float {
        switch (r) {
            case sakura::game::JudgeResult::Perfect: return 100.f;
            case sakura::game::JudgeResult::Great:   return 70.f;
            case sakura::game::JudgeResult::Good:    return 40.f;
            case sakura::game::JudgeResult::Bad:     return 10.f;
            default: return 0.f;
        }
    };
    for (const auto& s : m_result.samples) {
        const int q = std::clamp(s.timeMs / qSpan, 0, 3);
        wSum[q] += judgeW(s.result);
        ++wCnt[q];
    }
    m_weakestSection = 0;
    float lowestAcc = 101.f;
    for (int q = 0; q < 4; ++q) {
        m_sectionAcc[q] = wCnt[q] > 0 ? (wSum[q] / static_cast<float>(wCnt[q])) : m_result.accuracy;
        if (wCnt[q] > 0 && m_sectionAcc[q] < lowestAcc) {
            lowestAcc = m_sectionAcc[q];
            m_weakestSection = q;
        }
    }
    m_weakStartMs = std::max(0, m_weakestSection * qSpan - 1000);
    m_weakEndMs   = (m_weakestSection + 1) * qSpan + 1000;
    m_btnPracticeWeak = std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.78f,0.858f,0.16f,0.044f},"练习薄弱段",m_fontUI,0.019f);
    sakura::ui::VisualStyle::ApplyButton(m_btnPracticeWeak.get(), sakura::ui::ButtonVariant::Accent);
    m_btnPracticeWeak->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnPracticeWeak->SetOnClick([this]{
        auto opts = m_options;
        opts.mode    = sakura::game::PlayMode::Practice;
        opts.startMs = m_weakStartMs;
        opts.endMs   = m_weakEndMs;
        opts.loop    = true;
        m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager, m_chartInfo, m_result.difficultyIndex, opts),
                              sakura::scene::TransitionType::SakuraSweep, 0.40f);
    });

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

    // ── 在线排行榜成绩自动异步上报 ─────────────────────────────────────────────
    *m_alive = true;
    m_onlineRankReady = false;
    if (m_result.assisted)
    {
        m_onlineStatusText = "ONLINE · 辅助模式不参与全球排名";
    }
    else
    {
        auto& api = sakura::net::APIClient::GetInstance();
        m_onlineStatusText = api.IsAuthenticated() ? "ONLINE · 正在同步全球排行榜..." : "ONLINE · 未登录（成绩已缓存待同步）";
        auto alive = m_alive;
        api.SubmitScoreAsync(m_result, [this, alive](sakura::net::ScoreSubmitResult sub)
        {
            if (!*alive) return;
            if (sub.ok)
            {
                m_onlineRankReady = true;
                std::ostringstream oss;
                oss << "✦ 全球排行 #" << sub.rank << "  ·  " << std::fixed << std::setprecision(1) << sub.pp << " PP";
                if (sub.newBest) oss << " (PB)";
                m_onlineStatusText = oss.str();
            }
            else if (sub.queuedOffline)
            {
                m_onlineStatusText = "○ 离线已缓存 · 联网登录后自动补传";
            }
            else
            {
                m_onlineStatusText = "○ 未计入全球榜：" + sub.error;
            }
        });
    }

    // ── 樱花飘落持续发射器 ────────────────────────────────────────────────────
    m_particles.Clear();
    auto petalCfg = sakura::effects::ParticlePresets::SakuraPetal();
    m_sakuraPetalEmitter = m_particles.EmitContinuous(0.5f, -0.02f, 4.0f, petalCfg);
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneResult::OnExit()
{
    *m_alive = false;
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


    if (m_showDetail)
    {
        if (m_btnCloseDetail)  m_btnCloseDetail->Update(dt);
        if (m_btnPracticeWeak) m_btnPracticeWeak->Update(dt);
        return;
    }
    if (m_btnDetail) m_btnDetail->Update(dt);
    if (m_btnReplay) m_btnReplay->Update(dt);
    if (m_btnRetry)  m_btnRetry->Update(dt);
    if (m_btnBack)   m_btnBack ->Update(dt);
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
    VisualStyle::DrawBadgePill(r, {0.068f, 0.780f, 0.344f, 0.036f}, status, m_fontUI, 0.0165f,
        m_newBest ? Color{78, 52, 86, 225} : Color{28, 34, 62, 200},
        m_newBest ? gold : Color{186, 174, 218, 110},
        m_newBest ? gold : pink);
    if (!m_onlineStatusText.empty())
    {
        VisualStyle::DrawTextFit(r, m_fontUI, m_onlineStatusText, 0.074f, 0.825f, 0.0155f, 0.332f,
            m_onlineRankReady ? gold : dim);
    }

    // ── 右上判定明细琉璃卡片 ─────────────────────────────────────────────────
    VisualStyle::DrawGlassCard(r,{0.47f,0.185f,0.49f,0.375f},
        {24,30,56,220},{14,18,36,234},{250,216,157,168},{168,158,214,68},0.018f,0.52f);
    r.DrawText(m_fontUI,"JUDGMENTS / 判定明细",0.495f,0.206f,0.021f,white);
    if (m_result.synergyCount > 0 || m_result.feverCount > 0 || m_result.thumbHitCount > 0)
    {
        const std::string synSummary = "连携 " + std::to_string(m_result.synergyCount)
            + "  ·  横梁 " + std::to_string(m_result.thumbHitCount)
            + "  ·  满开 " + std::to_string(m_result.feverCount);
        r.DrawText(m_fontUI, synSummary, 0.935f, 0.210f, 0.0155f, gold, TextAlign::Right);
    }
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

    if (m_btnDetail) m_btnDetail->Render(r);
    m_btnBack->Render(r);m_btnReplay->Render(r);m_btnRetry->Render(r);
    m_particles.Render(r);
    if (m_showDetail) RenderDetailOverlay(r);
}

void SceneResult::RenderDetailOverlay(sakura::core::Renderer& r)
{
    using namespace sakura::core;
    using namespace sakura::ui;
    auto fmt = [](double v, int p = 1) { std::ostringstream s; s << std::fixed << std::setprecision(p) << v; return s.str(); };
    const Color gold{250, 216, 157, 255}, pink{246, 178, 206, 255}, cyan{138, 218, 248, 255};
    const Color white{248, 244, 252, 255}, dim{170, 178, 208, 255};

    VisualStyle::DrawScrim(r, 0.78f);
    VisualStyle::DrawGlassCard(r, {0.035f, 0.045f, 0.930f, 0.910f},
        {20, 24, 46, 244}, {12, 16, 32, 248}, {250, 216, 157, 195}, {168, 158, 214, 85}, 0.018f, 0.55f);

    r.DrawText(m_fontUI, "PERFORMANCE TELEMETRY / 深度演奏分析报告", 0.060f, 0.064f, 0.025f, white);
    const double ur = m_deviation * 10.0;
    r.DrawText(m_fontUI, "Unstable Rate (UR)  " + fmt(ur, 1) + "   ·   均值偏差 " + fmt(m_meanError, 1) + " ms",
        0.520f, 0.070f, 0.018f, gold);
    if (m_btnCloseDetail) m_btnCloseDetail->Render(r);
    VisualStyle::DrawGildedDivider(r, 0.055f, 0.945f, 0.116f, 0.72f);

    int totalMs = std::max(4000, static_cast<int>(m_result.playTimeSeconds * 1000.0));
    for (const auto& s : m_result.samples) totalMs = std::max(totalMs, s.timeMs + 200);

    // 1. Top-Left: Timing Scatter Plot
    VisualStyle::DrawGlassCard(r, {0.055f, 0.132f, 0.435f, 0.365f},
        {16, 20, 38, 220}, {12, 15, 30, 232}, {250, 216, 157, 110}, {130, 142, 188, 48}, 0.012f, 0.35f);
    r.DrawText(m_fontUI, "TIMING SCATTER / 全曲时间散点 (±150ms)", 0.072f, 0.146f, 0.017f, white);
    const float sx0 = 0.075f, sxW = 0.395f, syMid = 0.325f, syHalf = 0.130f;
    r.DrawFilledRect({sx0, syMid - syHalf * (50.f / 150.f), sxW, syHalf * (100.f / 150.f)}, {72, 134, 178, 30});
    r.DrawFilledRect({sx0, syMid - syHalf * (25.f / 150.f), sxW, syHalf * (50.f / 150.f)}, {250, 216, 157, 34});
    r.DrawLine(sx0, syMid, sx0 + sxW, syMid, {250, 216, 157, 150}, 0.0013f);
    for (const auto& s : m_result.samples)
    {
        if (!s.timed && s.result != sakura::game::JudgeResult::Miss) continue;
        const float px = sx0 + std::clamp(static_cast<float>(s.timeMs) / static_cast<float>(totalMs), 0.f, 1.f) * sxW;
        if (s.result == sakura::game::JudgeResult::Miss)
        {
            r.DrawCircleFilled(px, syMid + syHalf * 0.94f, 0.0026f, {242, 106, 132, 225}, 8);
            continue;
        }
        const float normErr = std::clamp(static_cast<float>(s.hitErrorMs) / 150.f, -1.f, 1.f);
        const float py = syMid - normErr * syHalf;
        const int absE = std::abs(s.hitErrorMs);
        const Color dotC = absE <= 25 ? gold : (s.hitErrorMs > 0 ? cyan : pink);
        r.DrawCircleFilled(px, py, 0.0023f, dotC.WithAlpha(210), 8);
    }
    r.DrawText(m_fontUI, "EARLY +150", sx0, 0.178f, 0.013f, cyan);
    r.DrawText(m_fontUI, "LATE -150", sx0, 0.462f, 0.013f, pink);

    // 2. Top-Right: Combo Curve & Judgment Donut Ring
    VisualStyle::DrawGlassCard(r, {0.510f, 0.132f, 0.435f, 0.365f},
        {16, 20, 38, 220}, {12, 15, 30, 232}, {250, 216, 157, 110}, {130, 142, 188, 48}, 0.012f, 0.35f);
    r.DrawText(m_fontUI, "COMBO & RATIO / 连击曲线与判定环", 0.528f, 0.146f, 0.017f, white);
    const float cx0 = 0.528f, cxW = 0.255f, cyBot = 0.455f, cyH = 0.255f;
    r.DrawLine(cx0, cyBot, cx0 + cxW, cyBot, {150, 160, 198, 110}, 0.0011f);
    int maxComboDenom = std::max(1, m_result.maxCombo);
    for (const auto& s : m_result.samples) maxComboDenom = std::max(maxComboDenom, s.comboAfter);
    float prevX = cx0, prevY = cyBot;
    for (std::size_t i = 0; i < m_result.samples.size(); ++i)
    {
        const auto& s = m_result.samples[i];
        const float x = cx0 + std::clamp(static_cast<float>(s.timeMs) / static_cast<float>(totalMs), 0.f, 1.f) * cxW;
        const float y = cyBot - std::clamp(static_cast<float>(s.comboAfter) / static_cast<float>(maxComboDenom), 0.f, 1.f) * cyH;
        if (i > 0) r.DrawLine(prevX, prevY, x, y, gold.WithAlpha(210), 0.0018f);
        if (s.result == sakura::game::JudgeResult::Miss || s.result == sakura::game::JudgeResult::Bad)
            r.DrawCircleFilled(x, cyBot - 0.006f, 0.0035f, {244, 98, 126, 240}, 10);
        prevX = x; prevY = y;
    }
    const int jCounts[5] = { m_result.perfectCount, m_result.greatCount, m_result.goodCount, m_result.badCount, m_result.missCount };
    const Color jCols[5] = { gold, cyan, {154,226,188,255}, {244,184,132,255}, {238,124,146,255} };
    const int jSum = std::max(1, jCounts[0] + jCounts[1] + jCounts[2] + jCounts[3] + jCounts[4]);
    float deg = -90.f;
    for (int i = 0; i < 5; ++i)
    {
        if (jCounts[i] <= 0) continue;
        const float sweep = 360.f * static_cast<float>(jCounts[i]) / static_cast<float>(jSum);
        r.DrawArc(0.865f, 0.320f, 0.056f, deg, deg + std::max(2.f, sweep - 1.5f), jCols[i], 0.0085f, 28);
        deg += sweep;
    }
    r.DrawText(m_fontUI, fmt(m_result.accuracy, 1) + "%", 0.865f, 0.308f, 0.019f, white, TextAlign::Center);

    // 3. Bottom-Left: Dual-Zone & 5-Lane Breakdown
    VisualStyle::DrawGlassCard(r, {0.055f, 0.518f, 0.435f, 0.405f},
        {16, 20, 38, 220}, {12, 15, 30, 232}, {250, 216, 157, 110}, {130, 142, 188, 48}, 0.012f, 0.35f);
    r.DrawText(m_fontUI, "ZONE & 5-LANE / 轨道与双区准度", 0.072f, 0.532f, 0.017f, white);
    std::array<float, 7> bSum{}; std::array<int, 7> bCnt{};
    auto wOf = [](sakura::game::JudgeResult jr) {
        return jr == sakura::game::JudgeResult::Perfect ? 100.f : jr == sakura::game::JudgeResult::Great ? 70.f :
               jr == sakura::game::JudgeResult::Good ? 40.f : jr == sakura::game::JudgeResult::Bad ? 10.f : 0.f;
    };
    for (const auto& s : m_result.samples)
    {
        const float w = wOf(s.result);
        if (s.isKeyboard) { const int idx = std::clamp(s.lane, 0, 4); bSum[idx] += w; ++bCnt[idx]; }
        else { bSum[5] += w; ++bCnt[5]; }
        if (s.isSynergy) { bSum[6] += w; ++bCnt[6]; }
    }
    const char* bNames[7] = { "轨道 1 (A)", "轨道 2 (S)", "轨道 3 (D)", "轨道 4 (F)", "拇指轨 (SP)", "鼠标区 (MS)", "双区协同" };
    for (int i = 0; i < 7; ++i)
    {
        const float ry = 0.576f + i * 0.046f;
        const float acc = bCnt[i] > 0 ? (bSum[i] / static_cast<float>(bCnt[i])) : 100.f;
        r.DrawText(m_fontUI, bNames[i], 0.072f, ry, 0.0155f, dim);
        r.DrawRoundedRect({0.195f, ry + 0.007f, 0.205f, 0.011f}, 0.005f, {36, 42, 68, 165}, true, 6);
        if (bCnt[i] > 0)
        {
            const Color barC = i == 4 || i == 6 ? gold : (i == 5 ? cyan : pink);
            r.DrawRoundedGradientRect({0.195f, ry + 0.007f, std::max(0.010f, 0.205f * acc / 100.f), 0.011f},
                0.005f, barC, gold, barC, gold, 6, true);
        }
        r.DrawText(m_fontUI, bCnt[i] > 0 ? (fmt(acc, 1) + "%") : "—", 0.472f, ry, 0.0155f, white, TextAlign::Right);
    }

    // 4. Bottom-Right: Quartile Section Accuracy & Practice Trigger
    VisualStyle::DrawGlassCard(r, {0.510f, 0.518f, 0.435f, 0.405f},
        {16, 20, 38, 220}, {12, 15, 30, 232}, {250, 216, 157, 110}, {130, 142, 188, 48}, 0.012f, 0.35f);
    r.DrawText(m_fontUI, "SECTION STABILITY / 四分段稳定性诊断", 0.528f, 0.532f, 0.017f, white);
    const char* qNames[4] = { "第一段  0%–25%", "第二段  25%–50%", "第三段  50%–75%", "第四段  75%–100%" };
    for (int q = 0; q < 4; ++q)
    {
        const float qy = 0.584f + q * 0.064f;
        const bool weak = (q == m_weakestSection);
        r.DrawText(m_fontUI, qNames[q], 0.528f, qy, 0.0165f, weak ? gold : dim);
        r.DrawRoundedRect({0.675f, qy + 0.008f, 0.180f, 0.013f}, 0.006f, {36, 42, 68, 165}, true, 6);
        const Color qCol = weak ? Color{246, 152, 132, 245} : cyan;
        r.DrawRoundedGradientRect({0.675f, qy + 0.008f, std::max(0.010f, 0.180f * m_sectionAcc[q] / 100.f), 0.013f},
            0.006f, qCol, gold, qCol, gold, 6, true);
        r.DrawText(m_fontUI, fmt(m_sectionAcc[q], 1) + "%", 0.928f, qy, 0.0165f, weak ? gold : white, TextAlign::Right);
    }
    r.DrawText(m_fontUI, "薄弱区间：" + std::to_string(m_weakStartMs / 1000) + "s – " + std::to_string(m_weakEndMs / 1000) + "s",
        0.528f, 0.868f, 0.017f, gold);
    if (m_btnPracticeWeak) m_btnPracticeWeak->Render(r);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────
void SceneResult::OnEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_TAB)
    {
        m_showDetail = !m_showDetail;
        return;
    }
    if (m_showDetail)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_showDetail = false;
            return;
        }
        if (m_btnCloseDetail && m_btnCloseDetail->HandleEvent(event)) return;
        if (m_btnPracticeWeak && m_btnPracticeWeak->HandleEvent(event)) return;
        return;
    }
    // ESC / Enter → 返回选歌
    if (event.type == SDL_EVENT_KEY_DOWN &&
        (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_RETURN))
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::SilkShutter, 0.38f);
        return;
    }

    if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_R){m_btnRetry->Activate();return;}
    if(m_btnDetail) m_btnDetail->HandleEvent(event);
    if(m_btnReplay) m_btnReplay->HandleEvent(event);
    if (m_btnRetry) m_btnRetry->HandleEvent(event);
    if (m_btnBack)  m_btnBack ->HandleEvent(event);
}

} // namespace sakura::scene
