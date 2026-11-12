// scene_calibration.cpp — 延迟校准场景

#include "scene_calibration.h"
#include "scene_settings.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/paths.h"
#include "utils/file_io.h"
#include "core/input.h"
#include "utils/logger.h"
#include "core/theme.h"
#include "utils/easing.h"
#include "ui/visual_style.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <filesystem>

namespace sakura::scene
{
namespace {
std::string MetronomeFile() {
    const auto path=sakura::core::Paths::User("cache/calibration-v1.wav");
    if(std::filesystem::exists(path))return path;
    constexpr int rate=44100,samples=rate*60;
    std::string wav;wav.reserve(44+samples*2);
    auto u16=[&](uint16_t v){wav.push_back(static_cast<char>(v));wav.push_back(static_cast<char>(v>>8));};
    auto u32=[&](uint32_t v){u16(static_cast<uint16_t>(v));u16(static_cast<uint16_t>(v>>16));};
    wav+="RIFF";u32(36+samples*2);wav+="WAVEfmt ";u32(16);u16(1);u16(1);u32(rate);u32(rate*2);u16(2);u16(16);wav+="data";u32(samples*2);
    for(int i=0;i<samples;++i){
        const int beatSample=(i-rate)%(rate/2);
        const double t=beatSample/static_cast<double>(rate);
        const double click=i>=rate && beatSample<rate/30 ? std::sin(6.283185307*1200*t)*std::exp(-t*140)*0.65 : 0;
        u16(static_cast<uint16_t>(static_cast<int16_t>(click*32767)));
    }
    return sakura::utils::AtomicWrite(path,wav)?path:"";
}
}

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneCalibration::SceneCalibration(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── SetupButtons ──────────────────────────────────────────────────────────────

void SceneCalibration::SetupButtons()
{
    m_btnApply = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.30f, 0.72f, 0.18f, 0.055f },
        "应用", m_font, 0.026f, 0.012f);
    sakura::ui::VisualStyle::ApplyButton(m_btnApply.get(), sakura::ui::ButtonVariant::Primary);
    m_btnApply->SetEnabled(false);
    m_btnApply->SetOnClick([this]() { ApplyResult(); });

