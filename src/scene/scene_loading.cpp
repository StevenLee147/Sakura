// scene_loading.cpp — 通用加载场景

#include "scene_loading.h"
#include "utils/logger.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::scene
{

// ── Tips 列表（静态） ─────────────────────────────────────────────────────────

const std::vector<std::string> SceneLoading::k_tips =
{
    "Tip: 保持节奏，熟能生巧。",
    "Tip: 合理使用 Offset 可以改善手感。",
    "Tip: Drag 音符只需在起止轨道按下即可。",
    "Tip: Slider 需要全程跟踪鼠标路径。",
    "Tip: 获得 SS 段位需要保持 99% 以上准确率。",
    "Tip: 连击越高，分数加成越多（最高 10%）。",
    "Tip: 可以在设置中调整判定偏移 (Judge Offset)。",
    "Tip: 每种音符类型都有独特的判定逻辑，多加练习！",
};

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneLoading::SceneLoading(SceneManager& mgr,
                             std::vector<LoadingTask> tasks,
                             std::function<std::unique_ptr<Scene>()> sceneFactory)
    : m_manager(mgr)
    , m_tasks(std::move(tasks))
    , m_sceneFactory(std::move(sceneFactory))
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneLoading::OnEnter()
{
    LOG_INFO("[SceneLoading] 进入加载场景，共 {} 个任务",
             static_cast<int>(m_tasks.size()));

    m_taskIndex      = 0;
    m_loadingDone    = false;
    m_progress       = 0.0f;
    m_targetProgress = 0.0f;
    m_doneTimer      = 0.0f;
    m_spinAngle      = 0.0f;

    // 随机 tip
    if (!k_tips.empty())
    {
        m_tipIndex = static_cast<int>(SDL_GetTicks() % k_tips.size());
    }

    // 获取字体
    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontUI  = rm.GetDefaultFontHandle();
    m_fontTip = rm.GetDefaultFontHandle();

    // 若没有任务，直接标记完成
    if (m_tasks.empty())
    {
        m_loadingDone    = true;
        m_targetProgress = 1.0f;
        m_progress       = 1.0f;
    }
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneLoading::OnExit()
{
    LOG_INFO("[SceneLoading] 退出加载场景");
}

// ── ExecuteNextTask ───────────────────────────────────────────────────────────

void SceneLoading::ExecuteNextTask()
{
    if (m_taskIndex >= static_cast<int>(m_tasks.size())) return;

    const auto& task = m_tasks[m_taskIndex];
    LOG_DEBUG("[SceneLoading] 执行任务 [{}/{}]: {}",
              m_taskIndex + 1,
              static_cast<int>(m_tasks.size()),
              task.name);

    try
    {
        task.work();
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[SceneLoading] 任务 '{}' 异常: {}", task.name, e.what());
    }

    ++m_taskIndex;
    m_targetProgress = static_cast<float>(m_taskIndex)
                     / static_cast<float>(m_tasks.size());

    if (m_taskIndex >= static_cast<int>(m_tasks.size()))
    {
        m_loadingDone    = true;
        m_targetProgress = 1.0f;
        LOG_INFO("[SceneLoading] 所有任务完成");
    }
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneLoading::OnUpdate(float dt)
{
    // 旋转动画
    m_spinAngle += SPIN_SPEED * dt;
    if (m_spinAngle >= 360.0f) m_spinAngle -= 360.0f;

    // 进度条平滑插值（EaseOutCubic 接近目标）
    float diff = m_targetProgress - m_progress;
    if (diff > 0.001f)
    {
        m_progress += diff * std::min(1.0f, dt * 8.0f);
    }
    else
    {
        m_progress = m_targetProgress;
    }

    // 每帧执行一个任务（同步模式，每帧一个避免卡顿太久）
    if (!m_loadingDone && m_taskIndex < static_cast<int>(m_tasks.size()))
    {
        ExecuteNextTask();
    }

    // 加载完成后等待进度条走满再跳转
    if (m_loadingDone)
    {
        m_doneTimer += dt;
        if (m_doneTimer >= DONE_DELAY && m_progress >= 0.99f)
        {
            // 构建目标场景并切换
            if (m_sceneFactory)
            {
                auto nextScene = m_sceneFactory();
                if (nextScene)
                {
                    m_manager.SwitchScene(std::move(nextScene),
                                          TransitionType::Fade, 0.5f);
                }
                else
                {
                    LOG_WARN("[SceneLoading] sceneFactory 返回 nullptr，加载完成但无目标场景");
                }
            }
        }
    }
}

// ── RenderSpinner ─────────────────────────────────────────────────────────────

void SceneLoading::RenderSpinner(sakura::core::Renderer& renderer,
                                  float cx, float cy, float radius)
{
    // 绘制旋转弧线（270° 的弧，留 90° 缺口）
    float startAngle = m_spinAngle;
    float endAngle   = m_spinAngle + 270.0f;

    renderer.DrawArc(cx, cy, radius,
                     startAngle, endAngle,
                     sakura::core::Color{ 200, 150, 230, 220 },
                     0.004f, 48);

    // 弧头的亮点
    float headRad = (m_spinAngle * 3.14159265f / 180.0f);
    float dotX = cx + radius * std::cos(headRad);
    float dotY = cy + radius * std::sin(headRad);
    renderer.DrawCircleFilled(dotX, dotY, 0.008f,
        sakura::core::Color{ 255, 200, 255, 240 });
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneLoading::OnRender(sakura::core::Renderer& renderer)
{
    // 背景
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 10, 8, 20, 255 });

    // 旋转动画（居中，进度条上方）
    RenderSpinner(renderer, 0.5f, 0.45f, 0.04f);

    // 进度条 (0.3, 0.55, 0.4, 0.03)
    {
        // 背景轨道
        renderer.DrawRoundedRect(
            { 0.3f, 0.55f, 0.4f, 0.03f }, 0.008f,
            sakura::core::Color{ 30, 25, 50, 200 }, true);

        // 填充
        float fillW = 0.4f * std::max(0.0f, std::min(1.0f, m_progress));
        if (fillW > 0.016f)
        {
            renderer.DrawRoundedRect(
                { 0.3f, 0.55f, fillW, 0.03f }, 0.008f,
                sakura::core::Color{ 180, 120, 210, 230 }, true);
        }

        // 边框
        renderer.DrawRoundedRect(
            { 0.3f, 0.55f, 0.4f, 0.03f }, 0.008f,
            sakura::core::Color{ 120, 80, 160, 160 }, false);
    }

    if (m_fontUI == sakura::core::INVALID_HANDLE) return;

    // 百分比文字
    {
        int pct = static_cast<int>(m_progress * 100.0f);
        std::string pctStr = std::to_string(pct) + "%";
        renderer.DrawText(m_fontUI, pctStr,
            0.5f, 0.594f, 0.024f,
            sakura::core::Color{ 220, 200, 240, 200 },
            sakura::core::TextAlign::Center);
    }

    // Tips 文字
    if (!k_tips.empty() && m_tipIndex >= 0 &&
        m_tipIndex < static_cast<int>(k_tips.size()))
    {
        renderer.DrawText(m_fontTip, k_tips[m_tipIndex],
            0.5f, 0.66f, 0.020f,
            sakura::core::Color{ 170, 160, 190, 160 },
            sakura::core::TextAlign::Center);
    }
}

} // namespace sakura::scene
