# 🌸 Sakura-樱

<p align="center">
  <strong>一款融合键盘下落式与鼠标点击的双模式音乐节奏游戏</strong>
</p>

<p align="center">
  <em>左手键盘追逐节拍，右手鼠标捕捉旋律 —— 在樱花飘落中感受音乐的律动</em>
</p>

---

## 游戏简介

**Sakura-樱** 是一款 Windows 平台的桌面音乐节奏游戏，拥有独特的双操作区域设计：

- **左屏 · 4K 下落轨道** — 使用键盘（A/S/D/F）按下从上方坠落的音符，支持 Tap（点击）和 Hold（长押）等多种音符类型
- **右屏 · 鼠标圆形音符** — 使用鼠标点击出现在屏幕右侧的圆形目标，支持 Circle（圆形）和 Slider（滑条）音符

左右双端同时操作，带来全新的节奏游戏体验。

## 主要特性

### 🎮 游戏玩法
- **双模式操作** — 键盘 4K 轨道 + 鼠标点击同时进行
- **多音符类型** — Tap、Hold、Drag、Circle、Slider
- **精准判定系统** — Perfect / Great / Good / Bad / Miss 五级判定
- **完整计分** — 百万分制计分 + SS/S/A/B/C/D 评级

### 🌸 樱花主题
- **日系动漫美术风格** — 以樱花为核心的视觉设计
- **粒子特效** — 飘落的樱花花瓣、击中粒子爆发、发光效果
- **多主题支持** — 深色、浅色、霓虹三种预设主题
- **动态背景** — 粒子系统驱动的动态场景背景

### 🎵 音频体验
- **多格式支持** — WAV、OGG、MP3 等音频格式
- **音效反馈** — 不同判定对应不同音效
- **音乐可视化** — 频谱分析驱动的视觉效果

### 📝 内置谱面编辑器
- **可视化编辑** — 在时间轴上直观地放置、拖拽、修改音符
- **音乐波形** — 实时音频波形显示辅助制谱
- **快捷操作** — 支持撤销/重做、复制/粘贴、网格吸附
- **导入导出** — 谱面文件导入/导出功能

### ⚙️ 自定义设置
- **键位绑定** — 自定义游戏按键
- **延迟校准** — 交互式音频延迟校准
- **流速调节** — 自由调节音符下落速度
- **音量控制** — 音乐/音效/全局音量分离调节

## 快速开始

### 系统要求

| 配置项 | 最低要求 |
|--------|----------|
| 操作系统 | Windows 10 (64-bit) |
| 处理器 | 双核 1.5 GHz |
| 内存 | 2 GB RAM |
| 显卡 | 支持 SDL2 硬件加速 |
| 存储空间 | 100 MB（不含谱面） |
| 音频 | 支持立体声输出 |

### 下载与安装

>  **当前为早期开发版本，功能可能不完整**

#### 方式一：下载发布版
1. 前往 [GitHub Releases](../../releases) 页面
2. 下载最新版本的压缩包
3. 解压到任意目录
4. 运行 `Sakura.exe`

#### 方式二：从源码编译
```bash
# 克隆仓库
git clone https://github.com/你的用户名/Sakura.git
cd Sakura

# 使用 MinGW 编译（或通过 VS Code 任务运行 "Build SDL2"）
mingw64/bin/g++.exe Sakura.cpp -o Sakura.exe \
  -Iinclude -Imingw64/include -Llib -Lmingw64/lib -Isinclude \
  -lmingw32 -lSDL2main -lSDL2 -mwindows \
  -lbrotlienc -lSDL2_image -lpng16 -lSDL2_ttf -lfreetype \
  -lz -lbz2 -lSDL2_mixer -lvorbisfile -lvorbis -lvorbisenc \
  -logg -lwavpack -lsetupapi -lole32 -limm32 -lwinmm \
  -lversion -loleaut32 -lbrotlidec -lbrotlicommon -lgcc -lstdc++
```

## 操作指南

### 游戏操作

| 操作 | 按键 |
|------|------|
| 4K 轨道 第1轨 | `A` |
| 4K 轨道 第2轨 | `S` |
| 4K 轨道 第3轨 | `D` |
| 4K 轨道 第4轨 | `F` |
| 鼠标音符点击 | 鼠标左键 |
| 暂停/继续 | `ESC` |

### 全局快捷键

| 操作 | 按键 |
|------|------|
| 全屏切换 | `F11` |
| 截图保存 | `F12` |
| 帮助信息 | `F1` |

### 编辑器操作

| 操作 | 按键 |
|------|------|
| 播放/暂停 | `空格` |
| 放置音符 | 鼠标左键点击轨道 |
| 删除音符 | 鼠标右键 / `Delete` |
| 切换音符类型 | `Z` / `X` |
| 撤销 | `Ctrl + Z` |
| 重做 | `Ctrl + Y` |
| 保存谱面 | `Ctrl + S` |
| 缩放时间轴 | `Ctrl + 滚轮` |
| 滚动时间轴 | 滚轮 |

## 谱面格式

