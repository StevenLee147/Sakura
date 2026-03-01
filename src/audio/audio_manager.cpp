// audio_manager.cpp — 音频管理器实现
// 使用 miniaudio 高层 ma_engine API

// miniaudio 头文件（实现已在 resource_manager.cpp 中定义）
// 这里只做声明引用，不重复定义
#include <miniaudio.h>

#include "audio_manager.h"
#include "sfx_generator.h"
#include "core/config.h"
#include "utils/logger.h"

#include <cstring>
#include <filesystem>

namespace sakura::audio
{

// ── 析构 ──────────────────────────────────────────────────────────────────────

AudioManager::~AudioManager()
{
    if (m_initialized)
    {
        Shutdown();
    }
}

// ── 单例 ──────────────────────────────────────────────────────────────────────

AudioManager& AudioManager::GetInstance()
{
    static AudioManager instance;
    return instance;
}

// ── 初始化 ────────────────────────────────────────────────────────────────────

bool AudioManager::Initialize()
{
    if (m_initialized)
    {
        LOG_WARN("AudioManager 已经初始化");
        return true;
    }

    // 从 Config 读取音量设置
    auto& cfg = sakura::core::Config::GetInstance();
    m_masterVolume = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kMasterVolume), 1.0f);
    m_musicVolume  = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kMusicVolume),  0.8f);
    m_sfxVolume    = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kSfxVolume),    0.8f);

    // 创建 ma_engine
    m_engine = new ma_engine();
    ma_engine_config engineConfig = ma_engine_config_init();
    // 使用默认设备和格式

    ma_result result = ma_engine_init(&engineConfig, m_engine);
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_engine_init 失败: error={}", static_cast<int>(result));
        delete m_engine;
        m_engine = nullptr;
        return false;
    }

    // 设置主音量
    ma_engine_set_volume(m_engine, m_masterVolume);

    m_initialized = true;
    LOG_INFO("AudioManager 初始化成功，主音量={:.2f}，音乐音量={:.2f}，音效音量={:.2f}",
             m_masterVolume, m_musicVolume, m_sfxVolume);
    return true;
}

// ── 关闭 ──────────────────────────────────────────────────────────────────────

void AudioManager::Shutdown()
{
    if (!m_initialized) return;

    // 停止并释放当前音乐
    if (m_music)
    {
        ma_sound_stop(m_music);
        ma_sound_uninit(m_music);
        delete m_music;
        m_music = nullptr;
    }

    // 释放引擎
    if (m_engine)
    {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }

    m_initialized  = false;
    m_musicPaused  = false;
    m_fadingOut    = false;
    LOG_INFO("AudioManager 已关闭");
}

// ── 背景音乐 ──────────────────────────────────────────────────────────────────

bool AudioManager::PlayMusic(const std::string& path, int loops)
{
    if (!m_initialized || !m_engine)
    {
        LOG_ERROR("AudioManager::PlayMusic: AudioManager 未初始化");
        return false;
    }

    if (!std::filesystem::exists(path))
    {
        LOG_ERROR("音乐文件不存在: {}", path);
        return false;
    }

    // 停止当前音乐
    StopMusic();

    // 创建新的流式 ma_sound
    m_music = new ma_sound();
    ma_uint32 flags = MA_SOUND_FLAG_STREAM;  // 流式加载，适合长音乐

    ma_result result = ma_sound_init_from_file(
        m_engine,
        path.c_str(),
        flags,
        nullptr,    // pGroup（无）
        nullptr,    // pFence（无）
        m_music
    );

    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_sound_init_from_file 失败 [{}]: error={}", path, static_cast<int>(result));
        delete m_music;
        m_music = nullptr;
        return false;
    }

    m_musicPath = path;

    // 设置循环
    bool shouldLoop = (loops == -1) || (loops > 0);
    ma_sound_set_looping(m_music, shouldLoop ? MA_TRUE : MA_FALSE);

    // 设置音量
    ApplyMusicVolume();

    // 设置播放速度
    ma_sound_set_pitch(m_music, m_playbackSpeed);

    // 开始播放
    result = ma_sound_start(m_music);
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_sound_start 失败: error={}", static_cast<int>(result));
        return false;
    }

    m_musicPaused = false;
    m_fadingOut   = false;
    LOG_INFO("开始播放音乐: {} (loop={})", path, loops);
    return true;
}

