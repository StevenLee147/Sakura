#pragma once

// label.h — 文字标签组件

#include "ui_base.h"
#include "core/resource_manager.h"
#include "core/renderer.h"
#include <string>

namespace sakura::ui
{

// Label — 文字显示组件
class Label : public UIBase
{
public:
    Label(sakura::core::NormRect bounds,
          const std::string& text,
          sakura::core::FontHandle fontHandle,
          float normFontSize,
          sakura::core::Color color,
          sakura::core::TextAlign align = sakura::core::TextAlign::Left);

    // ── 属性 ──────────────────────────────────────────────────────────────────

    void SetText(const std::string& text)    { m_text = text; }
    const std::string& GetText()             const { return m_text; }

    void SetFontSize(float normFontSize)     { m_normFontSize = normFontSize; }
    void SetColor(sakura::core::Color color) { m_color = color; }
    void SetAlign(sakura::core::TextAlign a) { m_align = a; }

    // 文字阴影（偏移为归一化值）
    void SetShadow(bool enabled,
                   sakura::core::Color shadowColor = { 0, 0, 0, 128 },
                   float offsetX = 0.001f, float offsetY = 0.002f);

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt) override;
    void Render(sakura::core::Renderer& renderer) override;

private:
    std::string             m_text;
    sakura::core::FontHandle m_fontHandle;
    float                   m_normFontSize;
    sakura::core::Color     m_color;
    sakura::core::TextAlign m_align;

    // 阴影
    bool                    m_shadowEnabled  = false;
    sakura::core::Color     m_shadowColor    = { 0, 0, 0, 128 };
    float                   m_shadowOffsetX  = 0.001f;
    float                   m_shadowOffsetY  = 0.002f;
};

} // namespace sakura::ui
