# Sakura-樱 快速开始指南

> Windows 10/11 x64 环境，MSVC + CMake + vcpkg + VS Code

---

## 1. 安装 Visual Studio Build Tools

下载 [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/zh-hans/visual-cpp-build-tools/)。

安装时勾选：
- **"使用 C++ 的桌面开发"** 工作负载
- 右侧确认包含：MSVC v143、Windows SDK、CMake tools for Windows

安装完成后，打开 **"x64 Native Tools Command Prompt for VS 2022"**，运行：

```powershell
cl
```

能输出版本号即可。

---

## 2. 安装 vcpkg

```powershell
# 选一个永久目录（推荐 C:\dev\vcpkg）
cd C:\dev
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

设置环境变量（**重要，CMakePresets 依赖它**）：

```powershell
# PowerShell — 设为用户级永久变量
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
```

设置完后**重启终端**使变量生效。验证：

```powershell
echo $env:VCPKG_ROOT
# 应输出 C:\dev\vcpkg
```

---

## 3. 安装 CMake 和 Ninja

### 方法 A — 用 VS Build Tools 自带的

VS Build Tools 已包含 CMake 和 Ninja。确认 PATH 中有它们：

```powershell
cmake --version   # >= 3.25
ninja --version
```

如果找不到，手动添加 PATH（通常在 `C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`）。

### 方法 B — 独立安装

- CMake: https://cmake.org/download/ — 安装时勾选 "Add to PATH"
- Ninja: https://github.com/ninja-build/ninja/releases — 解压到 PATH 目录

---

## 4. 安装 Git

https://git-scm.com/download/win — 安装默认选项即可。

```powershell
git --version
```

---

## 5. 安装 VS Code 及扩展

1. 下载安装 [VS Code](https://code.visualstudio.com/)
2. 安装以下扩展（Ctrl+Shift+X 搜索安装）：

| 扩展名 | 扩展 ID | 作用 |
|--------|---------|------|
| C/C++ | `ms-vscode.cpptools` | IntelliSense + 调试 |
| CMake Tools | `ms-vscode.cmake-tools` | CMake 配置/构建/调试集成 |
| CMake Language Support | `twxs.cmake` | CMake 语法高亮 |
| GitHub Copilot | `github.copilot` | AI 代码补全 |
| GitHub Copilot Chat | `github.copilot-chat` | AI 对话 |

---

## 6. 创建项目

### 6.1 初始化仓库

```powershell
cd I:\Projects
mkdir Sakura
cd Sakura
git init
```

### 6.2 从 save_for_rebuild 复制模板文件

将以下文件复制到项目根目录（文件内容已在 save_for_rebuild/templates/ 准备好）：

```
Sakura/
├── CMakeLists.txt              ← templates/CMakeLists.txt
├── CMakePresets.json            ← templates/CMakePresets.json
├── vcpkg.json                  ← templates/vcpkg.json
├── .gitignore                  ← templates/.gitignore
├── .github/
│   ├── instructions/
│   │   └── sakura.instructions.md  ← new_docs/instructions/sakura.instructions.md
│   └── workflows/
│       └── build.yml               ← templates/github_workflows/build.yml
```

PowerShell 命令：

```powershell
$root = "I:\Projects\Sakura"
$save = "$root\save_for_rebuild"

# 模板文件
Copy-Item "$save\templates\CMakeLists.txt" "$root\"
Copy-Item "$save\templates\CMakePresets.json" "$root\"
Copy-Item "$save\templates\vcpkg.json" "$root\"
Copy-Item "$save\templates\.gitignore" "$root\"

# GitHub 配置
New-Item -ItemType Directory -Force "$root\.github\instructions"
New-Item -ItemType Directory -Force "$root\.github\workflows"
Copy-Item "$save\new_docs\instructions\sakura.instructions.md" "$root\.github\instructions\"
Copy-Item "$save\templates\github_workflows\build.yml" "$root\.github\workflows\"
```

### 6.3 创建文档目录

```powershell
# 文档
Copy-Item "$save\new_docs\ROADMAP.md" "$root\"
Copy-Item "$save\new_docs\CHART_FORMAT_SPEC.md" "$root\"
Copy-Item "$save\new_docs\CODING_STANDARDS.md" "$root\"
Copy-Item "$save\new_docs\ARCHITECTURE.md" "$root\"
Copy-Item "$save\new_docs\DEPENDENCIES.md" "$root\"
```

### 6.4 创建源码和资源目录

```powershell
# 源码目录
$srcDirs = @(
    "src",
    "src/core",
    "src/game",
    "src/scene",
    "src/ui",
    "src/audio",
    "src/effects",
    "src/editor",
    "src/net",
    "src/utils",
    "src/data"
)
foreach ($dir in $srcDirs) {
    New-Item -ItemType Directory -Force "$root\$dir"
}

