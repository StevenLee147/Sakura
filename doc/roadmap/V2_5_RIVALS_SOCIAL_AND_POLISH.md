# Phase v2.5 详细规格：玩家社交档案、劲敌（Rivals）实时比拼、全球 Rating 天梯与交互打磨

覆盖版本：`v2.5.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 服务端玩家档案、劲敌关注关系与全球 Sakura Rating 天梯 (`server/`)

### 1.1 幂等数据库迁移与存储层契约 (`server/src/storage/sqlite_adapter.ts`, `types.ts`, `index.ts`)
- **`users` 表幂等列扩展**：
  - 服务端启动初始化时通过 `PRAGMA table_info(users)` 检测并幂等补齐两列（完全保留存量线上账号与成绩数据）：
    - `bio TEXT NOT NULL DEFAULT ''`：玩家个性签名（UTF-8 最长 `120` 字符，服务端统一去除控制字符并裁剪首尾空白）；
    - `featured_title TEXT NOT NULL DEFAULT ''`：玩家展示头衔/称号（UTF-8 最长 `32` 字符，如段位认定头衔或专属成就称号）。
- **幂等社交关系表 `user_follows`**：
  ```sql
  CREATE TABLE IF NOT EXISTS user_follows (
    follower_id TEXT NOT NULL,
    target_id   TEXT NOT NULL,
    created_at  INTEGER NOT NULL,
    PRIMARY KEY (follower_id, target_id),
    FOREIGN KEY (follower_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (target_id)   REFERENCES users(id) ON DELETE CASCADE
  );
  CREATE INDEX IF NOT EXISTS idx_user_follows_follower ON user_follows(follower_id, created_at DESC);
  CREATE INDEX IF NOT EXISTS idx_user_follows_target   ON user_follows(target_id);
  ```
- **`IStorageAdapter` 与 `PostgresCompatibleAdapter` 同步扩展**：
  - 新增 `getUserProfile(targetUserId, viewerUserId?)`、`updateUserProfile(userId, { bio?, featuredTitle? })`、`addRival(userId, targetUserId)`、`removeRival(userId, targetUserId)`、`listRivals(userId)`、`getGlobalRatingLadder(limit, viewerUserId?)`，并为 `getLeaderboard(chartId, difficulty, limit, currentUserId?, scope?)` 增加 `scope: "global" | "rivals"` 过滤能力。

### 1.2 REST API 路由与 JSON 契约 (`server/src/app.ts`)
1. **`GET /api/v1/users/:id/profile`**（公开可读，可选 Bearer Token 识别访问者视角；`:id` 为 `"me"` 时映射为当前鉴权用户）：
   - 返回结构：
     - 基础身份与社交统计：`user_id`、`username`、`is_guest`、`bio`、`featured_title`、`dan_badge`、`sakura_rating`、`total_pp`、`play_count`（累计有效成绩数）、`follower_count`（粉丝/被关注数）、`rival_count`（劲敌关注数）、`is_rival`（当前登录者是否已将其列为劲敌）、`is_mutual`（双方是否互为劲敌）、`created_at`；
     - **`best_scores`（B15 最佳单曲代表作数组）**：按单曲潜力值 `chart_rating` 降序取该玩家不同 `(chart_id, difficulty)` 历史最佳前 15 条记录（每项含 `score_id`、`chart_id`、`chart_title`、`difficulty`、`difficulty_level`、`score`、`accuracy`、`max_combo`、`grade`、`pp`、`chart_rating`、`is_full_combo`、`is_all_perfect`、`has_replay`、`played_at`）。
2. **`PATCH /api/v1/users/me/profile`**（需 Bearer 鉴权）：
   - 接收 `{ bio?: string, featured_title?: string }`；校验字符长度（`bio <= 120`、`featured_title <= 32`），更新成功后返回最新个人资料对象。
3. **`POST /api/v1/rivals/:id`**（需 Bearer 鉴权，幂等添加劲敌）：
   - 拦截自关注（`follower_id === target_id` 返回 `400`），目标用户不存在返回 `404`，单人关注上限 `100` 人保护（超限返回 `422`）；使用 `INSERT OR IGNORE INTO user_follows` 保证重复请求完全幂等，返回 `{ ok: true, is_rival: true, is_mutual, rival_count, follower_count }`。
4. **`DELETE /api/v1/rivals/:id`**（需 Bearer 鉴权，幂等取消劲敌）：
   - 执行 `DELETE FROM user_follows WHERE follower_id = ? AND target_id = ?`，无论此前是否关注均幂等返回 `{ ok: true, is_rival: false, rival_count, follower_count }`。
5. **`GET /api/v1/rivals`**（需 Bearer 鉴权）：
   - 返回当前玩家的全部劲敌列表（按 `sakura_rating DESC, total_pp DESC` 排序，每项含 `user_id`、`username`、`bio`、`featured_title`、`dan_badge`、`sakura_rating`、`total_pp`、`play_count`、`is_mutual`、`added_at`）。
6. **`GET /api/v1/rankings/rating`**（支持 `?limit=100`，可选 Bearer 鉴权）：
   - 返回全球 **Sakura Rating Top 100 天梯榜**（过滤零成绩空白游客号，按 `sakura_rating DESC, total_pp DESC, play_count DESC` 排序，含 `rank`、`user_id`、`username`、`featured_title`、`dan_badge`、`sakura_rating`、`total_pp`、`play_count`、`is_rival`），并在鉴权有效时一并返回请求者自身的 `my_rank` 与 `my_rating`。
7. **`GET /api/v1/leaderboards/:chartId?difficulty=...&scope=rivals`**（扩展谱面排行榜）：
   - `scope` 缺省或为 `"global"` 时保持原有全服排行榜行为不变；
   - 当 `scope=rivals` 时（要求有效 Bearer 鉴权，否则返回 `401`），仅检索 `{ currentUserId } ∪ { rivalIds }` 集合内玩家在该谱面难度下的最佳成绩，在圈内重排 `rank = 1..N` 并计算圈内 `my_rank`。

---

## 2. 客户端 `ProfileCardModal` 名片弹窗与劲敌实时比拼体验 (`src/net/api_client.*`, `src/ui/profile_card_modal.*`, `src/scene/scene_select.*`, `src/scene/scene_stats.*`, `src/scene/scene_arena.*`)

### 2.1 客户端网络契约与目标劲敌配置 (`src/net/api_client.*`, `src/core/config.*`)
- **`ConfigKeys` 新增持久化键**：
  - `ConfigKeys::kTargetRivalId = "online.target_rival_id"`（`string`，默认 `""`）；
  - `ConfigKeys::kTargetRivalName = "online.target_rival_name"`（`string`，默认 `""`）。
- **`APIClient` 同步与异步接口对扩展**：
  - 新增数据结构 `UserProfileScoreItem`、`UserProfileData`、`UserProfileResult`、`RivalSummaryItem`、`RivalListResult`、`RatingLadderRow`、`RatingLadderResult`；
  - 新增同步 + `*Async` 方法：`FetchUserProfile`、`UpdateMyProfile`、`AddRival`、`RemoveRival`、`FetchRivals`、`FetchRatingLadder`；
  - 扩展 `FetchLeaderboard` / `FetchLeaderboardAsync` 增加 `std::string_view scope = "global"` 参数，向下完全兼容现有调用点。

### 2.2 通用月夜墨樱名片模态框 `ProfileCardModal` (`src/ui/profile_card_modal.*`)
- **视觉与归一化布局规范**：
  - 构建高内聚 UI 模态控制器 `sakura::ui::ProfileCardModal`，严格采用 `0.0~1.0` 屏幕相对比例计算遮罩、居中琉璃主面板（如宽 `0.66 * W`、高 `0.76 * H`）、圆角描边、字号与按钮热区，完美适配 `1080p / 1440p / 4K`。
  - 延续「月夜墨樱与流光琉璃（Moonlit Ink-Sakura）」美术风格：深靛半透明背景压暗遮罩、樱粉鎏金渐变顶栏、根据玩家 `sakuraRating` 星阶动态变色的光环徽章。
- **核心信息与交互闭环**：
  - **名片头部**：展示昵称、`featured_title` 称号铭牌、`dan_badge` 段位章、精确两位小数 `Sakura Rating` 与星阶名称、游玩次数、粉丝/劲敌计数（互关显示「宿命劲敌 · Mutual」流光标签）及个人签名 `bio`。
  - **B15 最佳成绩代表作滚动区**：展示该玩家线上 Top 15 代表谱面成绩（曲名、难度与一位小数定数、得分、准确率、评级徽章、回放可用图标）。
  - **底部三大快捷动作**（查看非本人名片时）：
    1. **「加为劲敌 / 已是劲敌（点击移除）」**：即时调用 `AddRivalAsync` / `RemoveRivalAsync`，乐观刷新按钮状态并同步通知宿主场景更新劲敌缓存；
    2. **「设为目标劲敌 (Target Rival) / 取消锁定」**：将该玩家写入 `online.target_rival_id` 并缓存其代表成绩与同曲分差数据；
    3. **「挑战幽灵回放 (Challenge Replay Ghost)」**：针对当前选中含 `has_replay` 的代表作（若本地已安装该 `chartId`），调用 `DownloadReplayAsync` 拉取 `.skr` 回放至 `userdata/replays/rivals/`，自动配置幽灵伴跑对手并切入对应谱面开局。
  - **全场景无缝唤起**：
    - 在 `SceneSelect` 点击全球榜或劲敌榜任意玩家行；
    - 在 `SceneStats` 点击全球 Rating 天梯榜或我的劲敌列表任意条目；
    - 在 `SceneArena` 点击多人对战房间成员槽位或赛后领奖台玩家卡片；
    - 均统一唤起 `ProfileCardModal::Open(userId)` 异步加载并丝滑展开动画。

### 2.3 `SceneSelect` 三态排行榜与歌曲卡片 `VS Rival +/-` 实时分差胶囊
- **三态排行榜切换器**：
  - 将 `SceneSelect` 排行榜数据源开关扩展为 **`本地 / 全球 / 劲敌`** 三段式切换：
    - `本地`：读取本地 SQLite `Database` 历史成绩；
    - `全球`：请求 `GET /api/v1/leaderboards/:chartId?difficulty=...&scope=global`；
    - `劲敌`：请求 `GET /api/v1/leaderboards/:chartId?difficulty=...&scope=rivals`（展示本人与众劲敌的圈内名次对决；离线或未登录时展示清晰状态提示与快捷登录入口）。
- **歌曲卡片与详情区 `VS Rival +/-` 分差胶囊（Score Delta Pill）**：
  - 当玩家已设置 Target Rival（或在「劲敌」榜单下获取到该谱面难度排名最接近的劲敌成绩 `rivalScore`）且本地存在个人最佳成绩 `myBestScore` 时，在当前选中歌曲卡片右侧与谱面信息头部绘制醒目的相对比例胶囊徽标：
    - `delta = myBestScore - rivalScore >= 0`：翠绿鎏金底框渲染 `VS <RivalName> +14,200`；
    - `delta < 0`：绯樱玫红底框渲染 `VS <RivalName> -6,850`（若该条劲敌成绩附带线上回放，支持一键挂载为本局 `GhostRunner` 实时追赶目标）。

### 2.4 `SceneStats` 全球 Top 100 天梯、劲敌页签与个人资料编辑器
- **新增社交与竞技双页签**：
  - `SceneStats` 顶部导航扩展 **「全球天梯」** 与 **「劲敌名录」** 标签页：
    - **全球天梯页**：展示 `GET /api/v1/rankings/rating` 全球前 100 名高分玩家（名次、昵称、称号、段位章、Sakura Rating、游玩次数与劲敌标记），底部固定横条高亮当前登录玩家的全球排位；
    - **劲敌名录页**：展示已关注的全部劲敌卡片、与本人的 Rating 差值对比、当前锁定的 Target Rival 星标状态及快捷移除/查看名片按钮。
- **个人签名与展示称号编辑**：
  - 在 `SceneStats` 个人生涯主页提供 `bio` 输入框与 `featured_title` 选择/编辑控件（可从已达成的樱华道场段位、高难成就头衔中快捷点选或自由键入），点击保存后通过 `UpdateMyProfileAsync` 同步云端，并在离线导出 `1920×1080` B30 生涯战绩名片 PNG 时一并绘制玩家专属称号与签名。

---

## 3. 并行子智能体切分与 `v2.5.0` 验收清单

### 3.1 互斥文件域并行建议
1. **在线服务端域**：独占 `server/src/*`、`server/tests/*`。
2. **客户端网络与配置域**：独占 `src/net/api_client.*`、`src/core/config.*`。
3. **名片模态组件域**：独占 `src/ui/profile_card_modal.*`。
4. **选歌与多人房间联动域**：独占 `src/scene/scene_select.*`、`src/scene/scene_arena.*`。
5. **生涯统计天梯与名片导出域**：独占 `src/scene/scene_stats.*`、`tests/*`、`doc/*`。

### 3.2 门禁验收标准
- `server/tests/` 新增：玩家档案查询、`bio`/`featured_title` 边界校验、`POST/DELETE /api/v1/rivals/:id` 幂等性与自关注拦截、全球 Top 100 Rating 天梯排序及 `scope=rivals` 圈内排行榜筛选测试。
- `tests/`（C++ 单测）新增：`APIClient` 模拟传输层下的档案/劲敌/天梯序列化与反序列化测试、Target Rival 分差计算边界测试。
- `Sakura.exe --verify-online` 扩展双账号端到端社交验证：Player A 更新签名与称号 → Player B 查看 A 名片（校验 B15 与称号）并添加 A 为劲敌 → 双方拉取 `scope=rivals` 谱面榜与全球 Rating 天梯核对名次一致。
- 通过 `autopilot_release.py --release --target-tag v2.5.0 --channel stable --bump minor` 完成双产物发布与 `steven-lee.tech` 生产服同步。
