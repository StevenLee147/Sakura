#include "app.h"
#include "paths.h"
#include <filesystem>
#include <algorithm>
#include "ui/toast.h"
#include "config.h"
#include "theme.h"
#include "utils/logger.h"
#include "scene/test_scenes.h"
#include "scene/scene_splash.h"
#include "audio/audio_manager.h"
#include "audio/audio_visualizer.h"
#include "audio/clock_sync.h"
#include "game/chart_loader.h"
#include "game/achievement_manager.h"
#include "data/database.h"
#include "effects/particle_system.h"
#include "effects/screen_shake.h"
#include "effects/shader_manager.h"
#include "effects/video_decoder.h"
#include "ui/button.h"
#include "ui/visual_style.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
std::string ReadEnvironmentVariable(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value)
        return {};

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

std::string ResolveDatabasePath()
{
    std::string envPath = ReadEnvironmentVariable("SAKURA_DB_PATH");
    if (!envPath.empty())
        return envPath;

    auto path = std::filesystem::path(sakura::core::Config::GetInstance().Get<std::string>(
        sakura::core::ConfigKeys::kDatabasePath, "data/sakura.db"));
    return path.is_absolute() ? path.generic_string() : sakura::core::Paths::User(path.generic_string());
}
}

namespace sakura::core
{

App::App() = default;

App::~App()
{
    // 如果 Run() 异常退出，确保资源被释放
    if (m_renderer.IsValid() || m_window.IsValid())
    {
        Shutdown();
    }
}

bool App::Initialize()
{
    // ── 日志系统最先初始化 ─────────────────────────────────────────────────────
    sakura::utils::Logger::Init(Paths::User("logs/sakura.log"));

    LOG_INFO("正在初始化 Sakura-樱...");
    // ── 配置系统 ────────────────────────────────────────────────────────────────
    Config::GetInstance().Load(Paths::User("config/settings.json"));
    Theme::GetInstance().Initialize();

    // ── 数据库 ───────────────────────────────────────────────────────────────────
    if (!sakura::data::Database::GetInstance().Initialize(ResolveDatabasePath()))
    {
        LOG_WARN("Database 初始化失败（非致命）");
    }
    if (!sakura::game::AchievementManager::GetInstance().LoadAchievements())
    {
        LOG_WARN("AchievementManager 初始化失败（非致命）");
    }
    // ── SDL 初始化 ────────────────────────────────────────────────────────────
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        LOG_ERROR("SDL_Init 失败: {}", SDL_GetError());
        return false;
    }
    LOG_INFO("SDL 初始化成功");

    // ── 窗口 ──────────────────────────────────────────────────────────────────
    if (!m_window.Create("Sakura-樱",
        Config::GetInstance().Get<int>(ConfigKeys::kWindowWidth, 1600),
        Config::GetInstance().Get<int>(ConfigKeys::kWindowHeight, 900)))
    {
        return false;
    }

    // ── 渲染器 ────────────────────────────────────────────────────────────────
    if (!m_renderer.Initialize(m_window.GetSDLWindow()))
    {
        return false;
    }
    Input::SetScreenSize(m_window.GetWidth(), m_window.GetHeight());

    // ── 资源管理器 ───────────────────────────────────────────────────────────
    if (!ResourceManager::GetInstance().Initialize(m_renderer.GetSDLRenderer()))
    {
        LOG_WARN("ResourceManager 初始化失败（非致命）");
    }
    // ── ShaderManager（后处理特效）────────────────────────────────────────────
    {
        int sw = m_renderer.GetScreenWidth();
        int sh = m_renderer.GetScreenHeight();
        if (!sakura::effects::ShaderManager::GetInstance().Initialize(
                m_renderer.GetSDLRenderer(), sw, sh))
        {
            LOG_WARN("ShaderManager 初始化失败（非致命）");
        }
    }
    // ── 计时器 ────────────────────────────────────────────────────────────────
    m_timer.Reset();

    // ── 音频管理器 ────────────────────────────────────────────────────────────
    if (!sakura::audio::AudioManager::GetInstance().Initialize())
    {
        LOG_WARN("AudioManager 初始化失败（非致命）");
    }

