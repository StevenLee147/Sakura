#pragma once

// tab_bar.h — 标签栏 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <vector>
#include <functional>

namespace sakura::ui
{

// TabBar — 水平/垂直标签栏
// 选中高亮 + 滑动指示条动画; onChange 回调
class TabBar : public UIBase
{
public:
    // orientation: 当前只支持水平（Horizontal）和垂直（Vertical）
    enum class Orientation { Horizontal, Vertical };

    TabBar(sakura::core::NormRect bounds,
           const std::vector<std::string>& tabs,
           int selectedIndex                  = 0,
           sakura::core::FontHandle fontHandle = 0,
           float normFontSize                 = 0.026f,
           Orientation orientation            = Orientation::Horizontal);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    int GetSelectedIndex() const { return m_selectedIndex; }
    void SetSelectedIndex(int index);
    void SetTabs(const std::vector<std::string>& tabs);

    // 颜色
    void SetBgColor(sakura::core::Color c)        { m_bgColor = c; }
    void SetHoverColor(sakura::core::Color c)     { m_hoverColor = c; }
    void SetActiveColor(sakura::core::Color c)    { m_activeColor = c; }
    void SetTextColor(sakura::core::Color c)      { m_textColor = c; }
    void SetActiveTextColor(sakura::core::Color c){ m_activeTextColor = c; }
    void SetIndicatorColor(sakura::core::Color c) { m_indicatorColor = c; }

    // 回调
    void SetOnChange(std::function<void(int)> cb) { m_onChange = std::move(cb); }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

private:
    std::vector<std::string>  m_tabs;
    int                       m_selectedIndex;
    Orientation               m_orientation;

    sakura::core::FontHandle  m_fontHandle;
    float                     m_normFontSize;

    sakura::core::Color m_bgColor        = { 30, 30, 50, 200 };
    sakura::core::Color m_hoverColor     = { 70, 70, 100, 200 };
    sakura::core::Color m_activeColor    = { 50, 50, 80, 240 };
    sakura::core::Color m_textColor      = { 180, 180, 200, 200 };
    sakura::core::Color m_activeTextColor= { 220, 200, 255, 255 };
    sakura::core::Color m_indicatorColor = { 160, 100, 220, 255 };

    std::function<void(int)> m_onChange;

    // 指示条动画（从上次位置滑向当前 tab）
    float m_indicatorPos  = 0.0f;   // 归一化位置（0~1 即 0~n-1 tabs）
    float m_indicatorTarget = 0.0f;
    static constexpr float INDICATOR_DUR = 0.20f;

    int m_hoveredIndex = -1;

    // 计算第 i 个 tab 的区域
    sakura::core::NormRect GetTabRect(int i) const;
};

} // namespace sakura::ui
