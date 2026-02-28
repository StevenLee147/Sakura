#pragma once

// slider.h — 滑块 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <functional>
#include <optional>

namespace sakura::ui
{

// Slider — 水平滑块组件
// 支持拖拽、点击调整；可选标签+数值显示
// 所有坐标均为归一化（0.0~1.0）
class Slider : public UIBase
{
public:
    Slider(sakura::core::NormRect bounds,
           float minValue,
           float maxValue,
           float initValue,
           float step = 0.01f,
           sakura::core::FontHandle fontHandle = 0,
           float normFontSize = 0.025f);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    float GetValue()   const { return m_value; }
    float GetMinValue() const { return m_minValue; }
    float GetMaxValue() const { return m_maxValue; }

    void SetValue(float value);
    void SetRange(float minVal, float maxVal);
    void SetStep(float step)           { m_step = step; }

    // 可选标签文字（显示在滑块左侧）
    void SetLabel(const std::string& label) { m_label = label; }

    // 是否在右侧显示当前数值
    void SetShowValue(bool show)  { m_showValue = show; }

    // 格式化函数（默认保留2位小数）
    void SetValueFormatter(std::function<std::string(float)> formatter)
    {
        m_formatter = std::move(formatter);
    }

    // 颜色
    void SetTrackColor(sakura::core::Color c)    { m_trackColor = c; }
    void SetFillColor(sakura::core::Color c)     { m_fillColor = c; }
    void SetThumbColor(sakura::core::Color c)    { m_thumbColor = c; }
    void SetLabelColor(sakura::core::Color c)    { m_labelColor = c; }

    // 回调
    void SetOnChange(std::function<void(float)> cb) { m_onChange = std::move(cb); }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    float m_minValue;
    float m_maxValue;
    float m_value;
    float m_step;

    std::string m_label;
    bool        m_showValue   = true;

    sakura::core::FontHandle m_fontHandle;
    float                    m_normFontSize;

    sakura::core::Color m_trackColor = { 60, 60, 80, 200 };
    sakura::core::Color m_fillColor  = { 160, 100, 200, 220 };
    sakura::core::Color m_thumbColor = { 220, 180, 255, 255 };
    sakura::core::Color m_labelColor = { 220, 220, 220, 255 };

    std::function<void(float)>         m_onChange;
    std::function<std::string(float)>  m_formatter;

    bool  m_isDragging = false;
    bool  m_isHovered  = false;

    // 拇指悬停/激活动画（0=正常，1=悬停）
    float m_thumbAnim  = 0.0f;
    static constexpr float THUMB_ANIM_DUR = 0.12f;

    // 获取当前 t（0~1）
    float GetNormT() const;

    // 从归一化鼠标 X 更新值
    void UpdateValueFromMouseX(float mouseNormX);

    // 格式化数值
    std::string FormatValue(float v) const;
};

} // namespace sakura::ui
