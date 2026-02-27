# Sakura-樱 依赖库清单

> 所有依赖通过 vcpkg manifest mode 管理（`vcpkg.json`）

---

## 核心依赖

| 库名 | vcpkg 包名 | 版本要求 | 用途 | 许可证 |
|------|-----------|----------|------|--------|
| **SDL3** | `sdl3` | ≥ 3.2.0 | 窗口管理、事件处理、输入、GPU API | Zlib |
| **SDL3_image** | `sdl3-image` | ≥ 3.2.0 | 图片加载（PNG/JPG/WebP） | Zlib |
| **SDL3_ttf** | `sdl3-ttf` | ≥ 3.2.0 | TrueType/OpenType 字体渲染 | Zlib |
| **SDL3_mixer** | `sdl3-mixer` | ≥ 3.2.0 | 音频混合播放（WAV/FLAC/OGG/MP3） | Zlib |
| **SDL3_net** | `sdl3-net` | ≥ 3.0.0 | 网络通信（后期在线功能） | Zlib |
| **nlohmann/json** | `nlohmann-json` | ≥ 3.11.0 | JSON 解析/序列化（配置、谱面数据） | MIT |
| **SQLite3** | `sqlite3` | ≥ 3.45.0 | 本地数据库（成绩、设置、成就） | Public Domain |
| **spdlog** | `spdlog` | ≥ 1.13.0 | 高性能日志框架 | MIT |

---

## SDL3 GPU API 说明

SDL3 新增了跨后端 GPU 抽象层，会根据系统自动选择最佳后端：
- **Windows**: 优先 Vulkan，回退 D3D12 → D3D11
- 不需要单独安装 Vulkan SDK（SDL3 内部处理）
- Shader 使用 SDL3 内置的跨后端 Shader 编译方案

---

## SDL3_mixer 音频格式支持

SDL3_mixer 通过内置和可选的后端支持多种格式：

| 格式 | 支持方式 | 备注 |
|------|----------|------|
| WAV | 内置 | 无损无压缩，精确同步首选 |
| FLAC | 内置 | 无损压缩 |
| OGG Vorbis | 内置 (libvorbis) | 推荐的有损压缩格式 |
| MP3 | 内置 | 兼容性好 |
| Opus | 可选 (libopus) | 新一代编解码器 |

---

## 字体资源

| 字体 | 来源 | 许可证 | 用途 |
|------|------|--------|------|
| **Noto Sans CJK** | Google Fonts | OFL 1.1 | 主要UI字体，完整CJK支持 |

- 下载地址：https://fonts.google.com/noto/specimen/Noto+Sans+SC
- 建议使用 `NotoSansSC-Regular.ttf` 和 `NotoSansSC-Bold.ttf`
- 嵌入到 `resources/fonts/` 目录

---

## 构建工具链

| 工具 | 版本要求 | 用途 |
|------|----------|------|
| **MSVC** | VS 2022 Build Tools (v143) | C++20 编译器 |
| **CMake** | ≥ 3.25 | 构建系统 |
| **Ninja** | 最新 | CMake 生成器（推荐） |
| **vcpkg** | 最新 | C++ 包管理器 |
| **Git** | 最新 | 版本控制 |

---

## VS Code 推荐扩展

| 扩展 | ID | 用途 |
|------|-----|------|
| C/C++ | `ms-vscode.cpptools` | IntelliSense、调试 |
| CMake Tools | `ms-vscode.cmake-tools` | CMake 集成 |
| CMake Language Support | `twxs.cmake` | CMake 语法高亮 |
| GitHub Copilot | `github.copilot` | AI 编程助手 |

---

## vcpkg 安装步骤

```powershell
# 1. 安装 vcpkg（如果尚未安装）
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 2. 设置环境变量
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\path\to\vcpkg", "User")

# 3. 在项目目录中，CMake 会自动读取 vcpkg.json 并安装依赖
cmake --preset debug
cmake --build --preset debug
```

---

## 注意事项

1. **SDL3 状态**：SDL3 于 2025 年正式发布稳定版。vcpkg 中已有 SDL3 及其子库的 port。
2. **GPU API Shader**：SDL3 GPU API 使用平台无关的 Shader 格式，通过 `SDL_CreateGPUShader` 加载。开发时可用 HLSL/GLSL 编写，通过 SDL 工具编译。
3. **SQLite3**：vcpkg 中的包名为 `sqlite3`，CMake target 为 `unofficial::sqlite3::sqlite3`。
4. **spdlog**：默认 header-only 模式。如需编译模式，在 vcpkg triplet 中配置。
