# Phase v1.1 ~ v1.2 详细规格：在线生态（双模服务端、排行榜、谱面市场与云同步）

覆盖版本：`v1.1.0`（双模服务端骨架、自动更新、账号与全球排行榜）与 `v1.2.0`（在线谱面市场、工房一键发布与多端云存档同步）。

---

## Release 6：`v1.1.0` — 内建双模服务端、WinHTTP 网络层、自动更新与全球排行榜

### 6.1 仓库内建双模服务端 (`server/`)

在仓库根目录创建轻量、零外部云账号即可本地闭环运行的 `server/` 工程（Node.js 20+ / TypeScript + Hono）：
- **存储抽象层 (`server/src/storage/`)**：默认使用本地 SQLite + 本地文件目录（开箱即用支持本地开发与 GitHub Actions E2E 实服联调），同时提供标准 PostgreSQL / Cloudflare D1 + R2 驱动接口以便生产环境一键上云。
- **核心 REST API (`v1.1.0`)**：
  - `GET /api/v1/health` & `GET /api/v1/version`：服务健康检查与最新版本元数据。
  - `POST /api/v1/auth/guest`、`POST /api/v1/auth/register`、`POST /api/v1/auth/login`、`GET /api/v1/auth/me`：PBKDF2/Scrypt 密码哈希 + HMAC-SHA256 JWT 鉴权，支持一键免注册访客模式。
  - `POST /api/v1/scores`：提交正式游玩成绩与可选 `.skr` 回放摘要；服务端校验 `chart_hash`、音符总数一致、百万分上限与 PP 公式复算，拒绝辅助模式 (`assisted == true`) 或篡改分值。
  - `GET /api/v1/leaderboards/:chartId?difficulty=Expert&scope=global&limit=50`：返回单曲难度全球高分榜（每人取最高分记录）及玩家个人排名。
  - `GET /api/v1/replays/:scoreId`：下载对应上榜记录的 `.skr` 回放数据直接进 `SceneGame` 观看。

### 6.2 客户端网络层与自动更新 (`src/net/` & `src/core/updater.h/.cpp`)

- **可测跨平台 HTTP 抽象 (`src/net/http_client.h/.cpp`、`api_client.h/.cpp`)**：
  - 定义纯 C++20 `IHttpTransport` 接口并纳入 `sakura-game-logic`（Linux CI 与单元测试注入内存 Mock Transport；Windows 主程序使用系统内置 `winhttp.lib` 原生支持系统代理与 TLS 1.2/1.3 HTTPS，不引入 OpenSSL 编译体积）。
  - 后台线程池异步执行请求，主线程通过线程安全回调队列消费响应，绝不卡顿渲染帧；断网时显示离线徽标并正常游玩全部本地内容，待传成绩写入本地待同步队列。
- **自动更新检查器 (`src/core/updater.h/.cpp`)**：
  - 启动后后台异步请求 GitHub Releases Latest API（`https://api.github.com/repos/StevenLee147/Sakura/releases/latest`）或自定义更新源，解析 SemVer；若有新版本则弹出非打断式夜樱 Toast，并在设置「数据/关于」页提供版本详情与一键下载/打开 Release 页按钮。
- **游戏内交互接入**：
  - 主菜单右上角新增账号/在线状态胶囊按钮（支持访客一键生成昵称、注册/登录、服务器地址配置默认本地/官方可切）。
  - `SceneSelect` 右侧详情区新增「本地最佳 / 全球排行」切换标签，展示 Top 榜并支持点击条目观看线上回放；`SceneResult` 正式结算后自动异步上报成绩并显示全球排名变化。

### 6.3 `v1.1.0` 自动化端到端联调门禁
- `server/` 自带 `npm test` 接口单元测试。
- 主程序新增无头联调模式 `Sakura.exe --verify-online <http://127.0.0.1:port>`：真实走通 WinHTTP 注册登录 → 提交正式局成绩与 `.skr` 回放 → 拉取排行榜核对名次与分数 → 下载回放并重放校验逐帧一致。
- `.github/workflows/build.yml` 新增 `test-online` 步骤（后台启动 `server/` 并运行 `--verify-online`）；通过 `autopilot_release.py` 自动发布 `v1.1.0`。

---

## Release 7：`v1.2.0` — 在线谱面市场 (`SceneMarket`)、工房直传与多端云同步

### 7.1 服务端谱面市场与云同步扩展 (`server/`)

- **谱面市场 API**：
  - `POST /api/v1/charts`：校验登录态，接收 `.sakura` 谱面包上传，服务端解包核验 `info.json` 与各难度 JSON 格式合法性、计算内容 `chart_hash`、提取难度范围/BPM/作者元数据入库。
  - `GET /api/v1/charts?q=&minLevel=&maxLevel=&sort=popular|newest|rating&page=1`：关键词、难度区间与标签筛选。
  - `GET /api/v1/charts/:id/download`：流式下发 `.sakura` 包并累加下载计数（客户端校验 SHA256）。
  - `POST /api/v1/charts/:id/rate`：1~5 星评分与简评（每用户每谱幂等更新）。
- **云存档同步 API**：
  - `POST /api/v1/sync/push` & `GET /api/v1/sync/pull`：同步个人历史最高成绩、成就解锁记录与键位/延迟/流速核心设置；采用幂等合并策略（成绩取同 `chart_hash + difficulty` 最高分，成就取最早解锁时间并集）。

### 7.2 客户端谱面市场场景与工房发布 (`src/scene/scene_market.h/.cpp`)

- **谱面市场 (`SceneMarket`)**：
  - 从主菜单与选歌界面均可进入，保持月夜墨樱毛玻璃卡片视觉与 `0.0~1.0` 归一化布局。
  - 支持搜索框、排序切换（最新 / 热门 / 高分）、难度滑块过滤、谱面详情与难度徽章预览、星级评价，以及带实时进度条的「一键下载并安装」（自动校验 SHA256 解压至用户目录 `charts/`，标记「已安装 / 可更新」，点击即可直接开玩）。
- **谱面工房一键发布 (`SceneEditor`)**：
  - 工房在谱面保存并通过 `ValidateChartData` 校验后，已登录用户可点击「发布至市场」直接打包 `.sakura` 上传更新。
- **设置页云同步 (`SceneSettings`)**：
  - 「数据」页新增「立即云端同步」与「启动/结算自动云同步」开关，展示最近同步时间与合并摘要 Toast，离线时安全回退不报错。

### 7.3 `v1.2.0` 验收清单
- 扩展 `Sakura.exe --verify-online` 实服端到端回归：自动完成「工房导出并上传 `.sakura` 新谱面 → `SceneMarket` 接口检索并评分 → 模拟新用户目录下载安装该谱面 → 游玩上榜 → 触发云同步拉取恢复全部成绩/成就/设置」。
- 更新 `README.md`、`doc/PLAYER_GUIDE.md` 与 `doc/ARCHITECTURE.md` 在线生态章节。
- 执行 `python scripts/autopilot_release.py --release --target-tag v1.2.0 --channel stable --bump minor` 完成全部七版本终极发版闭环。
