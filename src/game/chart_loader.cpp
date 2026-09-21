// chart_loader.cpp — 谱面加载器实现

#include "chart_loader.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace sakura::game
{

// ── 辅助函数 ──────────────────────────────────────────────────────────────────

static std::string ToGenericString(const fs::path& path)
{
    return path.lexically_normal().generic_string();
}

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
    if (typeStr == "drag")   return NoteType::Hold;
    if (typeStr == "circle") return NoteType::Circle;
    if (typeStr == "slider") return NoteType::Slider;

    LOG_WARN("未知音符类型: '{}', 默认为 Tap", typeStr);
    return NoteType::Tap;
}

// ── LoadChartInfo ─────────────────────────────────────────────────────────────

std::optional<ChartInfo> ChartLoader::LoadChartInfo(const std::string& infoJsonPath)
{
    try
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

    if (!j.is_object()) return std::nullopt;
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

    auto safeFile = [](const std::string& name)
    {
        if (name.empty()) return true;
        const fs::path path(name);
        if (path.is_absolute() || path.has_root_name() || name.find(':') != std::string::npos) return false;
        for (const auto& part : path) if (part == "..") return false;
        return true;
    };
    if (!safeFile(info.musicFile) || !safeFile(info.coverFile) || !safeFile(info.backgroundFile) ||
        !std::isfinite(info.bpm) || info.bpm <= 0 || info.bpm > 1000 || std::abs(static_cast<long long>(info.offset)) > 60000)
        return std::nullopt;
    for (const auto& difficulty : info.difficulties)
        if (!safeFile(difficulty.chartFile) || difficulty.chartFile.empty() || !std::isfinite(difficulty.level) ||
            difficulty.level < 0 || difficulty.level > 100) return std::nullopt;
    if (info.difficulties.empty())
    {
        LOG_WARN("谱面 '{}' 无难度定义", info.id);
        return std::nullopt;
    }

    // 填充文件夹路径
    info.folderPath = ToGenericString(fs::path(infoJsonPath).parent_path());

    LOG_INFO("加载谱面信息成功: {} ({}) [{}难度]",
             info.title, info.id, info.difficulties.size());
    return info;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("谱面读取失败: {}", e.what());
        return std::nullopt;
    }
}

// ── LoadChartData ─────────────────────────────────────────────────────────────

std::optional<ChartData> ChartLoader::LoadChartData(const std::string& chartJsonPath)
{
    try
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

    if (!j.is_object()) return std::nullopt;
    for (const char* key : {"keyboard_notes", "mouse_notes", "timing_points", "sv_points"})
        if (j.contains(key) && (!j[key].is_array() || j[key].size() > 100000)) return std::nullopt;
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
            const std::string rawType = SafeGet<std::string>(n, "type", "tap");
            note.time       = SafeGet<int>(n, "time", 0);
            note.lane       = SafeGet<int>(n, "lane", 0);
            note.type       = ParseNoteType(rawType);
            note.duration   = SafeGet<int>(n, "duration", 0);
            if (rawType == "drag")
            {
                LOG_WARN("谱面 '{}' 在 time={} lane={} 仍使用已移除的 Drag 音符类型，将按 Hold 兼容加载",
                    chartJsonPath, note.time, note.lane);
            }
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

    std::stable_sort(data.timingPoints.begin(), data.timingPoints.end(),
              [](const TimingPoint& a, const TimingPoint& b) { return a.time < b.time; });

    std::stable_sort(data.svPoints.begin(), data.svPoints.end(),
              [](const SVPoint& a, const SVPoint& b) { return a.time < b.time; });

    std::stable_sort(data.keyboardNotes.begin(), data.keyboardNotes.end(),
              [](const KeyboardNote& a, const KeyboardNote& b) { return a.time < b.time; });

    std::stable_sort(data.mouseNotes.begin(), data.mouseNotes.end(),
              [](const MouseNote& a, const MouseNote& b) { return a.time < b.time; });

    LOG_INFO("加载谱面数据成功: 键盘音符={}, 鼠标音符={}, 时间点={}, SV点={}",
             data.keyboardNotes.size(),
             data.mouseNotes.size(),
             data.timingPoints.size(),
             data.svPoints.size());

    if (!ValidateChartData(data)) return std::nullopt;
    return data;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("谱面读取失败: {}", e.what());
        return std::nullopt;
    }
}

// ── ScanCharts ────────────────────────────────────────────────────────────────

std::vector<ChartInfo> ChartLoader::ScanCharts(const std::string& rootDir)
{
    std::vector<ChartInfo> charts;

    std::error_code scanError;
    if (!fs::is_directory(rootDir,scanError))
    {
        LOG_WARN("谱面根目录不存在: {}", rootDir);
        return charts;
    }

    fs::recursive_directory_iterator iterator(rootDir,fs::directory_options::skip_permission_denied,scanError),end;
    while(iterator!=end && !scanError) {
        const auto entry=*iterator;
        if(entry.is_regular_file(scanError) && entry.path().filename()=="info.json") {
            if(auto chartInfo=LoadChartInfo(entry.path().string()))charts.push_back(std::move(*chartInfo));
        }
        if(iterator.depth()>8)iterator.disable_recursion_pending();
        iterator.increment(scanError);
    }
    if(scanError)LOG_WARN("部分曲库无法读取: {}",scanError.message());

    std::stable_sort(charts.begin(), charts.end(), [](const auto& a, const auto& b) { return a.title < b.title; });
    LOG_INFO("扫描谱面目录 '{}': 找到 {} 个谱面", rootDir, charts.size());
    return charts;
}

// ── ValidateChartData ─────────────────────────────────────────────────────────

bool ChartLoader::ValidateChartData(const ChartData& data) const
{
    constexpr int maxTime = 3600000;
    auto position = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (data.keyboardNotes.size() + data.mouseNotes.size() > 100000 || data.timingPoints.empty()) return false;
    int previous = -1;
    for (const auto& note : data.keyboardNotes)
    {
        if (note.time < previous || note.time < 0 || note.time > maxTime || note.lane < 0 || note.lane > 3 ||
            (note.type != NoteType::Tap && note.type != NoteType::Hold) || note.duration < 0 ||
            note.duration > maxTime - note.time || (note.type == NoteType::Hold && note.duration == 0)) return false;
        previous = note.time;
    }
    previous = -1;
    for (const auto& note : data.mouseNotes)
    {
        if (note.time < previous || note.time < 0 || note.time > maxTime || !position(note.x) || !position(note.y) ||
            (note.type != NoteType::Circle && note.type != NoteType::Slider) || note.sliderDuration < 0 ||
            note.sliderDuration > maxTime - note.time || note.sliderPath.size() > 1024) return false;
        if (note.type == NoteType::Slider && (note.sliderDuration <= 0 || note.sliderPath.empty())) return false;
        for (const auto& [x, y] : note.sliderPath) if (!position(x) || !position(y)) return false;
        previous = note.time;
    }
    previous = -1;
    for (const auto& point : data.timingPoints)
    {
        if (point.time < previous || point.time > maxTime || !std::isfinite(point.bpm) || point.bpm <= 0 ||
            point.bpm > 1000 || point.timeSigNumerator < 1 || point.timeSigDenominator < 1) return false;
        previous = point.time;
    }
    previous = -1;
    for (const auto& point : data.svPoints)
    {
        if (point.time < previous || point.time > maxTime || !std::isfinite(point.speed) ||
            point.speed <= 0 || point.speed > 10) return false;
        previous = point.time;
    }
    return true;
}

} // namespace sakura::game
