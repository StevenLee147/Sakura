// audio_manager.cpp — 音频管理器实现
// 使用 miniaudio 高层 ma_engine API

// miniaudio 头文件（实现已在 audio_backend.cpp 中定义）
// 这里只做声明引用，不重复定义
#include <miniaudio.h>

#include "audio_manager.h"
#include "audio_visualizer.h"
#include "sfx_generator.h"
#include "time_stretcher.h"
#include "core/config.h"
#include "core/paths.h"
#include "core/skin_manager.h"
#include <algorithm>
#include <cmath>
#include "utils/logger.h"

#include <chrono>
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

namespace
{
std::atomic<bool> g_suppressAudioDeviceNotifs{ false };

void EngineDeviceNotificationProc(const ma_device_notification* pNotification)
{
    if (!pNotification || g_suppressAudioDeviceNotifs.load(std::memory_order_acquire))
        return;
    if (pNotification->type == ma_device_notification_type_stopped ||
        pNotification->type == ma_device_notification_type_rerouted ||
        pNotification->type == ma_device_notification_type_interruption_ended)
    {
        AudioManager::GetInstance().NotifyDeviceReroute();
    }
}
} // namespace

void AudioManager::NotifyDeviceReroute()
{
    m_deviceRebuildPending.store(true, std::memory_order_release);
}

float AudioManager::GetEstimatedOutputLatencyMs() const
{
    const int sr = std::max(1, m_actualSampleRate);
    return static_cast<float>(m_periodFrames * 1000.0) / static_cast<float>(sr);
}

void AudioManager::ClearDeck(MusicDeck& deck)
{
    if (deck.sound)
    {
        ma_sound_stop(deck.sound);
        ma_sound_uninit(deck.sound);
        delete deck.sound;
        deck.sound = nullptr;
    }
    if (deck.decoder)
    {
        ma_decoder_uninit(deck.decoder);
        delete deck.decoder;
        deck.decoder = nullptr;
    }
    if (deck.stretchedBuffer)
    {
        auto* ab = static_cast<ma_audio_buffer*>(deck.stretchedBuffer);
        ma_audio_buffer_uninit(ab);
        delete ab;
        deck.stretchedBuffer = nullptr;
    }
    std::vector<float>{}.swap(deck.stretchedPcm);
    std::vector<unsigned char>{}.swap(deck.compressedMusic);
    deck.activeStretchRate = 1.0f;
    deck.path.clear();
    deck.loopStartSec    = 0.0;
    deck.loopEndSec      = 0.0;
    deck.fadingOut       = false;
    deck.fadeOutTimer    = 0.0f;
    deck.fadeOutDuration = 0.0f;
}

bool AudioManager::Initialize()
{
    if (m_initialized)
        return true;

    auto& cfg        = sakura::core::Config::GetInstance();
    m_masterVolume   = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kMasterVolume), 1.0f);
    m_musicVolume    = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kMusicVolume), 0.8f);
    m_sfxVolume      = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kSfxVolume), 0.8f);
    m_hitsoundVolume = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kHitsoundVolume), 0.85f);
    m_uiSfxVolume    = cfg.Get<float>(std::string(sakura::core::ConfigKeys::kUiSfxVolume), 0.75f);
    m_latencyMode    = cfg.Get<std::string>(std::string(sakura::core::ConfigKeys::kAudioLatencyMode), "balanced");
    m_outputDeviceId = cfg.Get<std::string>(std::string(sakura::core::ConfigKeys::kAudioOutputDevice), "default");
    m_periodFrames   = SfxGenerator::LatencyFramesForMode(m_latencyMode);

    m_initialized = RebuildAudioBackend();
    return m_initialized;
}

