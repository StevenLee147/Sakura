# Phase v1.6 详细规格：幽灵对手同屏竞速与在线多人对战大厅

覆盖版本：`v1.6.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 单人幽灵对手同屏实时竞速 (`src/game/ghost_runner.*`)

### 1.1 确定性幽灵伴跑引擎 (`sakura-game-logic`)
- 实现轻量 `GhostRunner`：内部持有一个独立的只读 `PlaySession`，以当前歌曲时钟 `now` 同步推进目标 `.skr` 回放（可选：个人历史最佳 PB 回放，或排行榜下载的线上玩家回放）。
- 实时产出：当前时间点幽灵累计分数 `ghostScore`、实时分差 `deltaScore = myScore - ghostScore`、幽灵连击数，以及鼠标区归一化幽灵坐标 `(ghostX, ghostY)`。

### 1.2 局内竞速 HUD 与幽灵光标 (`SceneGame` & `SceneSelect`)
- `SceneSelect` 新增「幽灵竞速：关 / 个人最佳 / 线上对手」切换开关。
- `SceneGame`：
  - 顶部积分区下方渲染双向动态领跑条（领先显示鎏金翠绿 `+XXXX`，落后显示绯樱粉 `-XXXX`）；
  - 右侧鼠标区绘制柔和半透明幽灵光标与拖尾，直观学习顶尖玩家走位而不遮挡本机接近圈。

---

## 2. 在线多人对战房间系统 (`SceneArena` & `server/`)

### 2.1 服务端多人房间接口 (`server/src/`)
- 新增内存+持久化混合房间管理：
  - `POST /api/v1/rooms`（创建房间，返回 6 位房间码）、`GET /api/v1/rooms`（公开房间列表）、`POST /api/v1/rooms/:code/join`、`POST /api/v1/rooms/:code/leave`。
  - `POST /api/v1/rooms/:code/select`（房主切换曲目/难度/谱面哈希）、`POST /api/v1/rooms/:code/ready`（准备/开局状态切换）。
  - `POST /api/v1/rooms/:code/progress` & `GET /api/v1/rooms/:code/state`：局内高频轻量同步房间全员实时分数、准确率、连击与完赛结算结果，最终生成同场名次榜。

### 2.2 客户端对战大厅与同场竞技 (`src/scene/scene_arena.*`)
- 主菜单新增「多人对战」进入 `SceneArena`：支持房间列表浏览、输入 6 位房间码秒进、房主选歌、全员准备倒计时同步进入 `SceneGame`。
- 局内左侧边缘紧凑展示房间内前 4 名玩家实时名次与分差卡片，完赛后进入多人领奖台结算。

---

## 3. 验收清单
- `tests/` 新增 `GhostRunner` 逐毫秒分差与光标轨迹一致性测试。
- `Sakura.exe --verify-online` 扩展双客户端模拟联机测试：Player A 建房选曲 → Player B 加入准备 → 同步开局上报实时进度 → 完赛核对双方最终名次。
- 自动发布 `v1.6.0` 并通过 SSH 更新 `steven-lee.tech` 生产服。
