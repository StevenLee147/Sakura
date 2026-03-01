# 附录

---

## 附录 A — 技术栈依赖清单

| 库 | 用途 | vcpkg 包名 |
|----|------|------------|
| SDL3 | 窗口/输入/事件 | sdl3 |
| SDL3_image | 图片加载 | sdl3-image |
| SDL3_ttf | 字体渲染 | sdl3-ttf |
| SDL3_mixer | 音频播放 | sdl3-mixer |
| SDL3_net | 网络(v2.0) | sdl3-net |
| nlohmann/json | JSON 解析 | nlohmann-json |
| SQLite3 | 本地数据库 | sqlite3 |
| spdlog | 日志 | spdlog |
| fmt | 格式化 | fmt |
| FFmpeg | 视频解码 | ffmpeg |
| zlib | 压缩 | zlib |
| freetype | 字体(SDL3_ttf依赖) | freetype |
| Noto Sans SC | CJK 字体 | 手动下载 |

---

## 附录 B — 目录结构完整规划

```
Sakura/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── version.h.in
├── installer.iss              ← Inno Setup 脚本
├── .github/
│   ├── workflows/build.yml
│   └── instructions/sakura.instructions.md
├── config/
│   ├── settings.json          ← 用户设置
│   └── theme.json             ← 主题配置
├── data/
│   └── sakura.db              ← SQLite 数据库
├── logs/
│   └── sakura.log
├── replays/                   ← 回放文件 (.skr)
├── resources/
│   ├── charts/                ← 谱面
│   │   └── {chart_id}/
│   │       ├── info.json
│   │       ├── easy.json / normal.json / hard.json / expert.json
│   │       ├── music.ogg
│   │       ├── cover.png
│   │       ├── bg.png / bg.mp4
│   ├── fonts/
│   │   ├── NotoSansSC-Regular.ttf
│   │   └── NotoSansSC-Bold.ttf
│   ├── images/
│   │   ├── icon.ico
│   │   └── logo.png
│   ├── data/
│   │   └── achievements.json  ← 成就定义
│   ├── shaders/
│   │   ├── blur.hlsl / blur.spv
│   │   ├── vignette.hlsl / vignette.spv
│   │   ├── color_correction.hlsl / .spv
│   │   └── chromatic.hlsl / .spv
│   └── sound/
│       ├── music/
│       └── sfx/
│           ├── default/ soft/ drum/  ← hitsound sets
│           ├── judge/                ← 判定音效
│           └── ui/                   ← UI 音效
├── doc/
│   ├── ARCHITECTURE.md
│   ├── CHART_FORMAT_SPEC.md
│   ├── CODING_STANDARDS.md
│   ├── DEPENDENCIES.md
│   ├── QUICKSTART.md
│   ├── ROADMAP.md
│   └── roadmap/
│       ├── PHASE4_CONTENT.md
│       ├── PHASE5_DATA.md
│       ├── PHASE6_POLISH.md
│       ├── PHASE7_RELEASE.md
│       ├── PHASE8_ONLINE.md
│       └── APPENDICES.md
└── src/
    ├── main.cpp
    ├── core/
    │   ├── app.h / .cpp
    │   ├── window.h / .cpp
    │   ├── renderer.h / .cpp
    │   ├── input.h / .cpp
    │   ├── timer.h / .cpp
    │   ├── resource_manager.h / .cpp
    │   ├── config.h / .cpp
    │   ├── theme.h / .cpp
    │   └── updater.h / .cpp
    ├── scene/
    │   ├── scene.h
    │   ├── scene_manager.h / .cpp
    │   ├── scene_splash.h / .cpp
    │   ├── scene_loading.h / .cpp
    │   ├── scene_menu.h / .cpp
    │   ├── scene_select.h / .cpp
    │   ├── scene_game.h / .cpp
    │   ├── scene_pause.h / .cpp
    │   ├── scene_result.h / .cpp
    │   ├── scene_settings.h / .cpp
    │   ├── scene_calibration.h / .cpp
    │   ├── scene_editor.h / .cpp
    │   ├── scene_tutorial.h / .cpp
    │   ├── scene_stats.h / .cpp
    │   └── scene_replay.h / .cpp
    ├── game/
    │   ├── note.h
    │   ├── chart.h
    │   ├── chart_loader.h / .cpp
    │   ├── game_state.h / .cpp
    │   ├── judge.h / .cpp
    │   ├── score.h / .cpp
    │   ├── replay.h / .cpp
    │   ├── achievement_manager.h / .cpp
    │   └── pp_calculator.h / .cpp
    ├── editor/
    │   ├── editor_core.h / .cpp
    │   ├── editor_timeline.h / .cpp
    │   └── editor_command.h / .cpp
    ├── ui/
    │   ├── ui_base.h
    │   ├── label.h / .cpp
    │   ├── button.h / .cpp
    │   ├── scroll_list.h / .cpp
    │   ├── progress_bar.h / .cpp
    │   ├── slider.h / .cpp
    │   ├── toggle.h / .cpp
    │   ├── dropdown.h / .cpp
    │   ├── text_input.h / .cpp
    │   ├── tab_bar.h / .cpp
    │   └── toast.h / .cpp
    ├── audio/
    │   ├── audio_manager.h / .cpp
    │   └── audio_visualizer.h / .cpp
    ├── effects/
    │   ├── particle_system.h / .cpp
    │   ├── glow.h / .cpp
    │   ├── trail.h / .cpp
    │   ├── screen_shake.h / .cpp
    │   ├── shader_manager.h / .cpp
    │   ├── background.h / .cpp
    │   ├── video_decoder.h / .cpp
    │   └── video_renderer.h / .cpp
    ├── data/
    │   └── database.h / .cpp
    ├── net/                   ← v2.0 预留
    │   ├── api_client.h / .cpp
    │   └── net_manager.h / .cpp
    └── utils/
        ├── logger.h / .cpp
        └── easing.h           ← header-only
```

