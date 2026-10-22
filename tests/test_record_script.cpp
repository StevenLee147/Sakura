#include "test_framework.h"

#include "core/record_script.h"

#include <algorithm>
#include <string>

using namespace sakura::core;
using sakura::tests::Matchers::WithinAbs;

namespace
{

RecordScript ParseOrFail(const char* text)
{
    RecordScript script;
    std::string error;
    const bool parsed = ParseRecordScript(nlohmann::json::parse(text), script, error);
    if (!parsed) ::sakura::tests::Fail("ParseRecordScript", __FILE__, __LINE__, error);
    return script;
}

std::string ParseError(const char* text)
{
    RecordScript script;
    std::string error;
    if (ParseRecordScript(nlohmann::json::parse(text), script, error))
        ::sakura::tests::Fail("expected parse failure", __FILE__, __LINE__, text);
    return error;
}

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("录制脚本解析完整示例并按时间排序动作", "[record][script]")
{
    const auto script = ParseOrFail(R"({
        "width": 3840, "height": 2160, "fps": 60,
        "preview_width": 1280, "preview_height": 720,
        "ffmpeg": "ffmpeg",
        "config": { "gameplay.note_speed": 6.0 },
        "shots": [
            { "name": "menu", "scene": "menu", "seconds": 10,
              "actions": [ {"at": 6.0, "text": "abc"}, {"at": 3.0, "key": "Down"},
                           {"at": 4.0, "move": [0.2, 0.3]}, {"at": 5.0, "click": [0.5, 0.5]} ] },
            { "name": "select", "scene": "select", "chart": "spring_breeze", "difficulty": 1, "seconds": 8 },
            { "name": "play", "scene": "game", "chart": "spring_breeze", "difficulty": 1, "mode": "auto",
              "start_ms": 0, "end_ms": 0, "rate": 1.0, "until": "result", "tail_seconds": 5, "max_seconds": 400 },
            { "name": "editor", "scene": "editor", "chart": "spring_breeze", "difficulty": 0, "seconds": 8,
              "actions": [{"at": 1.0, "key": "Space"}] },
            { "name": "settings_visual", "scene": "settings", "page": 4, "seconds": 6 },
            { "name": "tutorial", "scene": "tutorial", "seconds": 8 },
            { "name": "stats", "scene": "stats", "seconds": 5 }
        ]
    })");

    REQUIRE(script.width == 3840);
    REQUIRE(script.height == 2160);
    REQUIRE(script.fps == 60);
    REQUIRE(script.previewWidth == 1280);
    REQUIRE(script.previewHeight == 720);
    REQUIRE(script.ffmpeg == "ffmpeg");
    REQUIRE(script.videoArgs == DefaultRecordVideoArgs());
    REQUIRE(script.config.at("gameplay.note_speed").get<double>() == 6.0);
    REQUIRE(script.shots.size() == 7);
    REQUIRE(script.warnings.empty());

    const auto& menu = script.shots[0];
    REQUIRE(menu.scene == RecordSceneKind::Menu);
    REQUIRE(menu.until == RecordUntil::Duration);
    REQUIRE_THAT(menu.seconds, WithinAbs(10.0, 1e-9));
    REQUIRE(menu.actions.size() == 4);
    REQUIRE(menu.actions[0].type == RecordActionType::Key);
    REQUIRE(menu.actions[0].key == "Down");
    REQUIRE(menu.actions[1].type == RecordActionType::Move);
    REQUIRE_THAT(menu.actions[1].x, WithinAbs(0.2, 1e-6));
    REQUIRE(menu.actions[2].type == RecordActionType::Click);
    REQUIRE(menu.actions[3].type == RecordActionType::Text);
    REQUIRE(menu.actions[3].text == "abc");

