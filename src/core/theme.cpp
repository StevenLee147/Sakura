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
    m_colors.bg            = {  10,   8,  22, 255 };  // 深夜蓝
    m_colors.surface       = {  25,  20,  50, 230 };  // 深蓝灰面板
    m_colors.surfaceBorder = { 100,  80, 160, 200 };  // 紫边框
    m_colors.text          = { 240, 230, 255, 255 };  // 淡白紫
    m_colors.textDim       = { 140, 120, 170, 200 };  // 暗紫灰
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
        { 100, 55, 155, 220 }, { 130, 80, 190, 235 }, { 70, 35, 120, 240 },
        Color::White, { 255, 210, 245, 150 });
    m_components.secondaryButton = MakeButtonStyle(
        { 35, 30, 65, 210 }, { 60, 50, 105, 230 }, { 20, 15, 45, 240 },
        Color::White, { 220, 200, 255, 120 });
    m_components.accentButton = MakeButtonStyle(
        { 95, 70, 30, 220 }, { 135, 100, 45, 235 }, { 70, 50, 20, 240 },
        Color::White, { 255, 230, 150, 160 });
    m_components.dangerButton = MakeButtonStyle(
        { 110, 40, 55, 220 }, { 150, 55, 75, 235 }, { 80, 25, 40, 240 },
        Color::White, { 255, 170, 180, 150 });
    m_components.panel = {
        { 15, 12, 30, 220 }, { 100, 80, 150, 150 }, { 0, 0, 0, 95 },
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
