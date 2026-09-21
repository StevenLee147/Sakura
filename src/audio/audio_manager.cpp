// audio_manager.cpp — 音频管理器实现
// 使用 miniaudio 高层 ma_engine API

// miniaudio 头文件（实现已在 audio_backend.cpp 中定义）
// 这里只做声明引用，不重复定义
#include <miniaudio.h>

#include "audio_manager.h"
#include "audio_visualizer.h"
#include "sfx_generator.h"
#include "core/config.h"
#include "core/paths.h"
#include <algorithm>
#include <cmath>
#include "utils/logger.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <cctype>

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

    m_sfxGroup = new ma_sound_group();
    if (ma_sound_group_init(m_engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, m_sfxGroup) != MA_SUCCESS)
    {
        delete m_sfxGroup;
        m_sfxGroup = nullptr;
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
        return false;
    }
    ma_sound_group_set_volume(m_sfxGroup, m_sfxVolume);
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
    StopMusic();
    AudioVisualizer::GetInstance().ClearSource();

    for (auto& [path, voices] : m_sfxCache)
        for (auto* voice : voices) if (voice) { ma_sound_uninit(voice); delete voice; }
    m_sfxCache.clear();
    if (m_sfxGroup) { ma_sound_group_uninit(m_sfxGroup); delete m_sfxGroup; m_sfxGroup = nullptr; }

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

bool AudioManager::PlayMusic(const std::string& path, int loops, double startPositionSeconds, bool startPaused)
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

    ma_result result;
    auto extension=std::filesystem::path(path).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".ogg" || extension==".oga"){
        // stb_vorbis callback streaming cannot report length. Its memory pull
        // decoder supports exact seeking while keeping compressed data in RAM.
        const auto size=std::filesystem::file_size(path);
        if(size==0 || size>256*1024*1024){delete m_music;m_music=nullptr;return false;}
        m_compressedMusic.resize(static_cast<size_t>(size));
        std::ifstream source(path,std::ios::binary);
        if(!source.read(reinterpret_cast<char*>(m_compressedMusic.data()),static_cast<std::streamsize>(size))){delete m_music;m_music=nullptr;m_compressedMusic.clear();return false;}
        m_musicDecoder=new ma_decoder();
        auto decoderConfig=ma_decoder_config_init(ma_format_f32,0,0);
        result=ma_decoder_init_memory(m_compressedMusic.data(),m_compressedMusic.size(),&decoderConfig,m_musicDecoder);
        if(result==MA_SUCCESS)result=ma_sound_init_from_data_source(m_engine,m_musicDecoder,MA_SOUND_FLAG_NO_SPATIALIZATION,nullptr,m_music);
        else {delete m_musicDecoder;m_musicDecoder=nullptr;}
    }else result = ma_sound_init_from_file(
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
        if(m_musicDecoder){ma_decoder_uninit(m_musicDecoder);delete m_musicDecoder;m_musicDecoder=nullptr;}
        m_compressedMusic.clear();
        return false;
    }

    m_musicPath = path;
    AudioVisualizer::GetInstance().SetSourceFile(path);

    // 设置循环
    bool shouldLoop = (loops == -1) || (loops > 0);
    ma_sound_set_looping(m_music, shouldLoop ? MA_TRUE : MA_FALSE);

    // 设置音量
    ApplyMusicVolume();

    // 设置播放速度
    ma_sound_set_pitch(m_music, m_playbackSpeed);

    // 在 start 前 seek 到指定起始位置，避免异步启动后再 seek 的竞争问题
    // 使用 ma_sound_seek_to_second 自动处理数据源采样率转换
    if (startPositionSeconds > 0.0)
    {
        ma_result seekResult = ma_sound_seek_to_second(m_music, static_cast<float>(startPositionSeconds));
        if (seekResult != MA_SUCCESS)
        {
            LOG_WARN("PlayMusic: 起始位置 seek 失败 (pos={:.3f}s, error={}), 将从头播放",
                     startPositionSeconds, static_cast<int>(seekResult));
        }
        else
        {
            LOG_DEBUG("PlayMusic: seek 到 {:.3f}s 成功", startPositionSeconds);
        }
    }

    // 开始播放
    result = startPaused ? MA_SUCCESS : ma_sound_start(m_music);
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_sound_start 失败: error={}", static_cast<int>(result));
        return false;
    }

    m_musicPaused = startPaused;
    m_fadingOut   = false;
    LOG_INFO("开始播放音乐: {} (loop={}, startPos={:.3f}s)", path, loops, startPositionSeconds);
    return true;
}

bool AudioManager::PlayMusicFromHandle(sakura::core::MusicHandle handle, int loops)
{
    auto path = sakura::core::ResourceManager::GetInstance().GetMusicPath(handle);
    if (!path)
    {
        LOG_WARN("PlayMusicFromHandle: 无效 MusicHandle {}", handle);
        return false;
    }

    return PlayMusic(*path, loops);
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
    if(m_musicDecoder){ma_decoder_uninit(m_musicDecoder);delete m_musicDecoder;m_musicDecoder=nullptr;}
    std::vector<unsigned char>{}.swap(m_compressedMusic);
    m_musicPath   = "";
    m_musicPaused = false;
    m_fadingOut   = false;
    AudioVisualizer::GetInstance().ClearSource();
    LOG_DEBUG("音乐已停止");
}

