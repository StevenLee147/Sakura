// miniaudio 是单文件库，在此处（且仅在此处）定义实现
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "resource_manager.h"
#include "utils/logger.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <filesystem>

namespace sakura::core
{

// ── 析构 ──────────────────────────────────────────────────────────────────────

ResourceManager::~ResourceManager()
{
    ReleaseAll();
}

// ── 单例 ──────────────────────────────────────────────────────────────────────

ResourceManager& ResourceManager::GetInstance()
{
    static ResourceManager instance;
    return instance;
}

// ── 初始化 ────────────────────────────────────────────────────────────────────

bool ResourceManager::Initialize(SDL_Renderer* renderer)
{
    if (!renderer)
    {
        LOG_ERROR("ResourceManager::Initialize: renderer 为 nullptr");
        return false;
    }
    m_renderer = renderer;

    // 初始化 SDL3_ttf
    if (!TTF_Init())
    {
        LOG_ERROR("TTF_Init 失败: {}", SDL_GetError());
        return false;
    }
    LOG_INFO("SDL3_ttf 初始化成功");

    // 加载默认字体（NotoSansSC-Regular.ttf，24pt）
    constexpr const char* DEFAULT_FONT_PATH = "resources/fonts/NotoSansSC-Regular.ttf";
    constexpr int DEFAULT_FONT_SIZE = 24;

    auto fontHandle = LoadFont(DEFAULT_FONT_PATH, DEFAULT_FONT_SIZE);
    if (!fontHandle)
    {
        LOG_WARN("默认字体加载失败，文字渲染将不可用");
    }
    else
    {
        m_defaultFontHandle = *fontHandle;
        LOG_INFO("默认字体加载成功 (handle={})", m_defaultFontHandle);
    }

    LOG_INFO("ResourceManager 初始化完成");
    return true;
}

// ── 释放所有资源 ──────────────────────────────────────────────────────────────

void ResourceManager::ReleaseAll()
{
    LOG_DEBUG("ResourceManager: 释放所有资源...");

    for (auto& [handle, tex] : m_textures)
    {
        if (tex) SDL_DestroyTexture(tex);
    }
    m_textures.clear();
    m_texturePaths.clear();

    for (auto& [handle, font] : m_fonts)
    {
        if (font) TTF_CloseFont(font);
    }
    m_fonts.clear();
    m_fontKeys.clear();

    for (auto& [handle, dec] : m_sounds)
    {
        if (dec)
        {
            ma_decoder_uninit(dec);
            delete dec;
        }
    }
    m_sounds.clear();
    m_soundPaths.clear();

    for (auto& [handle, dec] : m_musics)
    {
        if (dec)
        {
            ma_decoder_uninit(dec);
            delete dec;
        }
    }
    m_musics.clear();
    m_musicPaths.clear();

    m_defaultFontHandle = INVALID_HANDLE;
    m_nextHandle        = INVALID_HANDLE;

    TTF_Quit();
    LOG_DEBUG("ResourceManager: 资源释放完成");
}

// ── 纹理 ──────────────────────────────────────────────────────────────────────

std::optional<TextureHandle> ResourceManager::LoadTexture(const std::string& path)
{
    // 查缓存
    auto it = m_texturePaths.find(path);
    if (it != m_texturePaths.end())
    {
        LOG_DEBUG("纹理缓存命中: {}", path);
        return it->second;
    }

    if (!std::filesystem::exists(path))
    {
        LOG_ERROR("纹理文件不存在: {}", path);
        return std::nullopt;
    }

    SDL_Texture* tex = IMG_LoadTexture(m_renderer, path.c_str());
    if (!tex)
    {
        LOG_ERROR("IMG_LoadTexture 失败 [{}]: {}", path, SDL_GetError());
        return std::nullopt;
    }

    TextureHandle handle = NextHandle();
    m_texturePaths[path]    = handle;
    m_textures[handle]      = tex;

    LOG_DEBUG("纹理已加载: {} (handle={})", path, handle);
    return handle;
}

SDL_Texture* ResourceManager::GetTexture(TextureHandle handle) const
{
    auto it = m_textures.find(handle);
    return (it != m_textures.end()) ? it->second : nullptr;
}

void ResourceManager::UnloadTexture(TextureHandle handle)
{
    auto it = m_textures.find(handle);
    if (it == m_textures.end()) return;

    if (it->second) SDL_DestroyTexture(it->second);
    m_textures.erase(it);

    for (auto pit = m_texturePaths.begin(); pit != m_texturePaths.end(); ++pit)
    {
        if (pit->second == handle)
        {
            m_texturePaths.erase(pit);
            break;
        }
    }
}

// ── 字体 ──────────────────────────────────────────────────────────────────────

std::optional<FontHandle> ResourceManager::LoadFont(const std::string& path, int ptSize)
{
    std::string key = path + ":" + std::to_string(ptSize);

    auto it = m_fontKeys.find(key);
    if (it != m_fontKeys.end())
    {
        LOG_DEBUG("字体缓存命中: {}", key);
        return it->second;
    }

    if (!std::filesystem::exists(path))
    {
        LOG_ERROR("字体文件不存在: {}", path);
        return std::nullopt;
    }

    TTF_Font* font = TTF_OpenFont(path.c_str(), static_cast<float>(ptSize));
    if (!font)
    {
        LOG_ERROR("TTF_OpenFont 失败 [{}:{}]: {}", path, ptSize, SDL_GetError());
        return std::nullopt;
    }

    FontHandle handle = NextHandle();
    m_fontKeys[key]   = handle;
    m_fonts[handle]   = font;

    LOG_DEBUG("字体已加载: {}:{}pt (handle={})", path, ptSize, handle);
    return handle;
}

TTF_Font* ResourceManager::GetFont(FontHandle handle) const
{
    auto it = m_fonts.find(handle);
    return (it != m_fonts.end()) ? it->second : nullptr;
}

void ResourceManager::UnloadFont(FontHandle handle)
{
    auto it = m_fonts.find(handle);
    if (it == m_fonts.end()) return;

    if (it->second) TTF_CloseFont(it->second);
    m_fonts.erase(it);

    for (auto pit = m_fontKeys.begin(); pit != m_fontKeys.end(); ++pit)
    {
        if (pit->second == handle)
        {
            m_fontKeys.erase(pit);
            break;
        }
    }
}

// ── 音效 ──────────────────────────────────────────────────────────────────────

std::optional<SoundHandle> ResourceManager::LoadSound(const std::string& path)
{
    auto it = m_soundPaths.find(path);
    if (it != m_soundPaths.end())
    {
        LOG_DEBUG("音效缓存命中: {}", path);
        return it->second;
    }

    if (!std::filesystem::exists(path))
    {
        LOG_ERROR("音效文件不存在: {}", path);
        return std::nullopt;
    }

    ma_decoder* dec = new ma_decoder();
    ma_result result = ma_decoder_init_file(path.c_str(), nullptr, dec);
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_decoder_init_file 失败 [{}]: error={}", path, static_cast<int>(result));
        delete dec;
        return std::nullopt;
    }