bool AudioManager::PlayMusicFromHandle(sakura::core::MusicHandle /*handle*/, int loops)
{
    // ResourceManager 存储的是 ma_decoder，不能直接用于 ma_engine 播放
    // 直接通过文件路径播放更可靠；这个接口预留
    LOG_WARN("PlayMusicFromHandle: 暂不支持（请使用 PlayMusic(path)）");
    (void)loops;
    return false;
}

void AudioManager::PauseMusic()
{
    if (!m_music || m_musicPaused) return;

    ma_sound_stop(m_music);
    m_musicPaused = true;
    LOG_DEBUG("音乐已暂停");
}

void AudioManager::ResumeMusic()
{
    if (!m_music || !m_musicPaused) return;

    ma_sound_start(m_music);
    m_musicPaused = false;
    LOG_DEBUG("音乐已恢复");
}

void AudioManager::StopMusic()
{
    if (!m_music) return;

    ma_sound_stop(m_music);
    ma_sound_uninit(m_music);
    delete m_music;
    m_music       = nullptr;
    m_musicPath   = "";
    m_musicPaused = false;
    m_fadingOut   = false;
    LOG_DEBUG("音乐已停止");
}

void AudioManager::FadeOutMusic(int ms)
{
    if (!m_music || !IsPlaying()) return;

    // 使用 miniaudio 内置淡出 API
    // ma_sound_set_fade_in_milliseconds(sound, startVol, endVol, durationMs)
    float currentVol = m_musicVolume * m_masterVolume;
    ma_sound_set_fade_in_milliseconds(m_music, currentVol, 0.0f,
                                     static_cast<ma_uint64>(ms));

    m_fadingOut    = true;
    m_fadeDuration = static_cast<float>(ms) / 1000.0f;
    m_fadeTimer    = 0.0f;
    LOG_DEBUG("音乐淡出 {}ms", ms);
}

bool AudioManager::SetMusicPosition(double seconds)
{
    if (!m_music) return false;

    // 获取采样率以计算帧位置
    ma_uint32 sampleRate = ma_engine_get_sample_rate(m_engine);
    ma_uint64 frame = static_cast<ma_uint64>(seconds * sampleRate);

    ma_result result = ma_sound_seek_to_pcm_frame(m_music, frame);
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("SetMusicPosition 失败: error={}", static_cast<int>(result));
        return false;
    }
    return true;
}

double AudioManager::GetMusicPosition() const
{
    if (!m_music) return 0.0;

    float cursor = 0.0f;
    ma_result result = ma_sound_get_cursor_in_seconds(m_music, &cursor);
    if (result != MA_SUCCESS) return 0.0;
    return static_cast<double>(cursor);
}

double AudioManager::GetMusicDuration() const
{
    if (!m_music) return -1.0;

    float length = 0.0f;
    ma_result result = ma_sound_get_length_in_seconds(m_music, &length);
    if (result != MA_SUCCESS) return -1.0;
    return static_cast<double>(length);
}

bool AudioManager::IsPlaying() const
{
    if (!m_music) return false;
    return ma_sound_is_playing(m_music) == MA_TRUE && !m_musicPaused;
}

bool AudioManager::IsPaused() const
{
    return m_music != nullptr && m_musicPaused;
}

// ── 音效 ──────────────────────────────────────────────────────────────────────

void AudioManager::PlaySFX(const std::string& path)
{
    if (!m_initialized || !m_engine) return;

    if (!std::filesystem::exists(path))
    {
        LOG_WARN("音效文件不存在: {}", path);
        return;
    }

    // 使用 ma_engine_play_sound 实现 fire-and-forget 音效播放
    // 无需手动管理 ma_sound 生命周期
    ma_result result = ma_engine_play_sound(m_engine, path.c_str(), nullptr);
    if (result != MA_SUCCESS)
    {
        LOG_WARN("PlaySFX 失败 [{}]: error={}", path, static_cast<int>(result));
    }
}

void AudioManager::PlaySFXFromHandle(sakura::core::SoundHandle /*handle*/)
{
    // ResourceManager 存储的是 ma_decoder，不能直接用于 ma_engine 播放
    // 暂不支持，需要文件路径
    LOG_WARN("PlaySFXFromHandle: 暂不支持（请使用 PlaySFX(path)）");
}

// ── 音量控制 ──────────────────────────────────────────────────────────────────

void AudioManager::SetMasterVolume(float vol)
{
    m_masterVolume = std::max(0.0f, std::min(1.0f, vol));
    if (m_engine)
    {
        ma_engine_set_volume(m_engine, m_masterVolume);
    }
    ApplyMusicVolume();
}

