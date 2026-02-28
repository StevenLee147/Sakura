#pragma once

// toast.h — Toast 通知 UI 组件（右下角滑入/停留/滑出）

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <vector>
#include <deque>
#include <functional>

namespace sakura::ui
{

// Toast 类型
enum class ToastType
{
    Info,
    Success,
    Warning,
    Error
};

// 单条 Toast 数据
struct ToastData
{
    std::string message;
    ToastType   type          = ToastType::Info;
    float       duration      = 3.0f;       // 停留秒数
};

// ── 单条 Toast 显示实例 ──────────────────────────────────────────────────────

struct ToastInstance
{
    ToastData   data;
    float       lifeTimer     = 0.0f;       // 存活计时
    float       slideAnim     = 0.0f;       // 0=完全滑出右边，1=完全到位

    enum class State { SlideIn, Stay, SlideOut, Dead } state = State::SlideIn;

    static constexpr float SLIDE_IN_DUR  = 0.25f;
    static constexpr float SLIDE_OUT_DUR = 0.30f;

    bool IsDead() const { return state == State::Dead; }
};

// ── ToastManager ─────────────────────────────────────────────────────────────

// 管理所有 Toast 的队列，在每帧由场景驱动 Update/Render
// 使用静态单例模式，方便全局调用
class ToastManager
{
public:
    static ToastManager& Instance();

    // 推送通知
    void Show(const std::string& message,
              ToastType type        = ToastType::Info,
              float duration        = 3.0f);

    // 每帧调用
    void Update(float dt);
    void Render(sakura::core::Renderer& renderer,
                sakura::core::FontHandle fontHandle,
                float normFontSize = 0.024f);

    void SetMaxVisible(int max)  { m_maxVisible = max; }
    int  GetMaxVisible() const   { return m_maxVisible; }

private:
    ToastManager() = default;

    std::deque<ToastInstance> m_toasts;
    int                       m_maxVisible = 5;

    // Toast 尺寸（归一化）
    static constexpr float TOAST_W = 0.28f;
    static constexpr float TOAST_H = 0.060f;
    static constexpr float TOAST_X = 1.0f - TOAST_W - 0.015f;   // 右距
    static constexpr float TOAST_BASE_Y = 1.0f - TOAST_H - 0.02f; // 底距
    static constexpr float TOAST_GAP   = TOAST_H + 0.008f;        // 上下间距

    // 根据 type 返回颜色
    static sakura::core::Color GetTypeColor(ToastType type);
    static sakura::core::Color GetIconColor(ToastType type);
    static const char*         GetTypeIcon(ToastType type);
};

} // namespace sakura::ui
