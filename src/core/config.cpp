#include "config.h"
#include "utils/logger.h"

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
    setDefault(ConfigKeys::kWindowWidth,  1920);
    setDefault(ConfigKeys::kWindowHeight, 1080);
    setDefault(ConfigKeys::kFullscreen,   false);
    setDefault(ConfigKeys::kVSync,        true);
    setDefault(ConfigKeys::kFpsLimit,     0);

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

    // 图形
    setDefault(ConfigKeys::kParticles,    true);
    setDefault(ConfigKeys::kBloom,        false);
    setDefault(ConfigKeys::kSkinPath,     std::string("resources/skins/default"));

    m_dirty = false;  // 默认值不算脏
}

// ── 文件操作 ──────────────────────────────────────────────────────────────────

bool Config::Load(std::string_view path)
{
    m_filePath = std::string(path);
    m_dirty    = false;

    std::filesystem::path fsPath(path);
    if (!std::filesystem::exists(fsPath))
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

        std::ofstream ofs(fsPath);
        if (!ofs.is_open())
        {
            LOG_ERROR("Config: 无法写入配置文件: {}", m_filePath);
            return false;
        }

        ofs << m_data.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
        ofs.close();

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
