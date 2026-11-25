# Sakura-樱 开发路线图 v4（全自动持续迭代与发布版）

> 本路线图面向 **零人工介入（Unattended Autopilot）** 持续迭代与自动发版设计。
> 任意 Copilot 会话只需读取 [roadmap/AUTOPILOT_PLAYBOOK.md](roadmap/AUTOPILOT_PLAYBOOK.md) 与 `VERSION.json`，即可自动完成「代码实现 → 本地构建/单测/实机冒烟校验 → 打包 → 非交互升版打标 → 推送 `main` 与 Tag → 监控 GitHub Actions 产出 Release → 自动进入下一版本」完整闭环。

---

## 项目概况

| 项目 | 说明 |
|------|------|
| **名称** | Sakura-樱 |
| **类型** | 混合模式音乐节奏游戏（左手 4K + 空格横梁下落 / 右手鼠标点击滑条 + 双区组合技） |
| **技术栈** | C++20 / SDL3 / CMake / vcpkg / MSVC + TypeScript Hono (v1.1+ 在线后端) |
| **目标平台** | Windows 10/11 (64-bit)，核心逻辑兼容 Linux CI |
| **美术风格** | 日系动漫 · 月夜墨樱与流光琉璃 (Moonlit Ink-Sakura & Luminous Glass) |
| **难度范围** | 1.0 ~ 15.0（含小数） |
| **音符体系** | Tap / Hold / ThumbBar(空格) / Circle / Slider / Link(跨区同押) / Synergy(樱绽组合技) |

---

## 版本演进总览

```text
v0.1~v0.7-alpha (已完成基线)
      │
      ▼
v0.8.0-alpha (空格横梁 + 键鼠组合技 + Fever 满开槽 + Chart v3)
      │
      ▼
v0.8.1-alpha (通用音频分析制谱管线 + 官方精选曲包 8~10 首入库)
      │
      ▼
v0.9.0-beta  (WSOLA 变速不变调 + 全屏深度结算报告 + 工房 v3 / .sakura 谱面包)
      │
      ▼
v0.9.1-beta  (Windows 原生 MP4 背景视频舞台 + F3 性能诊断面板 + 时钟抗漂移)
      │
      ▼
v1.0.0       (正式版收官：全量压测 + Inno Setup .exe 安装器与便携 .zip 双产物 CI)
      │
      ▼
v1.1.0       (在线生态一期：内建 Hono 双模服务端 + WinHTTP 网络层 + 自动更新 + 全球排行榜)
      │
      ▼
v1.2.0       (在线生态二期：游戏内在线谱面市场 SceneMarket + 工房一键发布 + 多端云同步)
      │
      ▼
v1.3.0       (核心手感重塑：全场景小数等级统一 + 3xR防误触 + 锁线消残影强打击感 + 同押多压提示 + Δt自适应重制全曲库)
      │
      ▼
v1.4.0       (竞技挑战进阶：樱华道场段位认定6阶考级 + 继承血槽称号徽章 + 全球每日挑战榜 + 命名多曲单)
      │
      ▼
v1.5.0       (视听演出进化：SkinManager 4套内置皮肤与打击音包 + Chart v4 舞台脉冲滤镜演出与双语同步歌词)
      │
      ▼
v1.6.0       (同场竞速联机：PB/线上回放幽灵对手实时分差与幽灵光标 + SceneArena 在线多人房间对战)
      │
      ▼
v1.7.0       (生态互通扩充：Phira .pez / osu! .osz / SM .sm 双区智能谱面转换器 + 原创曲包 Vol.04 6首新曲)
      │
      ▼
v2.0.0       (2.0 终极收官：游戏内静默流式热更新原子自重启 + B30/R10 Sakura Rating 与高清 PNG 生涯名片导出)
```

| 版本 | 通道 | 阶段主题 | 核心交付物 | 状态 |
|------|------|----------|------------|------|
| v0.1 ~ v0.7.0 | dev/alpha | Phase 0~6 基建至琉璃焕新 | 核心引擎、双区玩法、确定性回放、夜樱琉璃视觉、离线录制 | ✅ 已完成 |
| v0.8.0 ~ v0.9.1 | alpha/beta | Chart v3、WSOLA 与 MP4 舞台 | 空格横梁、组合技、曲包、WSOLA 变速、深度结算、MP4 BGA | ✅ 已完成 |
| v1.0.0 ~ v1.2.0 | stable | 双产物正式发行与在线生态 | Inno Setup 双产物、双模后端、排行榜、谱面市场、云同步 | ✅ 已完成 |
| v1.3.0 | stable | 核心手感重塑与全曲库重制 | 难度/小数等级统一、3xR重开、消除左手下落残影、强打击特效音效、多压/跨区同押提示、Δt自适应重制15曲 | ⏳ 待发布 |
| v1.4.0 | stable | 樱华道场段位、日榜与曲单 | `SceneCourse` 多曲连奏段位考核（继承血槽+称号徽章）、每日挑战榜、命名多曲单收藏夹 | ⏳ 待发布 |
| v1.5.0 | stable | 自定义皮肤包与 Chart v4 演出 | 落地 `skin_path`（4套内置皮肤+打击音包）、Chart v4 舞台演出事件与时间轴双语歌词 | ⏳ 待发布 |
| v1.6.0 | stable | 幽灵伴跑竞速与在线多人对战 | 单人 PB/线上回放实时分差条与幽灵光标、`SceneArena` 多人建房同步竞技与领奖台 | ⏳ 待发布 |
| v1.7.0 | stable | Phira/osu!/SM 转换与曲包 Vol.04 | 原生解析 Phira `.pez` (Official/RPE)、`.osz`、`.sm` 转双区谱面、6首原创曲包 Vol.04 | ⏳ 待发布 |
| v2.0.0 | stable | 静默热更新自重启与 B30 生涯名片 | 后台流式下载+SHA256校验+PowerShell原子热替换自重启、B30/R10 Rating 与高清 PNG 名片导出 | ⏳ 待发布 |

