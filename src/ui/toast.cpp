// toast.cpp — Toast 通知 UI 组件实现

#include "toast.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ── ToastManager 单例 ─────────────────────────────────────────────────────────

ToastManager& ToastManager::Instance()
{
    static ToastManager s_instance;
    return s_instance;
}

// ── Show ──────────────────────────────────────────────────────────────────────

void ToastManager::Show(const std::string& message,
                        ToastType type,
                        float duration)
{
    ToastInstance inst;
    inst.data.message  = message;
    inst.data.type     = type;
    inst.data.duration = duration;
    inst.state         = ToastInstance::State::SlideIn;
    inst.slideAnim     = 0.0f;
    inst.lifeTimer     = 0.0f;

    // 如果队列已满，淘汰最老的
    while (static_cast<int>(m_toasts.size()) >= m_maxVisible)
    {
        m_toasts.pop_front();
    }
    m_toasts.push_back(std::move(inst));
}

// ── GetTypeColor ──────────────────────────────────────────────────────────────

sakura::core::Color ToastManager::GetTypeColor(ToastType type)
{
    switch (type)
    {
        case ToastType::Info:    return { 40,  55,  80,  230 };
        case ToastType::Success: return { 30,  70,  50,  230 };
        case ToastType::Warning: return { 80,  65,  20,  230 };
        case ToastType::Error:   return { 80,  30,  30,  230 };
    }
    return { 40, 40, 60, 230 };
}

sakura::core::Color ToastManager::GetIconColor(ToastType type)
{
    switch (type)
    {
        case ToastType::Info:    return { 100, 160, 255, 255 };
        case ToastType::Success: return { 100, 220, 130, 255 };
        case ToastType::Warning: return { 255, 200,  80, 255 };
        case ToastType::Error:   return { 255,  90,  90, 255 };
    }
    return { 200, 200, 200, 255 };
}

const char* ToastManager::GetTypeIcon(ToastType type)
{
    switch (type)
    {
        case ToastType::Info:    return "i";
        case ToastType::Success: return "✓";
        case ToastType::Warning: return "!";
        case ToastType::Error:   return "✕";
    }
    return "i";
}

// ── Update ────────────────────────────────────────────────────────────────────

void ToastManager::Update(float dt)
{
    for (auto& t : m_toasts)
    {
        if (t.IsDead()) continue;

        t.lifeTimer += dt;

        switch (t.state)
        {
            case ToastInstance::State::SlideIn:
            {
                float progress = t.lifeTimer / ToastInstance::SLIDE_IN_DUR;
                t.slideAnim    = std::min(progress, 1.0f);
                if (t.slideAnim >= 1.0f)
                {
                    t.state     = ToastInstance::State::Stay;
                    t.lifeTimer = 0.0f;
                }
                break;
            }
            case ToastInstance::State::Stay:
            {
                if (t.lifeTimer >= t.data.duration)
                {
                    t.state     = ToastInstance::State::SlideOut;
                    t.lifeTimer = 0.0f;
                }
                break;
            }
            case ToastInstance::State::SlideOut:
            {
                float progress = t.lifeTimer / ToastInstance::SLIDE_OUT_DUR;
                t.slideAnim    = 1.0f - std::min(progress, 1.0f);
                if (t.slideAnim <= 0.0f)
                {
                    t.state    = ToastInstance::State::Dead;
                }
                break;
            }
            default: break;
        }
    }

    // 清理已死亡的 toast
    m_toasts.erase(
        std::remove_if(m_toasts.begin(), m_toasts.end(),
                       [](const ToastInstance& t){ return t.IsDead(); }),
        m_toasts.end());
}

// ── Render ────────────────────────────────────────────────────────────────────

void ToastManager::Render(sakura::core::Renderer& renderer,
                           sakura::core::FontHandle fontHandle,
                           float normFontSize)
{
    if (m_toasts.empty()) return;

    // 从底部向上渲染
    int count = static_cast<int>(m_toasts.size());
    for (int i = 0; i < count; ++i)
    {
        auto& t = m_toasts[i];
        if (t.IsDead()) continue;

        // 从底部向上排列（第 0 个在最底部）
        float baseY   = TOAST_BASE_Y - static_cast<float>(i) * TOAST_GAP;
        float slideX  = sakura::utils::EaseOutQuad(t.slideAnim);
        // 右侧滑入：slideAnim=0 时完全在屏幕外，=1 时在正常位置
        float toastX  = TOAST_X + (1.0f - slideX) * (TOAST_W + 0.02f);
        float toastY  = baseY;

        sakura::core::NormRect rect { toastX, toastY, TOAST_W, TOAST_H };

        // 背景
        auto bgColor = GetTypeColor(t.data.type);
        bgColor.a    = static_cast<uint8_t>(bgColor.a * slideX);
        renderer.DrawRoundedRect(rect, 0.008f, bgColor, true);

        // 左侧颜色指示条
        auto iconColor = GetIconColor(t.data.type);
        iconColor.a    = static_cast<uint8_t>(iconColor.a * slideX);
        renderer.DrawFilledRect(
            { toastX, toastY, 0.005f, TOAST_H },
            iconColor);

        // 图标
        if (fontHandle != 0)
        {
            const char* icon = GetTypeIcon(t.data.type);
            float iconX = toastX + 0.022f;
            float iconY = toastY + TOAST_H * 0.5f;
            renderer.DrawText(fontHandle, icon,
                              iconX, iconY, normFontSize * 1.1f,
                              iconColor,
                              sakura::core::TextAlign::Center);
        }

        // 消息文字
        if (fontHandle != 0)
        {
            sakura::core::Color textColor = { 220, 220, 230,
                static_cast<uint8_t>(230 * slideX) };
            float msgX = toastX + 0.040f;
            float msgY = toastY + TOAST_H * 0.5f;
            renderer.DrawText(fontHandle, t.data.message,
                              msgX, msgY, normFontSize,
                              textColor,
                              sakura::core::TextAlign::Left);
        }
    }
}

} // namespace sakura::ui
