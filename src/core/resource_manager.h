#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

// 前向声明 miniaudio decoder（避免在头文件中包含大型单文件库）
struct ma_decoder;

namespace sakura::core
{

// 各类资源句柄类型（uint32_t 唯一 ID）
using TextureHandle = uint32_t;
using FontHandle    = uint32_t;
using SoundHandle   = uint32_t;
using MusicHandle   = uint32_t;

static constexpr uint32_t INVALID_HANDLE = 0;

// ResourceManager — 单例，管理所有游戏资源的加载、缓存与释放
class ResourceManager
{
public:
    static ResourceManager& GetInstance();

    // 禁止拷贝
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // ── 初始化/关闭 ────────────────────────────────────────────────────────────

    // 需要 SDL_Renderer* 和 miniaudio engine 指针
    bool Initialize(SDL_Renderer* renderer);

    // 释放所有资源（程序退出时调用）
    void ReleaseAll();

    // ── 纹理 ──────────────────────────────────────────────────────────────────

    // 加载图片为 GPU 纹理，相同路径只加载一次
    std::optional<TextureHandle> LoadTexture(const std::string& path);
    SDL_Texture* GetTexture(TextureHandle handle) const;

    // ── 字体 ──────────────────────────────────────────────────────────────────

    // 加载字体，key = "path:ptSize"，相同 key 共享 handle
    std::optional<FontHandle> LoadFont(const std::string& path, int ptSize);
    TTF_Font* GetFont(FontHandle handle) const;

    // ── 音效（短音频，预先解码到内存）────────────────────────────────────────

    std::optional<SoundHandle> LoadSound(const std::string& path);
    ma_decoder* GetSound(SoundHandle handle) const;

    // ── 音乐（长音频，流式解码）──────────────────────────────────────────────

    std::optional<MusicHandle> LoadMusic(const std::string& path);
    ma_decoder* GetMusic(MusicHandle handle) const;

    // ── 主动卸载 ──────────────────────────────────────────────────────────────

    void UnloadTexture(TextureHandle handle);
    void UnloadFont(FontHandle handle);
    void UnloadSound(SoundHandle handle);
    void UnloadMusic(MusicHandle handle);

    // ── 默认资源访问 ──────────────────────────────────────────────────────────

    // 默认字体（NotoSansSC-Regular, 24pt）
    FontHandle GetDefaultFontHandle() const { return m_defaultFontHandle; }
    TTF_Font*  GetDefaultFont()       const { return GetFont(m_defaultFontHandle); }

private:
    ResourceManager() = default;
    ~ResourceManager();

    uint32_t NextHandle() { return ++m_nextHandle; }

    // 内部资源条目
    template<typename T>
    struct Entry
    {
        uint32_t    handle = INVALID_HANDLE;
        T*          resource = nullptr;
    };

    SDL_Renderer* m_renderer = nullptr;

    // 资源映射（path/key → handle, handle → resource）
    std::unordered_map<std::string, TextureHandle>  m_texturePaths;
    std::unordered_map<TextureHandle, SDL_Texture*> m_textures;

    std::unordered_map<std::string, FontHandle>     m_fontKeys;
    std::unordered_map<FontHandle, TTF_Font*>       m_fonts;

    std::unordered_map<std::string, SoundHandle>    m_soundPaths;
    std::unordered_map<SoundHandle, ma_decoder*>    m_sounds;

    std::unordered_map<std::string, MusicHandle>    m_musicPaths;
    std::unordered_map<MusicHandle, ma_decoder*>    m_musics;

    uint32_t   m_nextHandle      = INVALID_HANDLE;
    FontHandle m_defaultFontHandle = INVALID_HANDLE;
};

} // namespace sakura::core
