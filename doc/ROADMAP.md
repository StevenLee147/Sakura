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
```

| 版本 | 通道 | 阶段主题 | 核心交付物 | 状态 |
|------|------|----------|------------|------|
| v0.1 ~ v0.5 | dev/alpha | Phase 0~4 基建至内容 | 核心引擎、双区玩法、工房、教程、成就、PP、统计 | ✅ 已完成 |
| v0.6 ~ v0.7.0 | alpha | Phase 5~6 回放与琉璃焕新 | 确定性 PlaySession、回放、设置、夜樱琉璃视觉、离线录制与 ZIP 打包 | ✅ 已完成 |
| v0.8.0-alpha | alpha | 核心玩法进化 (Chart v3) | 空格大拇指横梁、跨区连携 Link Note、樱绽组合技、Fever 满开槽、教程第 6 课 | 🔲 待执行 |
| v0.8.1-alpha | alpha | 官方曲包扩充 | 通用 STFT/HPSS 自动制谱与封面生成管线、`download/songs` 8~10 首三难度新曲 | 🔲 待执行 |
| v0.9.0-beta | beta | 专业工具链与深度分析 | WSOLA 变速不变调、全屏结算分析覆盖层、工房波形/SV曲线、`.sakura` 拖拽导入导出 | 🔲 待执行 |
| v0.9.1-beta | beta | 视听打磨与准正式版压测 | Media Foundation 原生 MP4 背景视频、F3 性能与延迟叠层、音频时钟抗漂移、多分辨率压测 | 🔲 待执行 |
| v1.0.0 | stable | 正式版双形态发行 | `installer/sakura.iss` 安装器、CI 双产物（Setup EXE + 便携 ZIP）、全曲定级与正式版文档 | 🔲 待执行 |
| v1.1.0 | stable | 在线生态一期：排行与更新 | `server/` 双模后端、`src/net/` WinHTTP、GitHub 自动检查更新、账号体系与全球排行榜 | 🔲 待执行 |
| v1.2.0 | stable | 在线生态二期：市场与云同步 | `SceneMarket` 在线谱面市场（搜索/评分/一键下载）、工房直传发布、多端幂等云存档同步 | 🔲 待执行 |

---

## 路线图文件索引

### 自动化执行总控

| 文件 | 说明 |
|------|------|
| [roadmap/AUTOPILOT_PLAYBOOK.md](roadmap/AUTOPILOT_PLAYBOOK.md) | 无人值守连续更新+发布执行协议、CI 自愈策略与一键续跑 Prompt |
| [../scripts/autopilot_release.py](../scripts/autopilot_release.py) | 非交互式本地构建/单测/冒烟/打包/升版打标/远端 GitHub Release 验证脚本 |

### 分版本实施规范 (v0.8 → v1.2)

| 文件 | 覆盖版本 | 内容 |
|------|----------|------|
| [roadmap/V0_8_SYNERGY_AND_SONGS.md](roadmap/V0_8_SYNERGY_AND_SONGS.md) | `v0.8.0-alpha`、`v0.8.1-alpha` | 空格横梁、Link/Synergy 组合技、Fever 槽、Chart v3 与音频自动制谱曲包 |
| [roadmap/V0_9_PRO_TOOLS_AND_BGA.md](roadmap/V0_9_PRO_TOOLS_AND_BGA.md) | `v0.9.0-beta`、`v0.9.1-beta` | WSOLA 变速不变调、全屏详细结算报告、工房 v3、`.sakura` 包与 MP4 视频舞台 |
| [roadmap/V1_0_OFFICIAL_RELEASE.md](roadmap/V1_0_OFFICIAL_RELEASE.md) | `v1.0.0` | Inno Setup 安装器、双产物 GitHub Release 流水线、全量回归与正式版定稿 |
| [roadmap/V1_1_1_2_ONLINE_ECOSYSTEM.md](roadmap/V1_1_1_2_ONLINE_ECOSYSTEM.md) | `v1.1.0`、`v1.2.0` | 内建 Hono 双模后端、WinHTTP 客户端、自动更新、全球排行榜、谱面市场与云同步 |

### 历史归档与参考文档

| 文件 | 内容 |
|------|------|
| [old_reference/ROADMAP_v2_phases0-3.md](old_reference/ROADMAP_v2_phases0-3.md) | Phase 0~3 完整开发记录（已归档） |
| [roadmap/PHASE4_CONTENT.md](roadmap/PHASE4_CONTENT.md) ~ [PHASE8_ONLINE.md](roadmap/PHASE8_ONLINE.md) | v3 路线图阶段草稿归档 |
| [RELEASE_REVIEW_0.6.0.md](RELEASE_REVIEW_0.6.0.md) | v0.6.0 全项目修复与交付验证基线 |
| [roadmap/APPENDICES.md](roadmap/APPENDICES.md) | 模块目录与场景流转附录 |

---

## 当前状态

- **当前版本：** v1.1.1
- **当前交付：** 完整离线流程、六页设置、练习/回放、工房试玩、存档备份恢复、月夜墨樱琉璃视觉、4K60 离线录制与 Windows x64 发行包。
- **下一目标版本：** 按 [roadmap/AUTOPILOT_PLAYBOOK.md](roadmap/AUTOPILOT_PLAYBOOK.md) 自动推进 `v0.8.0-alpha` 直至 `v1.2.0`。
