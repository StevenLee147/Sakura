#pragma once

// visual_style.h — Sakura 唯一主视觉的页面级绘制与组件样式助手

#include "button.h"
#include "scroll_list.h"
#include "core/renderer.h"

namespace sakura::ui
{

class VisualStyle
{
public:
    static void DrawSceneBackground(sakura::core::Renderer& renderer);
    static void DrawPlayfieldBackground(sakura::core::Renderer& renderer);
    static void DrawPanel(sakura::core::Renderer& renderer,
                          sakura::core::NormRect rect,
                          bool modal = false,
                          bool accent = false);
    static void DrawScrim(sakura::core::Renderer& renderer, float opacity = 0.58f);

    static ButtonColors ButtonColorsFor(ButtonVariant variant);
    static void ApplyButton(Button* button, ButtonVariant variant);
    static void ApplyScrollList(ScrollList* list);
};

} // namespace sakura::ui