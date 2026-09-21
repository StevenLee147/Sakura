// scene_calibration.cpp — 延迟校准场景

#include "scene_calibration.h"
#include "scene_settings.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/paths.h"
#include "utils/file_io.h"
#include "core/input.h"
#include "utils/logger.h"
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
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    sakura::ui::VisualStyle::DrawPanel(renderer, { 0.18f, 0.11f, 0.64f, 0.76f }, false, true);

    // 标题
    renderer.DrawText(m_font, "延迟校准",
        0.5f, 0.06f, 0.045f, { 220, 200, 255, 230 }, sakura::core::TextAlign::Center);

    // 说明
    renderer.DrawText(m_font, "跟随听到的节拍按空格，动画仅作参考",
        0.5f, 0.14f, 0.028f, { 180, 180, 200, 200 }, sakura::core::TextAlign::Center);
    renderer.DrawText(m_font, "先听 4 拍，再收集 20 次 · 请使用实际游玩的音频设备",
        0.5f, 0.18f, 0.024f, { 150, 150, 170, 160 }, sakura::core::TextAlign::Center);

    // 下落式校准动画（类似音游下落判定观察）
    {
        constexpr float laneX      = 0.5f;
        constexpr float laneTopY   = 0.26f;
        constexpr float judgeY     = 0.48f;
        constexpr float laneHalfW  = 0.08f;
        constexpr float noteRadius = 0.026f;

        float beatPhase = std::clamp(m_beatTimer / BEAT_INTERVAL, 0.0f, 1.0f);
        float fallT     = sakura::utils::EaseInSine(beatPhase);
        float noteY     = laneTopY + (judgeY - laneTopY) * fallT;
        float pulse     = sakura::utils::EaseOutExpo(m_pulseAnim);

        renderer.DrawFilledRect(
            sakura::core::NormRect{ laneX - laneHalfW, laneTopY, laneHalfW * 2.0f, judgeY - laneTopY },
            { 40, 40, 70, 90 });
        renderer.DrawRectOutline(
            sakura::core::NormRect{ laneX - laneHalfW, laneTopY, laneHalfW * 2.0f, judgeY - laneTopY },
            { 110, 100, 150, 150 }, 0.0018f);

        renderer.DrawLine(laneX - laneHalfW - 0.02f, judgeY, laneX + laneHalfW + 0.02f, judgeY,
            { 220, 180, 255, static_cast<uint8_t>(180 + 60 * pulse) }, 0.005f + 0.001f * pulse);

        renderer.DrawCircleFilled(laneX, noteY, noteRadius, { 175, 120, 245, 225 });
        renderer.DrawCircleOutline(laneX, noteY, noteRadius, { 235, 210, 255, 220 }, 0.0025f);
        renderer.DrawCircleOutline(
            laneX, judgeY, noteRadius + 0.006f + pulse * 0.005f,
            { 190, 150, 255, static_cast<uint8_t>(80 * pulse) }, 0.002f);
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
        renderer.DrawText(m_font, qualStr, 0.5f, 0.805f, 0.024f,
            qualColor, sakura::core::TextAlign::Center);
    }
    else
    {
        renderer.DrawText(m_font, !m_audioReady?"音频未就绪":m_totalTimeMs<2800?"先聆听节拍…":"校准中…",
            0.5f, 0.62f, 0.028f, { 160, 160, 180, 160 }, sakura::core::TextAlign::Center);
    }

    // 按钮
    if (m_btnApply) m_btnApply->Render(renderer);
    if (m_btnRetry) m_btnRetry->Render(renderer);
    if (m_btnBack)  m_btnBack->Render(renderer);

    // Toast

}

} // namespace sakura::scene
