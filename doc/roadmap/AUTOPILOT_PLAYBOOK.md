# Sakura-樱 Autopilot 全自动持续更新与发布手册

本文档定义了 Copilot 在 **无人值守（Unattended Autopilot）** 模式下连续完成多个版本开发、验证、打包、打标与 GitHub Release 发布的标准作业协议。

## 一键投喂 Prompt（支持任意新会话断点续跑）

将以下提示词直接发给处于 Autopilot 模式的 Copilot 即可从当前进度自动续跑直至全部版本发版完成：

```text
请阅读 doc/roadmap/AUTOPILOT_PLAYBOOK.md 与 doc/ROADMAP.md，进入全程零人工介入的持续更新+自动发布循环：
1. 先运行 python scripts/autopilot_release.py --status 检查当前 VERSION.json、本地 Git tag 与远端 GitHub Releases，定位下一个尚未发布的里程碑版本。
2. 严格按照对应版本的 roadmap 规范完成全部代码、资源与单测（遵循 0.0~1.0 归一化布局与绝不简化原则，兼顾 Windows MSVC 与 ci-linux 单测）。
3. 中间提交统一设置 VERSION_HOOK_BUMP=no-bump 防止弹窗阻塞；每完成一个里程碑版本的功能与测试后，调用 python scripts/autopilot_release.py 执行本地 Release 编译、CTest、--smoke-test、发行打包、非交互式升版打 Tag、直推 main 与 Tag、并阻塞等待远端 GitHub Actions 成功创建 GitHub Release 且校验发行资产。
4. 若本地测试或远端 GitHub Actions 失败，立即读取错误日志原地修复并重跑，严禁跳过或禁用功能。
5. 当前版本 GitHub Release 验证通过后，不要停下询问我，立即自动进入下一个版本的开发与发布，直至 v1.2.0 正式发布并验收通过。
```

---

## 版本发布阶梯与版本钩子参数表

仓库使用 `.githooks/pre-commit` + `scripts/sync_repo_version.py` 维护版本同步，每次提交时若未传入 `VERSION_HOOK_BUMP` 会弹出 GUI 选择框阻塞自动化。因此**必须严格按照下表传入通道与升级参数**：

| 序号 | 目标 Tag | 升级前通道动作 | `VERSION_HOOK_BUMP` | 核心规格文档 | 远端 Release 必检产物 |
|------|----------|----------------|---------------------|--------------|------------------------|
| 1 | `v0.8.0-alpha` | `alpha` | `minor` | `V0_8_SYNERGY_AND_SONGS.md` | `Sakura-v0.8.0-alpha-win64.zip` |
| 2 | `v0.8.1-alpha` | `alpha` | `patch` | `V0_8_SYNERGY_AND_SONGS.md` | `Sakura-v0.8.1-alpha-win64.zip` |
| 3 | `v0.9.0-beta` | `beta` | `minor` | `V0_9_PRO_TOOLS_AND_BGA.md` | `Sakura-v0.9.0-beta-win64.zip` |
| 4 | `v0.9.1-beta` | `beta` | `patch` | `V0_9_PRO_TOOLS_AND_BGA.md` | `Sakura-v0.9.1-beta-win64.zip` |
| 5 | `v1.0.0` | `stable` (`clear`) | `major` | `V1_0_OFFICIAL_RELEASE.md` | `Sakura-v1.0.0-win64.zip` + `Sakura-v1.0.0-win64-setup.exe` |
| 6 | `v1.1.0` | `stable` (`clear`) | `minor` | `V1_1_1_2_ONLINE_ECOSYSTEM.md` | `Sakura-v1.1.0-win64.zip` + `Sakura-v1.1.0-win64-setup.exe` |
| 7 | `v1.2.0` | `stable` (`clear`) | `minor` | `V1_1_1_2_ONLINE_ECOSYSTEM.md` | `Sakura-v1.2.0-win64.zip` + `Sakura-v1.2.0-win64-setup.exe` |

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

## 工程红线速查

1. **禁止硬编码像素**：坐标、间距、线宽、字号一律基于 `0.0~1.0` 归一化比例。
2. **禁止破坏存档与旧谱面**：SQLite 升级必须支持幂等列/表迁移；ChartLoader 必须同时兼容 v1、v2、v3；Replay 必须兼容 v1 与 v2。
3. **禁止提交构建产物与用户隐私数据**：`build/`、`userdata/`、`download/` 保持忽略，临时验证统一输出到 `build/qa/`。