    const auto& play = script.shots[2];
    REQUIRE(play.scene == RecordSceneKind::Game);
    REQUIRE(play.mode == RecordPlayMode::Auto);
    REQUIRE(play.until == RecordUntil::Result);
    REQUIRE(play.chart == "spring_breeze");
    REQUIRE(play.difficulty == 1);
    REQUIRE_THAT(play.tailSeconds, WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(play.maxSeconds, WithinAbs(400.0, 1e-9));

    REQUIRE(script.shots[4].scene == RecordSceneKind::Settings);
    REQUIRE(script.shots[4].page == 4);
    REQUIRE(std::string(RecordSceneKindName(script.shots[3].scene)) == "editor");
}

TEST_CASE("录制脚本兼容宣传片镜头表（校准场景、只按结算结束的演奏）", "[record][script]")
{
    const auto script = ParseOrFail(R"({
        "width": 3840, "height": 2160, "fps": 60, "preview_width": 1280, "preview_height": 720,
        "ffmpeg": "ffmpeg",
        "video_args": ["-vf", "scale=out_color_matrix=bt709:out_range=tv", "-c:v", "h264_nvenc", "-cq", "14"],
        "config": {},
        "shots": [
            { "name": "practice", "scene": "select", "chart": "sakura_breath", "difficulty": 2, "seconds": 7,
              "actions": [ { "at": 0.8, "click": [0.73, 0.615] } ] },
            { "name": "editor", "scene": "editor", "chart": "sakura_breath", "difficulty": 2, "seconds": 9,
              "actions": [ { "at": 0.6, "click": [0.95, 0.868] }, { "at": 1.0, "key": "Space" } ] },
            { "name": "calibration", "scene": "calibration", "seconds": 8 },
            { "name": "play_hard", "scene": "game", "chart": "sakura_breath", "difficulty": 1, "mode": "auto",
              "until": "result", "tail_seconds": 1, "max_seconds": 260 }
        ]
    })");

    REQUIRE(script.videoArgs.size() == 6);
    REQUIRE(script.videoArgs[3] == "h264_nvenc");
    REQUIRE(script.config.empty());
    REQUIRE(script.shots[2].scene == RecordSceneKind::Calibration);
    REQUIRE(script.shots[3].until == RecordUntil::Result);
    REQUIRE_THAT(script.shots[3].seconds, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(script.shots[1].actions[0].x, WithinAbs(0.95, 1e-6));
    REQUIRE_THAT(script.shots[1].actions[0].y, WithinAbs(0.868, 1e-6));
    REQUIRE(script.warnings.empty());
}

TEST_CASE("录制脚本对无效内容给出带路径的错误", "[record][script]")
{
    REQUIRE(Contains(ParseError(R"({"shots": []})"), "shots"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "lobby", "seconds": 1}]})"), "shots[0].scene"));
    REQUIRE(Contains(ParseError(R"({"width": 1279, "shots": [{"scene": "menu", "seconds": 1}]})"), "偶数"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "menu"}]})"), "shots[0].seconds"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "game", "until": "result"}]})"), "shots[0].chart"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "game", "chart": "x", "mode": "replay", "until": "result"}]})"),
                     "shots[0].replay"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "game", "chart": "x", "start_ms": 5000, "end_ms": 4000,
        "until": "result"}]})"), "shots[0].end_ms"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"name": "a", "scene": "menu", "seconds": 1},
        {"name": "A", "scene": "menu", "seconds": 1}]})"), "shots[1].name"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"name": "a/b", "scene": "menu", "seconds": 1}]})"), "shots[0].name"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "menu", "seconds": 1,
        "actions": [{"at": 1, "key": "A", "click": [0.5, 0.5]}]}]})"), "shots[0].actions[0]"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "menu", "seconds": 1,
        "actions": [{"at": 1, "click": [1.5, 0.5]}]}]})"), "shots[0].actions[0].click"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "menu", "seconds": 1,
        "actions": [{"at": 1, "key": "A", "mod": "hyper"}]}]})"), "shots[0].actions[0].mod"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "menu", "seconds": 1,
        "actions": [{"at": -0.5, "key": "A"}]}]})"), "shots[0].actions[0].at"));
    REQUIRE(Contains(ParseError(R"({"shots": [{"scene": "settings", "page": 6, "seconds": 1}]})"), "shots[0].page"));
}

TEST_CASE("录制脚本未识别字段只产生警告，名称缺省时自动编号", "[record][script]")
{
    const auto script = ParseOrFail(R"({"fsp": 30, "shots": [{"scene": "menu", "seconds": 1, "secs": 2},
        {"scene": "stats", "seconds": 1, "warmup_seconds": 0.5, "actions": [{"at": -0.5, "key": "A"}]}]})");
    REQUIRE(script.fps == 60);
    REQUIRE(script.warnings.size() == 2);
    REQUIRE(Contains(script.warnings[0], "fsp"));
    REQUIRE(Contains(script.warnings[1], "shots[0].secs"));
    REQUIRE(script.shots[0].name == "shot01");
    REQUIRE(script.shots[1].name == "shot02");
    REQUIRE_THAT(script.shots[1].actions[0].at, WithinAbs(-0.5, 1e-9));
}

