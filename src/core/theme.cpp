// theme.cpp — 全局 UI 主题系统实现

#include "theme.h"
#include "config.h"
#include "utils/logger.h"

namespace sakura::core
{

// ── 单例 ──────────────────────────────────────────────────────────────────────

Theme& Theme::GetInstance()
{
    static Theme s_instance;
    return s_instance;
}

// ── 初始化 ────────────────────────────────────────────────────────────────────

void Theme::Initialize()
{
    // 读取已保存的主题名
    auto& cfg  = Config::GetInstance();
    std::string name = cfg.Get("theme_name", std::string("sakura"));
    SetPresetByName(name);
    LOG_INFO("[Theme] 已初始化，当前主题: {}", name);
}

// ── 预设名称 ──────────────────────────────────────────────────────────────────

const char* Theme::PresetName() const
{
    switch (m_preset)
    {
        case ThemePreset::Sakura:   return "sakura";
        case ThemePreset::Midnight: return "midnight";
        case ThemePreset::Daylight: return "daylight";
    }
    return "sakura";
}

// ── 切换预设 ──────────────────────────────────────────────────────────────────

void Theme::SetPresetByName(std::string_view name)
{
    if (name == "midnight")     SetPreset(ThemePreset::Midnight);
    else if (name == "daylight") SetPreset(ThemePreset::Daylight);
    else                         SetPreset(ThemePreset::Sakura);
}

void Theme::SetPreset(ThemePreset preset)
{
    m_preset = preset;
    switch (preset)
    {
        case ThemePreset::Sakura:   ApplySakura();   break;
        case ThemePreset::Midnight: ApplyMidnight(); break;
        case ThemePreset::Daylight: ApplyDaylight(); break;
    }

    // 写回 Config
    Config::GetInstance().Set("theme_name", std::string(PresetName()));
    Config::GetInstance().Save();
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
}

// ── Midnight 预设 ─────────────────────────────────────────────────────────────
// 纯黑背景 + 紫色霓虹 — 赛博夜晚风

void Theme::ApplyMidnight()
{
    m_colors.primary       = { 180,  80, 255, 255 };  // 霓虹紫
    m_colors.secondary     = {  80, 200, 255, 255 };  // 霓虹青
    m_colors.accent        = { 255,  60, 180, 255 };  // 霓虹粉红
    m_colors.bg            = {   5,   5,  10, 255 };  // 近黑
    m_colors.surface       = {  15,  10,  25, 240 };  // 极深紫黑
    m_colors.surfaceBorder = { 130,  60, 200, 220 };  // 紫霓虹框
    m_colors.text          = { 230, 220, 255, 255 };  // 淡紫白
    m_colors.textDim       = { 120, 100, 160, 200 };  // 暗紫
    m_colors.noteColor     = { 180, 100, 255, 255 };  // 紫
    m_colors.holdColor     = {  80, 200, 255, 255 };  // 青
    m_colors.circleColor   = { 200, 120, 255, 220 };  // 紫圆
    m_colors.glowColor     = { 160,  80, 255, 180 };  // 紫光
    m_colors.judgeLine     = { 200, 150, 255, 230 };  // 紫白

    m_colors.judgeColors[0] = { 240, 200, 255, 255 };  // Perfect  淡紫白
    m_colors.judgeColors[1] = {  80, 200, 255, 255 };  // Great    青
    m_colors.judgeColors[2] = { 140, 255, 140, 255 };  // Good     亮绿
    m_colors.judgeColors[3] = { 255, 140,  40, 255 };  // Bad      橙
    m_colors.judgeColors[4] = { 255,  50,  80, 255 };  // Miss     霓虹红

    m_colors.laneColors[0] = {  20,  10,  40, 200 };
    m_colors.laneColors[1] = {  15,   8,  30, 200 };
    m_colors.laneColors[2] = {  20,  10,  40, 200 };
    m_colors.laneColors[3] = {  15,   8,  30, 200 };

    m_colors.gradeColors[0] = { 218, 165,  32, 255 };
    m_colors.gradeColors[1] = { 255, 200,   0, 255 };
    m_colors.gradeColors[2] = {  60, 200,  60, 255 };
    m_colors.gradeColors[3] = {  80, 160, 220, 255 };
    m_colors.gradeColors[4] = { 160, 160, 160, 255 };
    m_colors.gradeColors[5] = { 220,  60,  60, 255 };

    m_settings.particlesEnabled = true;
    m_settings.glowEnabled      = true;
    m_settings.shakeEnabled     = true;
    m_settings.vignetteEnabled  = true;
}

// ── Daylight 预设 ─────────────────────────────────────────────────────────────
// 浅灰背景 + 柔和蓝 — 日间清新风

void Theme::ApplyDaylight()
{
    m_colors.primary       = {  70, 150, 220, 255 };  // 柔蓝
    m_colors.secondary     = { 100, 180, 120, 255 };  // 柔绿
    m_colors.accent        = { 240, 140,  60, 255 };  // 暖橙
    m_colors.bg            = { 235, 238, 245, 255 };  // 浅灰白
    m_colors.surface       = { 255, 255, 255, 245 };  // 白面板
    m_colors.surfaceBorder = { 180, 190, 210, 220 };  // 浅蓝灰边框
    m_colors.text          = {  30,  40,  60, 255 };  // 深蓝黑
    m_colors.textDim       = { 100, 110, 140, 200 };  // 中灰蓝
    m_colors.noteColor     = {  70, 150, 220, 255 };  // 蓝
    m_colors.holdColor     = { 100, 180, 120, 255 };  // 绿
    m_colors.circleColor   = { 100, 170, 230, 220 };  // 淡蓝
    m_colors.glowColor     = {  80, 160, 240, 160 };  // 蓝光
    m_colors.judgeLine     = {  50,  80, 160, 220 };  // 深蓝判定线

    m_colors.judgeColors[0] = { 200, 150,  30, 255 };  // Perfect  暖金
    m_colors.judgeColors[1] = {  50, 160, 220, 255 };  // Great    蓝
    m_colors.judgeColors[2] = {  60, 180,  80, 255 };  // Good     绿
    m_colors.judgeColors[3] = { 200, 100,  30, 255 };  // Bad      橙
    m_colors.judgeColors[4] = { 200,  50,  50, 255 };  // Miss     红

    m_colors.laneColors[0] = { 200, 210, 230, 120 };
    m_colors.laneColors[1] = { 190, 200, 225, 120 };
    m_colors.laneColors[2] = { 200, 210, 230, 120 };
    m_colors.laneColors[3] = { 190, 200, 225, 120 };

    m_colors.gradeColors[0] = { 180, 130,  20, 255 };
    m_colors.gradeColors[1] = { 200, 160,   0, 255 };
    m_colors.gradeColors[2] = {  40, 160,  40, 255 };
    m_colors.gradeColors[3] = {  60, 120, 190, 255 };
    m_colors.gradeColors[4] = { 130, 130, 130, 255 };
    m_colors.gradeColors[5] = { 180,  40,  40, 255 };

    m_settings.particlesEnabled = true;
    m_settings.glowEnabled      = false;  // 日间风格关闭辉光
    m_settings.shakeEnabled     = false;  // 柔和体验关闭震动
    m_settings.vignetteEnabled  = false;  // 无晕影
}

} // namespace sakura::core
