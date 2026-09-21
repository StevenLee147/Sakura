// theme.cpp — 全局 UI 主题系统实现

#include "theme.h"
#include "utils/logger.h"

namespace sakura::core
{

namespace
{
ThemeButtonStyle MakeButtonStyle(Color normal,
                                 Color hover,
                                 Color pressed,
                                 Color text,
                                 Color border,
                                 float cornerRadius = 0.010f)
{
    ThemeButtonStyle style;
    style.normal       = normal;
    style.hover        = hover;
    style.pressed      = pressed;
    style.disabled     = { 100, 100, 100, 45 };
    style.text         = text;
    style.border       = border;
    style.cornerRadius = cornerRadius;
    return style;
}
}

// ── 单例 ──────────────────────────────────────────────────────────────────────

Theme& Theme::GetInstance()
{
    static Theme s_instance;
    return s_instance;
}

// ── 初始化 ────────────────────────────────────────────────────────────────────

void Theme::Initialize()
{
    SetPreset(ThemePreset::Sakura);
    LOG_INFO("[Theme] 已初始化唯一主视觉: {}", PresetName());
}

// ── 预设名称 ──────────────────────────────────────────────────────────────────

const char* Theme::PresetName() const
{
    switch (m_preset)
    {
        case ThemePreset::Sakura:   return "sakura";
    }
    return "sakura";
}

// ── 切换预设 ──────────────────────────────────────────────────────────────────

void Theme::SetPresetByName(std::string_view name)
{
    (void)name;
    SetPreset(ThemePreset::Sakura);
}

void Theme::SetPreset(ThemePreset preset)
{
    (void)preset;
    m_preset = ThemePreset::Sakura;
    ApplySakura();
    LOG_INFO("[Theme] 已切换为: {}", PresetName());
}

// ── Sakura 预设 ───────────────────────────────────────────────────────────────
// 深蓝背景 + 粉色主调 — 樱花夜晚风

void Theme::ApplySakura()
{
    m_colors.primary       = { 255, 150, 180, 255 };  // 樱花粉
    m_colors.secondary     = { 180, 130, 210, 255 };  // 淡紫
    m_colors.accent        = { 255, 210, 100, 255 };  // 暖金
    m_colors.bg            = {  12,  16,  29, 255 };  // 深夜蓝
    m_colors.surface       = {  23,  29,  45, 230 };  // 深蓝灰面板
    m_colors.surfaceBorder = { 100,  80, 160, 200 };  // 紫边框
    m_colors.text          = { 240, 230, 255, 255 };  // 淡白紫
    m_colors.textDim       = { 162, 166, 188, 255 };  // 暗紫灰
    m_colors.noteColor     = { 255, 160, 200, 255 };  // 粉红
    m_colors.holdColor     = { 200, 130, 255, 255 };  // 紫
    m_colors.circleColor   = { 255, 200, 230, 220 };  // 淡粉
    m_colors.glowColor     = { 255, 150, 200, 180 };  // 粉光
    m_colors.judgeLine     = { 255, 255, 255, 220 };  // 白

    // 判定颜色
    m_colors.judgeColors[0] = { 255, 220,  80, 255 };  // Perfect  金
    m_colors.judgeColors[1] = { 100, 220, 255, 255 };  // Great    青
    m_colors.judgeColors[2] = {  80, 200,  80, 255 };  // Good     绿
    m_colors.judgeColors[3] = { 220, 120,  40, 255 };  // Bad      橙
    m_colors.judgeColors[4] = { 220,  60,  60, 255 };  // Miss     红

    // 轨道
    m_colors.laneColors[0] = {  60,  50,  90, 180 };
    m_colors.laneColors[1] = {  50,  40,  80, 180 };
    m_colors.laneColors[2] = {  60,  50,  90, 180 };
    m_colors.laneColors[3] = {  50,  40,  80, 180 };

    // 评级
    m_colors.gradeColors[0] = { 218, 165,  32, 255 };  // SS 金
    m_colors.gradeColors[1] = { 255, 200,   0, 255 };  // S  亮金
    m_colors.gradeColors[2] = {  60, 200,  60, 255 };  // A  绿
    m_colors.gradeColors[3] = {  80, 160, 220, 255 };  // B  蓝
    m_colors.gradeColors[4] = { 160, 160, 160, 255 };  // C  灰
    m_colors.gradeColors[5] = { 220,  60,  60, 255 };  // D  红

    m_settings.particlesEnabled = true;
    m_settings.glowEnabled      = true;
    m_settings.shakeEnabled     = true;
    m_settings.vignetteEnabled  = true;

    m_components.primaryButton = MakeButtonStyle(
        { 225, 163, 181, 255 }, { 248, 190, 204, 255 }, { 185, 126, 149, 255 },
        { 30, 24, 38, 255 }, { 255, 220, 226, 150 });
    m_components.secondaryButton = MakeButtonStyle(
        { 30, 36, 54, 220 }, { 53, 58, 78, 240 }, { 20, 24, 40, 240 },
        { 227, 226, 237, 255 }, { 170, 172, 195, 55 });
    m_components.accentButton = MakeButtonStyle(
        { 95, 70, 30, 220 }, { 135, 100, 45, 235 }, { 70, 50, 20, 240 },
        Color::White, { 255, 230, 150, 160 });
    m_components.dangerButton = MakeButtonStyle(
        { 48, 32, 47, 180 }, { 89, 45, 63, 235 }, { 35, 25, 40, 240 },
        { 231, 169, 185, 255 }, { 200, 134, 155, 65 });
    m_components.panel = {
        { 19, 24, 39, 235 }, { 147, 154, 184, 45 }, { 0, 0, 0, 40 },
        { 255, 150, 200, 170 }, 0.012f, 0.0015f
    };
    m_components.modal = {
        { 14, 12, 28, 245 }, { 180, 130, 210, 210 }, { 0, 0, 0, 140 },
        { 255, 210, 100, 190 }, 0.014f, 0.0018f
    };
    m_components.progress = {
        { 20, 15, 35, 190 }, { 180, 110, 230, 220 }, { 100, 80, 150, 170 },
        { 240, 230, 255, 230 }, 0.008f
    };
}

} // namespace sakura::core
