#pragma once

// record_script.h — 离线录制脚本模型、解析与辅助逻辑
// 纯逻辑模块（不依赖 SDL），供 RecordApp 使用并由单元测试覆盖。

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace sakura::core
{

// 动作修饰键位掩码
inline constexpr unsigned RECORD_MOD_CTRL  = 1u;
inline constexpr unsigned RECORD_MOD_SHIFT = 2u;
inline constexpr unsigned RECORD_MOD_ALT   = 4u;

// 判断时间是否到达的容差（秒），远小于一帧
inline constexpr double RECORD_TIME_EPSILON = 1e-6;

enum class RecordSceneKind
{
    Splash,
    Menu,
    Select,
    Settings,
    Tutorial,
    Stats,
    Editor,
    Game,
    Calibration,
    Create
};

enum class RecordUntil
{
    Duration,   // 录制固定 seconds 秒
    Result      // 录制到结算界面完全出现，再追加 tail_seconds
};

enum class RecordPlayMode
{
    Auto,
    Practice,
    Replay,
    Standard
};

enum class RecordActionType
{
    Key,        // 按下并松开（可选 hold / mod）
    KeyDown,
    KeyUp,
    Move,       // 鼠标移动（可选 duration 平滑移动）
    Click,      // 移动 + 按下 + 松开（可选 hold / button / clicks）
    MouseDown,
    MouseUp,
    Wheel,
    Text
};

// 脚本中的一条动作（时间 at 以录制起点为 0，单位秒；预热阶段为负值）
struct RecordAction
{
    double           at          = 0.0;
    RecordActionType type        = RecordActionType::Key;
    std::string      key;                 // SDL 扫描码名称（SDL_GetScancodeFromName）
    unsigned         modifiers   = 0;     // RECORD_MOD_* 组合
    double           hold        = 0.0;   // key / click 按住时长（秒）
    double           duration    = 0.0;   // move 平滑移动时长（秒）
    float            x           = 0.5f;  // 归一化坐标（0~1）
    float            y           = 0.5f;
    bool             hasPosition = false;
    int              button      = 1;     // 1=左键 2=中键 3=右键
    int              clicks      = 1;
    float            wheel       = 0.0f;  // 正值 = 向上
    std::string      text;
};

enum class RecordInputType
{
    KeyDown,
    KeyUp,
    MouseMove,
    MouseDown,
    MouseUp,
    Wheel,
    Text
};

// 展开后的底层输入事件（按 time 稳定排序）
struct RecordInputEvent
{
    double          time      = 0.0;
    RecordInputType type      = RecordInputType::KeyDown;
    std::string     key;
    unsigned        modifiers = 0;
    float           x         = 0.5f;
    float           y         = 0.5f;
    int             button    = 1;
    int             clicks    = 1;
    float           wheel     = 0.0f;
    std::string     text;
};

struct RecordShot
{
    std::string      name;
    RecordSceneKind  scene         = RecordSceneKind::Menu;
    double           seconds       = 0.0;
    double           warmupSeconds = 0.0;
    RecordUntil      until         = RecordUntil::Duration;
    double           tailSeconds   = 3.0;
    double           maxSeconds    = 600.0;
    std::string      chart;
    int              difficulty    = 0;
    RecordPlayMode   mode          = RecordPlayMode::Auto;
    int              startMs       = 0;
    int              endMs         = 0;
    float            rate          = 1.0f;
    std::string      replay;
    int              page          = 0;
    std::vector<RecordAction> actions;   // 已按 at 稳定排序
};

struct RecordScript
{
    int                       width         = 3840;
    int                       height        = 2160;
    int                       fps           = 60;
    int                       previewWidth  = 1280;
    int                       previewHeight = 720;
    std::string               ffmpeg        = "ffmpeg";
    std::vector<std::string>  videoArgs;
    std::string               userdataTemplate;
    nlohmann::json            config        = nlohmann::json::object();
    std::vector<RecordShot>   shots;
    std::vector<std::string>  warnings;      // 未识别字段等非致命问题
};

// 默认编码参数：libx264 高质量 + BT.709 limited range 色彩标记
std::vector<std::string> DefaultRecordVideoArgs();

// 解析脚本 JSON；失败时返回 false 并写入 error（含字段路径）
bool ParseRecordScript(const nlohmann::json& document, RecordScript& script, std::string& error);

// 读取并解析脚本文件（允许 JSON 注释）
bool LoadRecordScript(const std::filesystem::path& path, RecordScript& script, std::string& error);

// 将嵌套 config 对象展开为 "a.b.c" 键值对；已带点号的键原样保留
std::vector<std::pair<std::string, nlohmann::json>> FlattenRecordConfig(const nlohmann::json& config);

// 秒数 → 帧数（四舍五入，最小 0）
long long RecordFrameCount(double seconds, int fps);

// 帧序号 → 时间（秒）
double RecordFrameTime(long long frame, int fps);

// 把脚本动作展开为底层输入事件；未指定 hold 的按键/点击在下一帧松开
std::vector<RecordInputEvent> ExpandRecordActions(const std::vector<RecordAction>& actions, int fps);

// 返回第一个 time 晚于 now（含容差）的事件下标；[cursor, 返回值) 为本次到期事件
size_t AdvanceRecordEvents(const std::vector<RecordInputEvent>& events, size_t cursor, double now);

// 32 位像素布局（各通道掩码）→ ffmpeg rawvideo pix_fmt；无法直接映射时返回空
std::optional<std::string> FfmpegPixelFormatForLayout(int bytesPerPixel,
                                                      std::uint32_t rMask,
                                                      std::uint32_t gMask,
                                                      std::uint32_t bMask,
                                                      std::uint32_t aMask);

// ffmpeg 参数：-y -f rawvideo -pix_fmt <fmt> -s WxH -r fps -i - <videoArgs...> <output>
std::vector<std::string> BuildFfmpegArguments(const std::string& ffmpeg,
                                              const std::string& pixelFormat,
                                              int width,
                                              int height,
                                              int fps,
                                              const std::vector<std::string>& videoArgs,
                                              const std::string& outputPath);

// 用于日志 / manifest 的命令行文本（含空格或引号的参数加引号）
std::string JoinCommandLine(const std::vector<std::string>& arguments);

const char* RecordSceneKindName(RecordSceneKind kind);
const char* RecordPlayModeName(RecordPlayMode mode);

} // namespace sakura::core