    // 加载默认 hitsound 集并注册 Button 全局 UI 音效
    {
        auto& am = sakura::audio::AudioManager::GetInstance();
        am.LoadHitsoundSet(Config::GetInstance().Get<std::string>("audio.hitsound", "default"));

        sakura::ui::Button::SetGlobalHoverSFX([&am]()
        {
            am.PlayUISFX(sakura::audio::UISFXType::ButtonHover);
        });
        sakura::ui::Button::SetGlobalClickSFX([&am]()
        {
            am.PlayUISFX(sakura::audio::UISFXType::ButtonClick);
        });
    }

    // ── 初始场景 ──────────────────────────────────────────────────────────────
    m_sceneManager.SwitchScene(
        std::make_unique<sakura::scene::SceneSplash>(m_sceneManager),
        sakura::scene::TransitionType::None
    );

    LOG_INFO("Sakura-樱 初始化完成");
    return true;
}

void App::Run()
{
    LOG_INFO("主循环启动...");
    m_running     = true;
    m_timer.Reset();

    while (m_running)
    {
        const Uint64 frameStart = SDL_GetTicksNS();
        m_timer.Tick();
        // 固定步长（离线录制）时全部逻辑使用虚拟时间；计时器仍用于 FPS 统计
        const float dt = m_fixedTimestep > 0.0f ? m_fixedTimestep : m_timer.GetDeltaTime();

        // ── 事件处理 ──────────────────────────────────────────────────────────
        ProcessEvents();

        // Judge against the audio clock on every displayed frame. A fixed 60 Hz
        // scene update adds latency and makes high-refresh displays visibly stutter.
        const Uint64 updateStart=SDL_GetTicksNS();
        Update(dt);
        m_updateCpuMs=(SDL_GetTicksNS()-updateStart)/1000000.0;

        // ── 可变帧率渲染 ──────────────────────────────────────────────────────
        Render();
        if (m_fixedTimestep <= 0.0f)
        {
            int limit = Config::GetInstance().Get<int>(ConfigKeys::kFpsLimit, 240);
            if (SDL_GetWindowFlags(m_window.GetSDLWindow()) & SDL_WINDOW_MINIMIZED) limit = 30;
            if (limit > 0)
            {
                const Uint64 budget = 1000000000ULL / static_cast<Uint64>(limit);
                const Uint64 elapsed = SDL_GetTicksNS() - frameStart;
                if (elapsed < budget) SDL_DelayPrecise(budget - elapsed);
            }
        }

        // ── FPS 日志（每 3 秒输出一次）────────────────────────────────────────
        m_fpsLogTimer += m_timer.GetDeltaTime();
        if (m_fpsLogTimer >= FPS_LOG_INTERVAL)
        {
            m_fpsLogTimer = 0.0f;
            LOG_DEBUG("FPS: {:.1f}  帧数: {}  运行时间: {:.1f}s",
                m_timer.GetFPS(),
                m_timer.GetFrameCount(),
                m_timer.GetElapsedTime());
        }
    }

    LOG_INFO("主循环结束");
}

void App::Shutdown()
{
    if (m_shutdown) return;
    m_shutdown = true;
    LOG_INFO("正在关闭 Sakura-樱...");
    // Scene OnExit owns textures/audio; run it while both subsystems are alive.
    m_sceneManager.Clear();
    sakura::ui::Button::SetGlobalHoverSFX({});
    sakura::ui::Button::SetGlobalClickSFX({});

    // 先关闭音频（避免资源释放竞争）
    sakura::audio::AudioManager::GetInstance().Shutdown();

    // 关闭后处理特效
    sakura::effects::ShaderManager::GetInstance().Shutdown();

    // 释放所有资源（渲染器销毁前）
    m_renderer.ReleaseTextResources();
    ResourceManager::GetInstance().ReleaseAll();

    m_renderer.Destroy();
    m_window.Destroy();

    SDL_Quit();

    // 关闭数据库
    sakura::data::Database::GetInstance().Shutdown();

    // 保存配置（如果有修改）
    Config::GetInstance().Save();

    LOG_INFO("Sakura-樱 已正常关闭");
    sakura::utils::Logger::Shutdown();
}

