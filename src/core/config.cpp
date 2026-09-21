#include "config.h"
#include "utils/logger.h"
#include "utils/file_io.h"
#include <algorithm>
#include <cmath>

#include <fstream>
#include <sstream>

namespace sakura::core
{

// ── 默认配置值 ────────────────────────────────────────────────────────────────

void Config::ApplyDefaults()
{
    // 仅在键不存在时写入默认值（不覆盖已有设置）
    auto setDefault = [this](std::string_view key, auto val)
    {
        if (!Has(key)) Set(key, val);
    };

    // 显示
    setDefault(ConfigKeys::kWindowWidth,  1600);
    setDefault(ConfigKeys::kWindowHeight, 900);
    setDefault(ConfigKeys::kFullscreen,   false);
    setDefault(ConfigKeys::kVSync,        true);
    setDefault(ConfigKeys::kFpsLimit,     240);

    // 音频
    setDefault(ConfigKeys::kMasterVolume, 1.0f);
    setDefault(ConfigKeys::kMusicVolume,  0.8f);
    setDefault(ConfigKeys::kSfxVolume,    1.0f);
    setDefault(ConfigKeys::kAudioOffset,  0);

    // 游戏玩法
    setDefault(ConfigKeys::kNoteSpeed,    5.0f);
    setDefault(ConfigKeys::kAutoPlay,     false);
    setDefault(ConfigKeys::kScrollDir,    std::string("down"));

    // 输入绑定（SDL_SCANCODE 数值）
    setDefault(ConfigKeys::kKeyPause,     41);   // SDL_SCANCODE_ESCAPE
    setDefault(ConfigKeys::kKeyRetry,     21);   // SDL_SCANCODE_R
    setDefault(ConfigKeys::kKeyBack,      41);   // SDL_SCANCODE_ESCAPE

    // 数据
    setDefault(ConfigKeys::kDatabasePath, std::string("data/sakura.db"));

    // 编辑器
    setDefault(ConfigKeys::kEditorMaxHistory, 200);

    // 图形
    setDefault(ConfigKeys::kParticles,    true);
    setDefault(ConfigKeys::kBloom,        false);
    setDefault(ConfigKeys::kSkinPath,     std::string("resources/skins/default"));

    // 教程
    setDefault(ConfigKeys::kTutorialCompleted,   false);
    setDefault(ConfigKeys::kTutorialPromptShown, false);

    setDefault("input.key_lane_0", 4);
    setDefault("input.key_lane_1", 22);
    setDefault("input.key_lane_2", 7);
    setDefault("input.key_lane_3", 9);
    setDefault("audio.hitsound", std::string("default"));
    setDefault("audio.hitsounds_enabled", true);
    setDefault("audio.judgment_sounds", false);
    setDefault("graphics.glow", true);
    setDefault("graphics.background_blur", true);
    setDefault("graphics.shake", true);
    setDefault("graphics.vignette", true);
    setDefault("graphics.reduced_motion", false);
    setDefault("graphics.effect_intensity", 0.7f);
    setDefault("graphics.show_fps", false);
    setDefault("gameplay.mouse_approach_ms", 1000);
    setDefault("gameplay.show_hit_error", true);
    setDefault("gameplay.background_dim", 0.75f);
    setDefault("gameplay.lane_opacity", 0.90f);
    setDefault("gameplay.cursor_trail", true);
    setDefault("gameplay.save_replays", true);
    setDefault("gameplay.show_combo", true);
    auto boundInt = [this](std::string_view key, int fallback, int low, int high)
    { Set(key, std::clamp(Get<int>(key, fallback), low, high)); };
    auto boundFloat = [this](std::string_view key, float fallback, float low, float high)
    {
        float value = Get<float>(key, fallback);
        Set(key, std::isfinite(value) ? std::clamp(value, low, high) : fallback);
    };
    boundInt(ConfigKeys::kWindowWidth, 1600, 960, 7680);
    boundInt(ConfigKeys::kWindowHeight, 900, 540, 4320);
    boundInt(ConfigKeys::kFpsLimit, 240, 0, 1000);
    boundInt(ConfigKeys::kAudioOffset, 0, -500, 500);
    boundInt("gameplay.mouse_approach_ms", 1000, 400, 2000);
    boundFloat(ConfigKeys::kNoteSpeed, 5.0f, 0.5f, 15.0f);
    boundFloat(ConfigKeys::kMasterVolume, 0.8f, 0, 1);
    boundFloat(ConfigKeys::kMusicVolume, 0.8f, 0, 1);
    boundFloat(ConfigKeys::kSfxVolume, 0.7f, 0, 1);
    boundFloat("gameplay.background_dim", 0.75f, 0, 1);
    boundFloat("gameplay.lane_opacity", 0.9f, 0.2f, 1);
    boundFloat("graphics.effect_intensity", 0.7f, 0, 1);
    // Scancodes outside SDL's table or duplicate lane bindings make charts unplayable.
    constexpr int defaults[] = {4, 22, 7, 9, 41, 21};
    const std::string keys[] = {"input.key_lane_0", "input.key_lane_1", "input.key_lane_2",
        "input.key_lane_3", "input.key_pause", "input.key_retry"};
    bool invalid = false;
    for (int i = 0; i < 6; ++i)
    {
        const int value = Get<int>(keys[i], defaults[i]);
        invalid |= value <= 0 || value >= 512 || value == 68;
        for (int j = 0; j < i; ++j) invalid |= value == Get<int>(keys[j], defaults[j]);
    }
    if (invalid) for (int i = 0; i < 6; ++i) Set(keys[i], defaults[i]);

    m_dirty = false;  // 默认值不算脏
}

// ── 文件操作 ──────────────────────────────────────────────────────────────────

bool Config::Load(std::string_view path)
{
    m_data = nlohmann::json::object();
    m_filePath = std::string(path);
    m_dirty    = false;

    std::filesystem::path fsPath(path);
    std::error_code existsError;
    if (!std::filesystem::exists(fsPath,existsError) && !existsError)
    {
        LOG_INFO("Config: 配置文件不存在 ({}), 使用默认值", path);
        ApplyDefaults();
        // 立即保存默认配置
        m_dirty = true;
        SaveForce();
        return true;   // 不算失败
    }

    try
    {
        std::ifstream ifs(fsPath);
        if (!ifs.is_open())
        {
            LOG_ERROR("Config: 无法打开配置文件: {}", path);
            ApplyDefaults();
            return false;
        }
        m_data = nlohmann::json::parse(ifs, nullptr, true, true);  // 允许注释
        ifs.close();
        if (!m_data.is_object()) m_data = nlohmann::json::object();

        // 补充新版本添加的缺失键
        ApplyDefaults();

        LOG_INFO("Config: 已加载 ({})", path);
        return true;
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR("Config: JSON 解析失败: {} ({})", e.what(), path);
        m_data = {};
        ApplyDefaults();
        return false;
    }
}

bool Config::Save()
{
    if (!m_dirty) return true;   // 未修改无需保存
    return SaveForce();
}

bool Config::SaveForce()
{
    if (m_filePath.empty()) m_filePath = "config/settings.json";

    try
    {
        // 确保目录存在
        std::filesystem::path fsPath(m_filePath);
        if (fsPath.has_parent_path())
        {
            std::filesystem::create_directories(fsPath.parent_path());
        }

        if (!sakura::utils::AtomicWrite(fsPath, m_data.dump(4, ' ', false,
                nlohmann::json::error_handler_t::replace)))
        {
            LOG_ERROR("Config: 无法保存配置: {}", m_filePath);
            return false;
        }

        m_dirty = false;
        LOG_INFO("Config: 已保存 ({})", m_filePath);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Config: 保存失败: {}", e.what());
        return false;
    }
}

// ── 辅助：键路径解析 ──────────────────────────────────────────────────────────

const nlohmann::json* Config::TraverseRead(std::string_view key) const
{
    const nlohmann::json* node = &m_data;
    std::string_view remaining = key;

    while (!remaining.empty())
    {
        const auto dotPos = remaining.find('.');
        const std::string segment(remaining.substr(0, dotPos));

        if (!node->is_object() || !node->contains(segment))
        {
            return nullptr;
        }
        node = &((*node)[segment]);

        if (dotPos == std::string_view::npos) break;
        remaining = remaining.substr(dotPos + 1);
    }
    return node;
}

nlohmann::json& Config::TraverseWrite(std::string_view key)
{
    nlohmann::json* node = &m_data;
    std::string_view remaining = key;

    while (!remaining.empty())
    {
        const auto dotPos = remaining.find('.');
        const std::string segment(remaining.substr(0, dotPos));

        if (!node->is_object())
        {
            *node = nlohmann::json::object();
        }
        node = &((*node)[segment]);

        if (dotPos == std::string_view::npos) break;
        remaining = remaining.substr(dotPos + 1);
    }
    return *node;
}

bool Config::Has(std::string_view key) const
{
    return TraverseRead(key) != nullptr;
}

void Config::Remove(std::string_view key)
{
    const auto dotPos = key.rfind('.');
    if (dotPos == std::string_view::npos)
    {
        m_data.erase(std::string(key));
    }
    else
    {
        const std::string parentKey(key.substr(0, dotPos));
        const std::string childKey (key.substr(dotPos + 1));
        if (const auto* parentNode = TraverseRead(parentKey))
        {
            // const_cast 安全：我们是写操作
            auto& parent = const_cast<nlohmann::json&>(*parentNode);
            if (parent.is_object()) parent.erase(childKey);
        }
    }
    m_dirty = true;
}

void Config::ResetToDefaults()
{
    m_data  = {};
    m_dirty = true;
    ApplyDefaults();
    m_dirty = true;   // 确保会追加保存
}

} // namespace sakura::core
