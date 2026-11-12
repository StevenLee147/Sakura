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

    const float pillR = std::min(m_bounds.height * 0.28f, 0.012f);
    renderer.DrawRoundedGradientRect(
        m_bounds, pillR,
        { 22, 28, 48, m_bgColor.a }, { 22, 28, 48, m_bgColor.a },
        m_bgColor, m_bgColor, 12, true);
    renderer.DrawRoundedRectOutlineGradient(
        m_bounds, pillR,
        { 250, 216, 157, 92 }, { 144, 154, 194, 46 }, 0.0013f, 12);

    int n = static_cast<int>(m_tabs.size());
    if (n == 0) return;

    // 滑动磨砂胶囊高亮底座
    if (m_orientation == Orientation::Horizontal)
    {
        const float tabW = m_bounds.width / static_cast<float>(n);
        const sakura::core::NormRect activePill{
            m_bounds.x + m_indicatorPos * tabW + 0.002f,
            m_bounds.y + 0.0025f,
            std::max(0.0f, tabW - 0.004f),
            std::max(0.0f, m_bounds.height - 0.005f)
        };
        renderer.DrawRoundedGradientRect(
            activePill, pillR * 0.85f,
            { 82, 58, 108, 226 }, { 64, 50, 96, 226 },
            m_activeColor, m_activeColor, 10, true);
        renderer.DrawRoundedRectOutlineGradient(
            activePill, pillR * 0.85f,
            { 250, 216, 157, 175 }, { 246, 172, 200, 85 }, 0.0012f, 10);
    }
    else
    {
        const float tabH = m_bounds.height / static_cast<float>(n);
        const sakura::core::NormRect activePill{
            m_bounds.x + 0.002f,
            m_bounds.y + m_indicatorPos * tabH + 0.002f,
            std::max(0.0f, m_bounds.width - 0.004f),
            std::max(0.0f, tabH - 0.004f)
        };
        renderer.DrawRoundedGradientRect(
            activePill, pillR * 0.85f,
            { 82, 58, 108, 226 }, { 64, 50, 96, 226 },
            m_activeColor, m_activeColor, 10, true);
    }

    // 各 tab 悬停与居中文字
    for (int i = 0; i < n; ++i)
    {
        auto r = GetTabRect(i);
        if (i != m_selectedIndex && i == m_hoveredIndex)
        {
            renderer.DrawRoundedRect(
                { r.x + 0.002f, r.y + 0.0025f, std::max(0.0f, r.width - 0.004f), std::max(0.0f, r.height - 0.005f) },
                pillR * 0.8f, m_hoverColor, true);
        }

        if (m_fontHandle != 0)
        {
            auto textColor = (i == m_selectedIndex) ? m_activeTextColor : m_textColor;
            const float textH = renderer.MeasureText(m_fontHandle, m_tabs[i], m_normFontSize).height;
            renderer.DrawText(m_fontHandle, m_tabs[i],
                              r.x + r.width * 0.5f,
                              r.y + (r.height - textH) * 0.5f,
                              m_normFontSize,
                              textColor,
                              sakura::core::TextAlign::Center);
        }
    }

    // 底部/侧边鎏金流光指示线
    constexpr float THICK = 0.0032f;
    if (m_orientation == Orientation::Horizontal)
    {
        const float tabW = m_bounds.width / static_cast<float>(n);
        const float indX = m_bounds.x + m_indicatorPos * tabW + tabW * 0.16f;
        const float indY = m_bounds.y + m_bounds.height - THICK - 0.001f;
        renderer.DrawRoundedGradientRect(
            { indX, indY, tabW * 0.68f, THICK }, THICK * 0.5f,
            { 246, 172, 200, 245 }, m_indicatorColor,
            { 246, 172, 200, 245 }, m_indicatorColor, 6, true);
    }
    else
    {
        const float tabH = m_bounds.height / static_cast<float>(n);
        const float indX = m_bounds.x + m_bounds.width - THICK - 0.001f;
        const float indY = m_bounds.y + m_indicatorPos * tabH + tabH * 0.18f;
        renderer.DrawRoundedRect({ indX, indY, THICK, tabH * 0.64f }, THICK * 0.5f, m_indicatorColor, true);
    }
}

} // namespace sakura::ui
