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

    sakura::core::Color m_bgColor        = { 16, 22, 38, 212 };
    sakura::core::Color m_hoverColor     = { 38, 48, 78, 195 };
    sakura::core::Color m_activeColor    = { 54, 44, 82, 228 };
    sakura::core::Color m_textColor      = { 174, 180, 204, 225 };
    sakura::core::Color m_activeTextColor= { 252, 242, 248, 255 };
    sakura::core::Color m_indicatorColor = { 250, 216, 157, 255 };

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
