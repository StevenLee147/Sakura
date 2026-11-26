# Phase v2.2 详细规格：Keep-Alive 传输层、双工作线程池、SWR/ETag 缓存、离线队列 v2 与多人对战实时增强

覆盖版本：`v2.2.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. `WinHttpTransport` Keep-Alive 连接池、压缩与流取消 (`src/net/http_client.h` / `src/net/http_client.cpp`)

### 1.1 传输层瓶颈分析与重构目标
现有 `WinHttpTransport::Send` 每次请求均重新执行 `WinHttpOpen` → `WinHttpConnect` → 完整 TCP + TLS 握手，未开启 HTTP 压缩，且在下载大体积谱面包或回放时缺乏中途取消机制，切出场景或关闭游戏时容易产生阻塞等待。

### 1.2 请求取消令牌 `CancelToken` 与接口扩展
在 `sakura::net` 命名空间扩展 `HttpRequest` 与 `HttpResponse`：
```cpp
namespace sakura::net
{

class CancelToken
{
public:
    void Cancel() noexcept { m_cancelled.store(true, std::memory_order_release); }
    bool IsCancelled() const noexcept { return m_cancelled.load(std::memory_order_acquire); }

private:
    std::atomic<bool> m_cancelled{ false };
};

struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int         timeoutMs = 10000;
    std::shared_ptr<CancelToken> cancelToken;
    std::function<void(std::size_t downloadedBytes, std::size_t totalBytes)> onProgress;
};

struct HttpResponse
{
    int         statusCode = 0;
    bool        cancelled  = false;
    std::string body;
    std::string error;
    std::vector<std::pair<std::string, std::string>> headers;

