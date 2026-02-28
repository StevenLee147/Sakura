#pragma once

// scene_calibration.h — 延迟校准场景（Step 2.4 完整实现）

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "ui/toast.h"

#include <deque>
#include <memory>
#include <string>

namespace sakura::scene
{

// SceneCalibration — 延迟校准
// BPM 120 节拍循环 + 脉冲圆圈；玩家按空格对拍
// 收集 20 次有效偏差（忽略 >200ms），计算平均值后保存到 Config 并显示 Toast
class SceneCalibration final : public Scene
{
public:
    explicit SceneCalibration(SceneManager& mgr);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    sakura::core::FontHandle m_font      = sakura::core::INVALID_HANDLE;

    // 节拍
    static constexpr float BPM            = 120.0f;
    static constexpr float BEAT_INTERVAL  = 60.0f / BPM;   // 0.5s
    static constexpr int   MAX_SAMPLES    = 20;
    static constexpr int   IGNORE_THRESH  = 200;           // ms

    float m_beatTimer       = 0.0f;   // 距离上次 beat 的时间（秒）
    int   m_lastBeatTimeMs  = 0;       // 上次节拍的绝对时间 ms（用于偏差计算）
    float m_totalTimeMs     = 0.0f;   // 累计运行时间 ms

    // 脉冲动画（0=无脉冲，1=刚好节拍）
    float m_pulseAnim = 0.0f;

    // 样本收集
    std::deque<int> m_samples;   // 偏差值（ms）
    bool  m_hasResult    = false;
    int   m_resultAvg    = 0;
    int   m_resultStddev = 0;

    // UI
    std::unique_ptr<sakura::ui::Button> m_btnApply;
    std::unique_ptr<sakura::ui::Button> m_btnRetry;
    std::unique_ptr<sakura::ui::Button> m_btnBack;

    void ComputeResult();
    void ApplyResult();
    void Retry();
    void SetupButtons();
};

} // namespace sakura::scene