TEST_CASE("FlattenRecordConfig 展开嵌套对象并保留点号键", "[record][config]")
{
    const auto entries = FlattenRecordConfig(nlohmann::json::parse(
        R"({"gameplay": {"note_speed": 6.5, "show_combo": false}, "graphics.glow": true, "library": {}})"));
    auto find = [&entries](const std::string& key)
    {
        return std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.first == key; });
    };
    REQUIRE(entries.size() == 4);
    REQUIRE(find("gameplay.note_speed") != entries.end());
    REQUIRE(find("gameplay.note_speed")->second.get<double>() == 6.5);
    REQUIRE(find("gameplay.show_combo")->second.get<bool>() == false);
    REQUIRE(find("graphics.glow")->second.get<bool>() == true);
    REQUIRE(find("library") != entries.end());
}

TEST_CASE("录制帧数与帧时间换算", "[record][timing]")
{
    REQUIRE(RecordFrameCount(2.0, 60) == 120);
    REQUIRE(RecordFrameCount(0.5, 60) == 30);
    REQUIRE(RecordFrameCount(1.0 / 3.0, 60) == 20);
    REQUIRE(RecordFrameCount(0.0, 60) == 0);
    REQUIRE(RecordFrameCount(-1.0, 60) == 0);
    REQUIRE_THAT(RecordFrameTime(180, 60), WithinAbs(3.0, 1e-12));
    REQUIRE_THAT(RecordFrameTime(-30, 60), WithinAbs(-0.5, 1e-12));
}

TEST_CASE("脚本动作展开为底层输入事件", "[record][actions]")
{
    RecordScript script = ParseOrFail(R"({"shots": [{"scene": "editor", "chart": "x", "seconds": 5, "actions": [
        {"at": 1.0, "key": "S", "mod": "ctrl+shift"},
        {"at": 2.0, "click": [0.95, 0.868], "hold": 0.1},
        {"at": 3.0, "move": [0.5, 0.5], "duration": 0.05},
        {"at": 4.0, "text": "abc"}
    ]}]})");
    const auto events = ExpandRecordActions(script.shots[0].actions, 60);
    REQUIRE(events.size() == 13);

    // Ctrl / Shift 包围主键：按下顺序 Ctrl→Shift→S，松开顺序 S→Shift→Ctrl，默认在下一帧松开
    REQUIRE(events[0].type == RecordInputType::KeyDown);
    REQUIRE(events[0].key == "Left Ctrl");
    REQUIRE(events[1].key == "Left Shift");
    REQUIRE(events[2].key == "S");
    REQUIRE(events[2].modifiers == (RECORD_MOD_CTRL | RECORD_MOD_SHIFT));
    REQUIRE(events[3].type == RecordInputType::KeyUp);
    REQUIRE(events[3].key == "S");
    REQUIRE_THAT(events[3].time, WithinAbs(1.0 + 1.0 / 60.0, 1e-9));
    REQUIRE(events[4].key == "Left Shift");
    REQUIRE(events[5].key == "Left Ctrl");

    // 点击：先移动再按下，hold 后松开，坐标保持归一化
    REQUIRE(events[6].type == RecordInputType::MouseMove);
    REQUIRE(events[7].type == RecordInputType::MouseDown);
    REQUIRE_THAT(events[7].x, WithinAbs(0.95, 1e-6));
    REQUIRE_THAT(events[7].y, WithinAbs(0.868, 1e-6));
    REQUIRE(events[7].button == 1);
    REQUIRE(events[8].type == RecordInputType::MouseUp);
    REQUIRE_THAT(events[8].time, WithinAbs(2.1, 1e-9));

    // 0.05 秒平滑移动在 60fps 下拆成 3 步，从上一个指针位置插值到目标
    REQUIRE(events[9].type == RecordInputType::MouseMove);
    REQUIRE_THAT(events[9].time, WithinAbs(3.0 + 0.05 / 3.0, 1e-9));
    REQUIRE_THAT(events[9].x, WithinAbs(0.95 - 0.45 / 3.0, 1e-5));
    REQUIRE_THAT(events[11].time, WithinAbs(3.05, 1e-9));
    REQUIRE_THAT(events[11].x, WithinAbs(0.5, 1e-6));
    REQUIRE_THAT(events[11].y, WithinAbs(0.5, 1e-6));

    REQUIRE(events[12].type == RecordInputType::Text);
    REQUIRE(events[12].text == "abc");
    REQUIRE(std::is_sorted(events.begin(), events.end(),
        [](const RecordInputEvent& a, const RecordInputEvent& b) { return a.time < b.time; }));
}

