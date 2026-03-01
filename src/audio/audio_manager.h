#pragma once

// audio_manager.h — 音频管理器（基于 miniaudio ma_engine 高层API）
// 单例，管理背景音乐和音效播放

#include "core/resource_manager.h"
#include "game/note.h"
#include <string>
#include <string_view>
#include <array>
#include <unordered_map>

// 前向声明 miniaudio 类型（避免在头文件中包含大型单文件库）
struct ma_engine;
struct ma_sound;

namespace sakura::audio
{

// ── UI 音效类型 ────────────────────────────────────────────────────────────────

enum class UISFXType
{
    ButtonHover,    // 鼠标悬停
    ButtonClick,    // 按钮点击
    Transition,     // 场景切换
    ResultScore,    // 分数滚动 tick
    ResultGrade,    // 评级出现
    Toast,          // Toast 通知
};

// ── Hitsound 类型 ─────────────────────────────────────────────────────────────

enum class HitsoundType
{
    Tap,
    HoldStart,
    HoldTick,
    Circle,
    SliderStart,
};

// AudioManager — 单例音频管理器
class AudioManager
{
public:
    static AudioManager& GetInstance();

    // 禁止拷贝
    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    // 初始化 miniaudio engine，从 Config 读取音量设置
    bool Initialize();

    // 关闭并释放所有音频资源
    void Shutdown();

    // ── 背景音乐 ──────────────────────────────────────────────────────────────

    // 播放音乐（path 相对于工作目录）
    // loops: -1 = 无限循环, 0 = 播放一次, n = 循环 n 次
    bool PlayMusic(const std::string& path, int loops = 0);

    // 使用已加载的 MusicHandle 播放（不重新加载文件）
    bool PlayMusicFromHandle(sakura::core::MusicHandle handle, int loops = 0);

    void PauseMusic();
    void ResumeMusic();
    void StopMusic();

    // 淡出停止（ms: 淡出时长毫秒）
    void FadeOutMusic(int ms);

    // 跳转到指定位置（秒，需要 miniaudio 支持）
    bool SetMusicPosition(double seconds);

    // 获取当前播放位置（秒）
    double GetMusicPosition() const;

    // 获取总时长（秒，-1 = 未知）
    double GetMusicDuration() const;

    bool IsPlaying() const;
    bool IsPaused()  const;

    // ── 音效 ──────────────────────────────────────────────────────────────────

    // 一次性播放音效（不循环）
    void PlaySFX(const std::string& path);
    void PlaySFXFromHandle(sakura::core::SoundHandle handle);

    // ── 音量控制 ──────────────────────────────────────────────────────────────

    // 设置各通道音量（0.0~1.0），自动与 MasterVolume 相乘
    void SetMasterVolume(float vol);
    void SetMusicVolume(float vol);
    void SetSFXVolume(float vol);

    float GetMasterVolume() const { return m_masterVolume; }
    float GetMusicVolume()  const { return m_musicVolume;  }
    float GetSFXVolume()    const { return m_sfxVolume;    }

    // ── 播放速度（编辑器/练习用）──────────────────────────────────────────────

    // 设置音乐播放速度（1.0 = 正常，0.5 = 半速，2.0 = 2x）
    void SetPlaybackSpeed(float speed);
    float GetPlaybackSpeed() const { return m_playbackSpeed; }

    // ── Hitsound 系统 ─────────────────────────────────────────────────────────

    // 加载 hitsound 集（从 resources/sound/sfx/{name}/ 读取）
    // 若文件不存在，先调用 SfxGenerator 生成占位文件
    bool LoadHitsoundSet(std::string_view name);

    // 播放 hitsound（直接指定 HitsoundType）
    void PlayHitsound(HitsoundType type);

    // 根据 NoteType 自动推断并播放 hitsound
    void PlayHitsoundForNote(sakura::game::NoteType noteType);

    // 播放判定音效（JudgeResult → perfect/great/good/bad/miss.wav）
    void PlayJudgeSFX(sakura::game::JudgeResult result);

    // 播放 UI 音效
    void PlayUISFX(UISFXType type);

    // 当前 hitsound set 名称
    const std::string& GetHitsoundSetName() const { return m_hitsoundSetName; }

    // ── 引擎访问 ──────────────────────────────────────────────────────────────

    ma_engine* GetEngine() const { return m_engine; }

private:
    AudioManager() = default;
    ~AudioManager();

    // 更新音乐 ma_sound 的实际音量（master * music）
    void ApplyMusicVolume();

    ma_engine* m_engine    = nullptr;   // miniaudio 高层引擎
    ma_sound*  m_music     = nullptr;   // 当前背景音乐 sound 对象
    std::string m_musicPath;            // 当前音乐文件路径

    float m_masterVolume   = 1.0f;
    float m_musicVolume    = 0.8f;
    float m_sfxVolume      = 0.8f;
    float m_playbackSpeed  = 1.0f;

    bool  m_initialized    = false;
    bool  m_musicPaused    = false;

    // 淡出状态
    bool  m_fadingOut      = false;
    float m_fadeTimer      = 0.0f;
    float m_fadeDuration   = 0.0f;
    float m_fadeStartVol   = 0.0f;

    // Hitsound 文件路径缓存（按类型索引）
    std::string m_hitsoundSetName;
    std::array<std::string, 5>  m_hitsoundPaths;   // indexed by HitsoundType
    std::array<std::string, 5>  m_judgeSFXPaths;   // Perfect/Great/Good/Bad/Miss
    std::array<std::string, 6>  m_uiSFXPaths;      // indexed by UISFXType
};

} // namespace sakura::audio
