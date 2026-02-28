#pragma once

// dropdown.h — 下拉框 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <vector>
#include <functional>

namespace sakura::ui
{

// Dropdown — 下拉选择框组件
// z-order 最高（需在所有其他组件之后渲染）
// 收起/展开带动画，onChange 回调
class Dropdown : public UIBase
{
public:
    Dropdown(sakura::core::NormRect bounds,
             const std::vector<std::string>& options,
             int selectedIndex                  = 0,
             sakura::core::FontHandle fontHandle = 0,
             float normFontSize                 = 0.028f);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    int GetSelectedIndex() const { return m_selectedIndex; }
    const std::string& GetSelectedOption() const;

    void SetOptions(const std::vector<std::string>& options);
    void SetSelectedIndex(int index);

    // 颜色
    void SetBgColor(sakura::core::Color c)        { m_bgColor = c; }
    void SetHoverColor(sakura::core::Color c)     { m_hoverColor = c; }
    void SetSelectedColor(sakura::core::Color c)  { m_selectedColor = c; }
    void SetTextColor(sakura::core::Color c)      { m_textColor = c; }
    void SetArrowColor(sakura::core::Color c)     { m_arrowColor = c; }

    // 回调
    void SetOnChange(std::function<void(int, const std::string&)> cb)
    {
        m_onChange = std::move(cb);
    }

    bool IsOpen() const { return m_isOpen; }
    void Close()        { m_isOpen = false; m_openAnim = 0.0f; }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    std::vector<std::string>     m_options;
    int                          m_selectedIndex;

    sakura::core::FontHandle     m_fontHandle;
    float                        m_normFontSize;

    sakura::core::Color m_bgColor        = { 45, 45, 65, 230 };
    sakura::core::Color m_hoverColor     = { 80, 80, 115, 240 };
    sakura::core::Color m_selectedColor  = { 110, 70, 160, 240 };
    sakura::core::Color m_textColor      = { 220, 220, 220, 255 };
    sakura::core::Color m_arrowColor     = { 180, 160, 210, 255 };

    std::function<void(int, const std::string&)> m_onChange;

    bool  m_isOpen   = false;
    float m_openAnim = 0.0f;   // 0=收起，1=全开
    static constexpr float OPEN_DURATION = 0.15f;

    int m_hoveredIndex = -1;

    // 每个下拉项的高度（归一化）= bounds.height
    float GetItemHeight() const { return m_bounds.height; }

    // 下拉面板的归一化区域
    sakura::core::NormRect GetDropdownRect() const;
};

} // namespace sakura::ui
