#pragma once

// skin_manager.h — 自定义皮肤与打击音包管理器 (v1.5.0)
// 纯 C++20 跨平台定义（零 SDL 头文件依赖，兼容 sakura-game-logic 与 Linux CI）

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sakura::core
{

struct SkinColor
{
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;

    bool operator==(const SkinColor&) const = default;
};

struct SkinDefinition
{
    std::string id          = "moonlit_sakura";
    std::string name        = "墨樱流光 · Moonlit Ink-Sakura";
    std::string author      = "Sakura Studio";
    std::string description = "经典夜樱粉金琉璃配色与清脆晶石打击音";
    std::string hitsoundSet = "default"; // default | cyber | pure | ukiyo | soft | drum
    std::string folderPath;              // 若存在 hitsounds/ 子目录则优先加载目录内 WAV
    bool        isBuiltin   = true;

    SkinColor outerGemTop   = { 252, 198, 222, 246 };
    SkinColor outerGemBot   = { 224, 128, 172, 236 };
    SkinColor innerGemTop   = { 178, 228, 255, 246 };
    SkinColor innerGemBot   = { 108, 168, 236, 236 };
    SkinColor thumbGemTop   = { 255, 228, 166, 248 };
    SkinColor thumbGemBot   = { 238, 162, 198, 238 };
    SkinColor chordColor    = { 255, 234, 172, 240 };
    SkinColor judgeLine     = { 252, 224, 168, 245 };
    SkinColor circleColor   = { 246, 178, 208, 255 };
    SkinColor sliderCore    = { 252, 222, 164, 240 };
    SkinColor sliderEdge    = { 142, 228, 236, 200 };
    SkinColor approachStart = { 126, 224, 255, 225 };
    SkinColor approachEnd   = { 252, 216, 152, 235 };

    std::array<SkinColor, 5> judgeColors = {{
        { 255, 224, 150, 255 }, // Perfect
        { 142, 222, 252, 255 }, // Great
        { 154, 232, 192, 255 }, // Good
        { 246, 186, 134, 255 }, // Bad
        { 238, 118, 138, 255 }, // Miss
    }};
};

class SkinManager
{
public:
    static SkinManager& GetInstance();

    static const std::vector<SkinDefinition>& GetBuiltinPresets();
    static std::optional<SkinDefinition> ParseSkinJson(std::string_view jsonText,
                                                       std::string_view fallbackId = "custom",
                                                       std::string_view folderPath = {},
                                                       bool isBuiltin = false);

    void ScanSkins(std::string_view builtinSkinsRoot = "resources/skins",
                   std::string_view userSkinsRoot = {});

    const std::vector<SkinDefinition>& GetAvailableSkins() const { return m_skins; }
    const SkinDefinition* FindSkin(std::string_view skinIdOrPath) const;

    bool SetActiveSkin(std::string_view skinIdOrPath);
    const SkinDefinition& GetActiveSkin() const { return m_activeSkin; }
    void LoadActiveFromConfig();

    // 若当前皮肤目录下存在 hitsounds/<fileName> 则返回该完整路径，否则返回空串
    std::string ResolveCustomHitsoundFile(std::string_view fileName) const;

private:
    SkinManager();

    std::vector<SkinDefinition> m_skins;
    SkinDefinition              m_activeSkin;
};

} // namespace sakura::core
