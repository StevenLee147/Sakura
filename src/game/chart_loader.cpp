// chart_loader.cpp — 谱面加载器实现

#include "chart_loader.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace sakura::game
{

// ── 辅助函数 ──────────────────────────────────────────────────────────────────

// 安全获取 JSON 字段，缺失时使用默认值
template<typename T>
static T SafeGet(const json& j, const std::string& key, T defaultVal)
{
    try
    {
        if (j.contains(key) && !j[key].is_null())
            return j[key].get<T>();
    }
    catch (const json::exception& e)
    {
        LOG_WARN("JSON 字段 '{}' 类型错误: {}", key, e.what());
    }
    return defaultVal;
}

// ── NoteType 字符串解析 ───────────────────────────────────────────────────────

NoteType ChartLoader::ParseNoteType(const std::string& typeStr) const
{
    if (typeStr == "tap")    return NoteType::Tap;
    if (typeStr == "hold")   return NoteType::Hold;
    if (typeStr == "drag")   return NoteType::Drag;
    if (typeStr == "circle") return NoteType::Circle;
    if (typeStr == "slider") return NoteType::Slider;

    LOG_WARN("未知音符类型: '{}', 默认为 Tap", typeStr);
    return NoteType::Tap;
}

// ── LoadChartInfo ─────────────────────────────────────────────────────────────

std::optional<ChartInfo> ChartLoader::LoadChartInfo(const std::string& infoJsonPath)
{
    if (!fs::exists(infoJsonPath))
    {
        LOG_ERROR("info.json 不存在: {}", infoJsonPath);
        return std::nullopt;
    }

    std::ifstream file(infoJsonPath);
    if (!file.is_open())
    {
        LOG_ERROR("无法打开 info.json: {}", infoJsonPath);
        return std::nullopt;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        LOG_ERROR("info.json 解析失败 [{}]: {}", infoJsonPath, e.what());
        return std::nullopt;
    }

    ChartInfo info;

    // 版本检查
    int version = SafeGet<int>(j, "version", 1);
    if (version < 1 || version > 2)
    {
        LOG_WARN("info.json 版本 {} 未知，尝试兼容加载", version);
    }
    info.version = version;

    // 必填字段
    info.id      = SafeGet<std::string>(j, "id",      "unknown");
    info.title   = SafeGet<std::string>(j, "title",   "Unknown");
    info.artist  = SafeGet<std::string>(j, "artist",  "Unknown");
    info.charter = SafeGet<std::string>(j, "charter", "Unknown");

    // 可选字段
    info.source  = SafeGet<std::string>(j, "source",  "");
    if (j.contains("tags") && j["tags"].is_array())
    {
        for (const auto& tag : j["tags"])
        {
            if (tag.is_string())
                info.tags.push_back(tag.get<std::string>());
        }
    }

    // 文件名映射
    info.musicFile       = SafeGet<std::string>(j, "music_file",       "music.ogg");
    info.coverFile       = SafeGet<std::string>(j, "cover_file",       "cover.png");
    info.backgroundFile  = SafeGet<std::string>(j, "background_file",  "bg.png");
    info.previewTime     = SafeGet<int>   (j, "preview_time", 0);

    // 音乐属性
    info.bpm    = SafeGet<float>(j, "bpm",    120.0f);
    info.offset = SafeGet<int>  (j, "offset", 0);

    // 难度列表
    if (j.contains("difficulties") && j["difficulties"].is_array())
    {
        for (const auto& diff : j["difficulties"])
        {
            DifficultyInfo di;
            di.name           = SafeGet<std::string>(diff, "name",           "Normal");
            // 兼容 int 和 float 类型的 level
            if (diff.contains("level"))
            {
                if (diff["level"].is_number_integer())
                    di.level = static_cast<float>(diff["level"].get<int>());
                else
                    di.level = SafeGet<float>(diff, "level", 5.0f);
            }
            di.chartFile      = SafeGet<std::string>(diff, "chart_file",     "normal.json");
            di.noteCount      = SafeGet<int>(diff, "note_count",      0);
            di.holdCount      = SafeGet<int>(diff, "hold_count",      0);
            di.mouseNoteCount = SafeGet<int>(diff, "mouse_note_count", 0);
            info.difficulties.push_back(std::move(di));
        }
    }

    if (info.difficulties.empty())
    {
        LOG_WARN("谱面 '{}' 无难度定义", info.id);
    }

    // 填充文件夹路径
    info.folderPath = fs::path(infoJsonPath).parent_path().string();

    LOG_INFO("加载谱面信息成功: {} ({}) [{}难度]",
             info.title, info.id, info.difficulties.size());
    return info;
}

// ── LoadChartData ─────────────────────────────────────────────────────────────

std::optional<ChartData> ChartLoader::LoadChartData(const std::string& chartJsonPath)
{
    if (!fs::exists(chartJsonPath))
    {
        LOG_ERROR("谱面数据文件不存在: {}", chartJsonPath);
        return std::nullopt;
    }

    std::ifstream file(chartJsonPath);
    if (!file.is_open())
    {
        LOG_ERROR("无法打开谱面数据文件: {}", chartJsonPath);
        return std::nullopt;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        LOG_ERROR("谱面数据解析失败 [{}]: {}", chartJsonPath, e.what());
        return std::nullopt;
    }

    ChartData data;
    data.version = SafeGet<int>(j, "version", 1);

    // ── 时间点 ──────────────────────────────────────────────────────────────

    if (j.contains("timing_points") && j["timing_points"].is_array())
    {
        for (const auto& tp : j["timing_points"])
        {
            TimingPoint point;
            point.time = SafeGet<int>(tp, "time", 0);
            point.bpm  = SafeGet<float>(tp, "bpm", 120.0f);

            // 支持 "time_signature": [4, 4] 数组格式
            if (tp.contains("time_signature") && tp["time_signature"].is_array()
                && tp["time_signature"].size() >= 2)
            {
                point.timeSigNumerator   = tp["time_signature"][0].get<int>();
                point.timeSigDenominator = tp["time_signature"][1].get<int>();
            }
            else
            {
                point.timeSigNumerator   = SafeGet<int>(tp, "numerator",   4);
                point.timeSigDenominator = SafeGet<int>(tp, "denominator", 4);
            }
            data.timingPoints.push_back(std::move(point));
        }
    }

    // 若无时间点则添加默认
    if (data.timingPoints.empty())
    {
        data.timingPoints.push_back({ 0, 120.0f, 4, 4 });
    }

    // ── SV 点 ────────────────────────────────────────────────────────────────

    if (j.contains("sv_points") && j["sv_points"].is_array())
    {
        for (const auto& sv : j["sv_points"])
        {
            SVPoint point;
            point.time   = SafeGet<int>(sv, "time", 0);
            point.speed  = SafeGet<float>(sv, "speed", 1.0f);
            point.easing = SafeGet<std::string>(sv, "easing", "linear");
            data.svPoints.push_back(std::move(point));
        }
    }

    // ── 键盘音符 ─────────────────────────────────────────────────────────────

    if (j.contains("keyboard_notes") && j["keyboard_notes"].is_array())
    {
        for (const auto& n : j["keyboard_notes"])
        {
            KeyboardNote note;
            note.time       = SafeGet<int>(n, "time", 0);
            note.lane       = SafeGet<int>(n, "lane", 0);
            note.type       = ParseNoteType(SafeGet<std::string>(n, "type", "tap"));
            note.duration   = SafeGet<int>(n, "duration", 0);
            note.dragToLane = SafeGet<int>(n, "drag_to_lane", -1);
            data.keyboardNotes.push_back(std::move(note));
        }
    }

    // ── 鼠标音符 ─────────────────────────────────────────────────────────────

    if (j.contains("mouse_notes") && j["mouse_notes"].is_array())
    {
        for (const auto& n : j["mouse_notes"])
        {
            MouseNote note;
            note.time            = SafeGet<int>(n, "time", 0);
            note.x               = SafeGet<float>(n, "x", 0.5f);
            note.y               = SafeGet<float>(n, "y", 0.5f);
            note.type            = ParseNoteType(SafeGet<std::string>(n, "type", "circle"));
            note.sliderDuration  = SafeGet<int>(n, "slider_duration", 0);

            // Slider 路径
            if (n.contains("slider_path") && n["slider_path"].is_array())
            {
                for (const auto& pt : n["slider_path"])
                {
                    if (pt.is_array() && pt.size() >= 2)
                    {
                        float px = pt[0].get<float>();
                        float py = pt[1].get<float>();
                        note.sliderPath.emplace_back(px, py);
                    }
                }
            }

            data.mouseNotes.push_back(std::move(note));
        }
    }

    // ── 按时间排序 ────────────────────────────────────────────────────────────

    std::sort(data.timingPoints.begin(), data.timingPoints.end(),
              [](const TimingPoint& a, const TimingPoint& b) { return a.time < b.time; });

    std::sort(data.svPoints.begin(), data.svPoints.end(),
              [](const SVPoint& a, const SVPoint& b) { return a.time < b.time; });

    std::sort(data.keyboardNotes.begin(), data.keyboardNotes.end(),
              [](const KeyboardNote& a, const KeyboardNote& b) { return a.time < b.time; });

    std::sort(data.mouseNotes.begin(), data.mouseNotes.end(),
              [](const MouseNote& a, const MouseNote& b) { return a.time < b.time; });

    LOG_INFO("加载谱面数据成功: 键盘音符={}, 鼠标音符={}, 时间点={}, SV点={}",
             data.keyboardNotes.size(),
             data.mouseNotes.size(),
             data.timingPoints.size(),
             data.svPoints.size());

    return data;
}

// ── ScanCharts ────────────────────────────────────────────────────────────────

std::vector<ChartInfo> ChartLoader::ScanCharts(const std::string& rootDir)
{
    std::vector<ChartInfo> charts;

    if (!fs::exists(rootDir) || !fs::is_directory(rootDir))
    {
        LOG_WARN("谱面根目录不存在: {}", rootDir);
        return charts;
    }

    for (const auto& entry : fs::recursive_directory_iterator(rootDir))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() != "info.json") continue;

        std::string infoPath = entry.path().string();
        auto chartInfo = LoadChartInfo(infoPath);
        if (chartInfo)
        {
            charts.push_back(std::move(*chartInfo));
        }
    }

    LOG_INFO("扫描谱面目录 '{}': 找到 {} 个谱面", rootDir, charts.size());
    return charts;
}

