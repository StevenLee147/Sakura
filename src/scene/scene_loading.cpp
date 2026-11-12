// scene_loading.cpp — 通用加载场景

#include "scene_loading.h"
#include "ui/visual_style.h"
#include "utils/logger.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sakura::scene
{

// ── Tips 列表（静态） ─────────────────────────────────────────────────────────

const std::vector<std::string> SceneLoading::k_tips =
{
    "Tip: 保持节奏，熟能生巧。",
    "Tip: 合理使用 Offset 可以改善手感。",
    "Tip: Hold 音符要按到尾部结束再松开。",
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
                                          TransitionType::SakuraSweep, 0.52f);
                }
                else
                {
                    LOG_WARN("[SceneLoading] sceneFactory 返回 nullptr，加载完成但无目标场景");
                }
            }
        }
    }
}

// ── RenderSpinner (星盘落樱旋转仪) ────────────────────────────────────────────
void SceneLoading::RenderSpinner(sakura::core::Renderer& renderer,
                                  float cx, float cy, float radius)
{
    const float aspect = static_cast<float>(renderer.GetScreenHeight()) / std::max(1, renderer.GetScreenWidth());

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        cx, cy, radius * 2.2f, radius * 2.2f,
        { 246, 172, 200, 70 }, { 246, 172, 200, 0 }, 40);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    // 外层鎏金细环 + 旋转主樱弧
    renderer.DrawCircleOutline(cx, cy, radius * 1.22f, { 250, 216, 157, 72 }, 0.0012f, 56);
    renderer.DrawArc(cx, cy, radius, m_spinAngle, m_spinAngle + 250.0f,
                     { 246, 172, 200, 238 }, 0.0036f, 48);
    // 内层逆向香槟金星轨弧
    renderer.DrawArc(cx, cy, radius * 0.72f, -m_spinAngle * 1.35f, -m_spinAngle * 1.35f + 195.0f,
                     { 250, 216, 157, 215 }, 0.0024f, 40);

    // 中心樱纹徽章
    const auto crestTex = sakura::ui::VisualStyle::GetSakuraCrestTexture();
    if (crestTex != sakura::core::INVALID_HANDLE)
    {
        const float s = radius * 1.10f;
        const float w = s * aspect;
        renderer.DrawSprite(crestTex, { cx - w * 0.5f, cy - s * 0.5f, w, s },
                            m_spinAngle * 0.25f, sakura::core::Color::White, 0.92f);
    }

    // 弧端流光珍珠
    const float headRad = (m_spinAngle + 250.0f) * std::numbers::pi_v<float> / 180.0f;
    const float dotX = cx + radius * aspect * std::cos(headRad);
    const float dotY = cy + radius * std::sin(headRad);
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(dotX, dotY, 0.016f, 0.016f, { 255, 228, 196, 180 }, { 255, 228, 196, 0 }, 24);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    renderer.DrawCircleFilled(dotX, dotY, 0.0055f, { 255, 245, 232, 255 }, 24);
}

// ── OnRender ──────────────────────────────────────────────────────────────────
void SceneLoading::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, m_spinAngle * 0.008f, 0.58f);

    // 中央磨砂琉璃加载面板
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer,
        { 0.24f, 0.25f, 0.52f, 0.48f },
        { 22, 28, 52, 206 }, { 14, 18, 34, 224 },
        { 250, 216, 157, 155 }, { 164, 154, 212, 68 },
        0.020f, 0.52f);

    RenderSpinner(renderer, 0.5f, 0.39f, 0.048f);

    // 琉璃流光进度槽
    const sakura::core::NormRect barRect{ 0.31f, 0.535f, 0.38f, 0.022f };
    renderer.DrawRoundedGradientRect(
        barRect, 0.011f,
        { 10, 14, 26, 225 }, { 10, 14, 26, 225 },
        { 18, 24, 42, 225 }, { 18, 24, 42, 225 }, 10, true);

    const float clamped = std::clamp(m_progress, 0.0f, 1.0f);
    const float fillW = barRect.width * clamped;
    if (fillW > 0.004f)
    {
        const float drawW = std::max(0.022f, fillW);
        renderer.DrawRoundedGradientRect(
            { barRect.x, barRect.y, std::min(barRect.width, drawW), barRect.height },
            0.011f,
            { 246, 166, 198, 245 }, { 250, 216, 157, 248 },
            { 236, 146, 186, 245 }, { 242, 198, 138, 248 }, 10, true);
    }
    renderer.DrawRoundedRectOutlineGradient(
        barRect, 0.011f,
        { 250, 216, 157, 140 }, { 168, 158, 214, 65 }, 0.0012f, 10);

    if (m_fontUI == sakura::core::INVALID_HANDLE) return;

    const int pct = static_cast<int>(clamped * 100.0f);
    const std::string taskLabel = (!m_loadingDone && m_taskIndex < static_cast<int>(m_tasks.size()))
        ? m_tasks[m_taskIndex].name : "准备就绪";
    renderer.DrawText(m_fontUI, taskLabel + "  ·  " + std::to_string(pct) + "%",
        0.5f, 0.576f, 0.022f,
        { 244, 236, 248, 230 },
        sakura::core::TextAlign::Center);

    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.32f, 0.68f, 0.625f, 0.55f);

    if (!k_tips.empty() && m_tipIndex >= 0 && m_tipIndex < static_cast<int>(k_tips.size()))
    {
        renderer.DrawText(m_fontTip, k_tips[m_tipIndex],
            0.5f, 0.648f, 0.019f,
            { 194, 198, 222, 205 },
            sakura::core::TextAlign::Center);
    }
}

} // namespace sakura::scene