void AudioManager::SetMusicVolume(float vol)
{
    m_musicVolume = std::max(0.0f, std::min(1.0f, vol));
    ApplyMusicVolume();
}

void AudioManager::SetSFXVolume(float vol)
{
    m_sfxVolume = std::max(0.0f, std::min(1.0f, vol));
    // SFX 通过 engine 音量控制，这里仅记录（fire-and-forget 音效不单独控制）
}

void AudioManager::ApplyMusicVolume()
{
    if (m_music)
    {
        ma_sound_set_volume(m_music, m_musicVolume);
    }
}

// ── 播放速度 ──────────────────────────────────────────────────────────────────

void AudioManager::SetPlaybackSpeed(float speed)
{
    m_playbackSpeed = std::max(0.1f, std::min(4.0f, speed));
    if (m_music)
    {
        ma_sound_set_pitch(m_music, m_playbackSpeed);
    }
    LOG_DEBUG("播放速度设置为 {:.2f}x", m_playbackSpeed);
}

// ── Hitsound 系统 ─────────────────────────────────────────────────────────────

bool AudioManager::LoadHitsoundSet(std::string_view name)
{
    // 先生成占位音效（若不存在）
    SfxGenerator::GenerateDefaults("resources/sound/sfx");

    m_hitsoundSetName = std::string(name);
    std::string base = "resources/sound/sfx/" + m_hitsoundSetName + "/";

    m_hitsoundPaths[static_cast<int>(HitsoundType::Tap)]         = base + "tap.wav";
    m_hitsoundPaths[static_cast<int>(HitsoundType::HoldStart)]   = base + "hold_start.wav";
    m_hitsoundPaths[static_cast<int>(HitsoundType::HoldTick)]    = base + "hold_tick.wav";
    m_hitsoundPaths[static_cast<int>(HitsoundType::Circle)]      = base + "circle.wav";
    m_hitsoundPaths[static_cast<int>(HitsoundType::SliderStart)] = base + "slider_start.wav";

    m_judgeSFXPaths[0] = base + "perfect.wav";
    m_judgeSFXPaths[1] = base + "great.wav";
    m_judgeSFXPaths[2] = base + "good.wav";
    m_judgeSFXPaths[3] = base + "bad.wav";
    m_judgeSFXPaths[4] = base + "miss.wav";

    // UI 音效统一放 ui/
    std::string ui = "resources/sound/sfx/ui/";
    m_uiSFXPaths[static_cast<int>(UISFXType::ButtonHover)]   = ui + "button_hover.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ButtonClick)]   = ui + "button_click.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::Transition)]    = ui + "transition.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ResultScore)]   = ui + "result_score.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ResultGrade)]   = ui + "result_grade.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::Toast)]         = ui + "toast.wav";

    LOG_INFO("[AudioManager] 已加载 hitsound set: {}", name);
    return true;
}

void AudioManager::PlayHitsound(HitsoundType type)
{
    if (!m_initialized) return;
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(m_hitsoundPaths.size())) return;
    const auto& path = m_hitsoundPaths[idx];
    if (!path.empty() && std::filesystem::exists(path))
        PlaySFX(path);
}

void AudioManager::PlayHitsoundForNote(sakura::game::NoteType noteType)
{
    HitsoundType ht;
    switch (noteType)
    {
        case sakura::game::NoteType::Hold:   ht = HitsoundType::HoldStart;   break;
        case sakura::game::NoteType::Drag:   ht = HitsoundType::Tap;          break;
        case sakura::game::NoteType::Circle: ht = HitsoundType::Circle;       break;
        case sakura::game::NoteType::Slider: ht = HitsoundType::SliderStart;  break;
        default:                             ht = HitsoundType::Tap;          break;
    }
    PlayHitsound(ht);
}

void AudioManager::PlayJudgeSFX(sakura::game::JudgeResult result)
{
    if (!m_initialized) return;
    int idx = static_cast<int>(result);
    if (idx < 0 || idx >= static_cast<int>(m_judgeSFXPaths.size())) return;
    const auto& path = m_judgeSFXPaths[idx];
    if (!path.empty() && std::filesystem::exists(path))
        PlaySFX(path);
}

void AudioManager::PlayUISFX(UISFXType type)
{
    if (!m_initialized) return;
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(m_uiSFXPaths.size())) return;
    const auto& path = m_uiSFXPaths[idx];
    if (!path.empty() && std::filesystem::exists(path))
        PlaySFX(path);
}

} // namespace sakura::audio