bool AudioManager::RebuildAudioBackend()
{
    const MusicDeck& prevActive = m_decks[static_cast<std::size_t>(m_activeDeck)];
    const bool hadMusic         = (prevActive.sound != nullptr && !prevActive.path.empty());
    const std::string savedPath = prevActive.path;
    const double savedPos       = hadMusic ? GetMusicPosition() : 0.0;
    const double savedLoopStart = prevActive.loopStartSec;
    const double savedLoopEnd   = prevActive.loopEndSec;
    const bool savedLooping     = m_offlineClock ? m_virtualLooping : (prevActive.sound && ma_sound_is_looping(prevActive.sound) == MA_TRUE);
    const bool savedPaused      = m_musicPaused;
    const std::string savedSet  = m_hitsoundSetName;

    g_suppressAudioDeviceNotifs.store(true, std::memory_order_release);
    ClearDeck(m_decks[0]);
    ClearDeck(m_decks[1]);
    for (auto& [p, voices] : m_sfxCache)
        for (auto& slot : voices) if (slot.sound) { ma_sound_uninit(slot.sound); delete slot.sound; slot.sound = nullptr; }
    m_sfxCache.clear();
    for (auto& [p, voices] : m_uiSfxCache)
        for (auto& slot : voices) if (slot.sound) { ma_sound_uninit(slot.sound); delete slot.sound; slot.sound = nullptr; }
    m_uiSfxCache.clear();
    if (m_sfxGroup)   { ma_sound_group_uninit(m_sfxGroup);   delete m_sfxGroup;   m_sfxGroup   = nullptr; }
    if (m_uiSfxGroup) { ma_sound_group_uninit(m_uiSfxGroup); delete m_uiSfxGroup; m_uiSfxGroup = nullptr; }
    if (m_engine)     { ma_engine_uninit(m_engine);          delete m_engine;     m_engine     = nullptr; }
    if (m_context)    { ma_context_uninit(m_context);        delete m_context;    m_context    = nullptr; }

    m_periodFrames = SfxGenerator::LatencyFramesForMode(m_latencyMode);
    m_engine       = new ma_engine();
    ma_engine_config engineConfig = ma_engine_config_init();
    ma_device_id chosenDevId{};
    bool useChosenDev = false;

    if (m_offlineClock)
    {
        engineConfig.noDevice   = MA_TRUE;
        engineConfig.channels   = 2;
        engineConfig.sampleRate = 48000;
    }
    else
    {
        m_context = new ma_context();
        if (ma_context_init(nullptr, 0, nullptr, m_context) == MA_SUCCESS)
        {
            engineConfig.pContext = m_context;
            if (!m_outputDeviceId.empty() && m_outputDeviceId != "default")
            {
                ma_device_info* pPlaybackInfos = nullptr;
                ma_uint32 playbackCount = 0;
                if (ma_context_get_devices(m_context, &pPlaybackInfos, &playbackCount, nullptr, nullptr) == MA_SUCCESS)
                {
                    for (ma_uint32 i = 0; i < playbackCount; ++i)
                    {
                        if (m_outputDeviceId == pPlaybackInfos[i].name)
                        {
                            chosenDevId  = pPlaybackInfos[i].id;
                            useChosenDev = true;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            delete m_context;
            m_context = nullptr;
        }
        if (useChosenDev)
            engineConfig.pPlaybackDeviceID = &chosenDevId;
        engineConfig.periodSizeInFrames   = static_cast<ma_uint32>(m_periodFrames);
        engineConfig.notificationCallback = EngineDeviceNotificationProc;
    }

    ma_result result = ma_engine_init(&engineConfig, m_engine);
    if (result != MA_SUCCESS && useChosenDev)
    {
        LOG_WARN("指定音频输出设备不可用 ({})，自动回退系统默认设备", m_outputDeviceId);
        engineConfig.pPlaybackDeviceID = nullptr;
        result = ma_engine_init(&engineConfig, m_engine);
    }
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_engine_init 失败: error={}", static_cast<int>(result));
        delete m_engine;
        m_engine = nullptr;
        if (m_context) { ma_context_uninit(m_context); delete m_context; m_context = nullptr; }
        g_suppressAudioDeviceNotifs.store(false, std::memory_order_release);
        return false;
    }

    m_actualSampleRate = static_cast<int>(ma_engine_get_sample_rate(m_engine));
    if (m_actualSampleRate <= 0)
        m_actualSampleRate = 48000;

    ma_engine_set_volume(m_engine, m_masterVolume);
    m_sfxGroup   = new ma_sound_group();
    m_uiSfxGroup = new ma_sound_group();
    if (ma_sound_group_init(m_engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, m_sfxGroup) != MA_SUCCESS ||
        ma_sound_group_init(m_engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, m_uiSfxGroup) != MA_SUCCESS)
    {
        if (m_sfxGroup)   { ma_sound_group_uninit(m_sfxGroup); delete m_sfxGroup; m_sfxGroup = nullptr; }
        if (m_uiSfxGroup) { delete m_uiSfxGroup; m_uiSfxGroup = nullptr; }
        ma_engine_uninit(m_engine); delete m_engine; m_engine = nullptr;
        if (m_context) { ma_context_uninit(m_context); delete m_context; m_context = nullptr; }
        g_suppressAudioDeviceNotifs.store(false, std::memory_order_release);
        return false;
    }
    ApplySfxVolumes();
    m_initialized = true;
    g_suppressAudioDeviceNotifs.store(false, std::memory_order_release);

    if (!savedSet.empty())
        LoadHitsoundSet(savedSet);
    if (hadMusic && std::filesystem::exists(savedPath))
    {
        auto& deck = m_decks[static_cast<std::size_t>(m_activeDeck)];
        if (InitDeckFromFile(deck, savedPath, savedLooping, savedPos, savedPaused, 60))
        {
            deck.loopStartSec = savedLoopStart;
            deck.loopEndSec   = savedLoopEnd;
            m_musicPath       = savedPath;
            m_musicPaused     = savedPaused;
            AudioVisualizer::GetInstance().SetSourceFile(savedPath);
        }
    }

    LOG_INFO("AudioManager 就绪: {} Hz, {} frames (~{:.1f} ms){}",
             m_actualSampleRate, m_periodFrames, GetEstimatedOutputLatencyMs(),
             m_offlineClock ? " [OfflineClock]" : "");
    return true;
}

void AudioManager::SetOfflineClock(bool enabled)
{
    if (m_initialized)
        return;
    m_offlineClock = enabled;
}

void AudioManager::Shutdown()
{
    if (!m_initialized)
        return;
    g_suppressAudioDeviceNotifs.store(true, std::memory_order_release);
    StopMusic();
    AudioVisualizer::GetInstance().ClearSource();
    for (auto& [p, voices] : m_sfxCache)
        for (auto& slot : voices) if (slot.sound) { ma_sound_uninit(slot.sound); delete slot.sound; slot.sound = nullptr; }
    m_sfxCache.clear();
    for (auto& [p, voices] : m_uiSfxCache)
        for (auto& slot : voices) if (slot.sound) { ma_sound_uninit(slot.sound); delete slot.sound; slot.sound = nullptr; }
    m_uiSfxCache.clear();
    if (m_sfxGroup)   { ma_sound_group_uninit(m_sfxGroup);   delete m_sfxGroup;   m_sfxGroup   = nullptr; }
    if (m_uiSfxGroup) { ma_sound_group_uninit(m_uiSfxGroup); delete m_uiSfxGroup; m_uiSfxGroup = nullptr; }
    if (m_engine)     { ma_engine_uninit(m_engine);          delete m_engine;     m_engine     = nullptr; }
    if (m_context)    { ma_context_uninit(m_context);        delete m_context;    m_context    = nullptr; }
    m_initialized = false;
    m_musicPaused = false;
    m_fadingOut   = false;
    g_suppressAudioDeviceNotifs.store(false, std::memory_order_release);
}

bool AudioManager::InitDeckFromFile(MusicDeck& deck, const std::string& path, bool shouldLoop,
                                    double startSec, bool startPaused, int fadeInMs)
{
    ClearDeck(deck);
    deck.sound             = new ma_sound();
    deck.activeStretchRate = 1.0f;

    ma_result result = MA_ERROR;
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool useWsola = !m_offlineClock && std::abs(m_playbackSpeed - 1.0f) >= 0.01f &&
        sakura::core::Config::GetInstance().Get<bool>("audio.pitch_lock", true);

    if (useWsola)
    {
        constexpr int kTargetSr = 48000;
        ma_decoder tempDec{};
        auto decCfg = ma_decoder_config_init(ma_format_f32, 2, kTargetSr);
        ma_result decRes = MA_ERROR;
        std::vector<unsigned char> oggBytes;
        if (ext == ".ogg" || ext == ".oga")
        {
            const auto fsz = std::filesystem::file_size(path);
            if (fsz > 0 && fsz <= 256 * 1024 * 1024)
            {
                oggBytes.resize(static_cast<std::size_t>(fsz));
                std::ifstream src(path, std::ios::binary);
                if (src.read(reinterpret_cast<char*>(oggBytes.data()), static_cast<std::streamsize>(fsz)))
                    decRes = ma_decoder_init_memory(oggBytes.data(), oggBytes.size(), &decCfg, &tempDec);
            }
        }
        else
        {
            decRes = ma_decoder_init_file(path.c_str(), &decCfg, &tempDec);
        }
        if (decRes == MA_SUCCESS)
        {
            std::vector<float> rawPcm;
            rawPcm.reserve(kTargetSr * 2 * 90);
            std::vector<float> chunk(8192 * 2);
            ma_uint64 framesRead = 0;
            constexpr std::size_t kMaxSamples = static_cast<std::size_t>(kTargetSr) * 2 * 900;
            while (rawPcm.size() < kMaxSamples &&
                   ma_decoder_read_pcm_frames(&tempDec, chunk.data(), 8192, &framesRead) == MA_SUCCESS &&
                   framesRead > 0)
            {
                rawPcm.insert(rawPcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(framesRead * 2));
            }
            ma_decoder_uninit(&tempDec);
            if (!rawPcm.empty())
            {
                deck.stretchedPcm = TimeStretcher::StretchInterleaved(rawPcm, 2, kTargetSr, m_playbackSpeed);
                auto* audioBuf = new ma_audio_buffer();
                deck.stretchedBuffer = audioBuf;
                auto bufCfg = ma_audio_buffer_config_init(
                    ma_format_f32, 2, deck.stretchedPcm.size() / 2, deck.stretchedPcm.data(), nullptr);
                bufCfg.sampleRate = kTargetSr;
                if (ma_audio_buffer_init(&bufCfg, audioBuf) == MA_SUCCESS)
                {
                    result = ma_sound_init_from_data_source(
                        m_engine, audioBuf, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, deck.sound);
                    if (result == MA_SUCCESS)
                        deck.activeStretchRate = m_playbackSpeed;
                }
                if (result != MA_SUCCESS)
                {
                    ma_audio_buffer_uninit(audioBuf);
                    delete audioBuf;
                    deck.stretchedBuffer = nullptr;
                    deck.stretchedPcm.clear();
                }
            }
        }
    }

    if (result != MA_SUCCESS)
    {
        if (ext == ".ogg" || ext == ".oga")
        {
            const auto size = std::filesystem::file_size(path);
            if (size == 0 || size > 256 * 1024 * 1024) { ClearDeck(deck); return false; }
            deck.compressedMusic.resize(static_cast<std::size_t>(size));
            std::ifstream source(path, std::ios::binary);
            if (!source.read(reinterpret_cast<char*>(deck.compressedMusic.data()), static_cast<std::streamsize>(size)))
            {
                ClearDeck(deck);
                return false;
            }
            deck.decoder = new ma_decoder();
            auto decCfg = ma_decoder_config_init(ma_format_f32, 0, 0);
            result = ma_decoder_init_memory(deck.compressedMusic.data(), deck.compressedMusic.size(), &decCfg, deck.decoder);
            if (result == MA_SUCCESS)
                result = ma_sound_init_from_data_source(m_engine, deck.decoder, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, deck.sound);
            else
                { delete deck.decoder; deck.decoder = nullptr; }
        }
        else
        {
            result = ma_sound_init_from_file(m_engine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, deck.sound);
        }
    }
    if (result != MA_SUCCESS)
    {
        ClearDeck(deck);
        return false;
    }

    deck.path = path;
    ma_sound_set_looping(deck.sound, shouldLoop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(deck.sound, m_musicVolume);
    ma_sound_set_pitch(deck.sound, m_playbackSpeed / deck.activeStretchRate);

    if (startSec > 0.0)
        ma_sound_seek_to_second(deck.sound, static_cast<float>(startSec / static_cast<double>(deck.activeStretchRate)));

    if (!m_offlineClock)
    {
        if (fadeInMs > 0)
            ma_sound_set_fade_in_milliseconds(deck.sound, 0.0f, 1.0f, static_cast<ma_uint64>(fadeInMs));
        if (!startPaused && ma_sound_start(deck.sound) != MA_SUCCESS)
        {
            ClearDeck(deck);
            return false;
        }
    }
    return true;
}

bool AudioManager::PlayMusic(const std::string& path, int loops, double startPositionSeconds, bool startPaused)
{
    if (!m_initialized || !m_engine || !std::filesystem::exists(path))
        return false;

    StopMusic();
    const bool shouldLoop = (loops == -1) || (loops > 0);
    auto& deck = m_decks[static_cast<std::size_t>(m_activeDeck)];
    if (!InitDeckFromFile(deck, path, shouldLoop, startPositionSeconds, startPaused, 0))
        return false;

    m_musicPath   = path;
    m_musicPaused = startPaused;
    m_fadingOut   = false;
    AudioVisualizer::GetInstance().SetSourceFile(path);

    if (m_offlineClock)
    {
        m_virtualDuration  = GetMusicDuration();
        m_virtualCursor    = (m_virtualDuration > 0.0) ? std::min(startPositionSeconds, m_virtualDuration) : startPositionSeconds;
        m_virtualLoopStart = 0.0;
        m_virtualLoopEnd   = 0.0;
        m_virtualPlaying   = !startPaused;
        m_virtualAtEnd     = false;
        m_virtualLooping   = shouldLoop;
    }
    return true;
}

bool AudioManager::CrossfadeMusic(const std::string& path, double startSec,
                                  double loopStartSec, double loopEndSec, int fadeMs)
{
    if (!m_initialized || !m_engine || !std::filesystem::exists(path))
        return false;

    const int clampedFade = std::clamp(fadeMs, 40, 2000);
    const int oldIdx      = m_activeDeck;
    const int nextIdx     = 1 - m_activeDeck;

    if (m_decks[static_cast<std::size_t>(oldIdx)].sound)
    {
        auto& oldDeck = m_decks[static_cast<std::size_t>(oldIdx)];
        if (!m_offlineClock)
            ma_sound_set_fade_in_milliseconds(oldDeck.sound, -1.0f, 0.0f, static_cast<ma_uint64>(clampedFade));
        oldDeck.fadingOut       = true;
        oldDeck.fadeOutTimer    = 0.0f;
        oldDeck.fadeOutDuration = static_cast<float>(clampedFade) / 1000.0f;
    }

    const bool wholeTrackLoop = (loopEndSec <= loopStartSec);
    auto& nextDeck = m_decks[static_cast<std::size_t>(nextIdx)];
    if (!InitDeckFromFile(nextDeck, path, wholeTrackLoop, std::max(0.0, startSec), false, clampedFade))
        return false;

    nextDeck.loopStartSec = std::max(0.0, loopStartSec);
    nextDeck.loopEndSec   = (loopEndSec > loopStartSec) ? loopEndSec : 0.0;
    m_activeDeck          = nextIdx;
    m_musicPath           = path;
    m_musicPaused         = false;
    m_fadingOut           = false;
    AudioVisualizer::GetInstance().SetSourceFile(path);

    if (m_offlineClock)
    {
        ClearDeck(m_decks[static_cast<std::size_t>(oldIdx)]);
        m_virtualDuration  = GetMusicDuration();
        m_virtualCursor    = std::max(0.0, startSec);
        m_virtualLoopStart = nextDeck.loopStartSec;
        m_virtualLoopEnd   = nextDeck.loopEndSec;
        m_virtualPlaying   = true;
        m_virtualAtEnd     = false;
        m_virtualLooping   = true;
    }
    return true;
}

void AudioManager::EnsureMenuBgm()
{
    if (!m_initialized)
        return;
    const bool enabled = sakura::core::Config::GetInstance().Get<bool>(
        std::string(sakura::core::ConfigKeys::kMenuBgmEnabled), true);
    static constexpr const char* kMenuBgmRel = "resources/sound/bgm/moonlit_menu.ogg";
    if (!enabled)
    {
        if (m_musicPath.find("moonlit_menu.ogg") != std::string::npos && IsPlaying())
            FadeOutMusic(280);
        return;
    }
    if (!std::filesystem::exists(kMenuBgmRel))
        return;
    if (m_musicPath == kMenuBgmRel && IsPlaying() && !m_fadingOut)
        return;
    SetPlaybackSpeed(1.0f);
    CrossfadeMusic(kMenuBgmRel, 0.0, 0.0, 0.0, 280);
}

bool AudioManager::PlayMusicFromHandle(sakura::core::MusicHandle handle, int loops)
{
    auto path = sakura::core::ResourceManager::GetInstance().GetMusicPath(handle);
    return path ? PlayMusic(*path, loops) : false;
}

void AudioManager::PauseMusic()
{
    auto* snd = m_decks[static_cast<std::size_t>(m_activeDeck)].sound;
    if (!snd || m_musicPaused) return;
    if (!m_offlineClock) ma_sound_stop(snd);
    m_musicPaused = true;
}

void AudioManager::ResumeMusic()
{
    auto* snd = m_decks[static_cast<std::size_t>(m_activeDeck)].sound;
    if (!snd || !m_musicPaused) return;
    if (m_offlineClock)
    {
        if (m_virtualAtEnd) { m_virtualCursor = 0.0; m_virtualAtEnd = false; }
        m_virtualPlaying = true;
    }
    else
    {
        ma_sound_start(snd);
    }
    m_musicPaused = false;
}

void AudioManager::StopMusic()
{
    ClearDeck(m_decks[0]);
    ClearDeck(m_decks[1]);
    m_musicPath        = "";
    m_musicPaused      = false;
    m_fadingOut        = false;
    m_virtualCursor    = 0.0;
    m_virtualDuration  = -1.0;
    m_virtualLoopStart = 0.0;
    m_virtualLoopEnd   = 0.0;
    m_virtualPlaying   = false;
    m_virtualAtEnd     = false;
    m_virtualLooping   = false;
    AudioVisualizer::GetInstance().ClearSource();
}

void AudioManager::FadeOutMusic(int ms)
{
    auto* snd = m_decks[static_cast<std::size_t>(m_activeDeck)].sound;
    if (!snd || !IsPlaying()) return;
    if (!m_offlineClock)
        ma_sound_set_fade_in_milliseconds(snd, -1.0f, 0.0f, static_cast<ma_uint64>(std::max(0, ms)));
    m_fadingOut    = true;
    m_fadeDuration = static_cast<float>(ms) / 1000.0f;
    m_fadeTimer    = 0.0f;
}

void AudioManager::Update(float dt)
{
    if (m_rebuildCooldown > 0.0f)
        m_rebuildCooldown = std::max(0.0f, m_rebuildCooldown - dt);
    if (!m_offlineClock && m_rebuildCooldown <= 0.0f && m_deviceRebuildPending.exchange(false, std::memory_order_acq_rel))
    {
        m_rebuildCooldown = 0.60f;
        LOG_INFO("[AudioManager] 检测到音频输出端点变更，正在执行零中断热重连...");
        RebuildAudioBackend();
    }

    for (auto& deck : m_decks)
    {
        if (deck.fadingOut)
        {
            deck.fadeOutTimer += dt;
            if (deck.fadeOutTimer >= deck.fadeOutDuration)
                ClearDeck(deck);
        }
    }

    auto& active = m_decks[static_cast<std::size_t>(m_activeDeck)];
    if (m_offlineClock && active.sound && m_virtualPlaying && !m_musicPaused)
    {
        m_virtualCursor += static_cast<double>(dt) * static_cast<double>(m_playbackSpeed);
        if (m_virtualLoopEnd > m_virtualLoopStart && m_virtualCursor >= m_virtualLoopEnd)
        {
            m_virtualCursor = m_virtualLoopStart;
        }
        else if (m_virtualDuration > 0.0 && m_virtualCursor >= m_virtualDuration)
        {
            if (m_virtualLooping)
                m_virtualCursor = std::fmod(m_virtualCursor, m_virtualDuration);
            else
                { m_virtualCursor = m_virtualDuration; m_virtualPlaying = false; m_virtualAtEnd = true; }
        }
    }
    else if (!m_offlineClock && active.sound && !m_musicPaused && !m_fadingOut &&
             active.loopEndSec > active.loopStartSec + 0.8)
    {
        const double pos = GetMusicPosition();
        const double dur = GetMusicDuration();
        const double effEnd = (dur > 0.5) ? std::min(active.loopEndSec, dur - 0.15) : active.loopEndSec;
        if (effEnd > active.loopStartSec + 0.5 && pos >= effEnd - 0.28)
        {
            const std::string p = active.path;
            const double ls = active.loopStartSec;
            const double le = active.loopEndSec;
            CrossfadeMusic(p, ls, ls, le, 280);
        }
    }

    if (m_fadingOut)
    {
        m_fadeTimer += dt;
        if (m_fadeTimer >= m_fadeDuration) StopMusic();
    }
}

bool AudioManager::SetMusicPosition(double seconds)
{
    const auto& active = m_decks[static_cast<std::size_t>(m_activeDeck)];
    if (!active.sound || !std::isfinite(seconds))
        return false;
    seconds = std::clamp(seconds, 0.0, std::max(0.0, GetMusicDuration()));
    if (m_offlineClock)
    {
        m_virtualCursor = seconds;
        m_virtualAtEnd  = false;
        return true;
    }
    return ma_sound_seek_to_second(
        active.sound, static_cast<float>(seconds / static_cast<double>(active.activeStretchRate))) == MA_SUCCESS;
}

double AudioManager::GetMusicPosition() const
{
    const auto& active = m_decks[static_cast<std::size_t>(m_activeDeck)];
    if (!active.sound)
        return 0.0;
    if (m_offlineClock)
        return m_virtualCursor;
    float cursor = 0.0f;
    if (ma_sound_get_cursor_in_seconds(active.sound, &cursor) != MA_SUCCESS)
        return 0.0;
    return static_cast<double>(cursor) * static_cast<double>(active.activeStretchRate);
}

double AudioManager::GetMusicDuration() const
{
    const auto& active = m_decks[static_cast<std::size_t>(m_activeDeck)];
    if (!active.sound)
        return -1.0;
    float length = 0.0f;
    if (ma_sound_get_length_in_seconds(active.sound, &length) != MA_SUCCESS)
        return -1.0;
    return static_cast<double>(length) * static_cast<double>(active.activeStretchRate);
}

bool AudioManager::IsPlaying() const
{
    const auto& active = m_decks[static_cast<std::size_t>(m_activeDeck)];
    if (!active.sound)
        return false;
    if (m_offlineClock)
        return m_virtualPlaying && !m_musicPaused;
    return ma_sound_is_playing(active.sound) == MA_TRUE && !m_musicPaused;
}

bool AudioManager::IsPaused() const
{
    return m_decks[static_cast<std::size_t>(m_activeDeck)].sound != nullptr && m_musicPaused;
}

void AudioManager::CacheSFX(const std::string& path, bool isUi)
{
    if (!m_initialized || !m_engine || path.empty() || !std::filesystem::exists(path))
        return;
    if (isUi)
    {
        if (m_uiSfxCache.contains(path))
            return;
        std::array<SfxVoiceSlot, 6> voices{};
        for (auto& slot : voices)
        {
            slot.sound = new ma_sound();
            if (ma_sound_init_from_file(m_engine, path.c_str(),
                    MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, m_uiSfxGroup, nullptr, slot.sound) != MA_SUCCESS)
            {
                delete slot.sound;
                slot.sound = nullptr;
            }
        }
        m_uiSfxCache.emplace(path, voices);
        return;
    }
    if (m_sfxCache.contains(path))
        return;
    std::array<SfxVoiceSlot, kVoicesPerSfx> voices{};
    for (auto& slot : voices)
    {
        slot.sound = new ma_sound();
        if (ma_sound_init_from_file(m_engine, path.c_str(),
                MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, m_sfxGroup, nullptr, slot.sound) != MA_SUCCESS)
        {
            delete slot.sound;
            slot.sound = nullptr;
        }
    }
    m_sfxCache.emplace(path, voices);
}

void AudioManager::PlaySFXInternal(const std::string& path, bool isUiSfx, bool allowHumanize)
{
    if (!m_initialized || !m_engine || path.empty())
        return;
    CacheSFX(path, isUiSfx);

    SfxVoiceSlot* chosen = nullptr;
    bool stoleActive     = false;
    auto selectFromRange = [&](auto* slots, std::size_t count)
    {
        SfxVoiceSlot* bestIdle = nullptr;
        SfxVoiceSlot* oldestBusy = nullptr;
        for (std::size_t i = 0; i < count; ++i)
        {
            auto& s = slots[i];
            if (!s.sound) continue;
            if (!m_offlineClock && ma_sound_is_playing(s.sound) == MA_TRUE)
            {
                if (!oldestBusy || s.playOrder < oldestBusy->playOrder)
                    oldestBusy = &s;
            }
            else
            {
                if (!bestIdle || s.playOrder < bestIdle->playOrder)
                    bestIdle = &s;
            }
        }
        if (bestIdle) chosen = bestIdle;
        else if (oldestBusy) { chosen = oldestBusy; stoleActive = true; }
    };

    if (isUiSfx)
    {
        auto it = m_uiSfxCache.find(path);
        if (it == m_uiSfxCache.end()) return;
        selectFromRange(it->second.data(), it->second.size());
    }
    else
    {
        auto it = m_sfxCache.find(path);
        if (it == m_sfxCache.end()) return;
        selectFromRange(it->second.data(), it->second.size());
    }
    if (!chosen || !chosen->sound)
        return;

    chosen->playOrder = ++m_voicePlayCounter;
    float gain  = 1.0f;
    float pitch = 1.0f;

    if (!isUiSfx)
    {
        const auto nowMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        while (!m_recentHitTimestampsMs.empty() && nowMs > m_recentHitTimestampsMs.front() + 35ULL)
            m_recentHitTimestampsMs.pop_front();
        m_recentHitTimestampsMs.push_back(nowMs);
        gain *= SfxGenerator::ComputePolyphonicGainScale(static_cast<int>(m_recentHitTimestampsMs.size()));

        if (allowHumanize && sakura::core::Config::GetInstance().Get<bool>(
                std::string(sakura::core::ConfigKeys::kSfxHumanize), true))
        {
            m_humanizeRng = m_humanizeRng * 1664525u + 1013904223u;
            const float rPitch = (static_cast<float>((m_humanizeRng >> 8U) & 0xFFFFu) / 32767.5f) - 1.0f;
            m_humanizeRng = m_humanizeRng * 1664525u + 1013904223u;
            const float rGain  = (static_cast<float>((m_humanizeRng >> 8U) & 0xFFFFu) / 32767.5f) - 1.0f;
            pitch = 1.0f + rPitch * 0.015f;
            gain *= (1.0f + rGain * 0.015f);
        }
    }

    if (!m_offlineClock)
    {
        ma_sound_stop(chosen->sound);
        ma_sound_seek_to_pcm_frame(chosen->sound, 0);
        ma_sound_set_pitch(chosen->sound, pitch);
        ma_sound_set_volume(chosen->sound, std::clamp(gain, 0.0f, 1.0f));
        ma_sound_set_fade_in_milliseconds(chosen->sound, 0.0f, 1.0f, stoleActive ? 3 : 1);
        ma_sound_start(chosen->sound);
    }
    AudioVisualizer::GetInstance().AddImpulse(isUiSfx ? 0.08f : 0.20f);
}

void AudioManager::PlaySFX(const std::string& path)
{
    PlaySFXInternal(path, false, false);
}

void AudioManager::PlaySFXFromHandle(sakura::core::SoundHandle handle)
{
    if (auto path = sakura::core::ResourceManager::GetInstance().GetSoundPath(handle))
        PlaySFXInternal(*path, false, false);
}

void AudioManager::SetMasterVolume(float vol)
{
    m_masterVolume = std::clamp(vol, 0.0f, 1.0f);
    if (m_engine)
        ma_engine_set_volume(m_engine, m_masterVolume);
    ApplyMusicVolume();
    ApplySfxVolumes();
}

void AudioManager::SetMusicVolume(float vol)
{
    m_musicVolume = std::clamp(vol, 0.0f, 1.0f);
    ApplyMusicVolume();
}

void AudioManager::SetSFXVolume(float vol)
{
    m_sfxVolume = std::clamp(vol, 0.0f, 1.0f);
    ApplySfxVolumes();
}

void AudioManager::SetHitsoundVolume(float vol)
{
    m_hitsoundVolume = std::clamp(vol, 0.0f, 1.0f);
    ApplySfxVolumes();
}

void AudioManager::SetUISFXVolume(float vol)
{
    m_uiSfxVolume = std::clamp(vol, 0.0f, 1.0f);
    ApplySfxVolumes();
}

void AudioManager::ApplyMusicVolume()
{
    for (auto& deck : m_decks)
    {
        if (deck.sound)
            ma_sound_set_volume(deck.sound, m_musicVolume);
    }
}

void AudioManager::ApplySfxVolumes()
{
    if (m_sfxGroup)
        ma_sound_group_set_volume(m_sfxGroup, std::clamp(m_sfxVolume * m_hitsoundVolume, 0.0f, 1.0f));
    if (m_uiSfxGroup)
        ma_sound_group_set_volume(m_uiSfxGroup, std::clamp(m_sfxVolume * m_uiSfxVolume, 0.0f, 1.0f));
}

bool AudioManager::SetLatencyMode(std::string_view mode)
{
    const std::string normalized = (mode == "ultra_low" || mode == "safe") ? std::string(mode) : std::string("balanced");
    if (m_latencyMode == normalized && m_periodFrames == SfxGenerator::LatencyFramesForMode(normalized))
        return true;
    m_latencyMode  = normalized;
    m_periodFrames = SfxGenerator::LatencyFramesForMode(normalized);
    return m_initialized ? RebuildAudioBackend() : true;
}

std::vector<AudioDeviceInfo> AudioManager::GetPlaybackDevices() const
{
    std::vector<AudioDeviceInfo> list;
    list.push_back({ "default", "系统默认输出设备", true });
    if (m_offlineClock)
        return list;

    ma_context tempCtx{};
    ma_context* ctx = m_context;
    bool ownsTemp   = false;
    if (!ctx && ma_context_init(nullptr, 0, nullptr, &tempCtx) == MA_SUCCESS)
    {
        ctx      = &tempCtx;
        ownsTemp = true;
    }
    if (ctx)
    {
        ma_device_info* pInfos = nullptr;
        ma_uint32 count        = 0;
        if (ma_context_get_devices(ctx, &pInfos, &count, nullptr, nullptr) == MA_SUCCESS)
        {
            for (ma_uint32 i = 0; i < count; ++i)
            {
                std::string devName(pInfos[i].name);
                if (!devName.empty())
                    list.push_back({ devName, devName, pInfos[i].isDefault == MA_TRUE });
            }
        }
    }
    if (ownsTemp)
        ma_context_uninit(&tempCtx);
    return list;
}

bool AudioManager::SetOutputDevice(std::string_view deviceId)
{
    const std::string nextId = deviceId.empty() ? std::string("default") : std::string(deviceId);
    if (m_outputDeviceId == nextId)
        return true;
    m_outputDeviceId = nextId;
    return m_initialized ? RebuildAudioBackend() : true;
}

void AudioManager::SetPlaybackSpeed(float speed)
{
    m_playbackSpeed = std::clamp(speed, 0.1f, 4.0f);
    for (auto& deck : m_decks)
    {
        if (deck.sound)
            ma_sound_set_pitch(deck.sound, m_playbackSpeed / deck.activeStretchRate);
    }
}

bool AudioManager::LoadHitsoundSet(std::string_view name)
{
    const std::string cacheRoot   = sakura::core::Paths::User("cache/sfx-v5");
    const std::string defaultBase = sakura::core::Paths::User("cache/sfx-v5/default/");
    SfxGenerator::GenerateDefaults(cacheRoot);

    const std::string requestedName(name);
    bool useRequestedDir = false;
    std::string requestedBase = defaultBase;
    if (!requestedName.empty() && requestedName != "." && requestedName != ".." && requestedName != "ui")
    {
        std::error_code ec;
        if (std::filesystem::is_directory(std::filesystem::path(cacheRoot) / requestedName, ec))
        {
            useRequestedDir = true;
            requestedBase   = sakura::core::Paths::User("cache/sfx-v5/" + requestedName + "/");
        }
    }
    m_hitsoundSetName = useRequestedDir ? requestedName : "default";

    auto& skinMgr = sakura::core::SkinManager::GetInstance();
    auto resolveWavPath = [&](std::string_view fileName) -> std::string
    {
        std::string customPath = skinMgr.ResolveCustomHitsoundFile(fileName);
        if (!customPath.empty())
            return customPath;
        if (useRequestedDir)
        {
            std::string candidate = requestedBase + std::string(fileName);
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec))
                return candidate;
        }
        return defaultBase + std::string(fileName);
    };

    m_hitsoundPaths[static_cast<int>(HitsoundType::Tap)]          = resolveWavPath("tap.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::HoldStart)]    = resolveWavPath("hold_start.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::HoldTick)]     = resolveWavPath("hold_tick.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::HoldEnd)]      = resolveWavPath("hold_end.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::ThumbBar)]     = resolveWavPath("thumb_bar.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::Circle)]       = resolveWavPath("circle.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::SliderStart)]  = resolveWavPath("slider_start.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::SliderTick)]   = resolveWavPath("slider_tick.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::LinkChord)]    = resolveWavPath("link_chord.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::SynergyBurst)] = resolveWavPath("synergy_burst.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::FeverTrigger)] = resolveWavPath("fever_trigger.wav");
    m_hitsoundPaths[static_cast<int>(HitsoundType::ComboBreak)]   = resolveWavPath("combo_break.wav");

    m_judgeSFXPaths[0] = resolveWavPath("perfect.wav");
    m_judgeSFXPaths[1] = resolveWavPath("great.wav");
    m_judgeSFXPaths[2] = resolveWavPath("good.wav");
    m_judgeSFXPaths[3] = resolveWavPath("bad.wav");
    m_judgeSFXPaths[4] = resolveWavPath("miss.wav");

    const std::string ui = sakura::core::Paths::User("cache/sfx-v5/ui/");
    m_uiSFXPaths[static_cast<int>(UISFXType::ButtonHover)]     = ui + "button_hover.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ButtonClick)]     = ui + "button_click.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::Transition)]      = ui + "transition.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ResultScore)]     = ui + "result_score.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::ResultGrade)]     = ui + "result_grade.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::Toast)]           = ui + "toast.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::CalibrationBeat)] = ui + "calibration_beat.wav";
    m_uiSFXPaths[static_cast<int>(UISFXType::CalibrationHit)]  = ui + "calibration_hit.wav";

    for (const auto& p : m_hitsoundPaths) CacheSFX(p, false);
    for (const auto& p : m_judgeSFXPaths) CacheSFX(p, false);
    for (const auto& p : m_uiSFXPaths)    CacheSFX(p, true);
    return true;
}

