# Sakura-樱 Autopilot 全自动持续更新与发布手册

本文档定义了 Copilot 在 **无人值守（Unattended Autopilot）** 模式下连续完成多个版本开发、验证、打包、打标、GitHub Release 发布与线上服同步的标准作业协议。

> **当前战役状态（已发布归档：`v0.8.0-alpha → v2.0.0` 共 13 个版本 ｜ 当前活跃战役：`v2.1.0 → v3.0.0` 共 6 个版本）**：前序规划的 **13 个里程碑版本**（`v0.8.0-alpha`、`v0.8.1-alpha`、`v0.9.0-beta`、`v0.9.1-beta`、`v1.0.0`、`v1.1.0`、`v1.2.0`、`v1.3.0`、`v1.4.0`、`v1.5.0`、`v1.6.0`、`v1.7.0`、`v2.0.0`）已 **100% 完成研发、门禁验证并上线 GitHub Releases 与 `https://steven-lee.tech:233`**；当前活跃战役为 **`v2.1.0 → v3.0.0` 共 6 个里程碑版本**（`v2.1.0`、`v2.2.0`、`v2.3.0`、`v2.4.0`、`v2.5.0`、`v3.0.0`），按本手册协议执行全程零人工介入连续迭代与自动发布。

## 一键投喂 Prompt（`v2.1.0 → v3.0.0` 全自动断点续跑）

任意放入本仓库并处于 Autopilot 模式的 Copilot 会话，直接投喂以下提示词即可自动定位断点并连续跑完 `v2.1.0 → v2.2.0 → v2.3.0 → v2.4.0 → v2.5.0 → v3.0.0` 全部研发、验证、发版与部署闭环：

```text
请阅读 doc/roadmap/AUTOPILOT_PLAYBOOK.md 与 doc/ROADMAP.md，进入全程零人工介入（Zero Human Intervention）的持续更新+自动发布循环：
1. 先运行 python scripts/autopilot_release.py --status 检查当前 VERSION.json、本地 Git tag 与远端 GitHub Releases，自动定位 v2.1.0 → v2.2.0 → v2.3.0 → v2.4.0 → v2.5.0 → v3.0.0 序列中下一个尚未发布的里程碑版本。
2. 每个版本先对齐共享头文件契约，再按互斥文件白名单扇出最高 10 路并行子智能体（Up to 10 Disjoint Parallel Subagents）同步完成全部代码、资源与单测（严格遵循 0.0~1.0 归一化布局与绝不简化原则，兼顾 Windows MSVC 与 ci-linux 单测）。
3. 中间增量提交统一设置 VERSION_HOOK_BUMP=no-bump 防止弹窗阻塞；每完成一个里程碑版本后，运行 python scripts/autopilot_release.py 执行本地 4 重门禁校验（① Release 编译 + CTest 全量单测、② Sakura.exe --smoke-test --verify-play 实机冒烟、③ server 单测 + Sakura.exe --verify-online 在线端到端验证、④ package_release.py 双产物发行打包），通过后自动非交互升版打 Tag 并推送 main 与 Tag。
4. 阻塞等待远端 GitHub Actions 完成跨平台构建，严格校验 GitHub Release 双发行产物（便携版 .zip、Inno Setup -setup.exe）与 SHA256SUMS.txt 齐全且为正式版；若本地门禁或远端 CI 失败，立即读取日志原地修复并重跑，严禁跳过门禁或禁用功能。
5. 每个版本 GitHub Release 校验通过后，自动通过免密 SSH 将最新 server/ 部署至 steven-lee.tech:233 (/srv/sakura-online) 并校验 https://steven-lee.tech:233/api/v1/health 返回最新版本号且状态健康。
6. 当前版本全链路验收通过后，绝不停下询问人工确认，立即自动进入下一个版本，连续推进 v2.1.0 → v2.2.0 → v2.3.0 → v2.4.0 → v2.5.0 → v3.0.0 直至全部正式发布并验收通过。
```

---

## 版本发布阶梯与版本钩子参数表

仓库使用 `.githooks/pre-commit` + `scripts/sync_repo_version.py` 维护版本同步，每次提交时若未传入 `VERSION_HOOK_BUMP` 会弹出 GUI 选择框阻塞自动化。因此**必须严格按照下表传入通道与升级参数**：

