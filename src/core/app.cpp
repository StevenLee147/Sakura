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
#include "game/chart_loader.h"
#include "game/achievement_manager.h"
#include "data/database.h"
#include "effects/screen_shake.h"
#include "effects/shader_manager.h"
#include "ui/button.h"

#include <cstdlib>
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
        const float dt = m_timer.GetDeltaTime();

        // ── 事件处理 ──────────────────────────────────────────────────────────
        ProcessEvents();

        // Judge against the audio clock on every displayed frame. A fixed 60 Hz
        // scene update adds latency and makes high-refresh displays visibly stutter.
        const Uint64 updateStart=SDL_GetTicksNS();
        Update(dt);
        m_updateCpuMs=(SDL_GetTicksNS()-updateStart)/1000000.0;

        // ── 可变帧率渲染 ──────────────────────────────────────────────────────
        Render();
        int limit = Config::GetInstance().Get<int>(ConfigKeys::kFpsLimit, 240);
        if (SDL_GetWindowFlags(m_window.GetSDLWindow()) & SDL_WINDOW_MINIMIZED) limit = 30;
        if (limit > 0)
        {
            const Uint64 budget = 1000000000ULL / static_cast<Uint64>(limit);
            const Uint64 elapsed = SDL_GetTicksNS() - frameStart;
            if (elapsed < budget) SDL_DelayPrecise(budget - elapsed);
        }

        // ── FPS 日志（每 3 秒输出一次）────────────────────────────────────────
        m_fpsLogTimer += dt;
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

void App::Render()
{
    const Uint64 renderStart=SDL_GetTicksNS();
    m_renderer.BeginFrame();
    m_renderer.Clear(Color::DarkBlue);

    // 场景渲染
    m_sceneManager.Render(m_renderer);

    // 后处理特效（暗角等覆盖层，在所有场景之上）
    m_renderer.Flush();
    sakura::effects::ShaderManager::GetInstance().ApplyPostProcess();

    // 子类可覆盖附加渲染
    OnRender();

    sakura::ui::ToastManager::Instance().Render(m_renderer, ResourceManager::GetInstance().GetDefaultFontHandle());
    if (Config::GetInstance().Get<bool>("graphics.show_fps", false))
        m_renderer.DrawText(ResourceManager::GetInstance().GetDefaultFontHandle(),
            std::to_string(static_cast<int>(m_timer.GetFPS())) + " FPS", 0.985f, 0.008f, 0.016f,
            Color{180, 225, 210, 255}, TextAlign::Right);
    m_renderer.Flush();
    m_renderCpuMs=(SDL_GetTicksNS()-renderStart)/1000000.0;
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

} // namespace sakura::core