TEST_CASE("到期事件按帧时间推进并容忍浮点误差", "[record][actions]")
{
    std::vector<RecordInputEvent> events(3);
    events[0].time = 0.0;
    events[1].time = 1.1;
    events[2].time = 3.0;
    REQUIRE(AdvanceRecordEvents(events, 0, -0.1) == 0);
    REQUIRE(AdvanceRecordEvents(events, 0, 0.0) == 1);
    REQUIRE(AdvanceRecordEvents(events, 1, RecordFrameTime(66, 60)) == 2);
    REQUIRE(AdvanceRecordEvents(events, 2, RecordFrameTime(179, 60)) == 2);
    REQUIRE(AdvanceRecordEvents(events, 2, RecordFrameTime(180, 60)) == 3);
}

TEST_CASE("读回像素布局映射到 ffmpeg rawvideo 格式", "[record][encoder]")
{
    // 通道掩码与 SDL_PIXELFORMAT_* 的定义一致
    REQUIRE(FfmpegPixelFormatForLayout(4, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u) == "bgra"); // ARGB8888
    REQUIRE(FfmpegPixelFormatForLayout(4, 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u) == "rgba"); // ABGR8888
    REQUIRE(FfmpegPixelFormatForLayout(4, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0u) == "bgr0");          // XRGB8888
    REQUIRE(FfmpegPixelFormatForLayout(4, 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0u) == "rgb0");          // XBGR8888
    REQUIRE(FfmpegPixelFormatForLayout(4, 0xFF000000u, 0x00FF0000u, 0x0000FF00u, 0x000000FFu) == "abgr"); // RGBA8888
    REQUIRE(FfmpegPixelFormatForLayout(4, 0x0000FF00u, 0x00FF0000u, 0xFF000000u, 0x000000FFu) == "argb"); // BGRA8888
    REQUIRE(FfmpegPixelFormatForLayout(4, 0xFF000000u, 0x00FF0000u, 0x0000FF00u, 0u) == "0bgr");          // RGBX8888
    REQUIRE(!FfmpegPixelFormatForLayout(4, 0x3FF00000u, 0x000FFC00u, 0x000003FFu, 0xC0000000u));          // ARGB2101010
    REQUIRE(!FfmpegPixelFormatForLayout(3, 0xFF0000u, 0x00FF00u, 0x0000FFu, 0u));                         // RGB24
    REQUIRE(!FfmpegPixelFormatForLayout(4, 0x00FF0000u, 0x00FF0000u, 0x000000FFu, 0xFF000000u));          // 重复通道
}

TEST_CASE("ffmpeg 参数与命令行文本", "[record][encoder]")
{
    const auto arguments = BuildFfmpegArguments("ffmpeg", "bgra", 3840, 2160, 60,
                                                { "-c:v", "libx264" }, "D:/out dir/play.mp4");
    const std::vector<std::string> expected = { "ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "bgra", "-s", "3840x2160",
                                                "-r", "60", "-i", "-", "-c:v", "libx264", "D:/out dir/play.mp4" };
    REQUIRE(arguments == expected);
    REQUIRE(JoinCommandLine(arguments).ends_with("-c:v libx264 \"D:/out dir/play.mp4\""));
    REQUIRE(JoinCommandLine({ "a", "", "say \"hi\"" }) == "a \"\" \"say \\\"hi\\\"\"");

    const auto defaults = DefaultRecordVideoArgs();
    REQUIRE(std::find(defaults.begin(), defaults.end(), "libx264") != defaults.end());
    REQUIRE(std::find(defaults.begin(), defaults.end(),
        "scale=out_color_matrix=bt709:out_range=tv,setparams=color_primaries=bt709:color_trc=bt709") != defaults.end());
    REQUIRE(std::find(defaults.begin(), defaults.end(), "+faststart") != defaults.end());
}