---

## 附录 C — 场景流程图

```
          ┌──────────────┐
          │   Splash     │ ← 程序启动
          │  (淡入淡出)   │
          └──────┬───────┘
                 │ (首次运行?)
          ┌──────┴──────────────────┐
          │                         │
    ┌─────▼──────┐           ┌──────▼───────┐
    │  Tutorial  │           │   主菜单      │
    │  (教程)    │──完成────→│   (Menu)      │
    └────────────┘           └──────┬───────┘
                                    │
              ┌──────────────┬──────┼──────────────┐
              │              │      │              │
        ┌─────▼────┐  ┌─────▼────┐ │        ┌─────▼────┐
        │  设置    │  │  统计    │ │        │  退出    │
        │Settings  │  │  Stats   │ │        └──────────┘
        │  ├校准   │  │  ├成就   │ │
        └──────────┘  └──────────┘ │
                                   │
                            ┌──────▼───────┐
                     ┌──────│   选歌        │──────┐
                     │      │  (Select)     │      │
                     │      └──────┬───────┘      │
                     │             │               │
               ┌─────▼────┐ ┌─────▼───────┐      │
               │  编辑器  │ │  Loading    │      │
               │ (Editor) │ │  (加载资源)  │      │
               │  ├试玩   │ └─────┬───────┘      │
               └──────────┘       │               │
                            ┌─────▼───────┐      │
                            │   游戏       │      │
                            │  (Game)      │      │
                            │  ├暂停(Push) │      │
                            └─────┬───────┘      │
                                  │               │
                            ┌─────▼───────┐      │
                            │   结算       │──────┘
                            │  (Result)    │ (返回选歌)
                            │  ├详细报告   │
                            │  ├查看回放   │
                            └──────────────┘
```

---

## 附录 D — 工时估算

| Phase | 预计工时 | 累计 |
|-------|----------|------|
| Phase 0 — 技术基建 | 25-35h | 35h |
| Phase 1 — 核心玩法 | 40-55h | 90h |
| Phase 2 — 编辑器与设置 | 35-50h | 140h |
| Phase 3 — 视觉与音频 | 30-40h | 180h |
| Phase 4 — 内容与体验 | 25-35h | 215h |
| Phase 5 — 数据与回放 | 20-30h | 245h |
| Phase 6 — 打磨优化 | 25-35h | 280h |
| Phase 7 — 发布 | 10-15h | 295h |
| **v1.0 总计** | **210-295h** | |
| Phase 8 — 在线功能 | 60-80h | 375h |

> 以上为 Vibe-Coding 辅助下的估算。实际开发时间可能因调试、迭代而浮动。
