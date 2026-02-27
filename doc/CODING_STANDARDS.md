# Sakura-樱 C++ 编码规范

> 适用于 C++20 / MSVC / SDL3 技术栈

---

## 1. 命名规范

### 1.1 总则

| 元素 | 风格 | 示例 |
|------|------|------|
| 命名空间 | `snake_case` | `sakura::core`, `sakura::game` |
| 类/结构体 | `PascalCase` | `SceneManager`, `NoteRenderer` |
| 函数/方法 | `PascalCase` | `UpdateScore()`, `RenderNote()` |
| 变量（局部） | `camelCase` | `noteSpeed`, `comboCount` |
| 变量（成员） | `m_` + `camelCase` | `m_currentScene`, `m_scoreData` |
| 变量（静态成员） | `s_` + `camelCase` | `s_instance`, `s_refCount` |
| 变量（全局） | `g_` + `camelCase` | `g_renderer`, `g_audioManager` |
| 常量 | `UPPER_SNAKE_CASE` | `MAX_COMBO`, `DEFAULT_BPM` |
| 枚举类型 | `PascalCase` | `NoteType`, `JudgeResult` |
| 枚举值 | `PascalCase` | `NoteType::Tap`, `JudgeResult::Perfect` |
| 模板参数 | `PascalCase` | `typename T`, `typename Container` |
| 宏 | `UPPER_SNAKE_CASE` | `SAKURA_VERSION`, `SAKURA_DEBUG` |
| 文件名 | `snake_case` | `scene_manager.h`, `note_renderer.cpp` |

### 1.2 具体规则

- 布尔变量用 `is`/`has`/`can`/`should` 前缀：`bool isPlaying`, `bool hasFinished`
- Getter/Setter：`GetName()` / `SetName()`，不用 `getName`
- 指针：避免在变量名中加 `p` 前缀，使用智能指针
- 缩写：常见缩写可大写（`ID`, `URL`, `BPM`），其他避免缩写

---

## 2. 文件组织

### 2.1 文件结构

```
src/
├── core/
│   ├── app.h / app.cpp                  # 应用程序主类
│   ├── window.h / window.cpp            # 窗口管理
│   ├── renderer.h / renderer.cpp        # GPU 渲染器封装
│   ├── input.h / input.cpp              # 输入系统
│   ├── resource_manager.h / .cpp        # 资源管理
│   └── timer.h / timer.cpp              # 高精度计时器
│
├── game/
│   ├── note.h / note.cpp                # 音符基类与类型
│   ├── judge.h / judge.cpp              # 判定系统
│   ├── score.h / score.cpp              # 计分系统
│   ├── chart_loader.h / chart_loader.cpp  # 谱面加载
│   └── game_state.h / game_state.cpp    # 游戏状态数据
│
├── scene/
│   ├── scene.h                          # 场景基类
│   ├── scene_manager.h / .cpp           # 场景管理器
│   ├── scene_menu.h / .cpp              # 主菜单
│   ├── scene_select.h / .cpp            # 选歌
│   ├── scene_game.h / .cpp              # 游戏
│   ├── scene_result.h / .cpp            # 结算
│   ├── scene_editor.h / .cpp            # 编辑器
│   └── scene_settings.h / .cpp          # 设置
│
├── ui/
│   ├── ui_base.h                        # UI 组件基类
│   ├── button.h / button.cpp            # 按钮
│   ├── scroll_list.h / scroll_list.cpp  # 滚动列表
│   ├── input_field.h / input_field.cpp  # 输入框
│   ├── panel.h / panel.cpp              # 面板
│   └── progress_bar.h / progress_bar.cpp  # 进度条
│
├── audio/
│   ├── audio_manager.h / .cpp           # 音频管理
│   └── audio_visualizer.h / .cpp        # 音频可视化
│
├── effects/
│   ├── particle_system.h / .cpp         # 粒子系统
│   ├── glow.h / glow.cpp               # 发光效果
│   ├── trail.h / trail.cpp             # 拖尾效果
│   └── shader_effects.h / .cpp          # Shader 特效
│
├── editor/
│   ├── editor_core.h / .cpp             # 编辑器核心
│   ├── editor_timeline.h / .cpp         # 时间轴
│   ├── editor_tools.h / .cpp            # 编辑工具
│   └── editor_io.h / .cpp              # 导入导出
│
├── net/
│   ├── api_client.h / .cpp              # REST API 客户端
│   └── network_manager.h / .cpp         # 网络管理
│
├── utils/
│   ├── logger.h / logger.cpp            # spdlog 封装
│   ├── math_utils.h                     # 数学工具
│   ├── easing.h                         # 缓动函数
│   └── string_utils.h                   # 字符串工具
│
└── main.cpp                             # 程序入口
```

