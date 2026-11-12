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

// ── Sakura 预设：琉璃月影 · 墨樱浮金 (Luminous Glass & Moonlit Ink-Sakura) ──
void Theme::ApplySakura()
{
    m_colors.primary         = { 246, 172, 200, 255 };  // #F6ACC8 琉璃樱粉
    m_colors.secondary       = { 168, 158, 228, 255 };  // 月雾鸢尾紫
    m_colors.accent          = { 250, 216, 157, 255 };  // #FAD89D 鎏金香槟

    m_colors.bg              = {  10,  14,  26, 255 };  // #0A0E1A 月夜墨靛
    m_colors.surface         = {  18,  24,  42, 212 };  // 磨砂琉璃面板主色
    m_colors.surfaceElevated = {  28,  36,  62, 228 };  // 悬浮高阶磨砂玻璃
    m_colors.surfaceBorder   = { 196, 178, 214, 110 };  // 柔光月影细边
    m_colors.gildedHairline  = { 250, 216, 157, 165 };  // 鎏金边缘高光
    m_colors.specularTop     = { 255, 244, 248,  46 };  // 顶部镜面光泽

    m_colors.text            = { 247, 244, 252, 255 };  // 珍珠雪白主文字
    m_colors.textDim         = { 174, 180, 204, 255 };  // 月雾银灰次级文字
    m_colors.textMuted       = { 118, 126, 154, 220 };  // 弱化说明文字

    m_colors.noteColor       = { 252, 176, 208, 255 };  // 晶莹樱瓣 Tap
    m_colors.holdColor       = { 188, 162, 246, 255 };  // 虹彩紫晶 Hold
    m_colors.circleColor     = { 255, 206, 226, 238 };  // 珍珠樱环 Circle
    m_colors.glowColor       = { 248, 168, 206, 195 };  // 樱色泛光
    m_colors.judgeLine       = { 252, 234, 206, 238 };  // 鎏金棱镜判定线

    // 判定颜色：香槟金 / 冰晶青 / 翡翠青 / 琥珀橙 / 绯红
    m_colors.judgeColors[0] = { 255, 226, 138, 255 };  // Perfect  鎏金
    m_colors.judgeColors[1] = { 130, 224, 250, 255 };  // Great    冰青
    m_colors.judgeColors[2] = { 126, 222, 168, 255 };  // Good     翠玉
    m_colors.judgeColors[3] = { 242, 158,  92, 255 };  // Bad      琥珀
    m_colors.judgeColors[4] = { 236,  96, 116, 255 };  // Miss     落绯

    // 轨道（D / F / J / K 交替月影与樱紫底色）
    m_colors.laneColors[0] = {  34,  42,  74, 178 };
    m_colors.laneColors[1] = {  46,  38,  76, 178 };
    m_colors.laneColors[2] = {  46,  38,  76, 178 };
    m_colors.laneColors[3] = {  34,  42,  74, 178 };

    // 评级颜色（与 rank_*.png 徽章视觉对齐）
    m_colors.gradeColors[0] = { 255, 226, 156, 255 };  // SS 虹彩鎏金
    m_colors.gradeColors[1] = { 252, 204, 126, 255 };  // S  香槟暖金
    m_colors.gradeColors[2] = { 246, 164, 204, 255 };  // A  琉璃樱粉
    m_colors.gradeColors[3] = { 132, 208, 242, 255 };  // B  月泉冰蓝
    m_colors.gradeColors[4] = { 152, 214, 186, 255 };  // C  竹青玉色
    m_colors.gradeColors[5] = { 174, 166, 192, 255 };  // D  雾灰银紫

    m_settings.transitionDuration = 0.42f;
    m_settings.particlesEnabled   = true;
    m_settings.glowEnabled        = true;
    m_settings.shakeEnabled       = true;
    m_settings.vignetteEnabled    = true;

    m_components.primaryButton = MakeButtonStyle(
        { 236, 164, 192, 242 }, { 252, 192, 214, 255 }, { 198, 128, 158, 248 },
        {  22,  18,  34, 255 }, { 255, 234, 212, 195 }, 0.012f);

    m_components.secondaryButton = MakeButtonStyle(
        {  24,  32,  54, 208 }, {  42,  52,  84, 236 }, {  16,  22,  38, 242 },
        { 242, 240, 250, 255 }, { 216, 198, 228,  88 }, 0.012f);

    m_components.accentButton = MakeButtonStyle(
        {  88,  66,  44, 222 }, { 126,  94,  58, 242 }, {  62,  46,  30, 246 },
        { 255, 246, 232, 255 }, { 250, 216, 157, 195 }, 0.012f);

    m_components.dangerButton = MakeButtonStyle(
        {  58,  28,  44, 204 }, {  98,  44,  68, 238 }, {  42,  20,  32, 244 },
        { 252, 196, 210, 255 }, { 238, 132, 158, 125 }, 0.012f);

    m_components.panel = {
        {  20,  26,  46, 208 }, {  12,  16,  30, 228 },
        { 250, 216, 157, 112 }, { 132, 144, 186,  46 },
        {   3,   5,  14, 116 }, { 246, 172, 200, 215 },
        0.014f, 0.0014f
    };

    m_components.modal = {
        {  24,  30,  54, 236 }, {  13,  17,  32, 246 },
        { 250, 216, 157, 185 }, { 188, 168, 224,  88 },
        {   2,   4,  12, 168 }, { 250, 216, 157, 235 },
        0.016f, 0.0016f
    };

    m_components.progress = {
        {  16,  22,  38, 205 }, { 246, 172, 200, 238 }, { 250, 216, 157, 115 },
        { 247, 244, 252, 240 }, 0.009f
    };
}

} // namespace sakura::core