void AudioManager::FadeOutMusic(int ms)
{
    if (!m_music || !IsPlaying()) return;

    // 使用 miniaudio 内置淡出 API
    // ma_sound_set_fade_in_milliseconds(sound, startVol, endVol, durationMs)
    float currentVol = 1.0f;
    ma_sound_set_fade_in_milliseconds(m_music, currentVol, 0.0f,
                                     static_cast<ma_uint64>(std::max(0, ms)));

    m_fadingOut    = true;
    m_fadeDuration = static_cast<float>(ms) / 1000.0f;
    m_fadeTimer    = 0.0f;
    LOG_DEBUG("音乐淡出 {}ms", ms);
}

void AudioManager::Update(float dt)
{
    if (m_fadingOut)
    {
        m_fadeTimer += dt;
        if (m_fadeTimer >= m_fadeDuration) StopMusic();
    }
}

bool AudioManager::SetMusicPosition(double seconds)
{
    if (!m_music || !std::isfinite(seconds)) return false;
    seconds = std::clamp(seconds, 0.0, std::max(0.0, GetMusicDuration()));

    // 使用 ma_sound_seek_to_second 自动处理数据源采样率转换
    ma_result result = ma_sound_seek_to_second(m_music, static_cast<float>(seconds));
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

void AudioManager::CacheSFX(const std::string& path)
{
    if (!m_initialized || m_sfxCache.contains(path) || !std::filesystem::exists(path)) return;
    std::array<ma_sound*, 4> voices{};
    for (auto& voice : voices)
    {
        voice = new ma_sound();
        if (ma_sound_init_from_file(m_engine, path.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                m_sfxGroup, nullptr, voice) != MA_SUCCESS)
        {
            delete voice;
            voice = nullptr;
        }
    }
    m_sfxCache.emplace(path, voices);
}

void AudioManager::PlaySFX(const std::string& path)
{
    if (!m_initialized || !m_engine) return;
    CacheSFX(path);
    const auto it = m_sfxCache.find(path);
    if (it == m_sfxCache.end()) return;
    ma_sound* chosen = nullptr;
    for (auto* voice : it->second)
        if (voice) { chosen = voice; if (!ma_sound_is_playing(voice)) break; }
    if (chosen)
    {
        ma_sound_stop(chosen);
        ma_sound_seek_to_pcm_frame(chosen, 0);
        ma_sound_start(chosen);
        AudioVisualizer::GetInstance().AddImpulse(0.20f);
    }
}

void AudioManager::PlaySFXFromHandle(sakura::core::SoundHandle handle)
{
    auto path = sakura::core::ResourceManager::GetInstance().GetSoundPath(handle);
    if (!path)
    {
        LOG_WARN("PlaySFXFromHandle: 无效 SoundHandle {}", handle);
        return;
    }

    PlaySFX(*path);
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
    if (m_sfxGroup) ma_sound_group_set_volume(m_sfxGroup, m_sfxVolume);
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
    // 先生成合成音效（若不存在）
    SfxGenerator::GenerateDefaults(sakura::core::Paths::User("cache/sfx-v2"));

    m_hitsoundSetName = std::string(name);
    std::string base = sakura::core::Paths::User("cache/sfx-v2/" + m_hitsoundSetName + "/");

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
    std::string ui = sakura::core::Paths::User("cache/sfx-v2/ui/");
    m_uiSFXPaths[static_cast<int>(UISFXType::ButtonHover)]   = ui + "button_hover.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ButtonClick)]   = ui + "button_click.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::Transition)]    = ui + "transition.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ResultScore)]   = ui + "result_score.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ResultGrade)]   = ui + "result_grade.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::Toast)]         = ui + "toast.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::CalibrationBeat)] = ui + "calibration_beat.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::CalibrationHit)]  = ui + "calibration_hit.wav";

    LOG_INFO("[AudioManager] 已加载 hitsound set: {}", name);
    for (const auto& path : m_hitsoundPaths) CacheSFX(path);
    for (const auto& path : m_judgeSFXPaths) CacheSFX(path);
    for (const auto& path : m_uiSFXPaths) CacheSFX(path);
    return true;
}

void AudioManager::PlayHitsound(HitsoundType type)
{
    if(!sakura::core::Config::GetInstance().Get<bool>("audio.hitsounds_enabled",true))return;
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
        case sakura::game::NoteType::Circle: ht = HitsoundType::Circle;       break;
        case sakura::game::NoteType::Slider: ht = HitsoundType::SliderStart;  break;
        default:                             ht = HitsoundType::Tap;          break;
    }
    PlayHitsound(ht);
}

void AudioManager::PlayJudgeSFX(sakura::game::JudgeResult result)
{
    if (!sakura::core::Config::GetInstance().Get<bool>("audio.hitsounds_enabled", true)) return;
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