    bool IsSuccess() const { return statusCode >= 200 && statusCode < 300; }
    bool IsNotModified() const { return statusCode == 304; }
    bool IsNetworkError() const { return statusCode == 0 && !cancelled; }
    std::string GetHeader(std::string_view name) const;
};

} // namespace sakura::net
```
- **流式分块取消**：`WinHttpTransport::Send` 在发起请求前、收到响应头后以及每次 `WinHttpQueryDataAvailable` / `WinHttpReadData` 循环迭代前检查 `request.cancelToken->IsCancelled()`；一旦置位，立即关闭当前 `hRequest` 句柄并返回 `cancelled = true`，绝不阻塞工作线程回收。

### 1.3 持久化 `HINTERNET` 会话与 `(host, port, secure)` 连接池
- **双持久 Session 句柄**：`WinHttpTransport` 实例生命周期内惰性创建并复用两个持久 `WinHttpOpen` 会话句柄（一个 `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` 用于公网域名如 `steven-lee.tech:233`，一个 `WINHTTP_ACCESS_TYPE_NO_PROXY` 用于本地回环 `127.0.0.1` / `localhost` / `::1`），析构时统一释放。
- **线程安全连接池**：
  - 以 `OriginKey{ std::string lowerHost; uint16_t port; bool secure; }` 为键，通过 `m_poolMutex` 维护 `HINTERNET` 连接句柄池（`WinHttpConnect`），上限 `MAX_POOLED_ORIGINS = 8`，超限或空闲超时按 LRU 淘汰，使连续 API 调用直接复用底层 Keep-Alive TLS 连接。
- **全自动 Gzip/Deflate 解压**：
  - 在 `hRequest` 上通过 `WinHttpSetOption` 设置 `WINHTTP_OPTION_DECOMPRESSION` 为 `WINHTTP_DECOMPRESSION_FLAG_ALL`（涵盖 `GZIP` 与 `DEFLATE`），大幅降低排行榜、谱面市场目录与云同步 JSON 的传输体积。
- **陈旧 Keep-Alive 断连单次自动重连**：
  - 当服务端空闲回收空闲 TCP 连接导致复用的连接在 `WinHttpSendRequest` 或 `WinHttpReceiveResponse` 阶段返回 `ERROR_WINHTTP_CONNECTION_ERROR`、`ERROR_WINHTTP_RESEND_REQUEST` 或 `ERROR_WINHTTP_CANNOT_CONNECT`（且请求尚未被取消）时，自动将失效的 `OriginKey` 连接句柄逐出连接池，重建全新 `WinHttpConnect` 并**透明重试 1 次**。

---

## 2. 双工作线程池与请求序号防抖 (`src/net/api_client.h` / `src/net/api_client.cpp`)

### 2.1 交互池与传输池物理分离
将原有单线程 `m_worker` 拆分为两个独立调度的有界线程池，彻底消除大文件下载阻塞高频交互请求的问题：

| 线程池名称 | 线程数 | 负责的请求类型 | 调度优先级特性 |
|---|---|---|---|
| **交互线程池 (`InteractivePool`)** | `2` 线程 | 登录/刷新鉴权、选歌排行榜拉取、今日挑战查询、成绩即时提交、`SceneArena` 房间状态轮询与局内进度同步 | 低延迟优先，支持按 `requestSeq` 跳过已作废任务 |
| **传输线程池 (`TransferPool`)** | `2` 线程 | 谱面市场 `.sakura` 包下载与发布上传、线上幽灵/回放 `.skr` 下载、`SyncCloudSave` 全量同步、`OfflineQueue` 后台冲刷 | 吞吐流式传输，实时上报全局字节进度供 UI 顶栏展示 |

### 2.2 单调递增 `requestSeq` 取消与 `180ms` 切歌排行榜防抖
- **`RequestSeq` 机制**：
  - `APIClient` 提供 `uint64_t NextRequestSeq()` 及按业务通道（如 `LeaderboardChannel`、`MarketQueryChannel`）绑定的最新序号表与关联 `CancelToken`。
  - 新请求入队时自动触发同通道旧请求 `CancelToken::Cancel()`；任务在出队执行前及主线程 `PumpCallbacks()` 派发回调前二次核对序号，过期结果直接丢弃，杜绝慢响应覆盖新选曲排行榜的竞态。
- **`180ms` 切歌防抖**：
  - 玩家在 `SceneSelect` 按住方向键或快速滚轮切歌时，不立即发出网络请求，而是重置 `180ms` 防抖计时器（若命中本地 `OnlineCache` 热缓存则立即零延迟渲染缓存快照，后台再按防抖窗口发起条件校验），将连续滑过 20 首曲目的排行榜请求合并为最终停留曲目的 1 次请求。

---

## 3. `OnlineCache` SWR + ETag 缓存与 `OfflineQueue` v2

将新增的 `src/net/online_cache.h/.cpp` 与 `src/net/offline_queue.h/.cpp` 纳入 `sakura-game-logic` 静态库。

### 3.1 `OnlineCache`（Stale-While-Revalidate + HTTP `ETag`）
- **双层存储架构**：
  - 内存哈希表（`std::mutex` 线程安全） + 磁盘持久化缓存 `Paths::User("cache/online_cache.json")`（通过 `AtomicWrite` 落盘，限制最大条目数 `256` 条，按 LRU 淘汰）。
  - 每条记录结构：`key`、`etag`、`payloadJson`、`updatedAt`、`softTtlSec`、`hardTtlSec`。
- **SWR 读取与条件请求流程**：
  1. 查询目标键（如 `leaderboard:<chartId>:<diff>`、`daily:<dateUtc8>`、`market:<queryHash>`）：若命中且未超过 `hardTtlSec`，立即向 UI 返回缓存数据以实现瞬时呈现。
  2. 若当前时间未超过 `updatedAt + softTtlSec`，无需发起网络请求。
  3. 若已超过 `softTtlSec`（处于 Stale 窗口），后台线程携带请求头 `If-None-Match: <etag>` 发起条件校验：
     - **服务端改造 (`server/src/app.ts`)**：为 `GET /api/v1/leaderboards/:chartId`、`GET /api/v1/daily` 与 `GET /api/v1/charts` 计算响应摘要并输出 `ETag` 响应头；当请求头 `If-None-Match` 匹配时直接返回 **`304 Not Modified`**（空响应体）。
     - 客户端收到 `304 Not Modified` 时仅刷新本地 `updatedAt`，零 JSON 解析与带宽开销；收到 `200 OK` 时更新 `etag`、`payloadJson` 并触发 UI 增量刷新。
  4. 当玩家本地提交某谱面成绩或今日挑战成绩成功后，主动将对应谱面/挑战键标记为 Stale，确保紧随其后的榜单刷新拉取最新名次。

### 3.2 `OfflineQueue` v2 统一离线动作队列 (`src/net/offline_queue.*`)
- **存储路径与无缝自动迁移**：
  - 统一持久化至 `Paths::User("cache/pending_actions.json")`（使用 `AtomicWrite`）。
  - **兼容迁移**：启动加载时自动检测旧版 `pending_scores.json`；若存在则将其全部条目无损转换为 `PendingActionType::StandardScore` 并合入 `pending_actions.json`，随后安全清理旧文件。
- **支持的三类离线动作 (`PendingActionType`)**：
  1. `StandardScore`：常规打歌成绩与关联本地回放路径（携带确定性幂等键 `client_submission_id` 防重复计分）；
  2. `DailyScore`：今日挑战成绩（绑定挑战日期 `dateUtc8` 与载荷）；
  3. `DanCourseRecord`：樱华道场段位认定通关成绩与段位徽章更新记录。
- **指数退避后台冲刷（Exponential Backoff）**：
  - 每条动作维护 `id`、`type`、`payloadJson`、`attemptCount`、`nextRetryAt`、`createdAt`、`lastError`。
  - 退避公式：`delaySec = std::min(300, 5 * (1 << std::min(attemptCount, 6)))`。
  - 在传输线程池按周期及每次网络恢复在线时扫描到期条目冲刷：遇到 `2xx` 移除条目；遇到 `401` 交由 `APIClient` 单飞刷新令牌后重试；遇到网络异常或 `5xx` 递增 `attemptCount` 等待下次退避窗口；遇到不可恢复的 `400/422` 协议校验错误记录日志后隔离移除，防止坏包永久堵塞队列。

---

## 4. `SceneArena` 增量同步、断线保位、快捷短语与缺谱一键下载

### 4.1 服务端多人对战房间协议升级 (`server/src/app.ts`)
1. **单调递增房间版本号 `revision` 与增量轮询**：
   - 每个房间维护整数 `revision`（建房初始为 `1`），任何成员进出、换歌、准备、开局、局内分数上报、快捷短语发送或房主移交均使 `revision + 1`。
   - `GET /api/v1/rooms/:code/state?since_revision=<n>`：同时刷新调用方 `last_seen_at` 心跳；若房间当前 `revision == n`，直接返回轻量 `{ "ok": true, "unchanged": true, "revision": n }`（或 `304`），客户端跳过状态重建。
2. **心跳检测、`60s` 断线席位保留与自动重连**：
   - 每位房间成员记录 `last_seen_at` 与 `connected` 布尔态（超过 `12s` 无心跳标记为 `connected: false`）。
   - **60 秒保位窗口**：成员断网闪退后，服务端在 **60 秒内**保留其房间座位、准备态及局内已上报的分数/连击；该玩家在 60 秒内重新调用 `JoinArenaRoom`、`FetchArenaRoomState` 或 `ReportArenaProgress` 即可无缝恢复 `connected: true` 继续比赛。
   - **超时清理与自动房主顺延**：超过 `60s` 仍未恢复连接时移除该成员；若被移除（或主动离开）的玩家是当前 `host_id`，服务端自动将最早加入且在线的成员提升为新 `host_id`，绝不留下无主死房。
3. **6 条预设快捷短语环状缓冲 (`POST /api/v1/rooms/:code/chat`)**：
   - 支持固定安全短语索引 `phrase_id`（`0~5`）：
     - `0`: `"请多指教！"`
     - `1`: `"准备好了，随时开局！"`
     - `2`: `"稍等，正在同步房间谱面~"`
     - `3`: `"这首定数好高，手下留情！"`
     - `4`: `"精彩演奏！"`
     - `5`: `"再来一局！"`
   - 房间内维护容量为 `16` 的 `messages` 环形队列（含 `id`、`user_id`、`username`、`phrase_id`、`text`、`sent_at`），内置单用户 `1.5s` 发言冷却限流，随房间状态增量下发。
4. **`market_chart_id` 谱面市场联动**：
   - 房主建房或调用 `POST /api/v1/rooms/:code/select` 切换曲目时，支持上传可选 `market_chart_id`；若客户端未传，服务端自动按 `chart_hash` 或 `chart_id` 在 `market_charts` 表中检索匹配的在线谱面，并在 `ArenaRoomState` 中下发 `market_chart_id` 与 `package_size`。

### 4.2 客户端 `SceneArena` 缺谱一键自动下载与局内实时排名浮层
- **缺谱自动检测与一键下载安装 (`src/scene/scene_arena.*`)**：
  - 房客加入房间或房主切换选曲时，客户端对比本地曲库的 `chartId` 与 `chartHash`。
  - 若本地缺失该谱面且 `room.marketChartId` 非空：
    - 房间状态区醒目提示「本地缺少房主所选谱面，可从谱面市场直接同步」，主操作按钮自动变为 **「一键下载并安装谱面」**（支持勾选房间内自动同步开关）；
    - 点击后通过 `APIClient` 传输线程池下载 `.sakura` 包至 `Paths::User("charts/")`，在房间面板内实时渲染归一化下载进度条（百分比与速率），并自动发送短语 `"稍等，正在同步房间谱面~"`；
    - 安装完成并自动重扫曲库校验 `chartHash` 一致后，按钮无缝恢复为「准备」！若该曲目为未上架市场的纯本地自制谱，则明确提示「房主使用的是未发布自制谱，请让房主先发布至谱面市场或更换曲目」。
- **房间快捷短语栏**：在 `SceneArena` 房间视图底部提供 6 个紧凑短语按钮与最新消息气泡滚动条。
- **`SceneGame` 紧凑实时对战名次浮层 (`src/scene/scene_game.*`)**：
  - 多人房间对局期间，在屏幕左侧安全边缘（归一化区域 `{0.014f, 0.24f, 0.146f, 0.28f}`，不遮挡 4K 轨道与右侧鼠标判定区）渲染半透明月夜毛玻璃实时名次卡：
  - 展示房间前 4 名及本机当前名次（`#1` 鎏金高亮）、实时滚动分数、与第一名或前一名分差（领先 `+XXXX` / 落后 `-XXXX`）、连击徽章及断线重连标记，名次变化时附带平滑纵向位移动画。

