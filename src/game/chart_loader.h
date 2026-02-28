#pragma once

// chart_loader.h — 谱面加载器
// 负责从 JSON 文件解析 ChartInfo 和 ChartData

#include "chart.h"
#include <optional>
#include <string>
#include <vector>

namespace sakura::game
{

// ChartLoader — 谱面文件读取与校验
class ChartLoader
{
public:
    ChartLoader()  = default;
    ~ChartLoader() = default;

    // ── 文件加载 ──────────────────────────────────────────────────────────────

    // 加载谱面元信息（info.json），同时填充 folderPath
    std::optional<ChartInfo>  LoadChartInfo(const std::string& infoJsonPath);

    // 加载谱面数据（{difficulty}.json）
    std::optional<ChartData>  LoadChartData(const std::string& chartJsonPath);

    // ── 目录扫描 ──────────────────────────────────────────────────────────────

    // 递归扫描 rootDir 中所有含 info.json 的子目录，返回 ChartInfo 列表
    // 加载失败的谱面会 LOG_ERROR 并跳过
    std::vector<ChartInfo> ScanCharts(const std::string& rootDir);

    // ── 校验 ──────────────────────────────────────────────────────────────────

    // 检查 ChartData 合法性：时间排序、轨道范围（0~3）、坐标范围（0~1）
    bool ValidateChartData(const ChartData& data) const;

private:
    // 解析 NoteType 字符串
    NoteType ParseNoteType(const std::string& typeStr) const;
};

} // namespace sakura::game
