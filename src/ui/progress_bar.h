#pragma once

// progress_bar.h — 进度条 UI 组件

#include "ui_base.h"
#include "core/renderer.h"
#include <string>

namespace sakura::ui
{

// ProgressBar — 水平进度条组件
// - value: 0.0~1.0 的当前进度
// - 平滑过渡动画（200ms EaseOutCubic）
// - 可选百分比文字标签
class ProgressBar : public UIBase
{
public:
    explicit ProgressBar(sakura::core::NormRect bounds,
                         float normCornerRadius = 0.008f);

    // ── 进度控制 ──────────────────────────────────────────────────────────────

    // 设置目标进度（0.0~1.0），内部平滑过渡
    void SetValue(float value);

    // 立即跳到指定进度，不做动画
    void SetValueImmediate(float value);

    float GetValue()             const { return m_targetValue; }
    float GetDisplayedValue()    const { return m_displayValue; }

    // ── 样式 ──────────────────────────────────────────────────────────────────

    void SetBgColor(sakura::core::Color color)       { m_bgColor    = color; }
    void SetFillColor(sakura::core::Color color)     { m_fillColor  = color; }
    void SetBorderColor(sakura::core::Color color)   { m_borderColor = color; }
    void SetShowBorder(bool show)                    { m_showBorder  = show; }

    // 可选文字（显示在进度条右侧或内部，传空串则不显示）
    void SetLabel(const std::string& label)          { m_label = label; }

    // 是否显示百分比文字（叠加在进度条中央）
    void SetShowPercentage(bool show, sakura::core::FontHandle font = 0,
                           float normFontSize = 0.025f);

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;

private:
    float  m_targetValue  = 0.0f;   // 目标进度
    float  m_displayValue = 0.0f;   // 当前显示进度（动画插值）
    float  m_animTimer    = 1.0f;   // 动画计时器（0=开始, 1=完成）
    float  m_animFrom     = 0.0f;   // 动画起始值
    static constexpr float ANIM_DURATION = 0.20f;  // 200ms

    float  m_normCornerRadius;

    sakura::core::Color m_bgColor     = { 25, 25, 45, 200 };
    sakura::core::Color m_fillColor   = { 140, 100, 200, 230 };
    sakura::core::Color m_borderColor = { 100, 80, 150, 180 };
    bool  m_showBorder  = true;

    // 文字
    std::string              m_label;
    bool                     m_showPercentage = false;
    sakura::core::FontHandle m_percentFont    = 0;
    float                    m_percentFontSize = 0.025f;
};

} // namespace sakura::ui
