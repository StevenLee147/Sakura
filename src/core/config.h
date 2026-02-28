#pragma once

// config.h — 游戏配置管理器（基于 nlohmann_json，单例）
// 支持嵌套键（"display.width"），线程不安全（游戏单线程使用）。

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <filesystem>

namespace sakura::core
{

// ── 预定义配置键常量 ──────────────────────────────────────────────────────────

namespace ConfigKeys
{
    // ── 显示 ─────────────────────────────────────────────────────────────────
    inline constexpr std::string_view kWindowWidth    = "display.window_width";    // int
    inline constexpr std::string_view kWindowHeight   = "display.window_height";   // int
    inline constexpr std::string_view kFullscreen     = "display.fullscreen";       // bool
    inline constexpr std::string_view kVSync          = "display.vsync";            // bool
    inline constexpr std::string_view kFpsLimit       = "display.fps_limit";        // int (0=无限制)

    // ── 音频 ─────────────────────────────────────────────────────────────────
    inline constexpr std::string_view kMasterVolume   = "audio.master_volume";     // float 0.0~1.0
    inline constexpr std::string_view kMusicVolume    = "audio.music_volume";      // float 0.0~1.0
    inline constexpr std::string_view kSfxVolume      = "audio.sfx_volume";        // float 0.0~1.0
    inline constexpr std::string_view kAudioOffset    = "audio.global_offset_ms";  // int   毫秒延迟补偿

    // ── 游戏玩法 ──────────────────────────────────────────────────────────────
    inline constexpr std::string_view kNoteSpeed      = "gameplay.note_speed";     // float 1.0~10.0
    inline constexpr std::string_view kAutoPlay       = "gameplay.auto_play";       // bool
    inline constexpr std::string_view kScrollDir      = "gameplay.scroll_dir";      // string "down"/"up"

    // ── 输入绑定 ──────────────────────────────────────────────────────────────
    inline constexpr std::string_view kKeyPause       = "input.key_pause";         // int (SDL_Scancode)
    inline constexpr std::string_view kKeyRetry       = "input.key_retry";         // int
    inline constexpr std::string_view kKeyBack        = "input.key_back";          // int

    // ── 图形 ─────────────────────────────────────────────────────────────────
    inline constexpr std::string_view kParticles      = "graphics.particles";      // bool
    inline constexpr std::string_view kBloom          = "graphics.bloom";           // bool
    inline constexpr std::string_view kSkinPath       = "graphics.skin_path";       // string
}

// ============================================================================
// Config — 配置管理单例
// ============================================================================
class Config
{
public:
    // 单例
    static Config& GetInstance()
    {
        static Config instance;
        return instance;
    }

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    // ── 文件操作 ──────────────────────────────────────────────────────────────

    // 从 JSON 文件加载（文件不存在则使用默认值）
    bool Load(std::string_view path = "config/settings.json");

    // 保存到当前路径（仅在 dirty 时保存）
    bool Save();

    // 强制保存（忽略 dirty 标记）
    bool SaveForce();

    // ── 读取 ──────────────────────────────────────────────────────────────────

    // 获取嵌套值，键用 "." 分隔（如 "display.width"）
    // 不存在则返回 defaultVal
    template<typename T>
    T Get(std::string_view key, T defaultVal = T{}) const
    {
        try
        {
            const nlohmann::json* node = TraverseRead(key);
            if (!node) return defaultVal;
            return node->get<T>();
        }
        catch (...)
        {
            return defaultVal;
        }
    }

    // ── 写入 ──────────────────────────────────────────────────────────────────

    template<typename T>
    void Set(std::string_view key, T value)
    {
        TraverseWrite(key) = value;
        m_dirty = true;
    }

    // 检测键是否存在
    bool Has(std::string_view key) const;

    // 删除键
    void Remove(std::string_view key);

    // ── 状态 ──────────────────────────────────────────────────────────────────

    bool IsDirty() const { return m_dirty; }
    std::string GetFilePath() const { return m_filePath; }

    // 重置为出厂默认值并标记 dirty
    void ResetToDefaults();

    // 直接获取 JSON 根节点（调试用）
    const nlohmann::json& GetRoot() const { return m_data; }

private:
    Config() = default;

    // 写入默认值（首次运行时调用）
    void ApplyDefaults();

    // 按 "a.b.c" 路径遍历读取（返回 nullptr 表示不存在）
    const nlohmann::json* TraverseRead(std::string_view key) const;

    // 按 "a.b.c" 路径遍历写入（自动创建中间节点）
    nlohmann::json& TraverseWrite(std::string_view key);

    nlohmann::json  m_data;
    std::string     m_filePath;
    bool            m_dirty = false;
};

} // namespace sakura::core