void AudioManager::PlayHitsound(HitsoundType type)
{
    if (!sakura::core::Config::GetInstance().Get<bool>("audio.hitsounds_enabled", true) || !m_initialized)
        return;
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(HitsoundType::Count))
        return;
    const auto& path = m_hitsoundPaths[static_cast<std::size_t>(idx)];
    const bool allowHumanize = (type != HitsoundType::FeverTrigger && type != HitsoundType::ComboBreak);
    if (!path.empty())
        PlaySFXInternal(path, false, allowHumanize);
}

void AudioManager::PlayHitsoundForNote(sakura::game::NoteType noteType)
{
    switch (noteType)
    {
        case sakura::game::NoteType::Hold:   PlayHitsound(HitsoundType::HoldStart);   break;
        case sakura::game::NoteType::Circle: PlayHitsound(HitsoundType::Circle);      break;
        case sakura::game::NoteType::Slider: PlayHitsound(HitsoundType::SliderStart); break;
        default:                             PlayHitsound(HitsoundType::Tap);         break;
    }
}

void AudioManager::PlayJudgeSFX(sakura::game::JudgeResult result)
{
    if (!sakura::core::Config::GetInstance().Get<bool>("audio.hitsounds_enabled", true) || !m_initialized)
        return;
    const int idx = static_cast<int>(result);
    if (idx < 0 || idx >= static_cast<int>(m_judgeSFXPaths.size()))
        return;
    const auto& path = m_judgeSFXPaths[static_cast<std::size_t>(idx)];
    if (!path.empty())
        PlaySFXInternal(path, false, false);
}

void AudioManager::PlayUISFX(UISFXType type)
{
    if (!m_initialized)
        return;
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(m_uiSFXPaths.size()))
        return;
    const auto& path = m_uiSFXPaths[static_cast<std::size_t>(idx)];
    if (!path.empty())
        PlaySFXInternal(path, true, false);
}

} // namespace sakura::audio
