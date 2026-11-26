#include "core/skin_manager.h"

#include "core/config.h"
#include "core/paths.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sakura::core
{

namespace
{

bool ParseChannel(const nlohmann::json& item, std::uint8_t& out)
{
    if (item.is_number_unsigned())
    {
        const auto v = item.get<std::uint64_t>();
        out = static_cast<std::uint8_t>(std::min<std::uint64_t>(v, 255ULL));
        return true;
    }
    if (item.is_number_integer())
    {
        const auto v = item.get<std::int64_t>();
        out = static_cast<std::uint8_t>(std::clamp<std::int64_t>(v, 0, 255));
        return true;
    }
    if (item.is_number_float())
    {
        const double v = item.get<double>();
        if (!std::isfinite(v))
        {
            return false;
        }
        const auto rounded = static_cast<std::int64_t>(std::llround(v));
        out = static_cast<std::uint8_t>(std::clamp<std::int64_t>(rounded, 0, 255));
        return true;
    }
    return false;
}

bool ParseHexNibble(char ch, std::uint8_t& out)
{
    if (ch >= '0' && ch <= '9')
    {
        out = static_cast<std::uint8_t>(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        out = static_cast<std::uint8_t>(10 + (ch - 'a'));
        return true;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        out = static_cast<std::uint8_t>(10 + (ch - 'A'));
        return true;
    }
    return false;
}

bool ParseHexByte(char hi, char lo, std::uint8_t& out)
{
    std::uint8_t h = 0;
    std::uint8_t l = 0;
    if (!ParseHexNibble(hi, h) || !ParseHexNibble(lo, l))
    {
        return false;
    }
    out = static_cast<std::uint8_t>((h << 4U) | l);
    return true;
}

bool TryParseSkinColor(const nlohmann::json& value, SkinColor& out)
{
    if (value.is_array())
    {
        if (value.size() != 3 && value.size() != 4)
        {
            return false;
        }
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;
        if (!ParseChannel(value[0], r) ||
            !ParseChannel(value[1], g) ||
            !ParseChannel(value[2], b))
        {
            return false;
        }
        if (value.size() == 4 && !ParseChannel(value[3], a))
        {
            return false;
        }
        out = SkinColor{ r, g, b, a };
        return true;
    }

    if (value.is_string())
    {
        std::string_view hex = value.get_ref<const std::string&>();
        while (!hex.empty() && std::isspace(static_cast<unsigned char>(hex.front())) != 0)
        {
            hex.remove_prefix(1);
        }
        while (!hex.empty() && std::isspace(static_cast<unsigned char>(hex.back())) != 0)
        {
            hex.remove_suffix(1);
        }

        if (hex.empty() || hex.front() != '#')
        {
            return false;
        }
        hex.remove_prefix(1);
        if (hex.size() != 6 && hex.size() != 8)
        {
            return false;
        }

        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;
        if (!ParseHexByte(hex[0], hex[1], r) ||
            !ParseHexByte(hex[2], hex[3], g) ||
            !ParseHexByte(hex[4], hex[5], b))
        {
            return false;
        }
        if (hex.size() == 8 && !ParseHexByte(hex[6], hex[7], a))
        {
            return false;
        }
        out = SkinColor{ r, g, b, a };
        return true;
    }

    return false;
}

void ReadColorField(const nlohmann::json& obj, const char* key, SkinColor& target)
{
    const auto it = obj.find(key);
    if (it != obj.end())
    {
        TryParseSkinColor(*it, target);
    }
}

void ReadStringField(const nlohmann::json& obj, const char* key, std::string& target, bool allowEmpty = false)
{
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string())
    {
        const auto& str = it->get_ref<const std::string&>();
        if (allowEmpty || !str.empty())
        {
            target = str;
        }
    }
}

std::string NormalizePathKey(std::string_view raw)
{
    std::string normalized;
    normalized.reserve(raw.size());
    for (char ch : raw)
    {
        normalized.push_back(ch == '\\' ? '/' : ch);
    }
    while (normalized.rfind("./", 0) == 0)
    {
        normalized.erase(0, 2);
    }
    while (normalized.size() > 1 && normalized.back() == '/')
    {
        normalized.pop_back();
    }
    return normalized;
}

} // namespace

SkinManager::SkinManager()
    : m_activeSkin(GetBuiltinPresets().front())
{
}

SkinManager& SkinManager::GetInstance()
{
    static SkinManager instance;
    return instance;
}

const std::vector<SkinDefinition>& SkinManager::GetBuiltinPresets()
{
    static const std::vector<SkinDefinition> kPresets = []()
    {
        std::vector<SkinDefinition> presets;
        presets.reserve(4);

        // 1) 墨樱流光 · Moonlit Ink-Sakura
        SkinDefinition moonlit{};
        moonlit.id            = "moonlit_sakura";
        moonlit.name          = "墨樱流光 · Moonlit Ink-Sakura";
        moonlit.author        = "Sakura Studio";
        moonlit.description   = "经典夜樱粉金琉璃配色与清脆晶石打击音";
        moonlit.hitsoundSet   = "default";
        moonlit.isBuiltin     = true;
        moonlit.outerGemTop   = { 252, 198, 222, 246 };
        moonlit.outerGemBot   = { 224, 128, 172, 236 };
        moonlit.innerGemTop   = { 178, 228, 255, 246 };
        moonlit.innerGemBot   = { 108, 168, 236, 236 };
        moonlit.thumbGemTop   = { 255, 228, 166, 248 };
        moonlit.thumbGemBot   = { 238, 162, 198, 238 };
        moonlit.chordColor    = { 255, 234, 172, 240 };
        moonlit.judgeLine     = { 252, 224, 168, 245 };
        moonlit.circleColor   = { 246, 178, 208, 255 };
        moonlit.sliderCore    = { 252, 222, 164, 240 };
        moonlit.sliderEdge    = { 142, 228, 236, 200 };
        moonlit.approachStart = { 126, 224, 255, 225 };
        moonlit.approachEnd   = { 252, 216, 152, 235 };
        moonlit.judgeColors   = {{
            { 255, 224, 150, 255 },
            { 142, 222, 252, 255 },
            { 154, 232, 192, 255 },
            { 246, 186, 134, 255 },
            { 238, 118, 138, 255 },
        }};
        presets.push_back(std::move(moonlit));

        // 2) 霓虹赛博 · Neon Cyber-Pulse
        SkinDefinition cyber{};
        cyber.id            = "neon_cyber";
        cyber.name          = "霓虹赛博 · Neon Cyber-Pulse";
        cyber.author        = "Sakura Studio";
        cyber.description   = "电光青紫高对比度赛博灯带与电子合成瞬态打击音";
        cyber.hitsoundSet   = "cyber";
        cyber.isBuiltin     = true;
        cyber.outerGemTop   = { 255, 68, 204, 248 };
        cyber.outerGemBot   = { 172, 28, 148, 238 };
        cyber.innerGemTop   = { 56, 246, 255, 248 };
        cyber.innerGemBot   = { 18, 148, 218, 238 };
        cyber.thumbGemTop   = { 196, 255, 92, 248 };
        cyber.thumbGemBot   = { 72, 224, 188, 238 };
        cyber.chordColor    = { 188, 255, 118, 242 };
        cyber.judgeLine     = { 96, 248, 255, 248 };
        cyber.circleColor   = { 64, 242, 255, 255 };
        cyber.sliderCore    = { 255, 84, 214, 242 };
        cyber.sliderEdge    = { 56, 246, 255, 212 };
        cyber.approachStart = { 255, 76, 212, 228 };
        cyber.approachEnd   = { 64, 248, 255, 242 };
        cyber.judgeColors   = {{
            { 112, 252, 255, 255 },
            { 255, 98, 224, 255 },
            { 182, 255, 108, 255 },
            { 255, 174, 76, 255 },
            { 255, 68, 108, 255 },
        }};
        presets.push_back(std::move(cyber));

        // 3) 竞技极简 · Esports Pure
        SkinDefinition pure{};
        pure.id            = "esports_pure";
        pure.name          = "竞技极简 · Esports Pure";
        pure.author        = "Sakura Studio";
        pure.description   = "赛事级高辨识纯色无干扰几何视效与干脆机械木鱼打击音";
        pure.hitsoundSet   = "pure";
        pure.isBuiltin     = true;
        pure.outerGemTop   = { 248, 250, 255, 252 };
        pure.outerGemBot   = { 198, 208, 228, 244 };
        pure.innerGemTop   = { 112, 198, 255, 252 };
        pure.innerGemBot   = { 68, 138, 224, 244 };
        pure.thumbGemTop   = { 255, 212, 84, 252 };
        pure.thumbGemBot   = { 228, 158, 42, 244 };
        pure.chordColor    = { 255, 226, 118, 246 };
        pure.judgeLine     = { 244, 248, 255, 250 };
        pure.circleColor   = { 255, 196, 92, 255 };
        pure.sliderCore    = { 246, 250, 255, 244 };
        pure.sliderEdge    = { 112, 198, 255, 216 };
        pure.approachStart = { 148, 214, 255, 232 };
        pure.approachEnd   = { 255, 214, 96, 246 };
        pure.judgeColors   = {{
            { 255, 222, 98, 255 },
            { 118, 206, 255, 255 },
            { 136, 236, 176, 255 },
            { 252, 172, 92, 255 },
            { 248, 92, 98, 255 },
        }};
        presets.push_back(std::move(pure));

        // 4) 浮世金翠 · Ukiyo Jade-Gold
        SkinDefinition ukiyo{};
        ukiyo.id            = "ukiyo_jade";
        ukiyo.name          = "浮世金翠 · Ukiyo Jade-Gold";
        ukiyo.author        = "Sakura Studio";
        ukiyo.description   = "东方青玉翡翠与鎏金朱砂古风配色及竹韵太鼓打击音";
        ukiyo.hitsoundSet   = "ukiyo";
        ukiyo.isBuiltin     = true;
        ukiyo.outerGemTop   = { 148, 238, 202, 248 };
        ukiyo.outerGemBot   = { 74, 168, 138, 238 };
        ukiyo.innerGemTop   = { 252, 232, 178, 248 };
        ukiyo.innerGemBot   = { 206, 162, 96, 238 };
        ukiyo.thumbGemTop   = { 255, 176, 122, 248 };
        ukiyo.thumbGemBot   = { 214, 92, 78, 238 };
        ukiyo.chordColor    = { 248, 218, 146, 242 };
        ukiyo.judgeLine     = { 244, 214, 148, 246 };
        ukiyo.circleColor   = { 126, 232, 192, 255 };
        ukiyo.sliderCore    = { 248, 222, 158, 242 };
        ukiyo.sliderEdge    = { 118, 226, 188, 208 };
        ukiyo.approachStart = { 148, 238, 202, 228 };
        ukiyo.approachEnd   = { 252, 212, 136, 240 };
        ukiyo.judgeColors   = {{
            { 252, 218, 132, 255 },
            { 138, 236, 198, 255 },
            { 186, 232, 154, 255 },
            { 242, 168, 108, 255 },
            { 226, 98, 92, 255 },
        }};
        presets.push_back(std::move(ukiyo));

        return presets;
    }();
    return kPresets;
}

std::optional<SkinDefinition> SkinManager::ParseSkinJson(
    std::string_view jsonText,
    std::string_view fallbackId,
    std::string_view folderPath,
    bool isBuiltin)
{
    const nlohmann::json doc = nlohmann::json::parse(
        jsonText.begin(), jsonText.end(), nullptr, false, true);
    if (doc.is_discarded() || !doc.is_object())
    {
        return std::nullopt;
    }

    SkinDefinition skin = GetBuiltinPresets().front();
    skin.id             = fallbackId.empty() ? std::string("custom") : std::string(fallbackId);
    skin.folderPath     = std::string(folderPath);
    skin.isBuiltin      = isBuiltin;

    ReadStringField(doc, "id", skin.id, false);
    ReadStringField(doc, "name", skin.name, false);
    ReadStringField(doc, "author", skin.author, true);
    ReadStringField(doc, "description", skin.description, true);
    ReadStringField(doc, "hitsound_set", skin.hitsoundSet, false);

    ReadColorField(doc, "outer_gem_top", skin.outerGemTop);
    ReadColorField(doc, "outer_gem_bot", skin.outerGemBot);
    ReadColorField(doc, "inner_gem_top", skin.innerGemTop);
    ReadColorField(doc, "inner_gem_bot", skin.innerGemBot);
    ReadColorField(doc, "thumb_gem_top", skin.thumbGemTop);
    ReadColorField(doc, "thumb_gem_bot", skin.thumbGemBot);
    ReadColorField(doc, "chord_color", skin.chordColor);
    ReadColorField(doc, "judge_line", skin.judgeLine);
    ReadColorField(doc, "circle_color", skin.circleColor);
    ReadColorField(doc, "slider_core", skin.sliderCore);
    ReadColorField(doc, "slider_edge", skin.sliderEdge);
    ReadColorField(doc, "approach_start", skin.approachStart);
    ReadColorField(doc, "approach_end", skin.approachEnd);

    const auto judgeIt = doc.find("judge_colors");
    if (judgeIt != doc.end() && judgeIt->is_array())
    {
        const std::size_t count = std::min<std::size_t>(judgeIt->size(), skin.judgeColors.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            TryParseSkinColor((*judgeIt)[i], skin.judgeColors[i]);
        }
    }

    return skin;
}

void SkinManager::ScanSkins(std::string_view builtinSkinsRoot, std::string_view userSkinsRoot)
{
    m_skins = GetBuiltinPresets();

    auto upsertSkin = [this](SkinDefinition&& parsed)
    {
        auto it = std::find_if(m_skins.begin(), m_skins.end(),
            [&parsed](const SkinDefinition& existing)
            {
                return existing.id == parsed.id;
            });
        if (it != m_skins.end())
        {
            if (parsed.folderPath.empty())
            {
                parsed.folderPath = it->folderPath;
            }
            *it = std::move(parsed);
        }
        else
        {
            m_skins.push_back(std::move(parsed));
        }
    };

    auto scanDirectory = [&upsertSkin](const std::filesystem::path& rootPath, bool isBuiltin)
    {
        if (rootPath.empty())
        {
            return;
        }

        std::error_code ec;
        if (!std::filesystem::exists(rootPath, ec) || !std::filesystem::is_directory(rootPath, ec))
        {
            return;
        }

        std::vector<std::filesystem::path> subDirs;
        for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec))
        {
            if (ec)
            {
                break;
            }
            std::error_code dirEc;
            if (entry.is_directory(dirEc) && !dirEc)
            {
                subDirs.push_back(entry.path());
            }
        }

        std::sort(subDirs.begin(), subDirs.end(),
            [](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
            {
                return lhs.filename().generic_string() < rhs.filename().generic_string();
            });

        for (const auto& subDir : subDirs)
        {
            const std::filesystem::path manifestPath = subDir / "skin.json";
            std::error_code fileEc;
            if (!std::filesystem::is_regular_file(manifestPath, fileEc) || fileEc)
            {
                continue;
            }

            std::ifstream ifs(manifestPath, std::ios::binary);
            if (!ifs.is_open())
            {
                LOG_WARN("SkinManager: 无法读取皮肤配置 ({})", manifestPath.generic_string());
                continue;
            }

            std::ostringstream oss;
            oss << ifs.rdbuf();
            const std::string fallbackId = subDir.filename().generic_string();
            const std::string folderStr  = subDir.generic_string();

            auto parsed = ParseSkinJson(
                oss.str(),
                fallbackId.empty() ? std::string_view("custom") : std::string_view(fallbackId),
                folderStr,
                isBuiltin);
            if (!parsed.has_value())
            {
                LOG_WARN("SkinManager: 跳过无效的 skin.json ({})", manifestPath.generic_string());
                continue;
            }

            upsertSkin(std::move(*parsed));
        }
    };

    const std::filesystem::path builtinPath = builtinSkinsRoot.empty()
        ? std::filesystem::path{}
        : std::filesystem::path(builtinSkinsRoot);
    if (!builtinPath.empty())
    {
        scanDirectory(builtinPath, true);
    }

    const std::string resolvedUserRoot = userSkinsRoot.empty()
        ? Paths::User("skins")
        : std::string(userSkinsRoot);
    if (!resolvedUserRoot.empty())
    {
        const std::filesystem::path userPath(resolvedUserRoot);
        std::error_code eqEc;
        const bool sameAsBuiltin = !builtinPath.empty()
            && std::filesystem::exists(builtinPath, eqEc)
            && !eqEc
            && std::filesystem::exists(userPath, eqEc)
            && !eqEc
            && std::filesystem::equivalent(builtinPath, userPath, eqEc)
            && !eqEc;
        if (!sameAsBuiltin)
        {
            scanDirectory(userPath, false);
        }
    }

    if (const SkinDefinition* activeMatch = FindSkin(m_activeSkin.id))
    {
        m_activeSkin = *activeMatch;
    }
    else if (!m_skins.empty())
    {
        m_activeSkin = m_skins.front();
    }
}

