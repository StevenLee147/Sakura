// scroll_list.cpp — 可滚动列表组件实现

#include "scroll_list.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

ScrollList::ScrollList(sakura::core::NormRect bounds,
                       sakura::core::FontHandle fontHandle,
                       float normItemHeight,
                       float normFontSize)
    : UIBase(bounds)
    , m_fontHandle(fontHandle)
    , m_normItemHeight(normItemHeight)
    , m_normFontSize(normFontSize)
{
}

// ── 数据管理 ──────────────────────────────────────────────────────────────────

void ScrollList::SetItems(const std::vector<std::string>& items)
{
    m_items = items;
    m_selectedIndex = -1;
    m_hoveredIndex  = -1;
    m_scrollOffset  = 0.0f;
    m_targetOffset  = 0.0f;
    m_scrollVelocity = 0.0f;
}

void ScrollList::AddItem(const std::string& item)
{
    m_items.push_back(item);
}

void ScrollList::ClearItems()
{
    m_items.clear();
    m_selectedIndex  = -1;
    m_hoveredIndex   = -1;
    m_scrollOffset   = 0.0f;
    m_targetOffset   = 0.0f;
    m_scrollVelocity = 0.0f;
}

void ScrollList::SetSelectedIndex(int index)
{
    if (index < -1 || index >= static_cast<int>(m_items.size()))
        index = -1;
    m_selectedIndex = index;
}

// ── 滚动工具 ──────────────────────────────────────────────────────────────────

float ScrollList::GetMaxScrollOffset() const
{
    float totalHeight = static_cast<float>(m_items.size()) * m_normItemHeight;
    float maxOffset   = totalHeight - m_bounds.height;
    return std::max(0.0f, maxOffset);
}

void ScrollList::ClampTargetOffset()
{
    m_targetOffset = std::max(0.0f, std::min(GetMaxScrollOffset(), m_targetOffset));
}

void ScrollList::ScrollToIndex(int index, bool immediate)
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;

    float itemTop    = static_cast<float>(index)     * m_normItemHeight;
    float itemBottom = static_cast<float>(index + 1) * m_normItemHeight;
    float viewTop    = m_scrollOffset;
    float viewBottom = m_scrollOffset + m_bounds.height;

    // 只有当条目不在可见区域时才滚动
    if (itemTop < viewTop)
    {
        m_targetOffset = itemTop;
    }
    else if (itemBottom > viewBottom)
    {
        m_targetOffset = itemBottom - m_bounds.height;
    }
    else
    {
        return; // 已在视野内，无需滚动
    }

    ClampTargetOffset();
    if (immediate)
        m_scrollOffset = m_targetOffset;
}

// ── 命中测试 ──────────────────────────────────────────────────────────────────

int ScrollList::GetItemIndexAt(float normX, float normY) const
{
    if (!HitTest(normX, normY)) return -1;

    // 计算列表内相对 Y（含滚动偏移）
    float relY = (normY - m_bounds.y) + m_scrollOffset;
    int   idx  = static_cast<int>(relY / m_normItemHeight);

    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return -1;
    return idx;
}

// ── Update ────────────────────────────────────────────────────────────────────

void ScrollList::Update(float dt)
{
    if (!m_isVisible) return;

    // 双击计时器
    if (m_lastClickTimer > 0.0f)
    {
        m_lastClickTimer -= dt;
        if (m_lastClickTimer < 0.0f)
        {
            m_lastClickTimer = 0.0f;
            m_lastClickIndex = -1;
        }
    }

    // 惯性衰减
    if (std::abs(m_scrollVelocity) > 0.001f)
    {
        m_targetOffset   += m_scrollVelocity * dt;
        m_scrollVelocity *= std::pow(0.92f, dt * 60.0f);  // 帧率无关衰减
        ClampTargetOffset();
    }

    // 弹性边界弹回（当目标超出边界时拉回）
    float maxOff = GetMaxScrollOffset();
    if (m_targetOffset < 0.0f)
    {
        m_targetOffset = 0.0f;
        m_scrollVelocity = 0.0f;
    }
    else if (m_targetOffset > maxOff)
    {
        m_targetOffset = maxOff;
        m_scrollVelocity = 0.0f;
    }

    // 平滑跟随目标（EaseOutCubic 风格的指数衰减）
    float diff = m_targetOffset - m_scrollOffset;
    if (std::abs(diff) > 0.0001f)
    {
        m_scrollOffset += diff * std::min(1.0f, dt * 18.0f);
    }
    else
    {
        m_scrollOffset = m_targetOffset;
    }

    // 更新悬停下标（每帧根据鼠标位置刷新）
    auto [mx, my] = sakura::core::Input::GetMousePosition();
    m_hoveredIndex = GetItemIndexAt(mx, my);
}

// ── Render ────────────────────────────────────────────────────────────────────