    m_btnRetry = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.52f, 0.72f, 0.18f, 0.055f },
        "重试", m_font, 0.026f, 0.012f);
    sakura::ui::VisualStyle::ApplyButton(m_btnRetry.get(), sakura::ui::ButtonVariant::Accent);
    m_btnRetry->SetOnClick([this]() { Retry(); });

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.39f, 0.90f, 0.22f, 0.055f },
        "返回设置", m_font, 0.026f, 0.012f);
    sakura::ui::VisualStyle::ApplyButton(m_btnBack.get(), sakura::ui::ButtonVariant::Secondary);
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
    sakura::audio::AudioManager::GetInstance().StopMusic();
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
    m_lastSampleBeat = -1;
    if(m_btnApply)m_btnApply->SetEnabled(false);
    auto& audio=sakura::audio::AudioManager::GetInstance();
    audio.SetPlaybackSpeed(1);
    const auto path=MetronomeFile();
    m_audioReady=!path.empty()&&audio.PlayMusic(path);
    if(!m_audioReady)sakura::ui::ToastManager::Instance().Show("无法播放校准节拍，请检查音频设备",sakura::ui::ToastType::Error,6);
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
    if(!sakura::core::Config::GetInstance().Save()){
        sakura::ui::ToastManager::Instance().Show("偏移保存失败，请检查用户目录",sakura::ui::ToastType::Error);return;
    }

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
    auto& audio=sakura::audio::AudioManager::GetInstance();
    m_totalTimeMs=static_cast<float>(audio.GetMusicPosition()*1000);
    m_beatTimer=std::fmod(m_totalTimeMs/1000.0f,BEAT_INTERVAL);
    m_pulseAnim    = std::max(0.0f, m_pulseAnim - dt * 4.0f);
    const int beat=static_cast<int>(m_totalTimeMs/500)*500;
    if(beat>=1000&&beat!=m_lastBeatTimeMs){m_lastBeatTimeMs=beat;m_pulseAnim=1;}
    if(m_audioReady&&!m_hasResult&&!audio.IsPlaying()&&!audio.IsPaused())Retry();

    if (m_btnApply)  m_btnApply->Update(dt);
    if (m_btnRetry)  m_btnRetry->Update(dt);
    if (m_btnBack)   m_btnBack->Update(dt);

}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneCalibration::OnEvent(const SDL_Event& event)
{
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST)sakura::audio::AudioManager::GetInstance().PauseMusic();
    if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)sakura::audio::AudioManager::GetInstance().ResumeMusic();
    if (m_btnApply) m_btnApply->HandleEvent(event);
    if (m_btnRetry) m_btnRetry->HandleEvent(event);
    if (m_btnBack)  m_btnBack->HandleEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_SPACE)
        {
            if(m_hasResult||!m_audioReady)return;
            const auto now=SDL_GetTicksNS();
            const int age=event.common.timestamp && event.common.timestamp<=now ? static_cast<int>(std::min<Uint64>((now-event.common.timestamp)/1000000,200)) : 0;
            const int hitTimeMs=static_cast<int>(sakura::audio::AudioManager::GetInstance().GetMusicPosition()*1000)-age;
            const int nearest=static_cast<int>(std::lround((hitTimeMs-1000)/500.0));
            const int diff=hitTimeMs-(1000+nearest*500);
            if (nearest>=4 && nearest!=m_lastSampleBeat && std::abs(diff) <= IGNORE_THRESH)
            {
                m_lastSampleBeat=nearest;
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
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    sakura::ui::VisualStyle::DrawPanel(renderer, { 0.17f, 0.115f, 0.66f, 0.760f }, false, true);

    // 顶部标题与徽章
    sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.43f, 0.024f, 0.14f, 0.026f }, "AUDIO OFFSET",
        m_font, 0.013f, { 36, 28, 56, 195 }, palette.gildedHairline, palette.accent);
    renderer.DrawText(m_font, "音频延迟校准",
        0.5f, 0.056f, 0.042f, palette.text, sakura::core::TextAlign::Center);

    renderer.DrawText(m_font, "闭眼或专注听节拍音按下空格 · 视觉脉冲仅供辅助",
        0.5f, 0.138f, 0.024f, palette.text, sakura::core::TextAlign::Center);
    renderer.DrawText(m_font, "前 4 拍热身，随后采集 20 次击打 · 建议使用日常耳机/外放设备",
        0.5f, 0.174f, 0.0185f, palette.textDim, sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.24f, 0.76f, 0.212f, 0.75f);

    // 下落式琉璃节拍轨道 + 脉冲光环
    {
        constexpr float laneX      = 0.5f;
        constexpr float laneTopY   = 0.232f;
        constexpr float judgeY     = 0.452f;
        constexpr float laneHalfW  = 0.082f;
        constexpr float noteRadius = 0.023f;

        const float beatPhase = std::clamp(m_beatTimer / BEAT_INTERVAL, 0.0f, 1.0f);
        const float fallT     = sakura::utils::EaseInSine(beatPhase);
        const float noteY     = laneTopY + (judgeY - laneTopY) * fallT;
        const float pulse     = sakura::utils::EaseOutExpo(m_pulseAnim);

        const sakura::core::NormRect trackRect{ laneX - laneHalfW, laneTopY, laneHalfW * 2.0f, judgeY - laneTopY + 0.036f };
        renderer.DrawRoundedGradientRect(trackRect, 0.012f,
            { 14, 18, 34, 210 }, { 14, 18, 34, 210 },
            { 28, 22, 48, 228 }, { 28, 22, 48, 228 });
        renderer.DrawRoundedRectOutlineGradient(trackRect, 0.012f,
            palette.gildedHairline, palette.surfaceBorder, 0.0015f);

        for (int tick = 1; tick <= 3; ++tick)
        {
            const float ty = laneTopY + (judgeY - laneTopY) * (static_cast<float>(tick) * 0.25f);
            renderer.DrawLine(laneX - laneHalfW + 0.012f, ty, laneX + laneHalfW - 0.012f, ty,
                { 160, 155, 195, 42 }, 0.001f);
        }

        // 判定环与节拍扩散波纹
        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        renderer.DrawRadialGradient(laneX, judgeY, 0.075f + 0.025f * pulse, 0.035f + 0.012f * pulse,
            { palette.primary.r, palette.primary.g, palette.primary.b, static_cast<uint8_t>(65 + 95 * pulse) },
            { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
        if (pulse > 0.02f)
        {
            const float ringR = noteRadius + 0.006f + (1.0f - pulse) * 0.026f;
            renderer.DrawRingGradient(laneX, judgeY, ringR - 0.003f, ringR + 0.004f,
                { palette.accent.r, palette.accent.g, palette.accent.b, static_cast<uint8_t>(165 * pulse) },
                { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
        }
        renderer.DrawRadialGradient(laneX, noteY, noteRadius * 1.9f, noteRadius * 1.9f,
            { 255, 182, 212, 120 }, { 255, 182, 212, 0 });
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

        renderer.DrawLine(laneX - laneHalfW - 0.022f, judgeY, laneX + laneHalfW + 0.022f, judgeY,
            { palette.accent.r, palette.accent.g, palette.accent.b, static_cast<uint8_t>(190 + 60 * pulse) },
            0.0032f + 0.0012f * pulse);
        renderer.DrawCircleOutline(laneX, judgeY, noteRadius + 0.003f, palette.gildedHairline, 0.0018f);

        // 樱珠音符
        renderer.DrawCircleFilled(laneX, noteY, noteRadius, palette.primary);
        renderer.DrawCircleFilled(laneX, noteY - noteRadius * 0.24f, noteRadius * 0.48f, { 255, 240, 248, 210 });
        renderer.DrawCircleOutline(laneX, noteY, noteRadius, { 255, 245, 250, 235 }, 0.0022f);
    }

    // 采集进度条与实时偏差散点标尺 (-150ms .. +150ms)
    const int cnt = static_cast<int>(m_samples.size());
    const float progRatio = std::clamp(static_cast<float>(cnt) / static_cast<float>(MAX_SAMPLES), 0.0f, 1.0f);
    renderer.DrawRoundedRect({ 0.31f, 0.506f, 0.38f, 0.013f }, 0.006f, { 18, 22, 38, 210 }, true);
    if (progRatio > 0.0f)
    {
        renderer.DrawRoundedGradientRect({ 0.31f, 0.506f, 0.38f * progRatio, 0.013f }, 0.006f,
            palette.primary, palette.accent, palette.primary, palette.accent);
    }
    renderer.DrawText(m_font,
        "采样进度  " + std::to_string(cnt) + " / " + std::to_string(MAX_SAMPLES),
        0.5f, 0.526f, 0.019f, palette.textDim, sakura::core::TextAlign::Center);

    {
        const sakura::core::NormRect meterRect{ 0.26f, 0.556f, 0.48f, 0.032f };
        renderer.DrawRoundedRect(meterRect, 0.008f, { 14, 18, 32, 215 }, true);
        renderer.DrawRoundedRectOutlineGradient(meterRect, 0.008f,
            { 160, 150, 195, 75 }, { 110, 120, 160, 40 }, 0.0012f);
        renderer.DrawLine(0.50f, meterRect.y + 0.004f, 0.50f, meterRect.y + meterRect.height - 0.004f,
            palette.accent, 0.0018f);
        renderer.DrawText(m_font, "EARLY -150ms", meterRect.x - 0.008f, meterRect.y + 0.007f, 0.0135f,
            palette.textMuted, sakura::core::TextAlign::Right);
        renderer.DrawText(m_font, "+150ms LATE", meterRect.x + meterRect.width + 0.008f, meterRect.y + 0.007f, 0.0135f,
            palette.textMuted, sakura::core::TextAlign::Left);

        const float cy = meterRect.y + meterRect.height * 0.5f;
        for (size_t idx = 0; idx < m_samples.size(); ++idx)
        {
            const int diffMs = m_samples[idx];
            const float norm = std::clamp(static_cast<float>(diffMs) / 150.0f, -1.0f, 1.0f);
            const float sx = 0.50f + norm * (meterRect.width * 0.46f);
            const bool isLatest = (idx + 1 == m_samples.size());
            const int absDiff = std::abs(diffMs);
            sakura::core::Color dotCol = (absDiff <= 18)
                ? palette.accent
                : ((absDiff <= 40) ? palette.primary : sakura::core::Color{ 255, 105, 120, 230 });
            dotCol.a = isLatest ? 250 : 155;
            renderer.DrawCircleFilled(sx, cy, isLatest ? 0.0065f : 0.0045f, dotCol, 20);
            if (isLatest)
                renderer.DrawCircleOutline(sx, cy, 0.0095f, { 255, 255, 255, 210 }, 0.0014f, 24);
        }
    }

    // 结果展示
    if (m_hasResult)
    {
        const std::string avgStr = std::string("建议偏移: ") +
                                   (m_resultAvg >= 0 ? "+" : "") +
                                   std::to_string(m_resultAvg) + " ms";
        const std::string stdStr = std::string("离散标准差: ±") + std::to_string(m_resultStddev) + " ms";

        std::string qualStr;
        sakura::core::Color qualColor;
        if (m_resultStddev <= 15)      { qualStr = "极佳 · 节奏稳定"; qualColor = { 110, 232, 165, 235 }; }
        else if (m_resultStddev <= 30) { qualStr = "良好 · 可直接应用"; qualColor = palette.accent; }
        else                           { qualStr = "波动较大 · 建议重测"; qualColor = { 255, 110, 120, 235 }; }

        renderer.DrawText(m_font, avgStr, 0.5f, 0.608f, 0.029f,
            palette.text, sakura::core::TextAlign::Center);
        renderer.DrawText(m_font, stdStr, 0.5f, 0.648f, 0.021f,
            palette.textDim, sakura::core::TextAlign::Center);
        sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.39f, 0.798f, 0.22f, 0.036f },
            qualStr, m_font, 0.017f, { 24, 28, 48, 210 }, qualColor, qualColor);
    }
    else
    {
        const char* statusMsg = !m_audioReady
            ? "音频设备未就绪"
            : (m_totalTimeMs < 2800.0f ? "请先聆听前 4 拍基准节拍…" : "请跟随听到的节拍按下 [Space]…");
        renderer.DrawText(m_font, statusMsg,
            0.5f, 0.625f, 0.024f, palette.accent, sakura::core::TextAlign::Center);
    }

    // 按钮
    if (m_btnApply) m_btnApply->Render(renderer);
    if (m_btnRetry) m_btnRetry->Render(renderer);
    if (m_btnBack)  m_btnBack->Render(renderer);
}

} // namespace sakura::scene