// ── ValidateChartData ─────────────────────────────────────────────────────────

bool ChartLoader::ValidateChartData(const ChartData& data) const
{
    bool valid = true;

    // 检查键盘音符
    for (size_t i = 0; i < data.keyboardNotes.size(); ++i)
    {
        const auto& n = data.keyboardNotes[i];

        if (n.time < 0)
        {
            LOG_WARN("键盘音符[{}] time={} 小于0", i, n.time);
            valid = false;
        }
        if (n.lane < 0 || n.lane > 3)
        {
            LOG_WARN("键盘音符[{}] lane={} 超出范围 [0,3]", i, n.lane);
            valid = false;
        }
        if (n.type == NoteType::Hold && n.duration <= 0)
        {
            LOG_WARN("键盘音符[{}] Hold 但 duration={}", i, n.duration);
        }
        // 按时间升序检查
        if (i > 0 && n.time < data.keyboardNotes[i-1].time)
        {
            LOG_WARN("键盘音符[{}] 未按时间升序排列", i);
            valid = false;
        }
    }

    // 检查鼠标音符
    for (size_t i = 0; i < data.mouseNotes.size(); ++i)
    {
        const auto& n = data.mouseNotes[i];

        if (n.time < 0)
        {
            LOG_WARN("鼠标音符[{}] time={} 小于0", i, n.time);
            valid = false;
        }
        if (n.x < 0.0f || n.x > 1.0f || n.y < 0.0f || n.y > 1.0f)
        {
            LOG_WARN("鼠标音符[{}] 坐标({:.2f},{:.2f}) 超出[0,1]范围", i, n.x, n.y);
            valid = false;
        }
        if (i > 0 && n.time < data.mouseNotes[i-1].time)
        {
            LOG_WARN("鼠标音符[{}] 未按时间升序排列", i);
            valid = false;
        }
    }

    // 检查时间点
    if (data.timingPoints.empty())
    {
        LOG_WARN("谱面无时间点");
        valid = false;
    }
    else if (data.timingPoints[0].time != 0)
    {
        LOG_WARN("第一个时间点不在 time=0");
    }

    return valid;
}

} // namespace sakura::game