void App::ProcessEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // 先转发给 Window 处理（F11、resize 等）
        const bool windowConsumed = m_window.HandleEvent(event);
        // SDL pointer coordinates are window units, which differ from framebuffer
        // pixels under Windows display scaling. Normalize in window coordinates.
        int inputW = 0, inputH = 0;
        SDL_GetWindowSize(m_window.GetSDLWindow(), &inputW, &inputH);
        Input::SetScreenSize(inputW, inputH);

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F3)
        {
            m_showDiagnostics = !m_showDiagnostics;
        }

        // 输入系统处理
        Input::ProcessEvent(event);

        // 场景事件处理
        if (!windowConsumed) m_sceneManager.HandleEvent(event);

        // 再转发给子类
        OnEvent(event);

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                if(!m_sceneManager.GetCurrentScene() || m_sceneManager.GetCurrentScene()->CanClose())m_running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                int renderW=0,renderH=0;
                SDL_GetRenderOutputSize(m_renderer.GetSDLRenderer(),&renderW,&renderH);
                m_renderer.Flush();
                sakura::effects::ShaderManager::GetInstance().OnResize(
                    renderW, renderH);
                if(event.type==SDL_EVENT_WINDOW_RESIZED && !m_window.IsFullscreen()) {
                    Config::GetInstance().Set(ConfigKeys::kWindowWidth,inputW);
                    Config::GetInstance().Set(ConfigKeys::kWindowHeight,inputH);
                    m_appliedWidth=inputW;m_appliedHeight=inputH;
                }
                break;
            }
            // ESC 键由各场景自行处理（主菜单弹确认框，游戏中暂停，其他场景返回上级）
            default:
                break;
        }
    }
}

void App::Update(float dt)
{
    // 同步屏幕尺寸给输入系统（用于归一化鼠标坐标）
    int inputW = 0, inputH = 0;
    SDL_GetWindowSize(m_window.GetSDLWindow(), &inputW, &inputH);
    Input::SetScreenSize(inputW, inputH);
    const int vsync = Config::GetInstance().Get<bool>(ConfigKeys::kVSync, true) ? 1 : 0;
    if (vsync != m_appliedVSync)
    {
        SDL_SetRenderVSync(m_renderer.GetSDLRenderer(), vsync);
        m_appliedVSync = vsync;
    }
    auto& settings = Theme::GetInstance().Settings();
    auto& config = Config::GetInstance();
    settings.particlesEnabled = config.Get<bool>(ConfigKeys::kParticles, true);
    settings.glowEnabled = config.Get<bool>("graphics.glow", true);
    settings.shakeEnabled = config.Get<bool>("graphics.shake", true) && !config.Get<bool>("graphics.reduced_motion", false);
    settings.vignetteEnabled = config.Get<bool>("graphics.vignette", true);
    sakura::audio::AudioManager::GetInstance().Update(dt);

    const int width = config.Get<int>(ConfigKeys::kWindowWidth, 1600);
    const int height = config.Get<int>(ConfigKeys::kWindowHeight, 900);
    if (width != m_appliedWidth || height != m_appliedHeight)
    {
        if (!m_window.IsFullscreen()) SDL_SetWindowSize(m_window.GetSDLWindow(), width, height);
        m_appliedWidth = width; m_appliedHeight = height;
    }

    // 全屏配置同步：检测 Config 中设置是否与当前窗口状态一致
    {
        bool cfgFullscreen = Config::GetInstance().Get<bool>(
            std::string(ConfigKeys::kFullscreen), false);
        if (cfgFullscreen != m_window.IsFullscreen()) {
            m_window.SetFullscreen(cfgFullscreen);
            if(!cfgFullscreen)SDL_SetWindowSize(m_window.GetSDLWindow(),width,height);
        }
    }

    // 场景更新
    m_sceneManager.Update(dt);
    sakura::ui::ToastManager::Instance().Update(dt);

    {
        auto& audio = sakura::audio::AudioManager::GetInstance();
        sakura::audio::AudioVisualizer::GetInstance().Update(
            dt,
            audio.GetMusicPosition(),
            audio.IsPlaying());
    }

    // 屏幕震动更新：将偏移量（归一化）转为像素写入渲染器
    {
        auto [sdx, sdy] = sakura::effects::ScreenShake::GetInstance().Update(dt);
        int pixDx = static_cast<int>(sdx * static_cast<float>(m_renderer.GetScreenWidth()));
        int pixDy = static_cast<int>(sdy * static_cast<float>(m_renderer.GetScreenHeight()));
        m_renderer.SetViewportShake(pixDx, pixDy);
    }

    OnUpdate(dt);

    // 帧末重置 pressed/released 状态
    Input::Update();
}