    SoundHandle handle = NextHandle();
    m_soundPaths[path] = handle;
    m_sounds[handle]   = dec;

    LOG_DEBUG("音效已加载: {} (handle={})", path, handle);
    return handle;
}

ma_decoder* ResourceManager::GetSound(SoundHandle handle) const
{
    auto it = m_sounds.find(handle);
    return (it != m_sounds.end()) ? it->second : nullptr;
}

void ResourceManager::UnloadSound(SoundHandle handle)
{
    auto it = m_sounds.find(handle);
    if (it == m_sounds.end()) return;

    if (it->second)
    {
        ma_decoder_uninit(it->second);
        delete it->second;
    }
    m_sounds.erase(it);

    for (auto pit = m_soundPaths.begin(); pit != m_soundPaths.end(); ++pit)
    {
        if (pit->second == handle)
        {
            m_soundPaths.erase(pit);
            break;
        }
    }
}

// ── 音乐 ──────────────────────────────────────────────────────────────────────

std::optional<MusicHandle> ResourceManager::LoadMusic(const std::string& path)
{
    auto it = m_musicPaths.find(path);
    if (it != m_musicPaths.end())
    {
        LOG_DEBUG("音乐缓存命中: {}", path);
        return it->second;
    }

    if (!std::filesystem::exists(path))
    {
        LOG_ERROR("音乐文件不存在: {}", path);
        return std::nullopt;
    }

    ma_decoder* dec = new ma_decoder();
    ma_result result = ma_decoder_init_file(path.c_str(), nullptr, dec);
    if (result != MA_SUCCESS)
    {
        LOG_ERROR("ma_decoder_init_file 失败 [{}]: error={}", path, static_cast<int>(result));
        delete dec;
        return std::nullopt;
    }

    MusicHandle handle = NextHandle();
    m_musicPaths[path] = handle;
    m_musics[handle]   = dec;

    LOG_DEBUG("音乐已加载: {} (handle={})", path, handle);
    return handle;
}

ma_decoder* ResourceManager::GetMusic(MusicHandle handle) const
{
    auto it = m_musics.find(handle);
    return (it != m_musics.end()) ? it->second : nullptr;
}

void ResourceManager::UnloadMusic(MusicHandle handle)
{
    auto it = m_musics.find(handle);
    if (it == m_musics.end()) return;

    if (it->second)
    {
        ma_decoder_uninit(it->second);
        delete it->second;
    }
    m_musics.erase(it);

    for (auto pit = m_musicPaths.begin(); pit != m_musicPaths.end(); ++pit)
    {
        if (pit->second == handle)
        {
            m_musicPaths.erase(pit);
            break;
        }
    }
}

} // namespace sakura::core
