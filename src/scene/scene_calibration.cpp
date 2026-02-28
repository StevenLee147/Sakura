// scene_calibration.cpp — 延迟校准场景

#include "scene_calibration.h"
#include "scene_settings.h"
#include "core/config.h"
#include "core/input.h"
#include "utils/logger.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneCalibration::SceneCalibration(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── SetupButtons ──────────────────────────────────────────────────────────────

void SceneCalibration::SetupButtons()
{
    sakura::ui::ButtonColors applyColors;
    applyColors.normal  = { 50, 130, 80,  220 };
    applyColors.hover   = { 70, 170, 110, 235 };
    applyColors.pressed = { 35,  95,  55, 240 };
    applyColors.text    = sakura::core::Color::White;

    sakura::ui::ButtonColors retryColors;
    retryColors.normal  = { 80, 60, 110, 220 };
    retryColors.hover   = { 110, 85, 150, 235 };
    retryColors.pressed = { 55,  40,  80, 240 };
    retryColors.text    = sakura::core::Color::White;

    sakura::ui::ButtonColors backColors;
    backColors.normal  = { 45, 45, 70,  220 };
    backColors.hover   = { 70, 65, 105, 235 };
    backColors.pressed = { 25, 25, 50,  240 };
    backColors.text    = sakura::core::Color::White;

    m_btnApply = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.30f, 0.72f, 0.18f, 0.055f },
        "应用", m_font, 0.026f, 0.012f);
    m_btnApply->SetColors(applyColors);
    m_btnApply->SetEnabled(false);
    m_btnApply->SetOnClick([this]() { ApplyResult(); });

    m_btnRetry = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.52f, 0.72f, 0.18f, 0.055f },
        "重试", m_font, 0.026f, 0.012f);
    m_btnRetry->SetColors(retryColors);
    m_btnRetry->SetOnClick([this]() { Retry(); });

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.39f, 0.90f, 0.22f, 0.055f },
        "返回设置", m_font, 0.026f, 0.012f);
    m_btnBack->SetColors(backColors);
    m_btnBack->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneSettings>(m_manager),
            TransitionType::SlideRight, 0.3f);
    });
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneCalibration::OnEnter()
{
    LOG_INFO("[SceneCalibration] 进入延迟校准");

    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_font   = rm.GetDefaultFontHandle();

    Retry();
    SetupButtons();
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneCalibration::OnExit()
{
    LOG_INFO("[SceneCalibration] 退出延迟校准");
}

// ── Retry ─────────────────────────────────────────────────────────────────────

void SceneCalibration::Retry()
{
    m_beatTimer      = 0.0f;
    m_lastBeatTimeMs = 0;
    m_totalTimeMs    = 0.0f;
    m_pulseAnim      = 0.0f;
    m_samples.clear();
    m_hasResult      = false;
    m_resultAvg      = 0;
    m_resultStddev   = 0;
}

// ── ComputeResult ─────────────────────────────────────────────────────────────

void SceneCalibration::ComputeResult()
{
    if (m_samples.empty()) return;

    // 平均值
    double sum = 0.0;
    for (int s : m_samples) sum += s;
    m_resultAvg = static_cast<int>(sum / m_samples.size());

    // 标准差
    double variance = 0.0;
    for (int s : m_samples)
    {
        double diff = s - m_resultAvg;
        variance += diff * diff;
    }
    m_resultStddev = static_cast<int>(std::sqrt(variance / m_samples.size()));

    m_hasResult = true;
    if (m_btnApply) m_btnApply->SetEnabled(true);

    LOG_INFO("[SceneCalibration] 校准结果: 平均偏差={}ms, 标准差={}ms",
             m_resultAvg, m_resultStddev);
}

// ── ApplyResult ───────────────────────────────────────────────────────────────

void SceneCalibration::ApplyResult()
{
    if (!m_hasResult) return;

    sakura::core::Config::GetInstance().Set(
        std::string(sakura::core::ConfigKeys::kAudioOffset), m_resultAvg);
    sakura::core::Config::GetInstance().Save();

    sakura::ui::ToastManager::Instance().Show(
        std::string("偏移已设置为 ") + std::to_string(m_resultAvg) + "ms",
        sakura::ui::ToastType::Success);

    LOG_INFO("[SceneCalibration] 应用偏移 {}ms", m_resultAvg);

    m_manager.SwitchScene(
        std::make_unique<SceneSettings>(m_manager),
        TransitionType::SlideRight, 0.3f);
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneCalibration::OnUpdate(float dt)
{
    m_totalTimeMs += dt * 1000.0f;
    m_beatTimer   += dt;
    m_pulseAnim    = std::max(0.0f, m_pulseAnim - dt * 4.0f);

    if (m_beatTimer >= BEAT_INTERVAL)
    {
        m_beatTimer      -= BEAT_INTERVAL;
        m_lastBeatTimeMs  = static_cast<int>(m_totalTimeMs);
        m_pulseAnim       = 1.0f;
    }

    if (m_btnApply)  m_btnApply->Update(dt);
    if (m_btnRetry)  m_btnRetry->Update(dt);
    if (m_btnBack)   m_btnBack->Update(dt);

    sakura::ui::ToastManager::Instance().Update(dt);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneCalibration::OnEvent(const SDL_Event& event)
{
    if (m_btnApply) m_btnApply->HandleEvent(event);
    if (m_btnRetry) m_btnRetry->HandleEvent(event);
    if (m_btnBack)  m_btnBack->HandleEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            // 收集偏差
            int hitTimeMs  = static_cast<int>(m_totalTimeMs);
            int lastBeat   = m_lastBeatTimeMs;
            int nextBeat   = m_lastBeatTimeMs + static_cast<int>(BEAT_INTERVAL * 1000.0f);

            // 选择最近的节拍
            int diffLast = hitTimeMs - lastBeat;
            int diffNext = nextBeat  - hitTimeMs;
            int diff     = (diffLast < diffNext) ? diffLast : -diffNext;

            if (std::abs(diff) <= IGNORE_THRESH)
            {
                m_samples.push_back(diff);
                if (static_cast<int>(m_samples.size()) > MAX_SAMPLES)
                    m_samples.pop_front();

                if (static_cast<int>(m_samples.size()) >= MAX_SAMPLES)
                    ComputeResult();

                LOG_DEBUG("[SceneCalibration] 偏差 {}ms (共 {} 次)",
                          diff, m_samples.size());
            }
            return;
        }

        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_manager.SwitchScene(
                std::make_unique<SceneSettings>(m_manager),
                TransitionType::SlideRight, 0.3f);
            return;
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneCalibration::OnRender(sakura::core::Renderer& renderer)
{
    renderer.Clear(sakura::core::Color::DarkBlue);

    // 标题
    renderer.DrawText(m_font, "延迟校准",
        0.5f, 0.06f, 0.045f, { 220, 200, 255, 230 }, sakura::core::TextAlign::Center);

    // 说明
    renderer.DrawText(m_font, "聆听节拍，在心跳位置按下 空格键",
        0.5f, 0.14f, 0.028f, { 180, 180, 200, 200 }, sakura::core::TextAlign::Center);
    renderer.DrawText(m_font, "收集 20 次后自动计算偏差",
        0.5f, 0.18f, 0.024f, { 150, 150, 170, 160 }, sakura::core::TextAlign::Center);

    // 脉冲圆圈（BPM 120）
    {
        float easedPulse = sakura::utils::EaseOutExpo(m_pulseAnim);
        float radius     = 0.06f + easedPulse * 0.015f;
        uint8_t alpha    = static_cast<uint8_t>(180 + 75 * easedPulse);
        float glow       = 0.08f + easedPulse * 0.02f;

        // 外发光
        renderer.DrawCircleOutline(0.5f, 0.45f, glow,
            { 180, 130, 255, static_cast<uint8_t>(60 * easedPulse) }, 0.003f);

        // 主圆
        renderer.DrawCircleFilled(0.5f, 0.45f, radius,
            { 160, 100, 220, alpha });

        renderer.DrawCircleOutline(0.5f, 0.45f, radius,
            { 200, 170, 255, 200 }, 0.002f);
    }

    // 进度（已收集样本数）
    int cnt = static_cast<int>(m_samples.size());
    renderer.DrawText(m_font,
        std::to_string(cnt) + " / " + std::to_string(MAX_SAMPLES),
        0.5f, 0.56f, 0.030f, { 200, 200, 220, 200 }, sakura::core::TextAlign::Center);

    // 结果展示
    if (m_hasResult)
    {
        std::string avgStr   = std::string("平均偏差: ") +
                               (m_resultAvg >= 0 ? "+" : "") +
                               std::to_string(m_resultAvg) + " ms";
        std::string stdStr   = std::string("标准差: ±") + std::to_string(m_resultStddev) + " ms";
        std::string qualStr;
        sakura::core::Color qualColor;
        if (m_resultStddev <= 15)  { qualStr = "稳定"; qualColor = { 100, 220, 130, 220 }; }
        else if (m_resultStddev <= 30) { qualStr = "一般"; qualColor = { 255, 200, 80, 220 }; }
        else                       { qualStr = "不稳定"; qualColor = { 255, 90, 90, 220 }; }

        renderer.DrawText(m_font, avgStr, 0.5f, 0.62f, 0.030f,
            { 220, 200, 255, 230 }, sakura::core::TextAlign::Center);
        renderer.DrawText(m_font, stdStr, 0.5f, 0.66f, 0.026f,
            { 180, 180, 200, 200 }, sakura::core::TextAlign::Center);
        renderer.DrawText(m_font, qualStr, 0.5f, 0.70f, 0.024f,
            qualColor, sakura::core::TextAlign::Center);
    }
    else
    {
        renderer.DrawText(m_font, "校准中...",
            0.5f, 0.62f, 0.028f, { 160, 160, 180, 160 }, sakura::core::TextAlign::Center);
    }

    // 按钮
    if (m_btnApply) m_btnApply->Render(renderer);
    if (m_btnRetry) m_btnRetry->Render(renderer);
    if (m_btnBack)  m_btnBack->Render(renderer);

    // Toast
    sakura::ui::ToastManager::Instance().Render(renderer, m_font, 0.024f);
}

} // namespace sakura::scene
