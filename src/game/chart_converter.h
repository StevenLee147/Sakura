#pragma once

// chart_converter.h — Phira (.pez) / osu! (.osz) / StepMania (.sm) 双区智能谱面转换引擎 (v1.7.0)
// 纯 C++20 跨平台实现（零 SDL 依赖，纳入 sakura-game-logic 与 Linux CI 单测）

#include "game/chart.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sakura::game
{

enum class ExternalChartFormat
{
    Unknown,
    SakuraNative,
    PhiraPez,
    PhiraJson,
    OsuArchive,
    OsuFile,
    StepManiaSm
};

struct ConversionResult
{
    bool                ok                    = false;
    ExternalChartFormat format                = ExternalChartFormat::Unknown;
    std::string         error;
    std::string         outputChartDir;
    ChartInfo           chartInfo;
    int                 convertedDifficulties = 0;
};

class ChartConverter
{
public:
    static ExternalChartFormat DetectFormat(const std::filesystem::path& sourcePath);

    // 一键将外部谱面包 (.pez / .osz / .sm / .osu / 文件夹) 转换并安装至 destChartsRoot/<slug>
    static ConversionResult ImportExternalPackage(const std::filesystem::path& sourcePath,
                                                  const std::filesystem::path& destChartsRoot);

    // Phigros Official JSON (formatVersion 1 / 3) -> Sakura 双区 Chart v4
    static std::optional<ChartData> ConvertPhigrosOfficialJson(std::string_view jsonText,
                                                               float fallbackBpm = 120.0f);

    // Phira / Re:PhiEdit RPE JSON (BPMList + 分数拍三元组 + 多判定线) -> Sakura 双区 Chart v4
    static std::optional<ChartData> ConvertPhiraRpeJson(std::string_view jsonText,
                                                        ChartInfo* outMeta = nullptr,
                                                        std::string* outLevelText = nullptr);

    // 自动识别 Phigros Official JSON 或 RPE JSON 并转换
    static std::optional<ChartData> ConvertAnyPhiraJson(std::string_view jsonText,
                                                        float fallbackBpm = 120.0f,
                                                        ChartInfo* outMeta = nullptr,
                                                        std::string* outLevelText = nullptr);

    // osu! (.osu v14: Mode 3 mania & Mode 0 standard) -> Sakura 双区 Chart v4
    static std::optional<ChartData> ConvertOsuBeatmapText(std::string_view osuText,
                                                          ChartInfo* outMeta = nullptr,
                                                          DifficultyInfo* outDiffInfo = nullptr);

    // StepMania (.sm: dance-single 多难度小节网格) -> Sakura 双区 Chart v4
    static bool ConvertStepManiaSmText(std::string_view smText,
                                       ChartInfo& outMeta,
                                       std::vector<std::pair<DifficultyInfo, ChartData>>& outDifficulties);

    // 对任意鼠标音符序列执行 Δt 正相关间距舒展与防重叠优化
    static void OptimizeMouseSpatialLayout(std::vector<MouseNote>& mouseNotes);
};

} // namespace sakura::game
