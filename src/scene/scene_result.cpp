// scene_result.cpp — 结算场景

#include "scene_result.h"
#include "scene_select.h"
#include "scene_game.h"
#include "core/resource_manager.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"
#include "utils/easing.h"

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
                         sakura::game::ChartInfo  chartInfo)
    : m_manager(mgr)
    , m_result(std::move(result))
    , m_chartInfo(std::move(chartInfo))
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
    m_elemTimer    = 0.0f;

    // ── 按钮 ──────────────────────────────────────────────────────────────────
    m_btnRetry = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{0.27f, 0.935f, 0.18f, 0.048f},
        "重 玩", m_fontUI);

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{0.55f, 0.935f, 0.18f, 0.048f},
        "返 回", m_fontUI);

    m_btnRetry->SetOnClick([this]()
    {
        // 重新构造同一谱面的 SceneGame（使用与本局相同的难度）
        m_manager.SwitchScene(
            std::make_unique<SceneGame>(m_manager, m_chartInfo,
                                        0 /* difficulty index — TODO: preserve */),
            sakura::scene::TransitionType::Fade, 0.4f);
    });

    m_btnBack->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::Fade, 0.4f);
    });

    // 停止可能残留的音乐
    sakura::audio::AudioManager::GetInstance().StopMusic();
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneResult::OnExit()
{
    LOG_INFO("[SceneResult] 退出结算场景");
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneResult::OnUpdate(float dt)
{
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

void SceneResult::OnRender(sakura::core::Renderer& renderer)
{
    // 背景
    renderer.DrawFilledRect({0.0f, 0.0f, 1.0f, 1.0f},
                            sakura::core::Color{10, 8, 22, 255});

    // ── 元素 0：标题 "RESULT" ─────────────────────────────────────────────────
    {
        float a = ElemAlpha(0);
        renderer.DrawText(m_fontUI, "RESULT",
                          0.50f, 0.04f, 0.04f,
                          sakura::core::Color{200, 200, 255,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
    }

    // ── 元素 1：评级大字 ───────────────────────────────────────────────────────
    {
        float a     = ElemAlpha(1);
        auto  col   = GradeColor(m_result.grade);
        col.a       = static_cast<uint8_t>(a * 255);
        renderer.DrawText(m_fontGrade, GradeText(m_result.grade),
                          0.50f, 0.11f, 0.12f, col,
                          sakura::core::TextAlign::Center);
    }

    // ── 元素 2：FC / AP 标记 ──────────────────────────────────────────────────
    {
        float a = ElemAlpha(2);
        if (m_result.isAllPerfect)
        {
            renderer.DrawText(m_fontUI, "★ ALL PERFECT ★",
                              0.50f, 0.27f, 0.025f,
                              sakura::core::Color{255, 220, 50,
                                  static_cast<uint8_t>(a * 255)},
                              sakura::core::TextAlign::Center);
        }
        else if (m_result.isFullCombo)
        {
            renderer.DrawText(m_fontUI, "✦ FULL COMBO ✦",
                              0.50f, 0.27f, 0.025f,
                              sakura::core::Color{100, 220, 255,
                                  static_cast<uint8_t>(a * 255)},
                              sakura::core::TextAlign::Center);
        }
    }

    // ── 元素 3：曲名 + 难度 ───────────────────────────────────────────────────
    {
        float a = ElemAlpha(3);
        std::string titleStr = m_result.chartTitle + "  [" + m_result.difficulty + "]";
        renderer.DrawText(m_fontUI, titleStr,
                          0.50f, 0.335f, 0.025f,
                          sakura::core::Color{200, 180, 255,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
    }

    // ── 元素 4：分数滚动数字 ──────────────────────────────────────────────────
    {
        float a = ElemAlpha(4);
        std::string scoreStr = std::to_string(m_displayScore);
        // 补零到 7 位
        while (scoreStr.size() < 7) scoreStr = "0" + scoreStr;
        renderer.DrawText(m_fontScore, scoreStr,
                          0.50f, 0.41f, 0.065f,
                          sakura::core::Color{255, 255, 255,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
    }

    // ── 元素 5：准确率 ────────────────────────────────────────────────────────
    {
        float a = ElemAlpha(5);
        std::ostringstream accSS;
        accSS << std::fixed << std::setprecision(2) << m_result.accuracy << "%";
        renderer.DrawText(m_fontUI, "准确率",
                          0.28f, 0.53f, 0.022f,
                          sakura::core::Color{160, 160, 200,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontUI, accSS.str(),
                          0.28f, 0.555f, 0.030f,
                          sakura::core::Color{255, 255, 255,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
    }

    // ── 元素 6：最大连击 ──────────────────────────────────────────────────────
    {
        float a = ElemAlpha(6);
        renderer.DrawText(m_fontUI, "最大连击",
                          0.72f, 0.53f, 0.022f,
                          sakura::core::Color{160, 160, 200,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontUI, std::to_string(m_result.maxCombo) + "x",
                          0.72f, 0.555f, 0.030f,
                          sakura::core::Color{255, 255, 255,
                              static_cast<uint8_t>(a * 255)},
                          sakura::core::TextAlign::Center);
    }

    // ── 元素 7：判定统计（5 行）─────────────────────────────────────────────
    {
        float a = ElemAlpha(7);
        struct JudgeRow { const char* label; int count; sakura::core::Color color; };
        JudgeRow rows[] =
        {
            { "Perfect", m_result.perfectCount, {255, 220,  80, 255} },
            { "Great",   m_result.greatCount,   {100, 220, 255, 255} },
            { "Good",    m_result.goodCount,    { 80, 200,  80, 255} },
            { "Bad",     m_result.badCount,     {220, 120,  40, 255} },
            { "Miss",    m_result.missCount,    {220,  60,  60, 255} },
        };
        for (int i = 0; i < 5; ++i)
        {
            float y = 0.62f + i * 0.038f;
            auto  c = rows[i].color;
            c.a = static_cast<uint8_t>(a * 255);
            renderer.DrawText(m_fontUI, rows[i].label,
                              0.38f, y, 0.022f, c, sakura::core::TextAlign::Right);
            renderer.DrawText(m_fontUI, std::to_string(rows[i].count),
                              0.62f, y, 0.022f, c, sakura::core::TextAlign::Left);
        }
    }

    // ── 元素 8：偏差分布图（横轴 ±150 ms）─────────────────────────────────
    {
        float a = ElemAlpha(8);
        if (a > 0.0f && !m_result.hitErrors.empty())
        {
            constexpr float CHART_CX = 0.50f;
            constexpr float CHART_Y  = 0.845f;
            constexpr float CHART_W  = 0.60f;
            constexpr float CHART_H  = 0.04f;
            constexpr float MAX_ERR  = 150.0f;

            // 背景轨道
            renderer.DrawFilledRect(
                { CHART_CX - CHART_W * 0.5f, CHART_Y,
                  CHART_W, CHART_H },
                sakura::core::Color{40, 40, 70,
                    static_cast<uint8_t>(a * 200)});

            // 中心线（0 ms）
            renderer.DrawLine(
                CHART_CX, CHART_Y,
                CHART_CX, CHART_Y + CHART_H,
                sakura::core::Color{120, 120, 200,
                    static_cast<uint8_t>(a * 180)},
                0.0015f);

            // 每个音符偏差点
            for (int err : m_result.hitErrors)
            {
                float normX = static_cast<float>(err) / MAX_ERR * 0.5f;
                normX = std::clamp(normX, -0.5f, 0.5f);
                float px = CHART_CX + normX * CHART_W;
                float col_t = std::abs(normX);
                uint8_t r = static_cast<uint8_t>(80  + col_t * 175);
                uint8_t g = static_cast<uint8_t>(200 - col_t * 140);
                renderer.DrawCircleFilled(px,
                                          CHART_Y + CHART_H * 0.5f,
                                          0.003f,
                                          sakura::core::Color{r, g, 100,
                                              static_cast<uint8_t>(a * 200)},
                                          16);
            }

            // 轴标签
            renderer.DrawText(m_fontUI, "-150ms",
                              CHART_CX - CHART_W * 0.5f - 0.01f,
                              CHART_Y + CHART_H * 0.5f - 0.01f,
                              0.016f,
                              sakura::core::Color{150, 150, 180,
                                  static_cast<uint8_t>(a * 200)},
                              sakura::core::TextAlign::Right);
            renderer.DrawText(m_fontUI, "+150ms",
                              CHART_CX + CHART_W * 0.5f + 0.01f,
                              CHART_Y + CHART_H * 0.5f - 0.01f,
                              0.016f,
                              sakura::core::Color{150, 150, 180,
                                  static_cast<uint8_t>(a * 200)},
                              sakura::core::TextAlign::Left);
        }
    }

    // ── 元素 9：按钮 ──────────────────────────────────────────────────────────
    if (ElemAlpha(9) > 0.0f)
    {
        if (m_btnRetry) m_btnRetry->Render(renderer);
        if (m_btnBack)  m_btnBack ->Render(renderer);
    }
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneResult::OnEvent(const SDL_Event& event)
{
    // ESC → 返回选歌（与"返回"按钮相同行为）
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            sakura::scene::TransitionType::Fade, 0.4f);
        return;
    }

    if (m_btnRetry) m_btnRetry->HandleEvent(event);
    if (m_btnBack)  m_btnBack ->HandleEvent(event);
}

} // namespace sakura::scene
