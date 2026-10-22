// record_script.cpp — 离线录制脚本解析与辅助逻辑

#include "record_script.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace sakura::core
{

namespace
{

using nlohmann::json;

constexpr int    MIN_RECORD_SIZE      = 16;
constexpr int    MAX_RECORD_SIZE      = 8192;
constexpr int    MAX_RECORD_FPS       = 240;
constexpr int    MIN_PREVIEW_WIDTH    = 960;
constexpr int    MAX_PREVIEW_WIDTH    = 7680;
constexpr int    MIN_PREVIEW_HEIGHT   = 540;
constexpr int    MAX_PREVIEW_HEIGHT   = 4320;
constexpr int    SETTINGS_PAGE_COUNT  = 6;
constexpr double MAX_SHOT_SECONDS     = 24.0 * 3600.0;
constexpr int    MAX_CHART_TIME_MS    = 24 * 3600 * 1000;
constexpr size_t MAX_SHOT_NAME_LENGTH = 120;

struct ParseFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

[[noreturn]] void Fail(const std::string& path, const std::string& message)
{
    throw ParseFailure(path + ": " + message);
}

std::string Lower(std::string text)
{
    for (auto& c : text)
        if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string Join(const std::string& path, const char* key)
{
    return path.empty() ? std::string(key) : path + "." + key;
}

void WarnUnknownKeys(const json& object, const std::set<std::string>& known, const std::string& path,
                     std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : object.items())
        if (!known.contains(key)) warnings.push_back(Join(path, key.c_str()) + ": 未识别的字段，已忽略");
}

double ReadNumber(const json& object, const char* key, const std::string& path, double fallback,
                  double low, double high)
{
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    if (!it->is_number()) Fail(Join(path, key), "必须是数字");
    const double value = it->get<double>();
    if (!std::isfinite(value) || value < low || value > high)
        Fail(Join(path, key), "超出范围 [" + std::to_string(low) + ", " + std::to_string(high) + "]");
    return value;
}

int ReadInt(const json& object, const char* key, const std::string& path, int fallback, int low, int high)
{
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    if (!it->is_number()) Fail(Join(path, key), "必须是整数");
    const double value = it->get<double>();
    if (!std::isfinite(value) || std::floor(value) != value) Fail(Join(path, key), "必须是整数");
    if (value < low || value > high)
        Fail(Join(path, key), "超出范围 [" + std::to_string(low) + ", " + std::to_string(high) + "]");
    return static_cast<int>(value);
}

std::string ReadString(const json& object, const char* key, const std::string& path, const std::string& fallback)
{
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    if (!it->is_string()) Fail(Join(path, key), "必须是字符串");
    return it->get<std::string>();
}

std::pair<float, float> ReadPoint(const json& value, const std::string& path)
{
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
        Fail(path, "必须是 [x, y] 归一化坐标");
    const double x = value[0].get<double>();
    const double y = value[1].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
        Fail(path, "坐标必须在 0~1 之间");
    return { static_cast<float>(x), static_cast<float>(y) };
}

unsigned ParseModifiers(const std::string& text, const std::string& path)
{
    unsigned modifiers = 0;
    std::string token;
    auto flush = [&]()
    {
        const std::string name = Lower(token);
        token.clear();
        if (name.empty()) return;
        if (name == "ctrl" || name == "control") modifiers |= RECORD_MOD_CTRL;
        else if (name == "shift") modifiers |= RECORD_MOD_SHIFT;
        else if (name == "alt") modifiers |= RECORD_MOD_ALT;
        else Fail(path, "未知修饰键 \"" + name + "\"（可用 ctrl / shift / alt，用 + 组合）");
    };
    for (char c : text)
    {
        if (c == '+') flush();
        else if (!std::isspace(static_cast<unsigned char>(c))) token.push_back(c);
    }
    flush();
    return modifiers;
}

int ParseButton(const std::string& text, const std::string& path)
{
    const std::string name = Lower(text);
    if (name == "left") return 1;
    if (name == "middle") return 2;
    if (name == "right") return 3;
    Fail(path, "未知鼠标键 \"" + text + "\"（可用 left / middle / right）");
}

bool IsValidShotName(const std::string& name)
{
    if (name.empty() || name.size() > MAX_SHOT_NAME_LENGTH || name == "." || name == "..") return false;
    if (name.back() == '.' || name.back() == ' ') return false;
    for (unsigned char c : name)
        if (c < 0x20 || std::strchr("<>:\"/\\|?*", c) != nullptr) return false;
    return true;
}

struct SceneEntry
{
    const char*     name;
    RecordSceneKind kind;
};

constexpr SceneEntry SCENES[] = {
    { "splash", RecordSceneKind::Splash },
    { "menu", RecordSceneKind::Menu },
    { "select", RecordSceneKind::Select },
    { "settings", RecordSceneKind::Settings },
    { "tutorial", RecordSceneKind::Tutorial },
    { "stats", RecordSceneKind::Stats },
    { "editor", RecordSceneKind::Editor },
    { "game", RecordSceneKind::Game },
    { "calibration", RecordSceneKind::Calibration },
    { "create", RecordSceneKind::Create },
};

RecordSceneKind ParseScene(const std::string& text, const std::string& path)
{
    const std::string name = Lower(text);
    for (const auto& entry : SCENES)
        if (name == entry.name) return entry.kind;
    std::string supported;
    for (const auto& entry : SCENES) supported += (supported.empty() ? "" : " / ") + std::string(entry.name);
    Fail(path, "不支持的场景 \"" + text + "\"（可用 " + supported + "）");
}

RecordPlayMode ParseMode(const std::string& text, const std::string& path)
{
    const std::string name = Lower(text);
    if (name == "auto" || name == "autoplay") return RecordPlayMode::Auto;
    if (name == "practice") return RecordPlayMode::Practice;
    if (name == "replay") return RecordPlayMode::Replay;
    if (name == "standard") return RecordPlayMode::Standard;
    Fail(path, "未知演奏模式 \"" + text + "\"（可用 auto / practice / replay / standard）");
}

RecordAction ParseAction(const json& object, const std::string& path, double warmupSeconds,
                         std::vector<std::string>& warnings)
{
    if (!object.is_object()) Fail(path, "动作必须是对象");
    static const std::set<std::string> KNOWN = {
        "at", "key", "key_down", "key_up", "move", "click", "mouse_down", "mouse_up", "wheel", "text",
        "mod", "hold", "duration", "button", "clicks", "pos"
    };
    WarnUnknownKeys(object, KNOWN, path, warnings);

    RecordAction action;
    if (!object.contains("at")) Fail(Join(path, "at"), "缺少动作时间");
    action.at = ReadNumber(object, "at", path, 0.0, -warmupSeconds - RECORD_TIME_EPSILON, MAX_SHOT_SECONDS);

    struct TypeEntry
    {
        const char*      key;
        RecordActionType type;
    };
    static constexpr TypeEntry TYPES[] = {
        { "key", RecordActionType::Key },
        { "key_down", RecordActionType::KeyDown },
        { "key_up", RecordActionType::KeyUp },
        { "move", RecordActionType::Move },
        { "click", RecordActionType::Click },
        { "mouse_down", RecordActionType::MouseDown },
        { "mouse_up", RecordActionType::MouseUp },
        { "wheel", RecordActionType::Wheel },
        { "text", RecordActionType::Text },
    };
    const TypeEntry* chosen = nullptr;
    for (const auto& entry : TYPES)
    {
        if (!object.contains(entry.key)) continue;
        if (chosen) Fail(path, std::string("一个动作只能包含一种类型（同时出现 ") + chosen->key + " 与 " + entry.key + "）");
        chosen = &entry;
    }
    if (!chosen) Fail(path, "缺少动作类型（key / key_down / key_up / move / click / mouse_down / mouse_up / wheel / text）");
    action.type = chosen->type;
    const std::string valuePath = Join(path, chosen->key);
    const json& value = object.at(chosen->key);

    action.modifiers = ParseModifiers(ReadString(object, "mod", path, ""), Join(path, "mod"));
    action.hold = ReadNumber(object, "hold", path, 0.0, 0.0, MAX_SHOT_SECONDS);
    action.duration = ReadNumber(object, "duration", path, 0.0, 0.0, MAX_SHOT_SECONDS);
    action.button = ParseButton(ReadString(object, "button", path, "left"), Join(path, "button"));
    action.clicks = ReadInt(object, "clicks", path, 1, 1, 255);

    switch (action.type)
    {
    case RecordActionType::Key:
    case RecordActionType::KeyDown:
    case RecordActionType::KeyUp:
        if (!value.is_string() || value.get<std::string>().empty()) Fail(valuePath, "必须是 SDL 扫描码名称，例如 \"Down\"、\"Return\"、\"A\"");
        action.key = value.get<std::string>();
        break;
    case RecordActionType::Move:
    case RecordActionType::Click:
    case RecordActionType::MouseDown:
    case RecordActionType::MouseUp:
    {
        const auto [x, y] = ReadPoint(value, valuePath);
        action.x = x;
        action.y = y;
        action.hasPosition = true;
        break;
    }
    case RecordActionType::Wheel:
    {
        if (!value.is_number() || !std::isfinite(value.get<double>())) Fail(valuePath, "必须是数字（正值向上滚动）");
        action.wheel = static_cast<float>(value.get<double>());
        if (const auto pos = object.find("pos"); pos != object.end())
        {
            const auto [x, y] = ReadPoint(*pos, Join(path, "pos"));
            action.x = x;
            action.y = y;
            action.hasPosition = true;
        }
        break;
    }
    case RecordActionType::Text:
        if (!value.is_string() || value.get<std::string>().empty()) Fail(valuePath, "必须是非空字符串");
        action.text = value.get<std::string>();
        break;
    }
    return action;
}

RecordShot ParseShot(const json& object, size_t index, std::vector<std::string>& warnings)
{
    const std::string path = "shots[" + std::to_string(index) + "]";
    if (!object.is_object()) Fail(path, "镜头必须是对象");
    static const std::set<std::string> KNOWN = {
        "name", "scene", "seconds", "warmup_seconds", "until", "tail_seconds", "max_seconds", "chart",
        "difficulty", "mode", "start_ms", "end_ms", "rate", "replay", "page", "actions"
    };
    WarnUnknownKeys(object, KNOWN, path, warnings);

    RecordShot shot;
    char fallbackName[32];
    std::snprintf(fallbackName, sizeof(fallbackName), "shot%02zu", index + 1);
    shot.name = ReadString(object, "name", path, fallbackName);
    if (!IsValidShotName(shot.name))
        Fail(Join(path, "name"), "名称会用作文件名，不能为空且不能包含 < > : \" / \\ | ? * 或控制字符");

    if (!object.contains("scene")) Fail(Join(path, "scene"), "缺少场景名称");
    shot.scene = ParseScene(ReadString(object, "scene", path, ""), Join(path, "scene"));

    const std::string until = Lower(ReadString(object, "until", path, ""));
    if (until == "result") shot.until = RecordUntil::Result;
    else if (!until.empty()) Fail(Join(path, "until"), "目前只支持 \"result\"");

    shot.warmupSeconds = ReadNumber(object, "warmup_seconds", path, 0.0, 0.0, 600.0);
    shot.tailSeconds = ReadNumber(object, "tail_seconds", path, 3.0, 0.0, 600.0);
    shot.maxSeconds = ReadNumber(object, "max_seconds", path, 600.0, 0.001, MAX_SHOT_SECONDS);
    if (shot.until == RecordUntil::Duration)
    {
        if (!object.contains("seconds")) Fail(Join(path, "seconds"), "缺少录制时长（或使用 \"until\": \"result\"）");
        shot.seconds = ReadNumber(object, "seconds", path, 0.0, 0.0, MAX_SHOT_SECONDS);
        if (shot.seconds <= 0.0) Fail(Join(path, "seconds"), "必须大于 0");
    }
    else
    {
        shot.seconds = ReadNumber(object, "seconds", path, 0.0, 0.0, MAX_SHOT_SECONDS);
    }

    shot.chart = ReadString(object, "chart", path, "");
    shot.difficulty = ReadInt(object, "difficulty", path, 0, 0, 1000);
    shot.mode = ParseMode(ReadString(object, "mode", path, "auto"), Join(path, "mode"));
    shot.startMs = ReadInt(object, "start_ms", path, 0, 0, MAX_CHART_TIME_MS);
    shot.endMs = ReadInt(object, "end_ms", path, 0, 0, MAX_CHART_TIME_MS);
    if (shot.endMs > 0 && shot.endMs <= shot.startMs) Fail(Join(path, "end_ms"), "必须为 0（曲终）或大于 start_ms");
    shot.rate = static_cast<float>(ReadNumber(object, "rate", path, 1.0, 0.5, 2.0));
    shot.replay = ReadString(object, "replay", path, "");
    shot.page = ReadInt(object, "page", path, 0, 0, SETTINGS_PAGE_COUNT - 1);

    if ((shot.scene == RecordSceneKind::Game || shot.scene == RecordSceneKind::Editor) && shot.chart.empty())
        Fail(Join(path, "chart"), "game / editor 镜头必须指定谱面 id");
    if (shot.scene == RecordSceneKind::Game && shot.mode == RecordPlayMode::Replay && shot.replay.empty())
        Fail(Join(path, "replay"), "replay 模式必须指定回放文件路径");

    if (const auto actions = object.find("actions"); actions != object.end())
    {
        if (!actions->is_array()) Fail(Join(path, "actions"), "必须是数组");
        for (size_t i = 0; i < actions->size(); ++i)
            shot.actions.push_back(ParseAction((*actions)[i], path + ".actions[" + std::to_string(i) + "]",
                shot.warmupSeconds, warnings));
        std::stable_sort(shot.actions.begin(), shot.actions.end(),
            [](const RecordAction& a, const RecordAction& b) { return a.at < b.at; });
        if (shot.until == RecordUntil::Duration && !shot.actions.empty() && shot.actions.back().at >= shot.seconds)
            warnings.push_back(path + ".actions: 部分动作晚于镜头结束时间，不会生效");
    }
    return shot;
}

} // namespace

std::vector<std::string> DefaultRecordVideoArgs()
{
    // 新版 ffmpeg 以滤镜输出帧的色彩属性为准，-color_primaries / -color_trc 对未标注的 RGB 输入不再生效，
    // 因此用 setparams 在滤镜链中标注 BT.709 原色与传递函数；命令行标记保留以兼容旧版本。
    return {
        "-vf", "scale=out_color_matrix=bt709:out_range=tv,setparams=color_primaries=bt709:color_trc=bt709",
        "-c:v", "libx264", "-preset", "medium", "-crf", "12",
        "-pix_fmt", "yuv420p", "-g", "60",
        "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
        "-movflags", "+faststart"
    };
}

bool ParseRecordScript(const nlohmann::json& document, RecordScript& script, std::string& error)
{
    try
    {
        if (!document.is_object()) Fail("$", "脚本根节点必须是 JSON 对象");
        RecordScript parsed;
        static const std::set<std::string> KNOWN = {
            "width", "height", "fps", "preview_width", "preview_height", "ffmpeg", "video_args", "config",
            "userdata_template", "shots"
        };
        WarnUnknownKeys(document, KNOWN, "", parsed.warnings);

        parsed.width = ReadInt(document, "width", "", 3840, MIN_RECORD_SIZE, MAX_RECORD_SIZE);
        parsed.height = ReadInt(document, "height", "", 2160, MIN_RECORD_SIZE, MAX_RECORD_SIZE);
        if (parsed.width % 2 != 0 || parsed.height % 2 != 0)
            Fail("width/height", "录制分辨率必须为偶数（yuv420p 编码要求）");
        parsed.fps = ReadInt(document, "fps", "", 60, 1, MAX_RECORD_FPS);
        parsed.previewWidth = ReadInt(document, "preview_width", "", 1280, MIN_PREVIEW_WIDTH, MAX_PREVIEW_WIDTH);
        parsed.previewHeight = ReadInt(document, "preview_height", "", 720, MIN_PREVIEW_HEIGHT, MAX_PREVIEW_HEIGHT);
        parsed.ffmpeg = ReadString(document, "ffmpeg", "", "ffmpeg");
        if (parsed.ffmpeg.empty()) Fail("ffmpeg", "不能为空");
        parsed.userdataTemplate = ReadString(document, "userdata_template", "", "");

        if (const auto args = document.find("video_args"); args != document.end())
        {
            if (!args->is_array()) Fail("video_args", "必须是字符串数组");
            for (size_t i = 0; i < args->size(); ++i)
            {
                if (!(*args)[i].is_string()) Fail("video_args[" + std::to_string(i) + "]", "必须是字符串");
                parsed.videoArgs.push_back((*args)[i].get<std::string>());
            }
        }
        else
        {
            parsed.videoArgs = DefaultRecordVideoArgs();
        }

        if (const auto config = document.find("config"); config != document.end())
        {
            if (!config->is_object()) Fail("config", "必须是对象（键为配置项，如 \"gameplay.note_speed\"）");
            parsed.config = *config;
        }

        const auto shots = document.find("shots");
        if (shots == document.end() || !shots->is_array() || shots->empty()) Fail("shots", "必须是非空数组");
        std::set<std::string> names;
        for (size_t i = 0; i < shots->size(); ++i)
        {
            auto shot = ParseShot((*shots)[i], i, parsed.warnings);
            if (!names.insert(Lower(shot.name)).second)
                Fail("shots[" + std::to_string(i) + "].name", "镜头名称重复：" + shot.name);
            parsed.shots.push_back(std::move(shot));
        }

        script = std::move(parsed);
        error.clear();
        return true;
    }
    catch (const ParseFailure& failure)
    {
        error = failure.what();
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::string("JSON 读取失败: ") + exception.what();
    }
    return false;
}

bool LoadRecordScript(const std::filesystem::path& path, RecordScript& script, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "无法打开录制脚本: " + path.generic_string();
        return false;
    }
    try
    {
        const json document = json::parse(stream, nullptr, true, true);
        return ParseRecordScript(document, script, error);
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::string("录制脚本 JSON 语法错误: ") + exception.what();
        return false;
    }
}

