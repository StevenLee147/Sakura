// tab_bar.cpp — 标签栏 UI 组件实现

#include "tab_bar.h"
#include "core/input.h"
#include "utils/easing.h"

#include <algorithm>
#include <cmath>

namespace sakura::ui
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

TabBar::TabBar(sakura::core::NormRect bounds,
               const std::vector<std::string>& tabs,
               int selectedIndex,
               sakura::core::FontHandle fontHandle,
               float normFontSize,
               Orientation orientation)
    : UIBase(bounds)
    , m_tabs(tabs)
    , m_selectedIndex(std::clamp(selectedIndex, 0, std::max(0, static_cast<int>(tabs.size()) - 1)))
    , m_orientation(orientation)
    , m_fontHandle(fontHandle)
    , m_normFontSize(normFontSize)
    , m_indicatorPos(static_cast<float>(m_selectedIndex))
    , m_indicatorTarget(static_cast<float>(m_selectedIndex))
{
}

// ── 属性 ──────────────────────────────────────────────────────────────────────

void TabBar::SetSelectedIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    m_selectedIndex  = index;
    m_indicatorTarget = static_cast<float>(index);
}

void TabBar::SetTabs(const std::vector<std::string>& tabs)
{
    m_tabs          = tabs;
    m_selectedIndex = std::min(m_selectedIndex, static_cast<int>(tabs.size()) - 1);
    m_indicatorTarget = static_cast<float>(m_selectedIndex);
}

// ── 辅助 ──────────────────────────────────────────────────────────────────────

sakura::core::NormRect TabBar::GetTabRect(int i) const
{
    if (m_tabs.empty()) return m_bounds;
    int n = static_cast<int>(m_tabs.size());

    if (m_orientation == Orientation::Horizontal)
    {
        float tabW = m_bounds.width  / static_cast<float>(n);
        return { m_bounds.x + tabW * static_cast<float>(i), m_bounds.y,
                 tabW, m_bounds.height };
    }
    else // Vertical
    {
        float tabH = m_bounds.height / static_cast<float>(n);
        return { m_bounds.x, m_bounds.y + tabH * static_cast<float>(i),
                 m_bounds.width, tabH };
    }
}

// ── Update ────────────────────────────────────────────────────────────────────

void TabBar::Update(float dt)
{
    if (!m_isVisible) return;

    float delta = (m_indicatorTarget - m_indicatorPos) * (dt / INDICATOR_DUR);
    m_indicatorPos += delta;
    // 防止精度问题
    if (std::abs(m_indicatorPos - m_indicatorTarget) < 0.001f)
        m_indicatorPos = m_indicatorTarget;
}

// ── HandleEvent ───────────────────────────────────────────────────────────────

bool TabBar::HandleEvent(const SDL_Event& event)
{
    if (!m_isVisible || !m_isEnabled) return false;

    auto [mx, my] = sakura::core::Input::GetMousePosition();

    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        m_hoveredIndex = -1;
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
        {
            auto r = GetTabRect(i);
            if (mx >= r.x && mx <= r.x + r.width &&
                my >= r.y && my <= r.y + r.height)
            {
                m_hoveredIndex = i;
                break;
            }
        }
        return false;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 1)
    {
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
        {
            auto r = GetTabRect(i);
            if (mx >= r.x && mx <= r.x + r.width &&
                my >= r.y && my <= r.y + r.height)
            {
                if (i != m_selectedIndex)
                {
                    m_selectedIndex   = i;
                    m_indicatorTarget = static_cast<float>(i);
                    if (m_onChange) m_onChange(i);
                }
                return true;
            }
        }
    }

    return false;
}

// ── Render ────────────────────────────────────────────────────────────────────

void TabBar::Render(sakura::core::Renderer& renderer)
{
    if (!m_isVisible) return;

    // 背景
    renderer.DrawFilledRect(m_bounds, m_bgColor);

    int n = static_cast<int>(m_tabs.size());
    if (n == 0) return;

    // 各 tab
    for (int i = 0; i < n; ++i)
    {
        auto r = GetTabRect(i);

        if (i == m_selectedIndex)
        {
            renderer.DrawFilledRect(r, m_activeColor);
        }
        else if (i == m_hoveredIndex)
        {
            renderer.DrawFilledRect(r, m_hoverColor);
        }

        // 文字
        if (m_fontHandle != 0)
        {
            auto textColor = (i == m_selectedIndex) ? m_activeTextColor : m_textColor;
            renderer.DrawText(m_fontHandle, m_tabs[i],
                              r.x + r.width * 0.5f,
                              r.y + r.height * 0.5f,
                              m_normFontSize,
                              textColor,
                              sakura::core::TextAlign::Center);
        }
    }

    // 滑动指示条（EaseOutQuad 平滑位置）
    float indicatorNorm = sakura::utils::EaseOutQuad(
        m_indicatorPos / static_cast<float>(n > 1 ? n - 1 : 1));
    (void)indicatorNorm; // 下面直接用 m_indicatorPos

    float THICK = 0.004f;
    if (m_orientation == Orientation::Horizontal)
    {
        float tabW = m_bounds.width / static_cast<float>(n);
        float indX = m_bounds.x + m_indicatorPos * tabW;
        float indY = m_bounds.y + m_bounds.height - THICK;
        renderer.DrawFilledRect({ indX, indY, tabW, THICK }, m_indicatorColor);
    }
    else
    {
        float tabH = m_bounds.height / static_cast<float>(n);
        float indX = m_bounds.x + m_bounds.width - THICK;
        float indY = m_bounds.y + m_indicatorPos * tabH;
        renderer.DrawFilledRect({ indX, indY, THICK, tabH }, m_indicatorColor);
    }
}

} // namespace sakura::ui