double App::CalcCpuP95Ms() const
{
    if (m_cpuHistoryCount == 0) return m_updateCpuMs + m_renderCpuMs;
    std::array<double, 120> sorted{};
    for (size_t i = 0; i < m_cpuHistoryCount; ++i) sorted[i] = m_cpuHistory[i];
    std::sort(sorted.begin(), sorted.begin() + m_cpuHistoryCount);
    const size_t idx = static_cast<size_t>((m_cpuHistoryCount - 1) * 0.95);
    return sorted[idx];
}

void App::RenderDiagnosticsOverlay()
{
    const FontHandle font = ResourceManager::GetInstance().GetDefaultFontHandle();
    if (font == INVALID_HANDLE) return;

    m_renderer.Flush();
    const auto sceneStats = m_renderer.GetCurrentFrameStats();
    const auto dispStats = (sceneStats.vertexCount > 0) ? sceneStats : m_renderer.GetLastFrameStats();
    const int particles = sakura::effects::ParticleSystem::GetFrameRenderedCount();
    const float fps = m_timer.GetFPS();
    const float dtMs = m_timer.GetDeltaTime() * 1000.0f;
    const double cpuP95 = CalcCpuP95Ms();
    const double driftMs = sakura::audio::AudioClockSync::GetGlobalDriftMs();
    const auto syncMode = sakura::audio::AudioClockSync::GetGlobalMode();
    const auto bgaMode  = sakura::effects::VideoDecoder::GetGlobalStageMode();

    const NormRect card{ 0.712f, 0.072f, 0.272f, 0.218f };
    sakura::ui::VisualStyle::DrawGlassCard(
        m_renderer, card,
        { 14, 18, 34, 216 }, { 8, 11, 24, 232 },
        { 246, 188, 212, 135 }, { 128, 198, 248, 52 }, 0.012f, 0.42f);

    sakura::ui::VisualStyle::DrawBadgePill(
        m_renderer, { card.x + 0.012f, card.y + 0.012f, 0.118f, 0.026f },
        "DIAGNOSTICS / F3", font, 0.0125f,
        { 42, 28, 62, 215 }, { 248, 198, 152, 165 }, { 252, 224, 178, 255 });

    std::ostringstream fpsSs;
    fpsSs << std::fixed << std::setprecision(0) << fps << " FPS";
    m_renderer.DrawText(font, fpsSs.str(), card.x + card.width - 0.014f, card.y + 0.014f, 0.019f,
        { 152, 238, 198, 255 }, TextAlign::Right);
    m_renderer.DrawLine(card.x + 0.012f, card.y + 0.046f, card.x + card.width - 0.012f, card.y + 0.046f,
        { 216, 188, 228, 54 }, 0.001f);

    auto drawRow = [&](int row, std::string_view label, const std::string& val, Color valCol) {
        const float ry = card.y + 0.055f + static_cast<float>(row) * 0.032f;
        m_renderer.DrawText(font, label, card.x + 0.014f, ry, 0.0145f, { 182, 190, 216, 235 });
        m_renderer.DrawText(font, val, card.x + card.width - 0.014f, ry, 0.0145f, valCol, TextAlign::Right);
    };

    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << dtMs << "ms (P95 " << cpuP95 << "ms)";
        drawRow(0, "帧间隔 / CPU", ss.str(), { 236, 240, 252, 255 });
    }
    {
        std::ostringstream ss;
        ss << "B:" << dispStats.batchCount << "  V:" << dispStats.vertexCount << "  I:" << dispStats.indexCount;
        drawRow(1, "批处理提交", ss.str(), { 148, 216, 252, 255 });
    }
    {
        std::ostringstream ss;
        ss << particles << "  |  " << sakura::effects::VideoDecoder::StageModeLabel(bgaMode);
        const Color stageCol = (bgaMode == sakura::effects::VideoStageMode::HardwareDecode)
            ? Color{ 152, 238, 198, 255 } : Color{ 246, 196, 222, 245 };
        drawRow(2, "活跃粒子 / 舞台", ss.str(), stageCol);
    }
    {
        std::ostringstream ss;
        ss << std::showpos << std::fixed << std::setprecision(2) << driftMs << std::noshowpos
           << " ms [" << sakura::audio::AudioClockSync::ModeLabel(syncMode) << "]";
        Color driftCol{ 152, 238, 198, 255 };
        if (syncMode == sakura::audio::ClockSyncMode::SoftSync) driftCol = { 252, 212, 138, 255 };
        else if (syncMode == sakura::audio::ClockSyncMode::HardSnap) driftCol = { 244, 126, 148, 255 };
        else if (syncMode == sakura::audio::ClockSyncMode::Idle) driftCol = { 176, 184, 208, 220 };
        drawRow(3, "音画时钟偏差", ss.str(), driftCol);
    }
    m_renderer.DrawText(font, "SDL3 GPU | WSOLA | Media Foundation BGA",
        card.x + 0.014f, card.y + card.height - 0.025f, 0.0125f, { 214, 184, 212, 185 });
}

