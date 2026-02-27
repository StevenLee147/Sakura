# Sakura-樱 项目 Copilot 指令

## 项目概况

| 项目 | 说明 |
|------|------|
| **名称** | Sakura-樱 |
| **类型** | 混合模式音乐节奏游戏（键盘4K下落 + 鼠标点击双操作区域） |
| **语言** | C++20 |
| **编译器** | MSVC (Visual Studio Build Tools) |
| **构建系统** | CMake (≥ 3.25) |
| **包管理** | vcpkg (manifest mode) |
| **图形** | SDL3 + SDL3 GPU API (底层可走 Vulkan/D3D12) |
| **目标平台** | Windows 10/11 (64-bit) |
| **IDE** | VS Code + CMake Tools 扩展 |

## 核心开发规则

### 1. 绝对禁止简化
- 出现错误时**修改错误**，不要禁用任何功能，不要进行任何简化
- 无论在任何调试或测试过程中，都不要禁用原应该存在的代码
- 如果代码导致或可能导致报错，请根据报错信息进行修改，而非删除
- 出现函数调用错误时，先检查工作区内所有自定义的源文件
- 按照提示创建新功能时，先查看工作区内是否有功能类似的实现可以复用
- 保障新版本包含原版本的所有功能（除非明确要求删除）

### 2. 坐标与布局系统
- **所有坐标、位置、尺寸、样式参数必须使用相对比例（0.0~1.0）表示**
- 禁止使用绝对像素值硬编码位置或尺寸
- 在渲染时根据当前窗口实际宽高动态计算像素值
- 示例：`float buttonX = 0.5f * screenWidth;` 而非 `int buttonX = 960;`
- UI 元素的间距、边距、字号等都基于屏幕短边或对角线的比例计算
- 必须适配不同分辨率屏幕（1080p / 1440p / 4K 等）

### 3. 代码组织
- 源文件按功能模块组织在 `src/` 子目录中
- 头文件声明 (.h) + 源文件实现 (.cpp) 分离
- 创建函数/变量/功能时，先查看对应模块目录下是否有功能类似的文件可以放置
- 模块目录结构：
  ```
  src/
  ├── core/          # 引擎核心（窗口、渲染、事件循环、资源管理）
  ├── game/          # 游戏逻辑（场景、判定、计分、音符）
  ├── editor/        # 谱面编辑器
  ├── ui/            # 自定义UI组件库
  ├── audio/         # 音频系统封装
  ├── effects/       # 视觉特效（粒子、发光、Shader等）
  ├── scene/         # 场景管理与具体场景实现
  ├── net/           # 网络/API客户端层（预留）
  └── utils/         # 工具函数、日志、调试
  ```

### 4. 构建与依赖
- 编译时通过 CMake 配置，不使用手动编译命令
- 第三方库通过 vcpkg manifest (`vcpkg.json`) 管理
- 不要在仓库中提交第三方库的源码或预编译二进制
- CMake preset 用于管理不同构建配置（Debug/Release/RelWithDebInfo）

### 5. 技术栈详情

| 组件 | 技术 | 用途 |
|------|------|------|
| 窗口/输入 | SDL3 | 窗口管理、事件处理、输入 |
| GPU 渲染 | SDL3 GPU API | 2D 渲染、Shader 特效 |
| 图片加载 | SDL3_image | PNG/JPG/WebP 加载 |
| 字体渲染 | SDL3_ttf | TrueType/OpenType 字体渲染（CJK支持） |
| 音频 | SDL3_mixer | 多格式音频播放（WAV/FLAC/OGG/MP3） |
| 网络 | SDL3_net | 网络通信（后期在线功能） |
| JSON | nlohmann/json | 配置文件、谱面数据 |
| 数据库 | SQLite3 | 本地成绩、玩家数据存储 |
| 日志 | spdlog | 分级日志系统 |
| 字体 | Noto Sans CJK | 嵌入式开源CJK字体 |

### 6. 场景管理架构
- 使用**状态机 + 场景类继承**模式
- 基类 `Scene` 定义 `OnEnter()`、`OnExit()`、`OnUpdate()`、`OnRender()`、`OnEvent()` 虚函数
- `SceneManager` 管理场景栈和切换动画
- 每个具体场景（主菜单、选歌、游戏、结算、编辑器、设置等）继承 `Scene`

### 7. 渲染与特效
- 使用 SDL3 GPU API 进行所有渲染操作
- 特效系统包括：粒子系统、发光效果、拖尾效果、Shader特效（模糊/扣色等）、背景视频/动画、屏幕震动
- 日系动漫风格 + 樱花核心视觉主题
- 支持自定义 Shader（SPIR-V 格式，兼容 Vulkan/D3D12）

### 8. 命名规范
- 类名：PascalCase（`SceneManager`、`NoteRenderer`）
- 函数名：PascalCase（`UpdateScore()`、`RenderNote()`）
- 变量名：camelCase（`noteSpeed`、`comboCount`）
- 成员变量：`m_` 前缀 + camelCase（`m_currentScene`、`m_scoreData`）
- 常量/宏：UPPER_SNAKE_CASE（`MAX_COMBO`、`DEFAULT_BPM`）
- 命名空间：小写（`sakura::core`、`sakura::game`）
- 文件名：snake_case（`scene_manager.h`、`note_renderer.cpp`）

### 9. 资源管理
- 开发时资源散文件放在 `resources/` 目录
- 发布时打包成自定义资源包
- 资源通过 `ResourceManager` 统一加载管理
- 支持异步加载和引用计数

### 10. 错误处理
- 使用 spdlog 记录所有错误和警告
- 关键资源加载失败时优雅降级而非崩溃
- SDL/GPU 调用后检查返回值或错误状态
