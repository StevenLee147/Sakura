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
    static void DrawSakuraLandscape(sakura::core::Renderer& renderer, float time, float opacity = 1.0f);
    static void DrawTextFit(sakura::core::Renderer& renderer, sakura::core::FontHandle font,
        std::string_view text, float x, float y, float size, float width, sakura::core::Color color);
    static void DrawPlayfieldBackground(sakura::core::Renderer& renderer);
    static void DrawPanel(sakura::core::Renderer& renderer,
                          sakura::core::NormRect rect,
                          bool modal = false,
                          bool accent = false);
    static void DrawGlassCard(sakura::core::Renderer& renderer,
                              sakura::core::NormRect rect,
                              sakura::core::Color topTint,
                              sakura::core::Color bottomTint,
                              sakura::core::Color topBorder,
                              sakura::core::Color bottomBorder,
                              float cornerRadius = 0.014f,
                              float shadowAlpha = 0.45f);
    static void DrawBadgePill(sakura::core::Renderer& renderer,
                              sakura::core::NormRect rect,
                              std::string_view text,
                              sakura::core::FontHandle font,
                              float fontSize,
                              sakura::core::Color fill,
                              sakura::core::Color border,
                              sakura::core::Color textColor);
    static void DrawGildedDivider(sakura::core::Renderer& renderer,
                                  float x1, float x2, float y,
                                  float alpha = 0.75f);
    static void DrawScrim(sakura::core::Renderer& renderer, float opacity = 0.58f);
    static sakura::core::TextureHandle GetSakuraCrestTexture();
    static sakura::core::TextureHandle GetRankEmblemTexture(std::string_view grade);
    static sakura::core::TextureHandle GetDefaultCoverTexture();
    static sakura::core::TextureHandle GetMoonlitBackdropTexture();
    static ButtonColors ButtonColorsFor(ButtonVariant variant);
    static void ApplyButton(Button* button, ButtonVariant variant);
    static void ApplyScrollList(ScrollList* list);
};

} // namespace sakura::ui