const SkinDefinition* SkinManager::FindSkin(std::string_view skinIdOrPath) const
{
    const auto& skins = m_skins.empty() ? GetBuiltinPresets() : m_skins;

    if (!skinIdOrPath.empty())
    {
        for (const auto& skin : skins)
        {
            if (skin.id == skinIdOrPath ||
                (!skin.folderPath.empty() && skin.folderPath == skinIdOrPath))
            {
                return &skin;
            }
        }
    }

    const std::string normQuery = NormalizePathKey(skinIdOrPath);
    if (!normQuery.empty())
    {
        for (const auto& skin : skins)
        {
            if (!skin.folderPath.empty() && NormalizePathKey(skin.folderPath) == normQuery)
            {
                return &skin;
            }
        }
    }

    if (skinIdOrPath.empty() || normQuery == "default" || normQuery == "resources/skins/default")
    {
        for (const auto& skin : skins)
        {
            if (skin.id == "moonlit_sakura")
            {
                return &skin;
            }
        }
    }

    return nullptr;
}

bool SkinManager::SetActiveSkin(std::string_view skinIdOrPath)
{
    if (m_skins.empty())
    {
        ScanSkins();
    }

    if (const SkinDefinition* matched = FindSkin(skinIdOrPath))
    {
        m_activeSkin = *matched;
        return true;
    }

    if (const SkinDefinition* fallback = FindSkin("moonlit_sakura"))
    {
        m_activeSkin = *fallback;
    }
    else
    {
        m_activeSkin = GetBuiltinPresets().front();
    }
    LOG_WARN("SkinManager: 未找到皮肤 '{}'，已回退至 '{}'", skinIdOrPath, m_activeSkin.id);
    return false;
}

void SkinManager::LoadActiveFromConfig()
{
    const std::string configuredSkin =
        Config::GetInstance().Get<std::string>(ConfigKeys::kSkinPath, "moonlit_sakura");
    SetActiveSkin(configuredSkin);
}

std::string SkinManager::ResolveCustomHitsoundFile(std::string_view fileName) const
{
    if (fileName.empty() || m_activeSkin.folderPath.empty())
    {
        return {};
    }

    const std::filesystem::path candidate =
        std::filesystem::path(m_activeSkin.folderPath) / "hitsounds" / std::filesystem::path(fileName);
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec)
    {
        return candidate.generic_string();
    }
    return {};
}

} // namespace sakura::core