# 资源目录
$resDirs = @(
    "resources/fonts",
    "resources/images",
    "resources/sound/sfx",
    "resources/sound/music",
    "resources/charts",
    "config",
    "logs"
)
foreach ($dir in $resDirs) {
    New-Item -ItemType Directory -Force "$root\$dir"
}
```

### 6.5 创建最小 main.cpp

在 `src/main.cpp` 放一个最小程序（ROADMAP Step 0.1 会完善它）：

```cpp
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        SDL_Log("SDL init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Sakura-樱", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```

---

## 7. 下载字体

从 [Google Fonts](https://fonts.google.com/noto/specimen/Noto+Sans+SC) 下载 Noto Sans SC。

解压后将以下文件放到 `resources/fonts/`：

- `NotoSansSC-Regular.ttf`
- `NotoSansSC-Bold.ttf`

---

## 8. 首次构建

### 8.1 配置（首次会下载编译所有 vcpkg 依赖，耗时 5~20 分钟）

在 VS Code 中打开项目文件夹。确保 MSVC 环境可用（推荐从 "x64 Native Tools Command Prompt" 启动 VS Code：`code I:\Projects\Sakura`）。

```powershell
cmake --preset debug
```

或在 VS Code 中：`Ctrl+Shift+P` → "CMake: Configure" → 选择 "debug" preset。

### 8.2 编译

```powershell
cmake --build --preset debug
```

或 VS Code: `Ctrl+Shift+P` → "CMake: Build"

### 8.3 运行

```powershell
.\build\debug\Sakura.exe
```

或 VS Code: `Ctrl+Shift+P` → "CMake: Run Without Debugging" (Shift+F5)

如果窗口弹出，环境搭建完成。

---

## 9. 配置调试

在 VS Code 中 CMake Tools 扩展会自动提供调试配置。按 F5 即可启动调试（断点、变量查看等）。

如果需要自定义，创建 `.vscode/launch.json`：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Sakura",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/debug/Sakura.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}/build/debug",
            "environment": [],
            "console": "integratedTerminal"
        }
    ]
}
```

---

## 10. 推送到 GitHub

```powershell
cd I:\Projects\Sakura
git add -A
git commit -m "chore: project skeleton"

# 在 GitHub 创建新仓库后
git remote add origin https://github.com/<your-username>/Sakura.git
git branch -M main
git push -u origin main
```

---

## 11. 验证 CI/CD

推送后查看 GitHub → Actions 页面，确认 build.yml 工作流运行通过。

---

## 常见问题

### vcpkg 依赖安装失败

```powershell
# 清理后重试
Remove-Item -Recurse -Force build
cmake --preset debug
```

### MSVC 找不到

确保从 "x64 Native Tools Command Prompt" 启动 VS Code，或在 CMakePresets.json 中指定编译器路径。

### SDL3 找不到

确认 `$env:VCPKG_ROOT` 已设置。CMakePresets.json 中的 `CMAKE_TOOLCHAIN_FILE` 依赖此变量定位 vcpkg。

### 中文乱码

CMakeLists.txt 已设置 `/utf-8`。确保所有源文件保存为 UTF-8 (BOM 可选)。VS Code 右下角状态栏可查看/切换编码。

---

## 目录结构总览（搭建完成后）

```
Sakura/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── .gitignore
├── ROADMAP.md
├── CHART_FORMAT_SPEC.md
├── CODING_STANDARDS.md
├── ARCHITECTURE.md
├── DEPENDENCIES.md
├── .github/
│   ├── instructions/
│   │   └── sakura.instructions.md
│   └── workflows/
│       └── build.yml
├── config/
├── logs/
├── resources/
│   ├── fonts/
│   │   ├── NotoSansSC-Regular.ttf
│   │   └── NotoSansSC-Bold.ttf
│   ├── images/
│   ├── sound/
│   │   ├── sfx/
│   │   └── music/
│   └── charts/
├── src/
│   ├── main.cpp
│   ├── core/
│   ├── game/
│   ├── scene/
│   ├── ui/
│   ├── audio/
│   ├── effects/
│   ├── editor/
│   ├── net/
│   ├── utils/
│   └── data/
└── build/
    └── debug/
```

接下来按 **ROADMAP.md** 从 Step 0.1 开始逐步开发。
