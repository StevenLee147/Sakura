# Sakura-樱 开发路线图 v2 — Phase 0~3 归档

> **归档日期：** 2026年3月1日
> **归档原因：** Phase 0~3 已基本完成，进入 Beta 规划阶段
> **当前版本：** v0.4.2-alpha

---

> 以下内容为 Phase 0（技术基建）→ Phase 3（视觉与音频）的完整开发记录。
> 新的路线图请查看 [ROADMAP.md](../ROADMAP.md)。

---

## 项目概况

| 项目 | 说明 |
|------|------|
| **名称** | Sakura-樱 |
| **类型** | 混合模式音乐节奏游戏（键盘4K下落 + 鼠标点击双操作区域） |
| **技术栈** | C++20 / SDL3 / SDL3 GPU API / CMake / vcpkg / MSVC |
| **目标平台** | Windows 10/11 (64-bit) |
| **美术风格** | 日系动漫 + 樱花核心视觉 |
| **难度范围** | 1.0 ~ 15.0（含小数） |
| **音符类型** | Tap / Hold / Drag / Circle / Slider（5种） |

---

## Phase 0 — 技术基建 (v0.1) ✅

> 搭建骨架，验证渲染管线，建立所有基础系统。

| Step | 内容 | 状态 |
|------|------|------|
| 0.1 | 项目骨架 — CMake + SDL3 最小窗口 | ✅ |
| 0.2 | 日志系统 — spdlog 封装 | ✅ |
| 0.3 | App 类与主循环 — 固定时间步长 | ✅ |
| 0.4 | 窗口管理 — 全屏/窗口切换 | ✅ |
| 0.5 | GPU 渲染器 — SDL3 GPU API 封装 | ✅ |
| 0.6 | 输入系统 — 键盘/鼠标状态管理 | ✅ |
| 0.7 | 资源管理器 — 纹理/字体/音频加载 | ✅ |
| 0.8 | 文字渲染 — TTF + UTF-8 中文支持 | ✅ |
| 0.9 | 场景管理 — 场景栈 + 过渡动画 | ✅ |
| 0.10 | 缓动函数 — 17种缓动曲线 | ✅ |
| 0.11 | 精灵渲染与几何图形 | ✅ |
| 0.12 | 配置系统 — JSON 读写 | ✅ |
| 0.13 | CI/CD — GitHub Actions | ✅ |

---

## Phase 1 — 核心玩法 (v0.2) ✅

> 完整可玩流程：启动→主菜单→选歌→游戏→结算。

| Step | 内容 | 状态 |
|------|------|------|
| 1.1 | 音频管理器 | ✅ |
| 1.2 | 谱面数据结构 | ✅ |
| 1.3 | 谱面加载器 | ✅ |
| 1.4 | 游戏状态管理 | ✅ |
| 1.5 | 判定系统 | ✅ |
| 1.6 | 计分系统 | ✅ |
| 1.7 | UI 组件库 | ✅ |
| 1.8 | 启动画面与加载场景 | ✅ |
| 1.9 | 主菜单 | ✅ |
| 1.10 | 选歌场景 | ✅ |
| 1.11 | 游戏场景（核心） | ✅ |
| 1.12 | 暂停菜单 | ✅ |
| 1.13 | 结算场景 | ✅ |

---

## Phase 2 — 编辑器与设置 (v0.3) ✅

> 谱面编辑器和完整设置系统。

| Step | 内容 | 状态 |
|------|------|------|
| 2.1 | 额外 UI 组件（Slider/Toggle/Dropdown/TextInput/TabBar/Toast） | ✅ |
| 2.2 | 设置场景 | ✅ |
| 2.3 | SQLite 数据层 | ✅ |
| 2.4 | 延迟校准 | ✅ |
| 2.5 | 编辑器（基础） | ✅ |
| 2.6 | 编辑器撤销/重做 | ✅ |
| 2.7 | 编辑器完善（5种音符 + 波形 + SV） | ✅ |
| 2.8 | 编辑器内试玩 | ✅ |
| 2.9 | 谱面导出/导入 | ✅ |

---

## Phase 3 — 视觉与音频 (v0.4) ✅

> 粒子系统、Shader特效、主题系统、音效系统、背景系统。

| Step | 内容 | 状态 |
|------|------|------|
| 3.1 | 粒子系统（樱花/爆发/连击/浮动/火花） | ✅ |
| 3.2 | 发光与拖尾 | ✅ |
| 3.3 | 屏幕震动 | ✅ |
| 3.4 | Shader 特效（模糊/暗角/色彩校正/色差） | ✅ |
| 3.5 | 特效集成到各场景 | ✅ |
| 3.6 | 主题系统（预设三套） | ✅ |
| 3.7 | Hitsound 系统 | ✅ |
| 3.8 | 背景系统 | ✅ |

---

## 已实现的技术栈

| 组件 | 技术 | 状态 |
|------|------|------|
| 窗口/输入 | SDL3 | ✅ |
| GPU 渲染 | SDL3 GPU API | ✅ |
| 图片加载 | SDL3_image | ✅ |
| 字体渲染 | SDL3_ttf | ✅ |
| 音频 | SDL3_mixer | ✅ |
| JSON | nlohmann/json | ✅ |
| 数据库 | SQLite3 | ✅ |
| 日志 | spdlog | ✅ |
| 字体 | Noto Sans CJK | ✅ |

---

## 已实现的目录结构

```
src/
├── main.cpp
├── core/       — app, window, renderer, input, timer, resource_manager, config, theme
├── scene/      — scene_manager, splash, loading, menu, select, game, pause, result, settings, calibration, editor, chart_wizard
├── game/       — note, chart, chart_loader, game_state, judge, score
├── editor/     — editor_core, editor_timeline, editor_command, editor_mouse_area, editor_preview
├── ui/         — button, scroll_list, label, progress_bar, slider, toggle, dropdown, text_input, tab_bar, toast
├── audio/      — audio_manager, sfx_generator
├── effects/    — particle_system, glow, trail, screen_shake, shader_manager, background
├── data/       — database
├── net/        — (预留)
└── utils/      — logger, easing
```