std::vector<std::pair<std::string, nlohmann::json>> FlattenRecordConfig(const nlohmann::json& config)
{
    std::vector<std::pair<std::string, nlohmann::json>> entries;
    auto visit = [&entries](auto&& self, const json& node, const std::string& prefix) -> void
    {
        for (const auto& [key, value] : node.items())
        {
            const std::string fullKey = prefix.empty() ? key : prefix + "." + key;
            if (value.is_object() && !value.empty()) self(self, value, fullKey);
            else entries.emplace_back(fullKey, value);
        }
    };
    if (config.is_object()) visit(visit, config, "");
    return entries;
}

long long RecordFrameCount(double seconds, int fps)
{
    if (!std::isfinite(seconds) || seconds <= 0.0 || fps <= 0) return 0;
    return std::llround(seconds * static_cast<double>(fps));
}

double RecordFrameTime(long long frame, int fps)
{
    return fps > 0 ? static_cast<double>(frame) / static_cast<double>(fps) : 0.0;
}

std::vector<RecordInputEvent> ExpandRecordActions(const std::vector<RecordAction>& actions, int fps)
{
    struct Pending
    {
        RecordInputEvent event;
        size_t           sequence = 0;
    };
    struct ModifierKey
    {
        unsigned    bit;
        const char* name;
    };
    static constexpr ModifierKey MODIFIER_KEYS[] = {
        { RECORD_MOD_CTRL, "Left Ctrl" },
        { RECORD_MOD_SHIFT, "Left Shift" },
        { RECORD_MOD_ALT, "Left Alt" },
    };

    const double frame = fps > 0 ? 1.0 / static_cast<double>(fps) : 1.0 / 60.0;
    std::vector<Pending> pending;
    size_t sequence = 0;
    float cursorX = 0.5f, cursorY = 0.5f;

    auto push = [&](double time, RecordInputType type) -> RecordInputEvent&
    {
        Pending item;
        item.event.time = time;
        item.event.type = type;
        item.event.x = cursorX;
        item.event.y = cursorY;
        item.sequence = sequence++;
        pending.push_back(std::move(item));
        return pending.back().event;
    };
    auto pressModifiers = [&](double time, unsigned modifiers)
    {
        for (const auto& modifier : MODIFIER_KEYS)
            if (modifiers & modifier.bit)
            {
                auto& event = push(time, RecordInputType::KeyDown);
                event.key = modifier.name;
                event.modifiers = modifiers;
            }
    };
    auto releaseModifiers = [&](double time, unsigned modifiers)
    {
        for (auto it = std::rbegin(MODIFIER_KEYS); it != std::rend(MODIFIER_KEYS); ++it)
            if (modifiers & it->bit)
            {
                auto& event = push(time, RecordInputType::KeyUp);
                event.key = it->name;
            }
    };

    for (const auto& action : actions)
    {
        const double release = action.at + std::max(action.hold, frame);
        switch (action.type)
        {
        case RecordActionType::Key:
        {
            pressModifiers(action.at, action.modifiers);
            auto& down = push(action.at, RecordInputType::KeyDown);
            down.key = action.key;
            down.modifiers = action.modifiers;
            auto& up = push(release, RecordInputType::KeyUp);
            up.key = action.key;
            up.modifiers = action.modifiers;
            releaseModifiers(release, action.modifiers);
            break;
        }
        case RecordActionType::KeyDown:
        {
            pressModifiers(action.at, action.modifiers);
            auto& down = push(action.at, RecordInputType::KeyDown);
            down.key = action.key;
            down.modifiers = action.modifiers;
            break;
        }
        case RecordActionType::KeyUp:
        {
            auto& up = push(action.at, RecordInputType::KeyUp);
            up.key = action.key;
            up.modifiers = action.modifiers;
            releaseModifiers(action.at, action.modifiers);
            break;
        }
        case RecordActionType::Move:
        {
            const float startX = cursorX, startY = cursorY;
            const int steps = action.duration > 0.0
                ? std::max(1, static_cast<int>(std::ceil(action.duration * static_cast<double>(fps) - 1e-9)))
                : 1;
            for (int i = 1; i <= steps; ++i)
            {
                const double progress = static_cast<double>(i) / static_cast<double>(steps);
                cursorX = static_cast<float>(startX + (action.x - startX) * progress);
                cursorY = static_cast<float>(startY + (action.y - startY) * progress);
                push(action.at + action.duration * progress, RecordInputType::MouseMove);
            }
            cursorX = action.x;
            cursorY = action.y;
            break;
        }
        case RecordActionType::Click:
        {
            cursorX = action.x;
            cursorY = action.y;
            push(action.at, RecordInputType::MouseMove);
            auto& down = push(action.at, RecordInputType::MouseDown);
            down.button = action.button;
            down.clicks = action.clicks;
            auto& up = push(release, RecordInputType::MouseUp);
            up.button = action.button;
            up.clicks = action.clicks;
            break;
        }
        case RecordActionType::MouseDown:
        case RecordActionType::MouseUp:
        {
            cursorX = action.x;
            cursorY = action.y;
            push(action.at, RecordInputType::MouseMove);
            auto& event = push(action.at, action.type == RecordActionType::MouseDown
                ? RecordInputType::MouseDown : RecordInputType::MouseUp);
            event.button = action.button;
            event.clicks = action.clicks;
            break;
        }
        case RecordActionType::Wheel:
        {
            if (action.hasPosition)
            {
                cursorX = action.x;
                cursorY = action.y;
                push(action.at, RecordInputType::MouseMove);
            }
            auto& event = push(action.at, RecordInputType::Wheel);
            event.wheel = action.wheel;
            break;
        }
        case RecordActionType::Text:
        {
            auto& event = push(action.at, RecordInputType::Text);
            event.text = action.text;
            break;
        }
        }
    }

    std::stable_sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b)
    {
        if (a.event.time != b.event.time) return a.event.time < b.event.time;
        return a.sequence < b.sequence;
    });
    std::vector<RecordInputEvent> events;
    events.reserve(pending.size());
    for (auto& item : pending) events.push_back(std::move(item.event));
    return events;
}