---

## 5. 测试矩阵与验收清单

### 5.1 C++ 单元测试 (`tests/test_v220_transport_and_arena.cpp`)
1. **传输取消与重连**：验证 `CancelToken` 在流式分块回调中途触发后立即中止请求并返回 `cancelled == true`；模拟陈旧连接首次发送失败后自动重连一次成功。
2. **双池隔离与序号防抖**：在传输池注入阻塞式模拟大文件下载任务的同时，断言交互池的排行榜与鉴权任务在毫秒级内并发完成；验证旧 `requestSeq` 回调被安全丢弃。
3. **`OnlineCache` SWR + `304`**：验证软 TTL 内零网络请求、Stale 窗口内携带 `If-None-Match` 并在收到 `304 Not Modified` 时保留原缓存正文且刷新时间戳、断网时回退硬 TTL 缓存。
4. **`OfflineQueue` v2 迁移与退避**：验证旧版 `pending_scores.json` 无损迁移至 `pending_actions.json`，三类动作（常规分、今日挑战、段位认定）持久化与指数退避重试间隔计算准确。

### 5.2 服务端与联机验证
- **`server/tests/server.test.ts`**：覆盖 `/leaderboards`、`/daily`、`/charts` 的 `ETag` 与 `304` 响应；验证房间 `since_revision` 增量返回、模拟成员失联 60 秒内重连保位、超时后房主自动顺延、快捷短语限流与环形缓冲、`market_chart_id` 自动关联。
- **`Sakura.exe --verify-online`**：模拟房主选定已上架市场的谱面 → 缺失该谱面的房客通过 `market_chart_id` 自动拉取安装 → 双方发送快捷短语、准备开局、同步局内进度、模拟短暂断线重连并完赛核对最终领奖台名次。