void App::Render()
{
    const Uint64 renderStart=SDL_GetTicksNS();
    sakura::effects::ParticleSystem::ResetFrameRenderedCount();
    OnFrameBegin();
    m_renderer.BeginFrame();
    m_renderer.Clear(Color::DarkBlue);

    // 场景渲染
    m_sceneManager.Render(m_renderer);

    // 后处理特效（暗角等覆盖层，在所有场景之上）
    m_renderer.Flush();
    sakura::effects::ShaderManager::GetInstance().ApplyPostProcess();

    sakura::ui::ToastManager::Instance().Render(m_renderer, ResourceManager::GetInstance().GetDefaultFontHandle());
    if (m_showDiagnostics)
    {
        RenderDiagnosticsOverlay();
    }
    else if (Config::GetInstance().Get<bool>("graphics.show_fps", false))
    {
        m_renderer.DrawText(ResourceManager::GetInstance().GetDefaultFontHandle(),
            std::to_string(static_cast<int>(m_timer.GetFPS())) + " FPS", 0.985f, 0.008f, 0.016f,
            Color{180, 225, 210, 255}, TextAlign::Right);
    }
    m_renderer.Flush();

    // 子类可覆盖附加渲染（含自动化巡检帧截取，确保捕获叠层）
    OnRender();
    m_renderer.Flush();

    m_renderCpuMs=(SDL_GetTicksNS()-renderStart)/1000000.0;
    m_cpuHistory[m_cpuHistoryWrite] = m_updateCpuMs + m_renderCpuMs;
    m_cpuHistoryWrite = (m_cpuHistoryWrite + 1) % m_cpuHistory.size();
    if (m_cpuHistoryCount < m_cpuHistory.size()) ++m_cpuHistoryCount;
    OnFrameEnd();
    m_renderer.EndFrame(); // Present / VSync wait is excluded from CPU submission time.
}

void App::OnUpdate(float /*dt*/)
{
    // 默认空实现，子类可覆盖
}

void App::OnRender()
{
    // 默认空实现，子类可覆盖
}

void App::OnEvent(const SDL_Event& /*event*/)
{
    // 默认空实现，子类可覆盖
}

void App::OnFrameBegin()
{
    // 默认空实现，子类可覆盖
}

void App::OnFrameEnd()
{
    // 默认空实现，子类可覆盖
}

} // namespace sakura::core