size_t AdvanceRecordEvents(const std::vector<RecordInputEvent>& events, size_t cursor, double now)
{
    while (cursor < events.size() && events[cursor].time <= now + RECORD_TIME_EPSILON) ++cursor;
    return cursor;
}

std::optional<std::string> FfmpegPixelFormatForLayout(int bytesPerPixel,
                                                      std::uint32_t rMask,
                                                      std::uint32_t gMask,
                                                      std::uint32_t bMask,
                                                      std::uint32_t aMask)
{
    if (bytesPerPixel != 4) return std::nullopt;
    std::string layout(4, '0');
    const std::pair<std::uint32_t, char> channels[] = { { rMask, 'r' }, { gMask, 'g' }, { bMask, 'b' }, { aMask, 'a' } };
    for (const auto& [mask, letter] : channels)
    {
        if (mask == 0)
        {
            if (letter == 'a') continue;
            return std::nullopt;
        }
        int byteIndex = -1;
        for (int shift = 0; shift < 4; ++shift)
            if (mask == (0xFFu << (shift * 8))) byteIndex = shift;
        if (byteIndex < 0) return std::nullopt;
        // ffmpeg 的 packed 格式名按内存字节顺序书写
        const int memoryIndex = std::endian::native == std::endian::little ? byteIndex : 3 - byteIndex;
        if (layout[memoryIndex] != '0') return std::nullopt;
        layout[memoryIndex] = letter;
    }
    static const std::set<std::string> SUPPORTED = { "bgra", "rgba", "argb", "abgr", "bgr0", "rgb0", "0rgb", "0bgr" };
    if (!SUPPORTED.contains(layout)) return std::nullopt;
    return layout;
}

