#pragma once

// scene_loading.h — 通用加载场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sakura::scene
{

// LoadingTask — 单个加载任务
struct LoadingTask
{
    std::string                name;    // 任务描述（调试用）
    std::function<void()>      work;    // 实际工作函数（同步）
};

// SceneLoading — 通用加载画面
// 显示旋转动画 + 进度条 + 百分比 + 随机 tips
// 加载完成后通过 sceneFactory 创建并切换到目标场景
class SceneLoading final : public Scene
{
public:
    // tasks      : 需要执行的加载任务列表
    // sceneFactory: 加载完成后创建目标场景的工厂函数（返回 unique_ptr<Scene>）
    SceneLoading(SceneManager& mgr,
                 std::vector<LoadingTask> tasks,
                 std::function<std::unique_ptr<Scene>()> sceneFactory);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override { (void)event; }

private:
    SceneManager& m_manager;

    std::vector<LoadingTask>                    m_tasks;
    std::function<std::unique_ptr<Scene>()>     m_sceneFactory;

    int   m_taskIndex     = 0;       // 下一个待执行的任务下标
    bool  m_loadingDone   = false;   // 所有任务已完成
    float m_progress      = 0.0f;   // 当前显示进度（平滑插值到 m_targetProgress）
    float m_targetProgress = 0.0f;  // 目标进度（任务完成后跳跃）

    // 切换等待（加载完毕后略微延迟再跳转，让进度条走满）
    float m_doneTimer = 0.0f;
    static constexpr float DONE_DELAY = 0.4f;

    // 旋转动画
    float m_spinAngle = 0.0f;   // 当前旋转角度（度）
    static constexpr float SPIN_SPEED = 240.0f;  // 度/秒

    // Tips 文化
    int   m_tipIndex = 0;
    static const std::vector<std::string> k_tips;

    // 字体
    sakura::core::FontHandle m_fontUI  = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontTip = sakura::core::INVALID_HANDLE;

    // 内部工具
    void ExecuteNextTask();
    void RenderSpinner(sakura::core::Renderer& renderer,
                       float cx, float cy, float radius);
};

} // namespace sakura::scene
