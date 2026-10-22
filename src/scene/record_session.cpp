// record_session.cpp — 离线逐帧录制模式实现

#include "record_session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <system_error>

#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/input.h"
#include "core/paths.h"
#include "effects/screen_shake.h"
#include "effects/shader_manager.h"
#include "game/chart_loader.h"
#include "scene/scene_calibration.h"
#include "scene/scene_chart_wizard.h"
#include "scene/scene_editor.h"
#include "scene/scene_game.h"
#include "scene/scene_loading.h"
#include "scene/scene_menu.h"
#include "scene/scene_pause.h"
#include "scene/scene_result.h"
#include "scene/scene_select.h"
#include "scene/scene_settings.h"
#include "scene/scene_splash.h"
#include "scene/scene_stats.h"
#include "scene/scene_tutorial.h"
#include "ui/toast.h"
#include "utils/file_io.h"
#include "utils/logger.h"

namespace sakura::scene
{

namespace
{

namespace fs = std::filesystem;
using sakura::core::RecordInputEvent;
using sakura::core::RecordInputType;
using sakura::core::RecordSceneKind;
using sakura::core::RecordShot;
using sakura::core::RecordUntil;

constexpr int         MAX_ENTER_FRAMES         = 30;
constexpr const char* USERDATA_MARKER          = ".sakura-record-userdata";
constexpr const char* MANIFEST_FILE            = "record-manifest.json";
constexpr Uint64      PROGRESS_LOG_INTERVAL_NS = 5ULL * 1000000000ULL;
constexpr const char* CSV_HEADER               = "frame,shot_time_s,scene,music_pos_s,music_playing,game_time_ms,game_phase\n";

std::string PathToUtf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path PathFromUtf8(const std::string& text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string NativeToUtf8(const std::string& nativePath)
{
    if (nativePath.empty()) return {};
    std::error_code ec;
    const fs::path absolute = fs::absolute(fs::path(nativePath), ec);
    return PathToUtf8(ec ? fs::path(nativePath) : absolute);
}

std::string FormatFixed(double value, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

bool InRange(Uint32 type, SDL_EventType first, SDL_EventType last)
{
    return type >= static_cast<Uint32>(first) && type < static_cast<Uint32>(last);
}

// 录制期间丢弃真实键鼠 / 文本 / 触控 / 手柄 / 手写笔 / 拖放输入，以及窗口焦点、尺寸、
// 最小化与可见性变化；退出请求（关闭窗口）保留，用于中止并保存已录制部分。
// 过滤器可能在非主线程调用，因此只依赖事件类型本身。
bool SDLCALL FilterRecordEvent(void* /*userdata*/, SDL_Event* event)
{
    const Uint32 type = event->type;
    if (InRange(type, SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_MOTION)
        || InRange(type, SDL_EVENT_MOUSE_MOTION, SDL_EVENT_JOYSTICK_AXIS_MOTION)
        || InRange(type, SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_FINGER_DOWN)
        || InRange(type, SDL_EVENT_FINGER_DOWN, SDL_EVENT_CLIPBOARD_UPDATE)
        || InRange(type, SDL_EVENT_DROP_FILE, SDL_EVENT_AUDIO_DEVICE_ADDED)
        || InRange(type, SDL_EVENT_PEN_PROXIMITY_IN, SDL_EVENT_CAMERA_DEVICE_ADDED))
    {
        return false;
    }
    switch (type)
    {
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_HIT_TEST:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        return false;
    default:
        return true;
    }
}

const char* SceneName(Scene* scene)
{
    if (!scene) return "none";
    if (dynamic_cast<SceneGame*>(scene)) return "game";
    if (dynamic_cast<SceneResult*>(scene)) return "result";
    if (dynamic_cast<ScenePause*>(scene)) return "pause";
    if (dynamic_cast<SceneMenu*>(scene)) return "menu";
    if (dynamic_cast<SceneSelect*>(scene)) return "select";
    if (dynamic_cast<SceneSettings*>(scene)) return "settings";
    if (dynamic_cast<SceneTutorial*>(scene)) return "tutorial";
    if (dynamic_cast<SceneStats*>(scene)) return "stats";
    if (dynamic_cast<SceneEditor*>(scene)) return "editor";
    if (dynamic_cast<SceneSplash*>(scene)) return "splash";
    if (dynamic_cast<SceneLoading*>(scene)) return "loading";
    if (dynamic_cast<SceneCalibration*>(scene)) return "calibration";
    if (dynamic_cast<SceneChartWizard*>(scene)) return "create";
    return "unknown";
}

const char* GamePhaseName(sakura::game::GamePhase phase)
{
    switch (phase)
    {
    case sakura::game::GamePhase::Idle:      return "idle";
    case sakura::game::GamePhase::Countdown: return "countdown";
    case sakura::game::GamePhase::Playing:   return "playing";
    case sakura::game::GamePhase::Paused:    return "paused";
    case sakura::game::GamePhase::Finished:  return "finished";
    }
    return "unknown";
}

SDL_Keymod ToSdlModifiers(unsigned modifiers)
{
    Uint32 result = SDL_KMOD_NONE;
    if (modifiers & sakura::core::RECORD_MOD_CTRL) result |= SDL_KMOD_LCTRL;
    if (modifiers & sakura::core::RECORD_MOD_SHIFT) result |= SDL_KMOD_LSHIFT;
    if (modifiers & sakura::core::RECORD_MOD_ALT) result |= SDL_KMOD_LALT;
    return static_cast<SDL_Keymod>(result);
}

sakura::game::PlayMode ToPlayMode(sakura::core::RecordPlayMode mode)
{
    switch (mode)
    {
    case sakura::core::RecordPlayMode::Auto:     return sakura::game::PlayMode::AutoPlay;
    case sakura::core::RecordPlayMode::Practice: return sakura::game::PlayMode::Practice;
    case sakura::core::RecordPlayMode::Replay:   return sakura::game::PlayMode::Replay;
    case sakura::core::RecordPlayMode::Standard: return sakura::game::PlayMode::Standard;
    }
    return sakura::game::PlayMode::AutoPlay;
}

// 录制使用独立的 <输出目录>/userdata，每次运行前重建；只清空带有录制标记（或为空）的目录
bool PrepareUserRoot(const fs::path& root, const fs::path& templateDirectory, std::string& error)
{
    std::error_code ec;
    if (fs::exists(root, ec))
    {
        const bool created = fs::exists(root / USERDATA_MARKER, ec);
        const bool empty = fs::is_directory(root, ec) && fs::is_empty(root, ec);
        if (!created && !empty)
        {
            error = "输出目录中已有并非录制模式创建的 userdata，为避免误删已停止: " + PathToUtf8(root);
            return false;
        }
        fs::remove_all(root, ec);
        if (ec)
        {
            error = "无法清空上次录制的 userdata（错误 " + std::to_string(ec.value()) + "）: " + PathToUtf8(root);
            return false;
        }
    }
    if (!templateDirectory.empty())
    {
        if (!fs::is_directory(templateDirectory, ec))
        {
            error = "userdata_template 不是有效目录: " + PathToUtf8(templateDirectory);
            return false;
        }
        fs::copy(templateDirectory, root, fs::copy_options::recursive, ec);
        if (ec)
        {
            error = "复制 userdata_template 失败（错误 " + std::to_string(ec.value()) + "）: " + PathToUtf8(templateDirectory);
            return false;
        }
    }
    fs::create_directories(root / "charts", ec);
    if (ec)
    {
        error = "无法创建录制用户目录（错误 " + std::to_string(ec.value()) + "）: " + PathToUtf8(root);
        return false;
    }
    std::ofstream marker(root / USERDATA_MARKER, std::ios::binary | std::ios::trunc);
    marker << "Created by Sakura --record. This directory is deleted and recreated on every recording run.\n";
    if (!marker)
    {
        error = "无法写入录制标记文件: " + PathToUtf8(root / USERDATA_MARKER);
        return false;
    }
    return true;
}

// 录制配置：预览窗口尺寸、关闭垂直同步与 FPS 显示、静音、跳过首次教程提示，再叠加脚本 config
void ApplyRecordConfig(const sakura::core::RecordScript& script)
{
    namespace keys = sakura::core::ConfigKeys;
    auto& config = sakura::core::Config::GetInstance();
    config.Load(sakura::core::Paths::User("config/settings.json"));
    config.Set(keys::kWindowWidth, script.previewWidth);
    config.Set(keys::kWindowHeight, script.previewHeight);
    config.Set(keys::kFullscreen, false);
    config.Set(keys::kVSync, false);
    config.Set(keys::kMasterVolume, 0.0f);
    config.Set(keys::kTutorialPromptShown, true);
    config.Set("graphics.show_fps", false);
    for (const auto& [key, value] : sakura::core::FlattenRecordConfig(script.config))
        config.Set(key, value);
    config.SaveForce();
}

} // namespace

// ── 生命周期 ──────────────────────────────────────────────────────────────────

RecordApp::RecordApp(sakura::core::RecordScript script,
                     std::filesystem::path scriptPath,
                     std::filesystem::path outputDirectory)
    : m_script(std::move(script))
    , m_scriptPath(std::move(scriptPath))
    , m_outputDirectory(std::move(outputDirectory))
{
}

RecordApp::~RecordApp()
{
    Stop();
}

bool RecordApp::Start()
{
    SetFixedTimestep(1.0f / static_cast<float>(m_script.fps));
    if (!Initialize())
    {
        m_fatalError = "Sakura 初始化失败，详见 userdata/logs/sakura.log";
        return false;
    }
    m_runStartTicks = SDL_GetTicksNS();

    std::string error;
    if (!ValidateKeys(error))
    {
        m_fatalError = error;
        LOG_ERROR("[Record] {}", error);
        return false;
    }

    SDL_Renderer* renderer = GetRenderer().GetSDLRenderer();
    m_target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                 m_script.width, m_script.height);
    if (!m_target)
    {
        m_fatalError = "无法创建 " + std::to_string(m_script.width) + "x" + std::to_string(m_script.height)
            + " 离屏录制纹理: " + SDL_GetError();
        LOG_ERROR("[Record] {}", m_fatalError);
        return false;
    }
    // 预览时整体覆盖窗口；线性过滤让缩小预览更平滑（不影响录制内容）
    if (!SDL_SetTextureBlendMode(m_target, SDL_BLENDMODE_NONE)
        || !SDL_SetTextureScaleMode(m_target, SDL_SCALEMODE_LINEAR))
        LOG_WARN("[Record] 设置录制纹理属性失败: {}", SDL_GetError());
    sakura::effects::ShaderManager::GetInstance().OnResize(m_script.width, m_script.height);

    // 预览窗口固定尺寸：用户无法缩放，被过滤的尺寸事件也就不会让预览视图失配
    SDL_Window* window = GetWindow().GetSDLWindow();
    if (!SDL_SetWindowResizable(window, false))
        LOG_WARN("[Record] 无法锁定预览窗口尺寸: {}", SDL_GetError());
    if (!SDL_SetWindowTitle(window, "Sakura-樱 · 离线录制中（关闭窗口会中止并保存已录制部分）"))
        LOG_WARN("[Record] 无法设置窗口标题: {}", SDL_GetError());

    SDL_SetEventFilter(FilterRecordEvent, nullptr);
    m_filterInstalled = true;

    LOG_INFO("[Record] 离线录制开始: {} 个镜头, {}x{} @ {} fps, 预览 {}x{}, 输出 {}",
             m_script.shots.size(), m_script.width, m_script.height, m_script.fps,
             m_script.previewWidth, m_script.previewHeight, PathToUtf8(m_outputDirectory));
    for (const auto& warning : m_script.warnings) LOG_WARN("[Record] 脚本警告: {}", warning);
    WriteManifest();
    return true;
}

void RecordApp::Stop(const std::string& failureReason)
{
    if (m_stopped) return;
    m_stopped = true;
    if (!failureReason.empty() && m_fatalError.empty()) m_fatalError = failureReason;
    if (m_phase != Phase::Finished)
        Abort(m_fatalError.empty() ? std::string("录制在全部镜头完成前结束") : m_fatalError);
    if (m_filterInstalled)
    {
        SDL_SetEventFilter(nullptr, nullptr);
        m_filterInstalled = false;
    }
    if (m_target)
    {
        SDL_DestroyTexture(m_target);
        m_target = nullptr;
    }
    Shutdown();
}

int RecordApp::GetExitCode() const
{
    const bool success = m_phase == Phase::Finished && !m_aborted && m_fatalError.empty() && m_allSucceeded
        && !m_shots.empty();
    return success ? 0 : 1;
}

bool RecordApp::ValidateKeys(std::string& error) const
{
    for (const auto& shot : m_script.shots)
        for (const auto& action : shot.actions)
        {
            const bool keyAction = action.type == sakura::core::RecordActionType::Key
                || action.type == sakura::core::RecordActionType::KeyDown
                || action.type == sakura::core::RecordActionType::KeyUp;
            if (keyAction && SDL_GetScancodeFromName(action.key.c_str()) == SDL_SCANCODE_UNKNOWN)
            {
                error = "镜头 \"" + shot.name + "\" 中的按键名 \"" + action.key
                    + "\" 无效（请使用 SDL 扫描码名称，例如 Down、Return、Space、Escape、Tab、A、F5）";
                return false;
            }
        }
    return true;
}

bool RecordApp::FindChart(const std::string& id, sakura::game::ChartInfo& chart, std::string& error) const
{
    // 与曲库一致：内置谱面 + 用户谱面目录，同 id 时用户谱面优先
    sakura::game::ChartLoader loader;
    auto charts = loader.ScanCharts("resources/charts");
    for (auto& custom : loader.ScanCharts(sakura::core::Paths::User("charts")))
    {
        auto existing = std::find_if(charts.begin(), charts.end(),
            [&custom](const sakura::game::ChartInfo& other) { return other.id == custom.id; });
        if (existing == charts.end()) charts.push_back(std::move(custom));
        else *existing = std::move(custom);
    }
    const auto found = std::find_if(charts.begin(), charts.end(),
        [&id](const sakura::game::ChartInfo& info) { return info.id == id; });
    if (found == charts.end())
    {
        error = "找不到谱面 id \"" + id + "\"（已扫描 resources/charts 与录制用户目录 charts/）";
        return false;
    }
    chart = *found;
    return true;
}

std::unique_ptr<Scene> RecordApp::CreateShotScene(const RecordShot& shot, std::string& error)
{
    auto& manager = GetSceneManager();
    sakura::game::ChartInfo chart;
    const bool needsChart = shot.scene == RecordSceneKind::Game || shot.scene == RecordSceneKind::Editor
        || (shot.scene == RecordSceneKind::Select && !shot.chart.empty());
    if (needsChart)
    {
        if (!FindChart(shot.chart, chart, error)) return nullptr;
        if (shot.difficulty >= static_cast<int>(chart.difficulties.size()))
        {
            error = "谱面 \"" + shot.chart + "\" 只有 " + std::to_string(chart.difficulties.size())
                + " 个难度，difficulty=" + std::to_string(shot.difficulty) + " 无效";
            return nullptr;
        }
    }

    switch (shot.scene)
    {
    case RecordSceneKind::Splash:
        return std::make_unique<SceneSplash>(manager);
    case RecordSceneKind::Menu:
        return std::make_unique<SceneMenu>(manager);
    case RecordSceneKind::Select:
        if (needsChart)
        {
            // 同步曲库偏好（与玩家选曲后的状态一致）；首帧前的显式选中在 EnterShot 中完成
            auto& config = sakura::core::Config::GetInstance();
            config.Set("library.last_chart", chart.id);
            config.Set("library.last_difficulty", shot.difficulty);
        }
        return std::make_unique<SceneSelect>(manager);
    case RecordSceneKind::Settings:
        return std::make_unique<SceneSettings>(manager);
    case RecordSceneKind::Tutorial:
        return std::make_unique<SceneTutorial>(manager);
    case RecordSceneKind::Stats:
        return std::make_unique<SceneStats>(manager);
    case RecordSceneKind::Editor:
        return std::make_unique<SceneEditor>(manager, chart.folderPath, chart.difficulties[shot.difficulty].chartFile,
                                             shot.difficulty);
    case RecordSceneKind::Calibration:
        return std::make_unique<SceneCalibration>(manager);
    case RecordSceneKind::Create:
        return std::make_unique<SceneChartWizard>(manager);
    case RecordSceneKind::Game:
    {
        sakura::game::PlayOptions options;
        options.mode    = ToPlayMode(shot.mode);
        options.rate    = shot.rate;
        options.startMs = shot.startMs;
        options.endMs   = shot.endMs;
        if (shot.mode == sakura::core::RecordPlayMode::Replay)
        {
            const fs::path replay = PathFromUtf8(shot.replay);
            std::error_code ec;
            if (!fs::is_regular_file(replay, ec))
            {
                error = "回放文件不存在: " + shot.replay;
                return nullptr;
            }
            try
            {
                options.replayFile = replay.string();
            }
            catch (const std::exception&)
            {
                error = "回放文件路径包含当前系统代码页无法表示的字符: " + shot.replay;
                return nullptr;
            }
        }
        return std::make_unique<SceneGame>(manager, chart, shot.difficulty, options);
    }
    }
    error = "未知场景";
    return nullptr;
}

// ── 镜头流程 ──────────────────────────────────────────────────────────────────

bool RecordApp::OpenShotOutputs(const RecordShot& shot, std::string& error)
{
    const fs::path video = m_outputDirectory / PathFromUtf8(shot.name + ".mp4");
    const fs::path csv   = m_outputDirectory / PathFromUtf8(shot.name + ".frames.csv");
    const fs::path log   = m_outputDirectory / PathFromUtf8(shot.name + ".ffmpeg.log");
    std::error_code ec;
    fs::remove(video, ec);   // 避免失败的镜头留下上一次运行的旧视频

    m_csv.close();
    m_csv.clear();
    m_csv.open(csv, std::ios::binary | std::ios::trunc);
    if (!m_csv)
    {
        error = "无法创建 " + PathToUtf8(csv);
        return false;
    }
    m_csv << CSV_HEADER;

    sakura::core::VideoEncoderSettings settings;
    settings.ffmpegPath = m_script.ffmpeg;
    settings.videoArgs  = m_script.videoArgs;
    settings.outputPath = PathToUtf8(video);
    settings.logPath    = PathToUtf8(log);
    settings.width      = m_script.width;
    settings.height     = m_script.height;
    settings.fps        = m_script.fps;
    m_encoder.Open(std::move(settings));
    return true;
}

void RecordApp::StartNextShot()
{
    if (m_shotIndex >= m_script.shots.size())
    {
        FinishAll();
        return;
    }
    const auto& shot = CurrentShot();
    const int fps = m_script.fps;
    m_shotEntry       = ShotBaseEntry(shot);
    m_shotFrame       = 0;
    m_enterFrames     = 0;
    m_recordedFrames  = 0;
    m_resultFrame     = -1;
    m_warmupFrames    = sakura::core::RecordFrameCount(shot.warmupSeconds, fps);
    m_tailFrames      = sakura::core::RecordFrameCount(shot.tailSeconds, fps);
    m_maxFrames       = std::max<long long>(1, sakura::core::RecordFrameCount(shot.maxSeconds, fps));
    m_endFrame        = shot.until == RecordUntil::Duration
        ? std::max<long long>(1, sakura::core::RecordFrameCount(shot.seconds, fps)) : -1;
    m_events          = sakura::core::ExpandRecordActions(shot.actions, fps);
    m_eventCursor     = 0;
    m_musicStartFrame = -1;
    m_musicStartPosition = 0.0;
    m_musicRate       = 1.0f;
    m_musicFile.clear();
    m_recordThisFrame = false;
    m_shotStartTicks  = SDL_GetTicksNS();
    m_lastProgressLog = m_shotStartTicks;
    m_readbackNs      = 0;
    m_submitNs        = 0;

    // 镜头之间清场：上一镜头的场景栈、音乐、按住的键鼠、屏幕震动与通知都不带入新镜头
    auto& manager = GetSceneManager();
    manager.Clear();
    auto& audio = sakura::audio::AudioManager::GetInstance();
    audio.StopMusic();
    audio.SetPlaybackSpeed(1.0f);
    sakura::core::Input::Reset();
    m_mouseButtons = 0;
    int windowW = 0, windowH = 0;
    SDL_GetWindowSize(GetWindow().GetSDLWindow(), &windowW, &windowH);
    m_mousePixelX = 0.5f * static_cast<float>(windowW);
    m_mousePixelY = 0.5f * static_cast<float>(windowH);
    sakura::effects::ScreenShake::GetInstance().Stop();
    sakura::ui::ToastManager::Instance().Clear();

    std::string error;
    auto scene = CreateShotScene(shot, error);
    if (!scene || !OpenShotOutputs(shot, error))
    {
        m_csv.close();
        RecordSkippedShot(shot, error);
        ++m_shotIndex;
        return;
    }
    m_shotScene = scene.get();
    manager.SwitchScene(std::move(scene), TransitionType::None);
    m_phase = Phase::Entering;
    LOG_INFO("[Record] 镜头 {}/{} \"{}\"（{}）开始", m_shotIndex + 1, m_script.shots.size(), shot.name,
             sakura::core::RecordSceneKindName(shot.scene));
}

void RecordApp::EnterShot()
{
    const auto& shot = CurrentShot();
    if (shot.scene == RecordSceneKind::Game)
    {
        // SceneGame 在谱面 / 音乐 / 回放 / 区间无效时会提示并退回曲库，这里直接判定镜头失败
        const auto* game = dynamic_cast<const SceneGame*>(m_shotScene);
        if (!game || game->State().GetPhase() == sakura::game::GamePhase::Idle || game->Session().Total() == 0)
        {
            std::string reason = game ? game->State().GetError() : std::string();
            if (reason.empty()) reason = "演奏未能开始（区间内没有完整音符或回放与谱面不匹配）";
            FinishShot(false, reason);
            return;
        }
    }
    if (shot.scene == RecordSceneKind::Settings)
        for (int i = 0; i < shot.page; ++i) InjectKeyTap("Tab");
    if (shot.scene == RecordSceneKind::Select && !shot.chart.empty())
    {
        // SceneSelect::Scan 恢复 library.last_chart 之前会先选中列表第一首并覆盖该键，
        // 因此进入后（首帧渲染前）显式选中脚本指定的曲目与难度
        auto* select = dynamic_cast<SceneSelect*>(m_shotScene);
        if (!select || !select->SelectChart(shot.chart, shot.difficulty))
        {
            FinishShot(false, "曲库列表中找不到谱面 \"" + shot.chart + "\"");
            return;
        }
    }

    m_phase          = Phase::Running;
    m_shotFrame      = 0;
    m_shotStartTicks = SDL_GetTicksNS();
    m_lastProgressLog = m_shotStartTicks;
    // 第 0 帧（OnEnter 之后、首次渲染之前）到期的脚本输入
    DispatchEvents(sakura::core::RecordFrameTime(VideoFrame(), m_script.fps));
    UpdateRunningShot();
}

void RecordApp::UpdateRunningShot()
{
    const auto& shot = CurrentShot();
    const long long frame = VideoFrame();
    if (shot.until == RecordUntil::Result && m_resultFrame < 0 && IsResultVisible())
    {
        m_resultFrame = std::max<long long>(frame, 0);
        m_endFrame = m_resultFrame + m_tailFrames + 1;
        LOG_INFO("[Record] 镜头 \"{}\" 在第 {} 帧进入结算界面，再录制 {} 帧", shot.name, m_resultFrame, m_tailFrames);
    }
    m_recordThisFrame = frame >= 0;
}

bool RecordApp::IsResultVisible()
{
    auto& manager = GetSceneManager();
    return !manager.IsTransitioning() && dynamic_cast<SceneResult*>(manager.GetCurrentScene()) != nullptr;
}

void RecordApp::OnUpdate(float /*dt*/)
{
    m_recordThisFrame = false;
    switch (m_phase)
    {
    case Phase::Idle:
        StartNextShot();
        break;
    case Phase::Entering:
        if (GetSceneManager().GetCurrentScene() == m_shotScene) EnterShot();
        else if (++m_enterFrames > MAX_ENTER_FRAMES) FinishShot(false, "场景未能进入");
        break;
    case Phase::Running:
        ++m_shotFrame;
        UpdateRunningShot();
        break;
    case Phase::Finished:
        break;
    }
}

void RecordApp::OnEvent(const SDL_Event& event)
{
    // 真实输入已被过滤；能到达这里的退出请求来自关闭预览窗口或终端 Ctrl+C
    if (event.type == SDL_EVENT_QUIT && m_phase != Phase::Finished)
        Abort("录制被中止（关闭了预览窗口或收到 Ctrl+C），已保存当前镜头已录制的部分");
}

void RecordApp::OnFrameBegin()
{
    if (!m_target) return;
    SDL_Renderer* renderer = GetRenderer().GetSDLRenderer();
    // 整帧（场景、后处理、Toast）都渲染到录制分辨率的离屏纹理；Renderer::BeginFrame 据此布局
    if (!SDL_SetRenderTarget(renderer, m_target))
    {
        if (!m_targetFailed) LOG_ERROR("[Record] 无法设置离屏录制目标: {}", SDL_GetError());
        m_targetFailed = true;
    }
    // 窗口事件处理可能把后处理离屏纹理重建为窗口尺寸，每帧确保与录制分辨率一致
    sakura::effects::ShaderManager::GetInstance().OnResize(m_script.width, m_script.height);
}

void RecordApp::OnFrameEnd()
{
    if (!m_target) return;
    SDL_Renderer* renderer = GetRenderer().GetSDLRenderer();
    const bool recordFrame = !m_targetFailed && m_phase == Phase::Running && m_recordThisFrame;
    if (m_targetFailed)
    {
        if (m_phase != Phase::Finished) Abort("无法设置离屏录制目标，录制终止");
    }
    else if (recordFrame)
    {
        // 屏幕震动会把视口偏移；读回前恢复完整视口
        SDL_SetRenderViewport(renderer, nullptr);
        CaptureFrame(renderer);
    }
    DrawPreview(renderer, recordFrame);
    // 下一帧时间点到期的脚本输入在两帧之间注入，与真实输入的到达时机一致
    if (m_phase == Phase::Running)
        DispatchEvents(sakura::core::RecordFrameTime(VideoFrame() + 1, m_script.fps));
}

void RecordApp::CaptureFrame(SDL_Renderer* renderer)
{
    const auto& shot = CurrentShot();
    const long long frame = VideoFrame();
    const Uint64 readStart = SDL_GetTicksNS();
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    const Uint64 readEnd = SDL_GetTicksNS();
    m_readbackNs += readEnd - readStart;
    if (!surface)
    {
        FinishShot(false, std::string("SDL_RenderReadPixels 失败: ") + SDL_GetError());
        return;
    }
    const bool submitted = m_encoder.SubmitFrame(surface);
    m_submitNs += SDL_GetTicksNS() - readEnd;
    if (!submitted)
    {
        FinishShot(false, "视频编码失败: " + m_encoder.GetError());
        return;
    }
    WriteCsvRow(frame);
    ++m_recordedFrames;

    const Uint64 now = SDL_GetTicksNS();
    if (now - m_lastProgressLog >= PROGRESS_LOG_INTERVAL_NS)
    {
        m_lastProgressLog = now;
        const double elapsed = static_cast<double>(now - m_shotStartTicks) / 1e9;
        LOG_INFO("[Record] \"{}\" 已录制 {} 帧（{:.2f}s 视频），{:.1f} 帧/秒", shot.name, m_recordedFrames,
                 sakura::core::RecordFrameTime(m_recordedFrames, m_script.fps),
                 elapsed > 0.0 ? static_cast<double>(m_recordedFrames) / elapsed : 0.0);
    }

    const long long next = frame + 1;
    if (m_endFrame >= 0 && next >= m_endFrame)
    {
        FinishShot(true, {});
        return;
    }
    if (shot.until == RecordUntil::Result && m_endFrame < 0 && next >= m_maxFrames)
        FinishShot(false, "max_seconds 内没有进入结算界面");
}

nlohmann::json RecordApp::ShotBaseEntry(const RecordShot& shot) const
{
    nlohmann::json entry = {
        { "name", shot.name },
        { "scene", sakura::core::RecordSceneKindName(shot.scene) },
        { "file", shot.name + ".mp4" },
        { "frames_csv", shot.name + ".frames.csv" },
        { "ffmpeg_log", shot.name + ".ffmpeg.log" },
        { "frames", 0 },
        { "fps", m_script.fps },
        { "width", m_script.width },
        { "height", m_script.height },
        { "duration_s", 0.0 },
        { "success", false },
        { "error", "" },
        { "ffmpeg_exit_code", nullptr },
        { "ffmpeg_command", "" },
        { "music_start_frame", nullptr },
        { "music_start_position_s", nullptr },
    };
    if (!shot.chart.empty())
    {
        entry["chart"] = shot.chart;
        entry["difficulty"] = shot.difficulty;
    }
    if (shot.scene == RecordSceneKind::Game)
    {
        entry["mode"] = sakura::core::RecordPlayModeName(shot.mode);
        entry["start_ms"] = shot.startMs;
        entry["end_ms"] = shot.endMs;
        entry["rate"] = shot.rate;
    }
    if (shot.until == RecordUntil::Result) entry["until"] = "result";
    return entry;
}

void RecordApp::FinishShot(bool success, const std::string& error)
{
    const auto& shot = CurrentShot();
    const auto encoded = m_encoder.Finish([] { SDL_PumpEvents(); });
    bool csvWritten = true;
    if (m_csv.is_open())
    {
        m_csv.flush();
        csvWritten = m_csv.good();
        m_csv.close();
    }

    std::string message = error;
    if (message.empty() && !encoded.error.empty()) message = encoded.error;
    if (message.empty() && m_recordedFrames == 0) message = "没有录制任何帧";
    if (message.empty() && !csvWritten) message = "写入 frames.csv 失败";
    const bool ok = success && message.empty() && encoded.succeeded;

    const int fps = m_script.fps;
    auto entry = m_shotEntry;
    entry["frames"]           = m_recordedFrames;
    entry["duration_s"]       = sakura::core::RecordFrameTime(m_recordedFrames, fps);
    entry["success"]          = ok;
    entry["error"]            = message;
    entry["ffmpeg_exit_code"] = encoded.started ? nlohmann::json(encoded.exitCode) : nlohmann::json(nullptr);
    entry["ffmpeg_command"]   = encoded.commandLine;
    entry["pixel_format"]     = encoded.pixelFormat;
    entry["frames_encoded"]   = encoded.framesWritten;
    if (m_musicStartFrame >= 0)
    {
        // 视频中音乐 0 秒对应的时间：剪辑时把原曲放在该位置即与画面逐帧同步（连续播放期间有效）
        entry["music_start_frame"]       = m_musicStartFrame;
        entry["music_start_position_s"]  = m_musicStartPosition;
        entry["music_playback_rate"]     = m_musicRate;
        entry["music_file"]              = m_musicFile;
        entry["music_offset_in_video_s"] = sakura::core::RecordFrameTime(m_musicStartFrame, fps)
            - m_musicStartPosition / static_cast<double>(m_musicRate);
    }
    if (shot.until == RecordUntil::Result)
        entry["result_frame"] = m_resultFrame >= 0 ? nlohmann::json(m_resultFrame) : nlohmann::json(nullptr);
    const double wallSeconds = static_cast<double>(SDL_GetTicksNS() - m_shotStartTicks) / 1e9;
    entry["render_wall_s"] = wallSeconds;
    entry["render_fps"] = wallSeconds > 0.0 ? static_cast<double>(m_recordedFrames) / wallSeconds : 0.0;
    if (m_recordedFrames > 0)
    {
        const double frames = static_cast<double>(m_recordedFrames);
        entry["readback_ms_avg"] = static_cast<double>(m_readbackNs) / 1e6 / frames;
        entry["encoder_submit_ms_avg"] = static_cast<double>(m_submitNs) / 1e6 / frames;
    }
    m_shots.push_back(std::move(entry));
    m_allSucceeded = m_allSucceeded && ok;

    if (ok)
        LOG_INFO("[Record] 镜头 \"{}\" 完成: {} 帧，用时 {:.1f}s（{:.1f} 帧/秒）", shot.name, m_recordedFrames,
                 wallSeconds, wallSeconds > 0.0 ? static_cast<double>(m_recordedFrames) / wallSeconds : 0.0);
    else
        LOG_ERROR("[Record] 镜头 \"{}\" 失败: {}", shot.name, message);

    m_shotScene       = nullptr;
    m_recordThisFrame = false;
    m_events.clear();
    m_eventCursor = 0;
    ++m_shotIndex;
    m_phase = Phase::Idle;
    WriteManifest();
}

void RecordApp::RecordSkippedShot(const RecordShot& shot, const std::string& error)
{
    auto entry = ShotBaseEntry(shot);
    entry["error"] = error;
    m_shots.push_back(std::move(entry));
    m_allSucceeded = false;
    LOG_ERROR("[Record] 镜头 \"{}\" 无法开始: {}", shot.name, error);
    WriteManifest();
}

void RecordApp::FinishAll()
{
    if (m_phase == Phase::Finished) return;
    if (m_aborted)
    {
        for (; m_shotIndex < m_script.shots.size(); ++m_shotIndex)
        {
            auto entry = ShotBaseEntry(m_script.shots[m_shotIndex]);
            entry["error"] = "录制已中止，镜头未执行";
            m_shots.push_back(std::move(entry));
        }
    }
    m_phase = Phase::Finished;
    m_recordThisFrame = false;
    WriteManifest();
    const double elapsed = m_runStartTicks ? static_cast<double>(SDL_GetTicksNS() - m_runStartTicks) / 1e9 : 0.0;
    LOG_INFO("[Record] 录制{}，共 {} 个镜头，用时 {:.1f}s，manifest: {}",
             GetExitCode() == 0 ? "全部成功" : "结束（存在失败或中止）", m_shots.size(), elapsed,
             PathToUtf8(m_outputDirectory / MANIFEST_FILE));
    RequestQuit();
}

void RecordApp::Abort(const std::string& reason)
{
    if (m_phase == Phase::Finished) return;
    LOG_WARN("[Record] {}", reason);
    m_aborted = true;
    m_abortReason = reason;
    m_allSucceeded = false;
    if (m_phase == Phase::Entering || m_phase == Phase::Running) FinishShot(false, reason);
    FinishAll();
}

// ── 脚本输入注入 ──────────────────────────────────────────────────────────────

void RecordApp::DispatchEvents(double now)
{
    const size_t end = sakura::core::AdvanceRecordEvents(m_events, m_eventCursor, now);
    while (m_eventCursor < end)
    {
        const auto input = m_events[m_eventCursor++];
        InjectEvent(input);
    }
}

void RecordApp::InjectEvent(const RecordInputEvent& input)
{
    // 不经过 SDL 事件队列（真实输入已被过滤），直接交给 Input 与场景，与 ProcessEvents 的分发一致。
    // 坐标换算为窗口单位并携带真实窗口 ID：输入映射（如工房 SDL_GetWindowFromID）按窗口尺寸归一化。
    SDL_Window* window = GetWindow().GetSDLWindow();
    int windowW = 0, windowH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    sakura::core::Input::SetScreenSize(windowW, windowH);
    const SDL_WindowID windowId = SDL_GetWindowID(window);
    const float pixelX = input.x * static_cast<float>(windowW);
    const float pixelY = input.y * static_cast<float>(windowH);

    SDL_Event event{};
    switch (input.type)
    {
    case RecordInputType::KeyDown:
    case RecordInputType::KeyUp:
    {
        const bool down = input.type == RecordInputType::KeyDown;
        const SDL_Scancode code = SDL_GetScancodeFromName(input.key.c_str());
        event.type         = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = windowId;
        event.key.scancode = code;
        event.key.mod      = ToSdlModifiers(input.modifiers);
        event.key.key      = SDL_GetKeyFromScancode(code, event.key.mod, true);
        event.key.down     = down;
        event.key.repeat   = false;
        break;
    }
    case RecordInputType::MouseMove:
        event.type            = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = windowId;
        event.motion.state    = m_mouseButtons;
        event.motion.x        = pixelX;
        event.motion.y        = pixelY;
        event.motion.xrel     = pixelX - m_mousePixelX;
        event.motion.yrel     = pixelY - m_mousePixelY;
        m_mousePixelX = pixelX;
        m_mousePixelY = pixelY;
        break;
    case RecordInputType::MouseDown:
    case RecordInputType::MouseUp:
    {
        const bool down = input.type == RecordInputType::MouseDown;
        const SDL_MouseButtonFlags mask = SDL_BUTTON_MASK(input.button);
        m_mouseButtons = down ? (m_mouseButtons | mask) : (m_mouseButtons & ~mask);
        event.type            = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = windowId;
        event.button.button   = static_cast<Uint8>(input.button);
        event.button.down     = down;
        event.button.clicks   = static_cast<Uint8>(input.clicks);
        event.button.x        = pixelX;
        event.button.y        = pixelY;
        m_mousePixelX = pixelX;
        m_mousePixelY = pixelY;
        break;
    }
    case RecordInputType::Wheel:
        event.type             = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID   = windowId;
        event.wheel.y          = input.wheel;
        event.wheel.integer_y  = static_cast<Sint32>(input.wheel);
        event.wheel.direction  = SDL_MOUSEWHEEL_NORMAL;
        event.wheel.mouse_x    = pixelX;
        event.wheel.mouse_y    = pixelY;
        break;
    case RecordInputType::Text:
        event.type          = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = windowId;
        event.text.text     = input.text.c_str();
        break;
    }
    LOG_DEBUG("[Record] 注入输入 type={} key={} pos=({:.3f},{:.3f}) @ {:.3f}s", static_cast<int>(input.type),
              input.key, input.x, input.y, input.time);
    sakura::core::Input::ProcessEvent(event);
    GetSceneManager().HandleEvent(event);
}

void RecordApp::InjectKeyTap(const char* keyName)
{
    RecordInputEvent input;
    input.key  = keyName;
    input.type = RecordInputType::KeyDown;
    InjectEvent(input);
    input.type = RecordInputType::KeyUp;
    InjectEvent(input);
}

// ── 输出 ──────────────────────────────────────────────────────────────────────

void RecordApp::WriteCsvRow(long long frame)
{
    auto& audio = sakura::audio::AudioManager::GetInstance();
    Scene* current = GetSceneManager().GetCurrentScene();
    const double position = audio.GetMusicPosition();
    const bool playing = audio.IsPlaying();
    std::string gameTime, gamePhase;
    if (const auto* game = dynamic_cast<const SceneGame*>(current))
    {
        gameTime  = std::to_string(game->State().GetCurrentTime());
        gamePhase = GamePhaseName(game->State().GetPhase());
    }
    m_csv << frame << ',' << FormatFixed(sakura::core::RecordFrameTime(frame, m_script.fps), 6) << ','
          << SceneName(current) << ',' << FormatFixed(position, 9) << ',' << (playing ? 1 : 0) << ','
          << gameTime << ',' << gamePhase << '\n';

    if (m_musicStartFrame < 0 && playing)
    {
        m_musicStartFrame    = frame;
        m_musicStartPosition = position;
        m_musicRate          = audio.GetPlaybackSpeed();
        m_musicFile          = NativeToUtf8(audio.GetMusicPath());
    }
}

void RecordApp::WriteManifest()
{
    const char* rendererName = nullptr;
    if (GetRenderer().IsValid()) rendererName = SDL_GetRendererName(GetRenderer().GetSDLRenderer());
    const nlohmann::json manifest = {
        { "version", 1 },
        { "script", PathToUtf8(m_scriptPath) },
        { "output_directory", PathToUtf8(m_outputDirectory) },
        { "width", m_script.width },
        { "height", m_script.height },
        { "fps", m_script.fps },
        { "preview_width", m_script.previewWidth },
        { "preview_height", m_script.previewHeight },
        { "ffmpeg", m_script.ffmpeg },
        { "video_args", m_script.videoArgs },
        { "renderer", rendererName ? rendererName : "" },
        { "complete", m_phase == Phase::Finished },
        { "success", GetExitCode() == 0 },
        { "aborted", m_aborted },
        { "error", m_fatalError.empty() ? m_abortReason : m_fatalError },
        { "warnings", m_script.warnings },
        { "shots", m_shots },
    };
    const std::string text = manifest.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!sakura::utils::AtomicWrite(m_outputDirectory / MANIFEST_FILE, text))
        LOG_ERROR("[Record] 无法写入 {}", PathToUtf8(m_outputDirectory / MANIFEST_FILE));
}

// ── 预览 ──────────────────────────────────────────────────────────────────────

void RecordApp::DrawPreview(SDL_Renderer* renderer, bool recorded)
{
    SDL_BlendMode previousBlend = SDL_BLENDMODE_BLEND;
    Uint8 previousR = 0, previousG = 0, previousB = 0, previousA = 255;
    SDL_GetRenderDrawBlendMode(renderer, &previousBlend);
    SDL_GetRenderDrawColor(renderer, &previousR, &previousG, &previousB, &previousA);

    // SDL 不允许在设置了渲染目标时 Present：切回窗口，把录制画面等比缩放到预览窗口
    if (!SDL_SetRenderTarget(renderer, nullptr))
        LOG_ERROR("[Record] 无法恢复窗口渲染目标: {}", SDL_GetError());
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 4, 4, 10, 255);
    SDL_RenderClear(renderer);