std::vector<std::string> BuildFfmpegArguments(const std::string& ffmpeg,
                                              const std::string& pixelFormat,
                                              int width,
                                              int height,
                                              int fps,
                                              const std::vector<std::string>& videoArgs,
                                              const std::string& outputPath)
{
    std::vector<std::string> arguments = {
        ffmpeg, "-y", "-f", "rawvideo", "-pix_fmt", pixelFormat,
        "-s", std::to_string(width) + "x" + std::to_string(height),
        "-r", std::to_string(fps), "-i", "-"
    };
    arguments.insert(arguments.end(), videoArgs.begin(), videoArgs.end());
    arguments.push_back(outputPath);
    return arguments;
}

std::string JoinCommandLine(const std::vector<std::string>& arguments)
{
    std::string line;
    for (const auto& argument : arguments)
    {
        if (!line.empty()) line.push_back(' ');
        const bool quote = argument.empty() || argument.find_first_of(" \t\"") != std::string::npos;
        if (!quote)
        {
            line += argument;
            continue;
        }
        line.push_back('"');
        for (char c : argument)
        {
            if (c == '"') line.push_back('\\');
            line.push_back(c);
        }
        line.push_back('"');
    }
    return line;
}

const char* RecordSceneKindName(RecordSceneKind kind)
{
    for (const auto& entry : SCENES)
        if (entry.kind == kind) return entry.name;
    return "unknown";
}

const char* RecordPlayModeName(RecordPlayMode mode)
{
    switch (mode)
    {
    case RecordPlayMode::Auto:     return "auto";
    case RecordPlayMode::Practice: return "practice";
    case RecordPlayMode::Replay:   return "replay";
    case RecordPlayMode::Standard: return "standard";
    }
    return "auto";
}

} // namespace sakura::core