void ScrollList::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    // 绘制背景
    sakura::core::Color bgColor = m_bgColor;
    bgColor.a = static_cast<uint8_t>(bgColor.a * m_opacity);
    renderer.DrawFilledRect(m_bounds, bgColor);

    // 边框
    sakura::core::Color borderColor = { 80, 80, 120, static_cast<uint8_t>(120 * m_opacity) };
    renderer.DrawRectOutline(m_bounds, borderColor, 0.001f);

    if (m_items.empty()) return;

    // 逐项渲染（只渲染可见范围内的条目）
    int firstVisible = static_cast<int>(m_scrollOffset / m_normItemHeight);
    firstVisible = std::max(0, firstVisible - 1);

    float listHeight = m_bounds.height;

    for (int i = firstVisible; i < static_cast<int>(m_items.size()); ++i)
    {
        // 计算当前条目的归一化 Y（相对屏幕，减去滚动偏移）
        float itemRelY = static_cast<float>(i) * m_normItemHeight - m_scrollOffset;
        float itemAbsY = m_bounds.y + itemRelY;

        // 超出下边界则停止
        if (itemRelY > listHeight) break;

        // 完全在上边界之外则跳过
        if (itemAbsY + m_normItemHeight < m_bounds.y) continue;

        // 条目矩形（在列表内裁剪）
        float clippedY = std::max(itemAbsY, m_bounds.y);
        float clippedBottom = std::min(itemAbsY + m_normItemHeight,
                                        m_bounds.y + m_bounds.height);
        float clippedH = clippedBottom - clippedY;
        if (clippedH <= 0.0f) continue;

        sakura::core::NormRect itemRect = {
            m_bounds.x, clippedY, m_bounds.width, clippedH
        };

        // 选择条目颜色
        sakura::core::Color itemColor;
        if (i == m_selectedIndex)
            itemColor = m_selectedColor;
        else if (i == m_hoveredIndex)
            itemColor = m_hoverColor;
        else
            itemColor = m_normalColor;

        itemColor.a = static_cast<uint8_t>(itemColor.a * m_opacity);
        renderer.DrawFilledRect(itemRect, itemColor);

        // 分隔线
        if (i > 0)
        {
            sakura::core::Color divColor = { 60, 60, 90, static_cast<uint8_t>(100 * m_opacity) };
            renderer.DrawLine(m_bounds.x,
                              clippedY,
                              m_bounds.x + m_bounds.width,
                              clippedY,
                              divColor, 0.0005f);
        }

        // 文字（仅当条目未被裁剪至太小时才绘制）
        if (clippedH > m_normFontSize * 0.5f)
        {
            float textX = m_bounds.x + 0.012f;
            float textY = itemAbsY + (m_normItemHeight - m_normFontSize) * 0.5f;

            // 若文字 Y 超出列表顶部则跳过文字绘制
            if (textY + m_normFontSize < m_bounds.y) continue;
            if (textY > m_bounds.y + m_bounds.height) break;

            sakura::core::Color textColor = m_textColor;
            if (i == m_selectedIndex)
                textColor = { 255, 220, 255, static_cast<uint8_t>(255 * m_opacity) };
            else
                textColor.a = static_cast<uint8_t>(textColor.a * m_opacity);

            renderer.DrawText(m_fontHandle, m_items[i],
                              textX, textY, m_normFontSize,
                              textColor,
                              sakura::core::TextAlign::Left);
        }
    }

    // 滚动条（仅在有溢出内容时显示）
    float maxOff = GetMaxScrollOffset();
    if (maxOff > 0.0f)
    {
        float scrollbarW = 0.004f;
        float scrollbarX = m_bounds.x + m_bounds.width - scrollbarW - 0.002f;

        // 轨道
        sakura::core::NormRect trackRect = {
            scrollbarX, m_bounds.y, scrollbarW, m_bounds.height
        };
        sakura::core::Color trackColor = { 30, 30, 50, static_cast<uint8_t>(150 * m_opacity) };
        renderer.DrawFilledRect(trackRect, trackColor);

        // 滑块
        float totalHeight = static_cast<float>(m_items.size()) * m_normItemHeight;
        float thumbH      = std::max(0.03f, m_bounds.height / totalHeight * m_bounds.height);
        float thumbY      = m_bounds.y + (m_scrollOffset / maxOff)
                          * (m_bounds.height - thumbH);

        sakura::core::NormRect thumbRect = {
            scrollbarX, thumbY, scrollbarW, thumbH
        };
        sakura::core::Color thumbColor = { 140, 110, 200,
            static_cast<uint8_t>(180 * m_opacity) };
        renderer.DrawRoundedRect(thumbRect, scrollbarW * 0.5f, thumbColor, true);
    }
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool ScrollList::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    auto [mx, my] = sakura::core::Input::GetMousePosition();

    switch (event.type)
    {
    case SDL_EVENT_MOUSE_WHEEL:
    {
        if (!HitTest(mx, my)) break;

        // 每格滚动 3 个条目高度，负号：向下滚动 wheel.y 为负
        float scrollDelta = -event.wheel.y * m_normItemHeight * 3.0f;
        m_targetOffset   += scrollDelta;
        m_scrollVelocity  = scrollDelta / 0.016f * 0.15f;  // 赋予初始惯性
        ClampTargetOffset();
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        if (event.button.button != SDL_BUTTON_LEFT) break;
        if (!HitTest(mx, my)) break;

        int idx = GetItemIndexAt(mx, my);
        if (idx < 0) break;

        // 双击检测
        if (idx == m_lastClickIndex && m_lastClickTimer > 0.0f)
        {
            m_lastClickTimer = 0.0f;
            m_lastClickIndex = -1;
            if (m_onDoubleClick) m_onDoubleClick(idx);
        }
        else
        {
            m_lastClickIndex = idx;
            m_lastClickTimer = DOUBLE_CLICK_TIME;
        }

        // 选中
        if (m_selectedIndex != idx)
        {
            m_selectedIndex = idx;
            if (m_onSelectionChanged) m_onSelectionChanged(idx);
        }
        return true;
    }

    default:
        break;
    }

    return false;
}

} // namespace sakura::ui
