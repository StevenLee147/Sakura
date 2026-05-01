#pragma once

// theme.h — 全局 UI 主题系统
// 管理 Sakura 唯一主视觉、动画参数、特效开关

#include "renderer.h"   // sakura::core::Color

#include <string>
#include <string_view>

namespace sakura::core
{

// ── 主题颜色结构 ──────────────────────────────────────────────────────────────

struct ThemeColors
{
    // 主色调
    Color primary;          // 主要强调色（按钮底色、焦点元素）
    Color secondary;        // 次要色（非焦点元素）
    Color accent;           // 高亮/点缀色（连击数字、重要标签）

    // 背景层
    Color bg;               // 场景背景
    Color surface;          // 面板/卡片背景
    Color surfaceBorder;    // 面板边框

    // 文字
    Color text;             // 主要文字
    Color textDim;          // 次级/禁用文字

    // 游戏元素
    Color noteColor;        // 普通音符
    Color holdColor;        // Hold 音符
    Color circleColor;      // 鼠标区域圆圈
    Color glowColor;        // 辉光基础色
    Color judgeLine;        // 判定线颜色

    // 判定颜色  [0]=Perfect [1]=Great [2]=Good [3]=Bad [4]=Miss
    Color judgeColors[5];

    // 轨道颜色  [0..3] 各轨道高亮色
    Color laneColors[4];

    // 评级颜色  [0]=SS [1]=S [2]=A [3]=B [4]=C [5]=D
    Color gradeColors[6];
};

// ── 主题预设枚举 ──────────────────────────────────────────────────────────────
// 当前产品只保留 Sakura 主视觉。

enum class ThemePreset
{
    Sakura,     // 深蓝 + 粉色（默认）
};

// ── 主题设置参数 ──────────────────────────────────────────────────────────────

struct ThemeSettings
{
    float transitionDuration = 0.4f;   // 场景切换时长（秒）
    bool  particlesEnabled   = true;   // 粒子特效总开关
    bool  glowEnabled        = true;   // 辉光特效总开关
    bool  shakeEnabled       = true;   // 屏幕震动总开关
    bool  vignetteEnabled    = true;   // 晕影总开关
};

struct ThemeSpacing
{
    float xxs = 0.004f;
    float xs  = 0.008f;
    float sm  = 0.012f;
    float md  = 0.020f;
    float lg  = 0.032f;
    float xl  = 0.048f;
};

struct ThemeTypography
{
    float caption = 0.016f;
    float bodySm  = 0.020f;
    float body    = 0.026f;
    float title   = 0.040f;
    float display = 0.064f;
    float hero    = 0.090f;
};

struct ThemeRadii
{
    float xs = 0.004f;
    float sm = 0.008f;
    float md = 0.012f;
    float lg = 0.018f;
};

struct ThemeMotion
{
    float fast   = 0.15f;
    float normal = 0.25f;
    float slow   = 0.40f;
};

struct ThemeButtonStyle
{
    Color normal   = { 35, 30, 65, 210 };
    Color hover    = { 60, 50, 105, 230 };
    Color pressed  = { 20, 15, 45, 240 };
    Color disabled = { 100, 100, 100, 40 };
    Color text     = Color::White;
    Color border   = { 255, 255, 255, 120 };
    float cornerRadius = 0.010f;
};

struct ThemePanelStyle
{
    Color fill   = { 15, 12, 30, 220 };
    Color border = { 100, 80, 150, 150 };
    Color shadow = { 0, 0, 0, 90 };
    Color accent = { 255, 150, 200, 180 };
    float cornerRadius    = 0.012f;
    float borderThickness = 0.0015f;
};

struct ThemeProgressStyle
{
    Color bg     = { 25, 25, 45, 200 };
    Color fill   = { 140, 100, 200, 230 };
    Color border = { 100, 80, 150, 180 };
    Color text   = { 240, 240, 255, 220 };
    float cornerRadius = 0.008f;
};

struct ThemeComponentStyles
{
    ThemeButtonStyle primaryButton;
    ThemeButtonStyle secondaryButton;
    ThemeButtonStyle accentButton;
    ThemeButtonStyle dangerButton;
    ThemePanelStyle  panel;
    ThemePanelStyle  modal;
    ThemeProgressStyle progress;
};

// ── Theme 单例 ────────────────────────────────────────────────────────────────

class Theme
{
public:
    static Theme& GetInstance();

    // 初始化唯一 Sakura 主视觉；不从配置读取可选主题。
    void Initialize();

    // 兼容旧调用；当前会锁定到 Sakura，不向用户提供可选主题。
    void SetPreset(ThemePreset preset);
    void SetPresetByName(std::string_view name);

    // 当前颜色 / 设置 / 预设名称
    const ThemeColors&   Colors()   const { return m_colors;   }
    const ThemeSettings& Settings() const { return m_settings; }
    const ThemeSpacing&  Spacing()  const { return m_spacing;  }
    const ThemeTypography& Typography() const { return m_typography; }
    const ThemeRadii&    Radii()    const { return m_radii;    }
    const ThemeMotion&   Motion()   const { return m_motion;   }
    const ThemeComponentStyles& Components() const { return m_components; }
    ThemePreset          Preset()   const { return m_preset;   }
    const char*          PresetName()  const;

    // 便捷颜色访问
    const Color& Primary()    const { return m_colors.primary;  }
    const Color& Secondary()  const { return m_colors.secondary;}
    const Color& Accent()     const { return m_colors.accent;   }
    const Color& BgColor()    const { return m_colors.bg;       }
    const Color& Surface()    const { return m_colors.surface;  }
    const Color& Text()       const { return m_colors.text;     }
    const Color& TextDim()    const { return m_colors.textDim;  }
    const Color& NoteColor()  const { return m_colors.noteColor;}
    const Color& GlowColor()  const { return m_colors.glowColor;}

private:
    Theme() = default;
    Theme(const Theme&)            = delete;
    Theme& operator=(const Theme&) = delete;

    void ApplySakura();

    ThemeColors   m_colors;
    ThemeSettings m_settings;
    ThemeSpacing  m_spacing;
    ThemeTypography m_typography;
    ThemeRadii    m_radii;
    ThemeMotion   m_motion;
    ThemeComponentStyles m_components;
    ThemePreset   m_preset = ThemePreset::Sakura;
};

} // namespace sakura::core
