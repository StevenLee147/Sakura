# Phase v2.0 详细规格：游戏内静默热更新自重启、B30/R10 生涯名片与 2.0 正式收官

覆盖版本：`v2.0.0`（通道：`stable`，版本钩子升级：`major`）。

---

## 1. 游戏内静默下载热更新与原子自重启 (`src/core/updater.*`)

将现有仅打开浏览器的 `Updater` 升级为完整的客户端内建自更新闭环：
- **匹配发行资产**：解析 GitHub Releases Latest JSON，自动识别当前运行形态（若程序目录含 `unins000.exe` 则为安装版，优先匹配 `-win64-setup.exe` 或便携包；便携版匹配 `-win64.zip`）以及同 Release 下的 `SHA256SUMS.txt`。
- **后台流式下载与校验**：通过 `IHttpTransport` 流式下载至 `%TEMP%/SakuraUpdate/`，在设置页与主菜单更新弹窗实时渲染下载进度条（MB/s 与百分比），下载完成后严格校验 SHA-256 摘要，篡改或残缺包立即中止并清理。
- **原子热替换与自重启守护**：
  - 便携版：生成带 PID 等待锁与失败回滚保护的 PowerShell 更新脚本（等待当前 `Sakura.exe` 退出 → 备份旧核心文件 → 覆盖新程序与 `resources/` 且完整保留 `userdata/` → 自动拉起新版 `Sakura.exe`）。
  - 安装版：校验签名摘要后带 `/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS` 参数拉起 Inno Setup 升级器无缝覆盖重启。

---

## 2. 玩家潜力值 Rating（B30 + R10）与生涯名片导出

### 2.1 双区潜力值算法 (`PPCalculator` 扩展)
- 单曲定数成绩换算单曲潜力值（结合曲目小数定数 `level`、准确率曲线与同押/连携完成度），计算玩家综合 **Sakura Rating**：
  - **Best 30 (B30)**：不同谱面难度历史最佳前 30 名均值（权重 75%）；
  - **Recent 10 (R10)**：最近有效正式游玩前 10 佳表现（权重 25%，防刷分退化保护）。

### 2.2 生涯档案与高清 PNG 战绩名片导出 (`SceneStats` & `SceneMenu`)
- 主菜单右上角与 `SceneStats` 展示玩家精确两位小数 Rating 星阶徽章（青樱 / 银月 / 鎏金 / 绯虹 / 极星）、最高段位称号与 B30 成绩矩阵视图。
- 支持在 `SceneStats` 点击「导出 B30 生涯名片」，离线渲染一张 1920×1080 月夜墨樱风格高清 PNG 战绩长图至 `userdata/exports/` 并一键打开目录分享。
- 服务端账号资料与排行榜同步展示玩家最新 `rating` 与段位徽章。

---

## 3. `v2.0.0` 终极收官验收清单
- `tests/` 新增 SHA256SUMS 解析校验、更新器热替换脚本生成测试、B30/R10 Rating 边界单测及全模块回归。
- 全面刷新 `README.md`、`doc/ROADMAP.md`、`doc/PLAYER_GUIDE.md` 与 `doc/ARCHITECTURE.md` 至 2.0 正式版终态。
- 通过 `autopilot_release.py --release --target-tag v2.0.0 --channel stable --bump major` 完成双产物发布与 `steven-lee.tech` 生产服终态同步。