| 序号 | 目标 Tag | 升级前通道动作 | `VERSION_HOOK_BUMP` | 核心规格文档 | 远端必检产物与线上探针状态 |
|------|----------|----------------|---------------------|--------------|----------------------------|
| 1~7 | `v0.8.0-alpha` ~ `v1.2.0` | 已发布归档 | — | `V0_8` ~ `V1_1_1_2` | ✅ 已全部发布上线 |
| 8 | `v1.3.0` | `stable` (`clear`) | `minor` | `V1_3_GAMEPLAY_FEEL_AND_RECHART.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 9 | `v1.4.0` | `stable` (`clear`) | `minor` | `V1_4_COURSE_DAILY_AND_COLLECTIONS.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 10 | `v1.5.0` | `stable` (`clear`) | `minor` | `V1_5_SKINS_AND_STAGE_V4.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 11 | `v1.6.0` | `stable` (`clear`) | `minor` | `V1_6_GHOST_AND_MULTIPLAYER.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 12 | `v1.7.0` | `stable` (`clear`) | `minor` | `V1_7_PHIRA_CONVERTER_AND_VOL4.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 13 | `v2.0.0` | `stable` (`clear`) | `major` | `V2_0_SELF_UPDATER_AND_CAREER.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 14 | `v2.1.0` | `stable` (`clear`) | `minor` | `V2_1_AUTH_PERSISTENCE_AND_ACCOUNT.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 15 | `v2.2.0` | `stable` (`clear`) | `minor` | `V2_2_ONLINE_TRANSPORT_AND_ARENA.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 16 | `v2.3.0` | `stable` (`clear`) | `minor` | `V2_3_UI_OVERHAUL_AND_SELECT_UX.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 17 | `v2.4.0` | `stable` (`clear`) | `minor` | `V2_4_LOW_LATENCY_AUDIO_AND_SFX.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 18 | `v2.5.0` | `stable` (`clear`) | `minor` | `V2_5_RIVALS_SOCIAL_AND_POLISH.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |
| 19 | `v3.0.0` | `stable` (`clear`) | `major` | `V3_0_SEASON_RANKED_AND_VOL5.md` | ✅ 已发布上线（双产物 + SHA256 + `:233`） |

---

## 单版本最高 10 路并行子智能体（Up-to-10 Parallel Subagents）执行策略

在每个里程碑版本内部，主协调智能体（Coordinator）应优先对齐核心共享头文件契约，随后在同一轮工具调用中通过 `task` 工具（`agent_type: "general-purpose"`）按需扇出 **最高可达 10 个并发子智能体（Up to 10 Concurrent Sub-Agents）**，每个子智能体拥有**严格互斥的文件作用域（Disjoint File Scopes）**同步开发：

1. **Subagent 1（核心玩法逻辑）**：独占修改 `src/game/` 新增/扩展模块与根 `CMakeLists.txt`。
2. **Subagent 2（引擎核心与配置）**：独占修改 `src/core/`、`src/utils/`。
3. **Subagent 3（本地 SQLite 数据层）**：独占修改 `src/data/`。
4. **Subagent 4（在线客户端网络层）**：独占修改 `src/net/`。
5. **Subagent 5（在线服务端与接口单测）**：独占修改 `server/`。
6. **Subagent 6（新增核心场景实现）**：独占修改版本新增独立场景（如 `src/scene/scene_course.*`、`src/scene/scene_arena.*`）。
7. **Subagent 7（主菜单与选歌场景联动）**：独占修改 `src/scene/scene_menu.*`、`src/scene/scene_select.*`、`src/ui/`。
8. **Subagent 8（演奏舞台与结算场景联动）**：独占修改 `src/scene/scene_game.*`、`src/scene/scene_result.*`、`src/effects/`。
9. **Subagent 9（音频合成与曲包资源管线）**：独占修改 `src/audio/`、`scripts/`、`resources/charts/`。
10. **Subagent 10（单测、冒烟入口与文档）**：独占修改 `tests/`、`src/main.cpp`、`doc/`。

**并行铁律**：根据版本改动体量灵活裁剪为 5～10 路并发；各子智能体 Prompt 必须显式列出允许写入的文件白名单，严禁跨界修改其他文件或自行执行 `cmake --build` / `git commit`；待全部并发子智能体返回后，由主协调智能体统一执行编译、`ctest`、`--smoke-test`、`--verify-online` 与发版提交流程。

---

## 标准单版本闭环执行流

### Step A — 开发与增量保存
1. 阅读对应版本规格文档与关联模块现有源码。
2. 所有新增非 UI 逻辑同步注册到 `CMakeLists.txt` 的 `sakura-game-logic`（若属于跨平台逻辑层）与 `tests/CMakeLists.txt`，确保 Linux `ci-linux` 构建不缺符号、不引入未隔离的 Windows 专属头文件。
3. 如需在版本中途保存进度，必须带环境变量提交：
   ```powershell
   $env:VERSION_HOOK_BUMP = 'no-bump'
   git add -A
   git commit -m "feat(...): ..."
   ```

### Step B — 本地全量门禁与自动发版
直接运行仓库内置发版器完成构建、测试、冒烟、打包、升版提交、推送与远端 Release 监控：
```powershell
python scripts/autopilot_release.py --release \
  --target-tag v0.8.0-alpha \
  --channel alpha \
  --bump minor \
  --message "release(v0.8.0-alpha): thumb bar, link/synergy notes, fever gauge and chart v3"
