# Phase v1.0 详细规格：正式版收官与双形态安装包发布

覆盖版本：`v1.0.0`（清空预发布后缀 `active_tag: ""`，首个稳定正式版）。

---

## Release 5：`v1.0.0` — 全量压测、Inno Setup 安装器与双产物 CI 发布

### 5.1 Inno Setup 安装程序 (`installer/sakura.iss`)

创建标准 Windows x64 安装脚本 `installer/sakura.iss`：
- **安装目录与权限**：默认路径 `{autopf}\Sakura`（支持免管理员用户态安装切换或自定义如 `C:\Games\Sakura`），64 位模式 (`ArchitecturesInstallIn64BitMode=x64compatible`)。
- **组件与快捷方式**：打包由 `scripts/package_release.py` 产出的经过 SHA256 校验的纯净发行目录（含 `Sakura.exe`、SDL3/依赖 DLL、MSVC x64 CRT、`resources/`、`config/`、`doc/`、`licenses/`）；可选创建桌面快捷方式、开始菜单程序组与 `.sakura` 谱面包文件关联。
- **存档安全卸载**：卸载程序仅清理安装目录程序文件，默认保留 `%APPDATA%\Sakura\Sakura` 用户成绩、设置、回放与自制谱面，弹窗询问用户是否额外清理个人存档，杜绝误删玩家记录。
- **版本号自动注入**：通过 `ISCC.exe /DMyAppVersion=<ver> /DSourceStageDir=<dir>` 从 `VERSION.json` 动态注入版本号，无需手工改写 `.iss`。

### 5.2 GitHub Actions 双产物发布流水线 (`.github/workflows/build.yml`)

升级 CI 构建与 `release` 任务：
- 在 `Windows Release (MSVC x64)` 构建并跑通 CTest 与 `package_release.py` 后，调用 `windows-2022` Runner 预装的 `iscc` 编译 `installer/sakura.iss`（本地若未安装 Inno Setup 则在 `package_release.py` 中打印提示并正常生成绿色 ZIP + 便携 ZIP，不阻塞本地打包；CI 强制校验 `.exe` 与 `.zip` 双产物生成）。
- `release` Job 下载并重命名双产物：
  - `Sakura-${TAG}-win64-setup.exe`（标准向导安装版）
  - `Sakura-${TAG}-win64.zip`（解压即玩免安装版）
  - `Sakura-${TAG}-SHA256SUMS.txt`（双产物 SHA256 校验文件）
- 当 `TAG` 无 `-dev` / `-alpha` / `-beta` 后缀（如 `v1.0.0`）时，自动标记 `prerelease: false` 成为仓库 Latest Official Release。

### 5.3 正式版全量边界压测与文档定稿

- **异常与压力回归测试 (`tests/test_v1_stress.cpp`)**：
  - 空谱面、零音符难度、超长高密度谱面（`1500+` 音符含密集 Hold/Slider/Link/Synergy）、畸形 UTF-8 与截断 JSON、SQLite 损坏自动备份重建、缺失封面/音频/视频优雅回退全覆盖。
- **文档与指南全面对齐**：
  - 更新 `README.md`（移除“仍处于 Alpha 阶段”文案，补充 v3 空格横梁与组合技按键说明、安装版与绿色版获取方式）。
  - 更新 `doc/PLAYER_GUIDE.md`、`doc/CHART_FORMAT_SPEC.md`、`doc/ARCHITECTURE.md`、`THIRD_PARTY_NOTICES.md`，并输出 `doc/RELEASE_REVIEW_1.0.0.md` 正式版验收报告。

### 5.4 `v1.0.0` 验收清单
- 执行 `python scripts/version_hook_entry.py tag clear` 清空预发布通道。
- 执行 `python scripts/autopilot_release.py --release --target-tag v1.0.0 --channel stable --bump major`，验证远端 GitHub Release 同时挂载 `Sakura-v1.0.0-win64-setup.exe`、`Sakura-v1.0.0-win64.zip` 与 `SHA256SUMS.txt` 且 `isPrerelease == false`。