    int outputW = 0, outputH = 0;
    if (SDL_GetCurrentRenderOutputSize(renderer, &outputW, &outputH) && outputW > 0 && outputH > 0)
    {
        const float scale = std::min(static_cast<float>(outputW) / static_cast<float>(m_script.width),
                                     static_cast<float>(outputH) / static_cast<float>(m_script.height));
        const float drawW = static_cast<float>(m_script.width) * scale;
        const float drawH = static_cast<float>(m_script.height) * scale;
        const SDL_FRect destination{ (static_cast<float>(outputW) - drawW) * 0.5f,
                                     (static_cast<float>(outputH) - drawH) * 0.5f, drawW, drawH };
        SDL_RenderTexture(renderer, m_target, nullptr, &destination);

        // 状态条只画在预览窗口上，不会进入录制画面；调试字体仅支持 ASCII
        char status[256] = {};
        const size_t total = m_script.shots.size();
        if (m_phase == Phase::Finished)
        {
            std::snprintf(status, sizeof(status), "DONE  %zu shot(s)  %s", total,
                          GetExitCode() == 0 ? "OK" : "FAILED - see record-manifest.json");
        }
        else if (m_shotIndex < total)
        {
            std::string name = m_script.shots[m_shotIndex].name;
            for (auto& c : name)
                if (static_cast<unsigned char>(c) >= 128) c = '?';
            const double fps = static_cast<double>(GetTimer().GetFPS());
            if (m_phase == Phase::Running && VideoFrame() < 0)
                std::snprintf(status, sizeof(status), "WARMUP  %zu/%zu %s  %.2fs  %.1f fps", m_shotIndex + 1, total,
                              name.c_str(), sakura::core::RecordFrameTime(VideoFrame(), m_script.fps), fps);
            else if (m_phase == Phase::Running)
            {
                const std::string endText = m_endFrame >= 0 ? "/" + std::to_string(m_endFrame) : " (until result)";
                std::snprintf(status, sizeof(status), "REC  %zu/%zu %s  frame %lld%s  %.2fs  %.1f fps",
                              m_shotIndex + 1, total, name.c_str(), m_recordedFrames, endText.c_str(),
                              sakura::core::RecordFrameTime(m_recordedFrames, m_script.fps), fps);
            }
            else
                std::snprintf(status, sizeof(status), "PREPARING  %zu/%zu %s", m_shotIndex + 1, total, name.c_str());
        }

        const float glyph = std::max(1.0f, std::floor(static_cast<float>(outputH) * 0.022f
                                                      / static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE)));
        const float charSize = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * glyph;
        const float margin = std::max(2.0f, std::round(static_cast<float>(outputH) * 0.012f));
        const float textLength = static_cast<float>(std::strlen(status));
        const SDL_FRect box{ margin, margin, margin * 3.0f + charSize * (textLength + 1.0f), charSize + margin * 2.0f };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        SDL_RenderFillRect(renderer, &box);
        if (recorded) SDL_SetRenderDrawColor(renderer, 236, 72, 98, 255);
        else SDL_SetRenderDrawColor(renderer, 150, 150, 170, 255);
        const SDL_FRect indicator{ margin * 2.0f, margin * 2.0f, charSize, charSize };
        SDL_RenderFillRect(renderer, &indicator);
        SDL_SetRenderDrawColor(renderer, 242, 236, 245, 255);
        SDL_SetRenderScale(renderer, glyph, glyph);
        SDL_RenderDebugText(renderer, (margin * 3.0f + charSize) / glyph, (margin * 2.0f) / glyph, status);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    }

    SDL_SetRenderDrawBlendMode(renderer, previousBlend);
    SDL_SetRenderDrawColor(renderer, previousR, previousG, previousB, previousA);
}

