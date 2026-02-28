#pragma once

// scroll_list.h — 可滚动列表 UI 组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>
#include <vector>
#include <functional>

namespace sakura::ui
{

// ScrollList — 可滚动列表组件
// - 支持鼠标滚轮滚动（带惯性 + 边界弹回）
// - 点击选中，双击触发回调
// - 所有坐标均为归一化（0.0~1.0）
class ScrollList : public UIBase
{
public:
    ScrollList(sakura::core::NormRect bounds,
               sakura::core::FontHandle fontHandle,
               float normItemHeight = 0.06f,
               float normFontSize   = 0.028f);

    // ── 数据 ──────────────────────────────────────────────────────────────────

    void SetItems(const std::vector<std::string>& items);
    void AddItem(const std::string& item);
    void ClearItems();

    int GetSelectedIndex() const { return m_selectedIndex; }

    // -1 = 取消选中
    void SetSelectedIndex(int index);

    // ── 样式 ──────────────────────────────────────────────────────────────────

    void SetNormalColor(sakura::core::Color color)   { m_normalColor = color; }
    void SetHoverColor(sakura::core::Color color)    { m_hoverColor  = color; }
    void SetSelectedColor(sakura::core::Color color) { m_selectedColor = color; }
    void SetTextColor(sakura::core::Color color)     { m_textColor = color; }
    void SetBgColor(sakura::core::Color color)       { m_bgColor = color; }

    // ── 回调 ──────────────────────────────────────────────────────────────────

    void SetOnSelectionChanged(std::function<void(int index)> cb)
    {
        m_onSelectionChanged = std::move(cb);
    }
    void SetOnDoubleClick(std::function<void(int index)> cb)
    {
        m_onDoubleClick = std::move(cb);
    }

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;
    bool HandleEvent(const SDL_Event& event) override;

    // 将指定条目滚动到可见区域
    void ScrollToIndex(int index, bool immediate = false);

private:
    // 列表内容
    std::vector<std::string>       m_items;
    int                            m_selectedIndex = -1;
    int                            m_hoveredIndex  = -1;

    // 布局
    sakura::core::FontHandle m_fontHandle;
    float m_normItemHeight;
    float m_normFontSize;

    // 颜色
    sakura::core::Color m_bgColor       = { 20, 20, 40, 200 };
    sakura::core::Color m_normalColor   = { 35, 35, 60, 200 };
    sakura::core::Color m_hoverColor    = { 55, 55, 90, 220 };
    sakura::core::Color m_selectedColor = { 90, 75, 140, 240 };
    sakura::core::Color m_textColor     = { 220, 220, 240, 255 };

    // 滚动状态
    float m_scrollOffset  = 0.0f;   // 当前滚动偏移（归一化高度单位）
    float m_targetOffset  = 0.0f;   // 目标偏移（含惯性）
    float m_scrollVelocity = 0.0f;  // 惯性速度（归一化/s）

    // 双击检测
    int   m_lastClickIndex = -1;
    float m_lastClickTimer = 0.0f;
    static constexpr float DOUBLE_CLICK_TIME = 0.35f;  // 350ms 内第二次为双击

    // 回调
    std::function<void(int)> m_onSelectionChanged;
    std::function<void(int)> m_onDoubleClick;

    // 内部工具
    int   GetItemIndexAt(float normX, float normY) const;
    float GetMaxScrollOffset() const;
    void  ClampTargetOffset();
};

} // namespace sakura::ui