### 目录结构
```
resource/charts/{id}/
├── info.json          # 谱面元信息
├── chart.json         # 键盘轨道音符数据
├── mousechart.json    # 鼠标音符数据
├── music.wav          # 音乐文件
└── bg.png             # 背景图片
```

### info.json 示例
```json
{
    "id": "1",
    "bpm": 180,
    "music_path": "music.wav",
    "bgm_path": "bg.png",
    "chart_author": "作者名",
    "music_author": "曲师名",
    "name": "曲目名称",
    "chartpath": "resource/charts/1/chart.json",
    "mousechartpath": "resource/charts/1/mousechart.json",
    "difficulty": 5.0,
    "offset": 0
}
```

### chart.json（键盘音符）
```json
{
    "notes": [
        { "time": 5, "lane": 0, "type": "tap" },
        { "time": 7, "lane": 2, "type": "hold", "duration": 2 }
    ]
}
```

### mousechart.json（鼠标音符）
```json
{
    "notes": [
        { "time": 10, "x": 0.3, "y": 0.3, "type": "circle" },
        {
            "time": 14, "x": 0.4, "y": 0.4, "type": "slider",
            "slider_length": 1500,
            "slider_curve": [[0.8, 0.2], [0.9, 0.4], [0.7, 0.6]]
        }
    ]
}
```

> **坐标说明：** 鼠标音符的 `x`、`y` 为 0~1 的归一化坐标，会根据实际屏幕分辨率自动缩放。

## 自定义主题

游戏支持通过 `config/theme.json` 自定义颜色和特效：

```json
{
    "colors": {
        "primary": [100, 150, 255, 255],
        "secondary": [255, 100, 150, 255]
    },
    "animations": {
        "buttonHover": 150,
        "buttonClick": 100
    },
    "effects": {
        "particles": true,
        "glow": true
    }
}
```

内置三种预设主题：
- **深色 (Dark)** — 深蓝色调，适合专注游戏
- **浅色 (Light)** — 柔和粉白色调，清新可爱
- **霓虹 (Neon)** — 高饱和荧光色，充满活力

## 项目结构

```
Sakura/
├── Sakura.cpp              # 主程序入口
├── sinclude/               # 游戏自定义头文件
│   ├── sglobal.h           # 全局常量、枚举、结构体定义
│   ├── sscenes.h           # 场景管理与所有场景实现
│   ├── sgame_enhanced.h    # 增强游戏场景（面向对象）
│   ├── smain_enhanced.h    # 主应用循环与全局初始化
│   ├── sui.h               # 现代化 UI 组件库
│   ├── srendering.h        # 底层渲染函数
│   ├── seffects.h          # 粒子/发光/拖尾特效系统
│   ├── sanimation.h        # 缓动动画框架
│   ├── stheme.h            # 主题与颜色管理
│   ├── saudio.h            # 音频播放封装
│   ├── seditor.h           # 谱面编辑器核心
│   ├── seditor_ui.h        # 增强编辑器 UI
│   ├── sresourcem.h        # 资源加载与管理
│   ├── sinteraction.h      # 交互与输入法管理
│   └── sdebug.h            # 日志与调试系统
├── include/                # 第三方库头文件
│   ├── SDL2/               # SDL2 全家桶
│   ├── nlohmann/           # JSON 库
│   ├── freetype/           # 字体渲染
│   └── ...
├── lib/                    # 静态链接库
├── mingw64/                # MinGW 编译器
├── resource/               # 游戏资源
│   ├── charts/             # 谱面数据
│   ├── notes/              # 音符贴图
│   └── sound/              # 音效文件
├── config/                 # 运行时配置
│   └── theme.json          # 主题配置
├── logs/                   # 运行日志
└── ROADMAP.md              # 开发路线图
```

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++ |
| 图形/窗口 | SDL2 |
| 图片加载 | SDL2_image + libpng |
| 字体渲染 | SDL2_ttf + FreeType |
| 音频 | SDL2_mixer + Vorbis + WavPack |
| JSON | nlohmann/json |
| 编译器 | MinGW-w64 (g++) |
| 压缩 | zlib + bzip2 + Brotli |

## 社区

- **QQ 交流群：** 936341181
- **GitHub：** [项目主页](../../)

## 开发状态

当前版本处于 **早期开发阶段**，核心功能仍在积极开发中。

详细的开发计划和功能规划请查看 [ROADMAP.md](ROADMAP.md)。

### 当前进度
- [x] 基础游戏框架（SDL2 初始化、场景管理）
- [x] 4K 键盘轨道战玩法
- [x] 鼠标圆形音符玩法
- [x] 场景切换动画系统（7种过渡效果）
- [x] 主题系统（3种预设主题）
- [x] 粒子/发光/拖尾特效引擎
- [x] 现代化 UI 组件库（按钮、面板、滚动列表、输入框等）
- [x] 谱面编辑器框架（待修复）
- [ ] 编辑器功能修复
- [ ] 完整结算界面
- [ ] 完善计分与评级系统
- [ ] 官方谱面库
- [ ] 在线社区功能

## 贡献

欢迎提交 Issue 和 Pull Request！

## 许可证

许可证待定。

---

<p align="center">
  🌸 <em>在樱花树下，聆听音乐的心跳</em> 🌸
</p>