### 2.2 头文件规则

- 使用 `#pragma once` 作为头文件保护
- 头文件中只放声明，实现放 `.cpp`（内联函数/模板函数除外）
- 包含顺序（用空行分隔每组）：
  1. 对应的 `.h` 文件（仅 `.cpp` 中）
  2. C++ 标准库
  3. 第三方库头文件
  4. 项目内头文件

```cpp
// note_renderer.cpp
#include "note_renderer.h"

#include <vector>
#include <cmath>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "core/renderer.h"
#include "game/note.h"
```

---

## 3. 代码风格

### 3.1 缩进与格式

- 缩进：4 空格（不用 Tab）
- 行宽上限：120 字符
- 大括号：Allman 风格（大括号独占一行）

```cpp
namespace sakura::game
{

class Judge
{
public:
    JudgeResult Evaluate(int timeDelta);

private:
    int m_perfectWindow = 25;
    int m_greatWindow = 50;
};

void Judge::Evaluate(int timeDelta)
{
    if (std::abs(timeDelta) <= m_perfectWindow)
    {
        return JudgeResult::Perfect;
    }
    else if (std::abs(timeDelta) <= m_greatWindow)
    {
        return JudgeResult::Great;
    }
}

} // namespace sakura::game
```

### 3.2 现代 C++20 用法

- 优先使用 `auto` 当类型明显时
- 使用 `std::unique_ptr` / `std::shared_ptr` 管理资源
- 使用 `std::string_view` 代替 `const std::string&` 作为只读参数
- 使用 `std::optional` 表示可能不存在的值
- 使用 `std::variant` 替代 union
- 使用 `constexpr` 替代 `#define` 常量
- 使用 Range-based for 循环
- 使用结构化绑定 `auto [key, value] = ...`
- 使用 `<=>` 三路比较运算符
- 使用 `std::format` 替代 `printf` / `sprintf`

```cpp
// 好
constexpr int MAX_LANES = 4;
constexpr float PERFECT_WINDOW_MS = 25.0f;

auto notes = LoadNotes(chartPath);
for (const auto& note : notes)
{
    // ...
}

// 不好
#define MAX_LANES 4
```

### 3.3 枚举

- 使用 `enum class` 而非 `enum`

```cpp
enum class NoteType : uint8_t
{
    Tap,
    Hold,
    Drag,
    Circle,
    Slider
};

enum class JudgeResult : uint8_t
{
    Perfect,
    Great,
    Good,
    Bad,
    Miss
};
```

---

## 4. 注释规范

### 4.1 文件头注释

```cpp
// =============================================================================
// Sakura-樱 | scene_game.cpp
// 游戏场景实现 — 键盘4K下落 + 鼠标点击双模式游戏逻辑
// =============================================================================
```

### 4.2 函数注释

```cpp
/// @brief 评估音符的判定结果
/// @param noteTime 音符目标时间（毫秒）
/// @param hitTime 玩家按键时间（毫秒）
/// @return 判定结果（Perfect/Great/Good/Bad/Miss）
JudgeResult Evaluate(int noteTime, int hitTime);
```

### 4.3 行内注释

- 使用 `//` 注释，与代码对齐
- 注释应解释"为什么"，而非"做了什么"
- 复杂算法或业务逻辑需要详细注释

```cpp
// 使用双缓冲避免渲染撕裂
SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);

// 判定窗口按严格程度从小到大检查，优先匹配最精准的判定
if (delta <= PERFECT_WINDOW) { ... }
```

---

## 5. 错误处理

- 初始化/资源加载失败：记录日志 + 返回错误码或 `std::optional`
- 运行时错误：记录日志 + 尝试优雅降级
- 不使用异常（SDL/游戏开发中异常性能开销大）
- assert 仅用于开发阶段的不变式检查

```cpp
std::optional<Texture> ResourceManager::LoadTexture(std::string_view path)
{
    auto* surface = IMG_Load(path.data());
    if (!surface)
    {
        spdlog::error("Failed to load texture: {} - {}", path, SDL_GetError());
        return std::nullopt;
    }
    // ...
}
```

---

## 6. 性能指引

- 避免在游戏主循环中执行堆分配（预分配 + 对象池）
- 渲染排序减少 GPU 状态切换
- 使用 `std::vector::reserve` 预分配容量
- 音符数据按时间排序，使用二分查找定位当前活跃音符
- 粒子系统使用固定大小数组而非动态容器