```

该脚本内部依次强制校验：
1. `cmake --preset release -DSAKURA_BUILD_TESTS=ON` & `cmake --build --preset release --parallel`
2. `ctest --test-dir build/release -C Release --output-on-failure --no-tests=error`
3. `build/release/Release/Sakura.exe --smoke-test build/qa/<tag> --size 1600 900 --verify-play`
4. （`v1.1.0+` 额外执行）`npm --prefix server test` 与本地实服 `Sakura.exe --verify-online`
5. `python scripts/package_release.py --build-dir build/release --replace`
6. 切换 `VERSION_HOOKS.json` 通道，使用 `VERSION_HOOK_BUMP` 提交（附带 Copilot App `Co-authored-by` trailer），验证 `VERSION.json` 与新建 Git tag 一致
7. `git push origin main` 与 `git push origin <tag>`
8. 轮询并 `gh run watch` 对应 `<tag>` 的 `Build` 工作流直至全绿，最后 `gh release view <tag>` 验证附件存在。

### Step C — 远端 CI 失败自愈协议
若 `gh run watch` 返回非零退出码：
1. 执行 `gh run view <runId> --log-failed` 检查是 `Windows Debug`、`Windows Release`、`Linux Tests (Ninja)` 还是 `Create GitHub Release` 报错。
2. 在本地修复编译警告/跨平台头文件/单测断言并重新通过本地门禁。
3. 若远端尚未生成正式 Release：
   - 提交修复（`$env:VERSION_HOOK_BUMP='no-bump'`）
   - 删除远端与本地旧 tag：`git push origin :refs/tags/<tag>`、`git tag -d <tag>`
   - 在最新 HEAD 重建 annotated tag：`git tag -a <tag> -m "Release <tag>"`
   - 重新推送 `git push origin main` 与 `git push origin <tag>` 并继续 `gh run watch`。
4. 严禁因 CI 失败而删除测试或注释功能。

---

## `v3.x` 后续扩展快速接入清单

当规划 `v3.0.0` 之后的后续扩展版本时，按以下三步即可无缝复用本自动化管线：
1. 在 `doc/roadmap/` 下新建对应规格文件（如 `V3_1_<TOPIC>.md`），明确跨平台逻辑层（`sakura-game-logic`）、UI 归一化布局、SQLite 幂等迁移与服务端契约。
2. 在 `doc/ROADMAP.md` 里程碑表与本文件发布阶梯表中追加目标 Tag（常规功能迭代使用 `--channel stable --bump minor`，热修补丁使用 `--channel stable --bump patch`）。
3. 启动 Autopilot 会话运行 `python scripts/autopilot_release.py --status`，按上述 10 路互斥文件域并行开发与全量门禁闭环直推上线，客户端内置的 `Updater` v2 将自动向存量玩家推送 SHA-256 校验热更新。

---

## 工程红线速查

1. **禁止硬编码像素**：坐标、间距、线宽、字号一律基于 `0.0~1.0` 归一化比例。
2. **禁止破坏存档与旧谱面**：SQLite 升级必须支持幂等列/表迁移；ChartLoader 必须同时兼容 v1、v2、v3、v4；Replay 必须兼容 v1 与 v2；热更新脚本必须确保 `%APPDATA%` 与 `userdata/` 零数据丢失。
3. **禁止提交构建产物与用户隐私数据**：`build/`、`userdata/`、`download/` 保持忽略，临时验证统一输出到 `build/qa/`。
