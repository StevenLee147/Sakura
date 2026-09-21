# Sakura 依赖与构建

以 `vcpkg.json` 和其中固定的 baseline 为准。当前桌面发行目标是 Windows x64，编译标准 C++20。

| 组件 | 实际用途 |
|---|---|
| SDL3 | 窗口、输入、文件选择器、GPU renderer、2D 绘图与呈现 |
| SDL3_image，png/jpeg/webp features | PNG、JPEG、WebP、BMP 图片读取与 PNG 保存 |
| SDL3_ttf / FreeType | Noto Sans SC 字体渲染 |
| miniaudio | 播放时钟、WAV/MP3/FLAC 解码、混音、音量、变速、跳转 |
| stb_vorbis | miniaudio 的 OGG Vorbis 解码支持 |
| nlohmann/json | 谱面、设置、回放、备份元信息 |
| SQLite3 | 本地成绩、统计与成就；当前数据库 schema 3 |
| spdlog / fmt | 日志与格式化 |

运行时优先使用 SDL 的 `gpu` renderer，失败时由 SDL 选择可用后端。后处理使用渲染目标、纹理和几何绘图；当前发行不依赖外部 SPIR-V 文件。音频使用 miniaudio，未使用 SDL_mixer。当前离线功能不依赖 SDL_net。

WAV/MP3/FLAC 使用文件流播放；OGG Vorbis 保留压缩数据并使用可跳转的内存解码器，避免 callback decoder 无法返回时长的问题。OGG 单文件限制 256 MiB，频谱分析使用独立解码游标。变速同时影响音高。

## Windows 开发

安装 Visual Studio 2022 / Build Tools（v143，含 Windows SDK）、CMake 3.25+、Git 和 vcpkg。

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset debug -DSAKURA_BUILD_TESTS=ON
cmake --build --preset debug --parallel
ctest --test-dir build/debug -C Debug --output-on-failure
```

Release 对应 `release` preset。Visual Studio 生成器支持在同一个构建目录中生成多个配置：`cmake --build build/debug --config Release`。

## 逻辑测试

`SakuraTests` 不依赖窗口、音频设备或 GPU；可用于 Windows CTest 和 Linux `ci-linux` preset。系统环境缺少 vcpkg package config 时，可通过 CMake 提供 nlohmann/json 头文件、SQLite3 库以及可选 spdlog。桌面程序始终要求完整依赖。

## 打包

```powershell
python scripts/package_release.py --build-dir build/release
```

脚本自动定位 Visual Studio 的 x64 CRT；找不到时使用 `--crt-dir` 指向 `Microsoft.VC143.CRT`。输出包括 App-local DLL、字体及依赖许可证、内置曲目、成就配置、中文说明、每个文件的 SHA256 清单和 ZIP 的校验文件。不会收集开发者存档、日志、缓存或本机设置。

最低运行系统 Windows 10 1903，使用 UTF-8 activeCodePage manifest 支持中文安装路径和数据路径。Windows 11 同样支持。仓库不存入第三方源码或预编译 DLL。

许可证索引见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。