---

## 路线图文件索引

### 自动化执行总控

| 文件 | 说明 |
|------|------|
| [roadmap/AUTOPILOT_PLAYBOOK.md](roadmap/AUTOPILOT_PLAYBOOK.md) | 无人值守连续更新+发布+线上服 SSH 自动部署协议与一键续跑 Prompt |
| [../scripts/autopilot_release.py](../scripts/autopilot_release.py) | 非交互式本地门禁、升版打标、GitHub Release 监控与 `steven-lee.tech` 生产服同步脚本 |

### 分版本实施规范 (v0.8 → v2.0)

| 文件 | 覆盖版本 | 内容 |
|------|----------|------|
| [roadmap/V0_8_SYNERGY_AND_SONGS.md](roadmap/V0_8_SYNERGY_AND_SONGS.md) | `v0.8.0-alpha`、`v0.8.1-alpha` | 空格横梁、Link/Synergy 组合技、Fever 槽、Chart v3 与音频自动制谱曲包 |
| [roadmap/V0_9_PRO_TOOLS_AND_BGA.md](roadmap/V0_9_PRO_TOOLS_AND_BGA.md) | `v0.9.0-beta`、`v0.9.1-beta` | WSOLA 变速不变调、全屏详细结算报告、工房 v3、`.sakura` 包与 MP4 视频舞台 |
| [roadmap/V1_0_OFFICIAL_RELEASE.md](roadmap/V1_0_OFFICIAL_RELEASE.md) | `v1.0.0` | Inno Setup 安装器、双产物 GitHub Release 流水线、全量回归与正式版定稿 |
| [roadmap/V1_1_1_2_ONLINE_ECOSYSTEM.md](roadmap/V1_1_1_2_ONLINE_ECOSYSTEM.md) | `v1.1.0`、`v1.2.0` | 内建 Hono 双模后端、WinHTTP 客户端、自动更新、全球排行榜、谱面市场与云同步 |
| [roadmap/V1_3_GAMEPLAY_FEEL_AND_RECHART.md](roadmap/V1_3_GAMEPLAY_FEEL_AND_RECHART.md) | `v1.3.0` | 小数等级统一、`R`键三连防误触、消除下落残影、强打击感视听、同押提示与 Δt 自适应谱面重制 |
| [roadmap/V1_4_COURSE_DAILY_AND_COLLECTIONS.md](roadmap/V1_4_COURSE_DAILY_AND_COLLECTIONS.md) | `v1.4.0` | 樱华道场段位认定、继承血槽称号徽章、每日挑战日榜与命名多曲单 |
| [roadmap/V1_5_SKINS_AND_STAGE_V4.md](roadmap/V1_5_SKINS_AND_STAGE_V4.md) | `v1.5.0` | 自定义皮肤与打击音包系统、Chart Format v4 舞台演出事件与双语歌词 |
| [roadmap/V1_6_GHOST_AND_MULTIPLAYER.md](roadmap/V1_6_GHOST_AND_MULTIPLAYER.md) | `v1.6.0` | 幽灵对手实时分差与幽灵光标、`SceneArena` 在线多人对战房间 |
| [roadmap/V1_7_PHIRA_CONVERTER_AND_VOL4.md](roadmap/V1_7_PHIRA_CONVERTER_AND_VOL4.md) | `v1.7.0` | Phira `.pez` / osu! `.osz` / SM `.sm` 智能双区谱面转换器与曲包 Vol.04 |
| [roadmap/V2_0_SELF_UPDATER_AND_CAREER.md](roadmap/V2_0_SELF_UPDATER_AND_CAREER.md) | `v2.0.0` | 游戏内静默下载热更新自重启、B30/R10 Rating 潜力值与生涯名片 PNG 导出 |

### 历史归档与参考文档

| 文件 | 内容 |
|------|------|
| [old_reference/ROADMAP_v2_phases0-3.md](old_reference/ROADMAP_v2_phases0-3.md) | Phase 0~3 完整开发记录（已归档） |
| [roadmap/PHASE4_CONTENT.md](roadmap/PHASE4_CONTENT.md) ~ [PHASE8_ONLINE.md](roadmap/PHASE8_ONLINE.md) | v3 路线图阶段草稿归档 |
| [RELEASE_REVIEW_0.6.0.md](RELEASE_REVIEW_0.6.0.md) | v0.6.0 全项目修复与交付验证基线 |
| [roadmap/APPENDICES.md](roadmap/APPENDICES.md) | 模块目录与场景流转附录 |

---

## 当前状态

- **当前版本：** v1.2.0
- **当前交付：** 完整双域协同演奏（Chart v3）、15 首曲库、WSOLA 变速不变调、深度结算遥测、MP4 动态视频舞台、Inno Setup 双产物发行、内建双模服务端、全球排行榜、在线谱面市场（`SceneMarket`）与多端幂等云存档同步。
- **里程碑状态：** `v0.8.0-alpha` 至 `v1.2.0` 全部七个规划版本已完成研发、自动化验证与发布。
