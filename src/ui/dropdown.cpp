// dropdown.cpp — 下拉框 UI 组件实现

#include "dropdown.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

Dropdown::Dropdown(sakura::core::NormRect bounds,
                   const std::vector<std::string>& options,
                   int selectedIndex,
                   sakura::core::FontHandle fontHandle,
                   float normFontSize)
    : UIBase(bounds)
    , m_options(options)
    , m_selectedIndex(std::max(0, std::min(selectedIndex,
                                            static_cast<int>(options.size()) - 1)))
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
{
}

// ── 属性 ──────────────────────────────────────────────────────────────────────

const std::string& Dropdown::GetSelectedOption() const
{
    static std::string empty;
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_options.size()))
        return empty;
    return m_options[m_selectedIndex];
}

void Dropdown::SetOptions(const std::vector<std::string>& options)
{
    m_options   = options;
    m_selectedIndex = std::min(m_selectedIndex,
                               static_cast<int>(options.size()) - 1);
    if (m_selectedIndex < 0) m_selectedIndex = 0;
}

void Dropdown::SetSelectedIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(m_options.size()))
    {
        m_selectedIndex = index;
    }
}

// ── 辅助 ──────────────────────────────────────────────────────────────────────

sakura::core::NormRect Dropdown::GetDropdownRect() const
{
    float itemH = GetItemHeight();
    float totalH = itemH * static_cast<float>(m_options.size());
    return { m_bounds.x, m_bounds.y + m_bounds.height, m_bounds.width, totalH };
}

// ── Update ────────────────────────────────────────────────────────────────────

void Dropdown::Update(float dt)
{
    if (!m_isVisible) return;

    float target = m_isOpen ? 1.0f : 0.0f;
    float delta  = (target - m_openAnim) * (dt / OPEN_DURATION);
    m_openAnim   = std::clamp(m_openAnim + delta, 0.0f, 1.0f);
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool Dropdown::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    auto [mx, my] = sakura::core::Input::GetMousePosition();

    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        if (m_isOpen)
        {
            // 检测悬停的下拉项
            auto dropRect = GetDropdownRect();
            m_hoveredIndex = -1;
            for (int i = 0; i < static_cast<int>(m_options.size()); ++i)
            {
                float itemY = dropRect.y + GetItemHeight() * static_cast<float>(i);
                sakura::core::NormRect itemRect {
                    dropRect.x, itemY, dropRect.width, GetItemHeight()
                };
                if (mx >= itemRect.x && mx <= itemRect.x + itemRect.width &&
                    my >= itemRect.y && my <= itemRect.y + itemRect.height)
                {
                    m_hoveredIndex = i;
                    break;
                }
            }
        }
        return false;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 1)
    {
        // 点击主按钮：切换开关
        if (HitTest(mx, my))
        {
            m_isOpen = !m_isOpen;
            if (!m_isOpen) m_hoveredIndex = -1;
            return true;
        }

        // 点击下拉项
        if (m_isOpen)
        {
            auto dropRect = GetDropdownRect();
            for (int i = 0; i < static_cast<int>(m_options.size()); ++i)
            {
                float itemY = dropRect.y + GetItemHeight() * static_cast<float>(i);
                sakura::core::NormRect itemRect {
                    dropRect.x, itemY, dropRect.width, GetItemHeight()
                };
                if (mx >= itemRect.x && mx <= itemRect.x + itemRect.width &&
                    my >= itemRect.y && my <= itemRect.y + itemRect.height)
                {
                    int oldIdx = m_selectedIndex;
                    m_selectedIndex = i;
                    m_isOpen        = false;
                    m_hoveredIndex  = -1;
                    if (i != oldIdx && m_onChange)
                        m_onChange(i, m_options[i]);
                    return true;
                }
            }

            // 点击其他区域关闭
            m_isOpen       = false;
            m_hoveredIndex = -1;
            return false;
        }
    }

    return false;
}

// ── Render ────────────────────────────────────────────────────────────────────

void Dropdown::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    float cornerR = m_bounds.height * 0.3f;

    // 主按钮背景
    renderer.DrawRoundedRect(m_bounds, cornerR, m_bgColor, true);
    renderer.DrawRoundedRect(m_bounds, cornerR, { 100, 80, 140, 180 }, false);

    // 当前选项文字
    if (m_fontHandle != 0)
    {
        const std::string& selText = GetSelectedOption();
        renderer.DrawText(m_fontHandle, selText,
                          m_bounds.x + m_bounds.width * 0.05f,
                          m_bounds.y + m_bounds.height * 0.5f,
                          m_normFontSize,
                          m_textColor,
                          sakura::core::TextAlign::Left);
    }

    // 箭头（简单三角形用线段模拟）
    {
        float ax = m_bounds.x + m_bounds.width * 0.90f;
        float ay = m_bounds.y + m_bounds.height * 0.5f;
        float as = m_bounds.height * 0.15f;
        float dir = m_isOpen ? -1.0f : 1.0f;   // 上/下
        renderer.DrawLine(ax - as, ay - dir * as * 0.5f,
                          ax,       ay + dir * as * 0.5f,
                          m_arrowColor, 0.002f);
        renderer.DrawLine(ax + as, ay - dir * as * 0.5f,
                          ax,       ay + dir * as * 0.5f,
                          m_arrowColor, 0.002f);
    }

    // 下拉列表（根据动画进度裁切高度，EaseOutQuad）
    if (m_openAnim > 0.001f)
    {
        float easedT  = sakura::utils::EaseOutQuad(m_openAnim);
        auto dropRect = GetDropdownRect();
        float visibleH = dropRect.height * easedT;
        float itemH    = GetItemHeight();

        // 背景
        renderer.DrawRoundedRect(
            { dropRect.x, dropRect.y, dropRect.width, visibleH },
            cornerR, m_bgColor, true);
        renderer.DrawRoundedRect(
            { dropRect.x, dropRect.y, dropRect.width, visibleH },
            cornerR, { 100, 80, 140, 180 }, false);

        // 渲染可见的每一项
        int visCount = static_cast<int>(std::ceil(
            visibleH / (itemH > 0.001f ? itemH : 0.001f)));
        visCount = std::min(visCount, static_cast<int>(m_options.size()));

        for (int i = 0; i < visCount; ++i)
        {
            float itemY = dropRect.y + itemH * static_cast<float>(i);
            sakura::core::NormRect itemRect { dropRect.x, itemY, dropRect.width, itemH };

            // 背景高亮
            if (i == m_selectedIndex)
            {
                renderer.DrawFilledRect(itemRect, m_selectedColor);
            }
            else if (i == m_hoveredIndex)
            {
                renderer.DrawFilledRect(itemRect, m_hoverColor);
            }

            // 文字
            if (m_fontHandle != 0)
            {
                renderer.DrawText(m_fontHandle, m_options[i],
                                  itemRect.x + itemRect.width * 0.05f,
                                  itemY + itemH * 0.5f,
                                  m_normFontSize,
                                  m_textColor,
                                  sakura::core::TextAlign::Left);
            }
        }
    }
}

} // namespace sakura::ui
