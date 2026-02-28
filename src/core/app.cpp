#include "app.h"
#include "config.h"
#include "utils/logger.h"
#include "scene/test_scenes.h"
#include "scene/scene_splash.h"
#include "audio/audio_manager.h"
#include "game/chart_loader.h"

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
    sakura::utils::Logger::Init("logs/sakura.log");

    LOG_INFO("正在初始化 Sakura-樱...");
    // ── 配置系统 ────────────────────────────────────────────────────────────────
    Config::GetInstance().Load("config/settings.json");
    // ── SDL 初始化 ────────────────────────────────────────────────────────────
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        LOG_ERROR("SDL_Init 失败: {}", SDL_GetError());
        return false;
    }
    LOG_INFO("SDL 初始化成功");

    // ── 窗口 ──────────────────────────────────────────────────────────────────
    if (!m_window.Create("Sakura-樱", 1920, 1080))
    {
        return false;
    }

    // ── 渲染器 ────────────────────────────────────────────────────────────────
    if (!m_renderer.Initialize(m_window.GetSDLWindow()))
    {
        return false;
    }
    // ── 资源管理器 ───────────────────────────────────────────────────────────
    if (!ResourceManager::GetInstance().Initialize(m_renderer.GetSDLRenderer()))
    {
        LOG_WARN("ResourceManager 初始化失败（非致命）");
    }
    // ── 计时器 ────────────────────────────────────────────────────────────────
    m_timer.Reset();

    // ── 音频管理器 ────────────────────────────────────────────────────────────
    if (!sakura::audio::AudioManager::GetInstance().Initialize())
    {
        LOG_WARN("AudioManager 初始化失败（非致命）");
    }

    // ── 谱面加载器验证（Step 1.3 验收）──────────────────────────────────────────
    {
        sakura::game::ChartLoader loader;
        auto charts = loader.ScanCharts("resources/charts/");
        if (!charts.empty())
        {
            const auto& firstChart = charts[0];
            if (!firstChart.difficulties.empty())
            {
                std::string chartDataPath = firstChart.folderPath + "/"
                                          + firstChart.difficulties[0].chartFile;
                auto chartData = loader.LoadChartData(chartDataPath);
                if (chartData)
                {
                    bool valid = loader.ValidateChartData(*chartData);
                    LOG_INFO("谱面验证 [{}]: 键盘音符={}, 鼠标音符={}, 校验={}",
                             firstChart.id,
                             chartData->keyboardNotes.size(),
                             chartData->mouseNotes.size(),
                             valid ? "通过" : "失败");
                }
            }
        }
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
    m_accumulator = 0.0;

    while (m_running)
    {
        m_timer.Tick();
        const float dt = m_timer.GetDeltaTime();

        // ── 事件处理 ──────────────────────────────────────────────────────────
        ProcessEvents();

        // ── 固定时间步长更新（最多 MAX_STEPS 步，防止死亡螺旋）────────────
        m_accumulator += static_cast<double>(dt);
        int steps = 0;
        while (m_accumulator >= FIXED_TIMESTEP && steps < MAX_STEPS)
        {
            Update(static_cast<float>(FIXED_TIMESTEP));
            m_accumulator -= FIXED_TIMESTEP;
            ++steps;
        }

        // ── 可变帧率渲染 ──────────────────────────────────────────────────────
        Render();

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
    LOG_INFO("正在关闭 Sakura-樱...");

    // 先关闭音频（避免资源释放竞争）
    sakura::audio::AudioManager::GetInstance().Shutdown();

    // 释放所有资源（渲染器销毁前）
    ResourceManager::GetInstance().ReleaseAll();

    m_renderer.Destroy();
    m_window.Destroy();

    SDL_Quit();

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
        m_window.HandleEvent(event);

        // 输入系统处理
        Input::ProcessEvent(event);

        // 场景事件处理
        m_sceneManager.HandleEvent(event);

        // 再转发给子类
        OnEvent(event);

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                {
                    m_running = false;
                }
                break;
            default:
                break;
        }
    }
}

void App::Update(float dt)
{
    // 同步屏幕尺寸给输入系统（用于归一化鼠标坐标）
    Input::SetScreenSize(m_renderer.GetScreenWidth(), m_renderer.GetScreenHeight());

    // 场景更新
    m_sceneManager.Update(dt);

    OnUpdate(dt);

    // 帧末重置 pressed/released 状态
    Input::Update();
}

void App::Render()
{
    m_renderer.BeginFrame();
    m_renderer.Clear(Color::DarkBlue);

    // 场景渲染
    m_sceneManager.Render(m_renderer);

    // 子类可覆盖附加渲染
    OnRender();

    m_renderer.EndFrame();
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
