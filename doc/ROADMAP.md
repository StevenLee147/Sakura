# Sakura-樱 开发路线图 v3

> 每个 Step 都是一个可以直接交给 Copilot 的任务。按顺序执行即可。
> 标记 `[PROMPT]` 的是建议直接复制给 Copilot 的提示词。
> 质量优先，稳步推进。在线功能推迟到 v2.0。

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

## 版本规划总览

```
v0.1 Foundation ──→ v0.2 Playable ──→ v0.3 Editor ──→ v0.4 Visual
     (done)              (done)             (done)         (done)
        ↓                                                    ↓
v0.5 Content ──→ v0.6 Data ──→ v0.7 Polish ──→ v1.0 Release
                                                     ↓
                                              v2.0 Online (另行规划)
```

| 版本 | 阶段 | 核心目标 | 状态 |
|------|------|----------|------|
| v0.1 | Phase 0 — 技术基建 | 引擎骨架、渲染管线、基础系统 | ✅ 已完成 |
| v0.2 | Phase 1 — 核心玩法 | 完整可玩流程：选歌→游戏→结算 | ✅ 已完成 |
| v0.3 | Phase 2 — 编辑器与设置 | 谱面编辑器、设置系统、本地存储 | ✅ 已完成 |
| v0.4 | Phase 3 — 视觉与音频 | 粒子/Shader/主题/音效/背景 | ✅ 已完成 |
| v0.5 | Phase 4 — 内容与体验 | 教程、测试谱面、成就、统计、PP | 🔲 待开发 |
| v0.6 | Phase 5 — 数据与回放 | Replay系统、数据导出导入、详细报告 | 🔲 待开发 |
| v0.7 | Phase 6 — 打磨优化 | 性能、背景视频、自动更新、打包 | 🔲 待开发 |
| v1.0 | Phase 7 — 发布 | 最终测试、安装程序、正式发布 | 🔲 待开发 |
| v2.0 | Phase 8 — 在线功能 | 账号、排行榜、谱面市场、云同步 | 🔲 待开发 |

---

## 路线图文件索引

### 已完成阶段（归档）

| 文件 | 内容 |
|------|------|
| [old_reference/ROADMAP_v2_phases0-3.md](old_reference/ROADMAP_v2_phases0-3.md) | Phase 0~3 完整开发记录（已归档） |

### 待开发阶段

| 文件 | 阶段 | 版本 |
|------|------|------|
| [roadmap/PHASE4_CONTENT.md](roadmap/PHASE4_CONTENT.md) | Phase 4 — 内容与体验 | v0.5 |
| [roadmap/PHASE5_DATA.md](roadmap/PHASE5_DATA.md) | Phase 5 — 数据与回放 | v0.6 |
| [roadmap/PHASE6_POLISH.md](roadmap/PHASE6_POLISH.md) | Phase 6 — 打磨优化 | v0.7 |
| [roadmap/PHASE7_RELEASE.md](roadmap/PHASE7_RELEASE.md) | Phase 7 — 发布 | v1.0 |
| [roadmap/PHASE8_ONLINE.md](roadmap/PHASE8_ONLINE.md) | Phase 8 — 在线功能 | v2.0 |

### 附录

| 文件 | 内容 |
|------|------|
| [roadmap/APPENDICES.md](roadmap/APPENDICES.md) | 技术栈清单、目录结构、场景流程图、工时估算 |

---

## 当前状态

- **当前版本：** v0.4.2-alpha
- **下一目标：** Phase 4 — 内容与体验 (v0.5)
- **已实现模块：** 引擎核心 / 游戏玩法 / 编辑器 / 设置 / 视觉特效 / 音效系统 / 主题 / 数据库

Sketch:
加一点别的键？。加个组合技什么的 比如鼠标点击+按键盘上特定的按键（