// ── 入口 ──────────────────────────────────────────────────────────────────────

int RunRecordMode(const std::filesystem::path& scriptPath, const std::filesystem::path& outputDirectory)
{
    const fs::path output = outputDirectory.empty() ? scriptPath.parent_path() / "record-out" : outputDirectory;
    std::error_code ec;
    fs::create_directories(output, ec);
    if (!fs::is_directory(output, ec)) return 2;

    auto writeFailure = [&](const std::string& message)
    {
        const nlohmann::json manifest = {
            { "version", 1 },
            { "script", PathToUtf8(scriptPath) },
            { "output_directory", PathToUtf8(output) },
            { "complete", false },
            { "success", false },
            { "aborted", false },
            { "error", message },
            { "shots", nlohmann::json::array() },
        };
        sakura::utils::AtomicWrite(output / MANIFEST_FILE,
                                   manifest.dump(2, ' ', false, nlohmann::json::error_handler_t::replace));
    };

    sakura::core::RecordScript script;
    std::string error;
    if (!sakura::core::LoadRecordScript(scriptPath, script, error))
    {
        writeFailure(error);
        return 2;
    }

    // 脚本中的相对路径以脚本所在目录为基准；不含路径分隔符的 ffmpeg 交给 PATH 查找
    const fs::path scriptDirectory = scriptPath.parent_path();
    auto resolve = [&scriptDirectory](const std::string& text)
    {
        fs::path path = PathFromUtf8(text);
        if (path.is_relative()) path = scriptDirectory / path;
        return path.lexically_normal();
    };
    if (script.ffmpeg.find_first_of("/\\") != std::string::npos) script.ffmpeg = PathToUtf8(resolve(script.ffmpeg));
    for (auto& shot : script.shots)
        if (!shot.replay.empty()) shot.replay = PathToUtf8(resolve(shot.replay));
    const fs::path userTemplate = script.userdataTemplate.empty() ? fs::path() : resolve(script.userdataTemplate);

    const fs::path userRoot = output / "userdata";
    if (!PrepareUserRoot(userRoot, userTemplate, error))
    {
        writeFailure(error);
        return 2;
    }
    sakura::core::Paths::SetUserRoot(userRoot);
    ApplyRecordConfig(script);
    sakura::audio::AudioManager::GetInstance().SetOfflineClock(true);

    RecordApp app(std::move(script), scriptPath, output);
    if (!app.Start())
    {
        app.Stop();
        return 1;
    }
    app.Run();
    app.Stop();
    return app.GetExitCode();
}

} // namespace sakura::scene
