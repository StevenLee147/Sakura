#pragma once

// theme.h — 全局 UI 主题系统
// 管理颜色方案、动画参数、特效开关
// 支持三套预设：Sakura / Midnight / Daylight

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

enum class ThemePreset
{
    Sakura,     // 深蓝 + 粉色（默认）
    Midnight,   // 纯黑 + 紫色霓虹
    Daylight,   // 浅灰 + 柔和蓝
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

// ── Theme 单例 ────────────────────────────────────────────────────────────────

class Theme
{
public:
    static Theme& GetInstance();

    // 根据保存的配置（theme_name 键）初始化，找不到配置时默认 Sakura
    void Initialize();

    // 切换预设（立刻应用，并写回 Config）
    void SetPreset(ThemePreset preset);
    void SetPresetByName(std::string_view name);

    // 当前颜色 / 设置 / 预设名称
    const ThemeColors&   Colors()   const { return m_colors;   }
    const ThemeSettings& Settings() const { return m_settings; }
    ThemePreset          Preset()   const { return m_preset;   }
    const char*          PresetName()  const;

    // 便捷颜色访问
    const Color& Primary()    const { return m_colors.primary;  }
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
    void ApplyMidnight();
    void ApplyDaylight();

    ThemeColors   m_colors;
    ThemeSettings m_settings;
    ThemePreset   m_preset = ThemePreset::Sakura;
};

} // namespace sakura::core
