# Phase v3.0 详细规格：樱华赛季排位赛（Sakura Season Ranked）、官方曲包 Vol.05「镜花里章」与 3.0 正式版终极收官

覆盖版本：`v3.0.0`（通道：`stable`，版本钩子升级：`major`）。

---

## 1. 樱华赛季排位系统 Sakura Season Ranked (`src/game/season.*`, `server/`, `src/net/api_client.*`, `src/scene/scene_arena.*`, `src/scene/scene_menu.*`)

### 1.1 跨平台确定性月度赛季逻辑引擎 (`src/game/season.*` 纳入 `sakura-game-logic`)
- **零 SDL 依赖跨平台实现**：
  - 新建 `src/game/season.h` 与 `src/game/season.cpp` 并注册进根 `CMakeLists.txt` 的 `sakura-game-logic` 静态库（同时在 `server/src/storage/sqlite_adapter.ts` 实现同构确定性算法，确保客户端离线推演与服务端在线裁定 100% 一致）。
- **确定性月度课题轮换（3 阶梯度谱面池）**：
  - 以 UTC+8 自然月标识符 `seasonId`（格式 `"YYYY-MM"`）作为确定性哈希种子，从官方内置曲库中无重复抽取 **3 张当季排位课题谱面**，严格对应三大战术梯度：
    1. **先锋课题 (Vanguard)**：难度定数区间 `6.0 ~ 9.5`（检验基础节奏稳定度与高完美率控分）；
    2. **中坚课题 (Mid)**：难度定数区间 `9.5 ~ 12.5`（检验双区切换流利度、曲线滑条与同押协调）；
    3. **大将课题 (Boss)**：难度定数区间 `12.5 ~ 15.0`（检验高密度多押、`Space` 横梁爆发与键鼠协同极限统治力）。
- **赛季积分 Season Points (SP) 计算公式**：
  - 仅接受标准流速（`std::abs(playbackRate - 1.0f) < 0.01f`）且未开启辅助（`assisted == false`）的有效完赛成绩；
  - 单课题积分 `CalculateStageSP(const GameResult& res, float chartLevel, int slotIndex)`：
    - 结合槽位基准分上限（先锋 `1000 SP`、中坚 `1150 SP`、大将 `1350 SP`，三题满额合计 `3500 SP`）、曲目精确小数定数加成、分段陡峭准确率曲线（`95.0%`、`98.0%`、`99.5%`、`100.0% AP` 逐阶放大精度的区分度）以及 `Full Combo` / 协同音符命中奖励四舍五入或保留整数 SP；
  - 玩家当季总积分 `totalSP = vanguardBestSP + midBestSP + bossBestSP`（三张课题各自历史最高 SP 之和）。
- **五大樱华赛季段位阶梯（5 Rank Tiers）**：
  | 阶位序号 | 段位名称 | 总 SP 晋升门槛 | 视觉主色（月夜墨樱琉璃色板） |
  |---|---|---|---|
  | Tier 1 | **青铜樱** | `0 <= totalSP < 1200` | 暖铜樱褐 `#D69A7E` |
  | Tier 2 | **白银月** | `1200 <= totalSP < 1900` | 霜月银蓝 `#B8D4F2` |
  | Tier 3 | **黄金华** | `1900 <= totalSP < 2500` | 鎏金樱珀 `#FFD87A` |
  | Tier 4 | **绯红霞** | `2500 <= totalSP < 3050` | 绯霞流丹 `#FF6E9C` |
  | Tier 5 | **极星神乐** | `totalSP >= 3050` | 极星虹彩琉璃 `#9EF0FF` / `#FF9CEE` |

### 1.2 服务端赛季存储与三大接口 (`server/`)
- **幂等建表 `season_scores`**：
  ```sql
  CREATE TABLE IF NOT EXISTS season_scores (
    season_id        TEXT NOT NULL,
    user_id          TEXT NOT NULL,
    slot_index       INTEGER NOT NULL,
    chart_id         TEXT NOT NULL,
    difficulty       TEXT NOT NULL,
    difficulty_level REAL NOT NULL,
    score            INTEGER NOT NULL,
    accuracy         REAL NOT NULL,
    max_combo        INTEGER NOT NULL,
    grade            TEXT NOT NULL,
    is_full_combo    INTEGER NOT NULL DEFAULT 0,
    is_all_perfect   INTEGER NOT NULL DEFAULT 0,
    stage_sp         INTEGER NOT NULL,
    replay_data      TEXT,
    updated_at       INTEGER NOT NULL,
    PRIMARY KEY (season_id, user_id, slot_index),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
  );
  CREATE INDEX IF NOT EXISTS idx_season_scores_season_user ON season_scores(season_id, user_id);
  ```
- **REST API 契约 (`server/src/app.ts`)**：
  1. `GET /api/v1/season/current`（可选 Bearer 鉴权）：
     - 返回当前赛季 `season_id`、赛季主题标题、三张课题规格数组 `stages[3]`（含 `slot_index`、`slot_role`: `"vanguard" | "mid" | "boss"`、`chart_id`、`chart_title`、`difficulty`、`difficulty_level`、`max_stage_sp`），以及已登录玩家的个人赛季状态 `my_standing`（各槽位最佳分与 `stage_sp`、汇总 `total_sp`、当前段位 `rank_tier`、下一段位所需差值与当前全球排位 `rank`）。
  2. `POST /api/v1/season/submit`（需 Bearer 鉴权）：
     - 校验提交谱面与难度严格匹配当季 `stages` 中某一槽位，拒绝 `assisted == true` 或变速成绩；服务端复算 `stage_sp`，若刷新该槽位个人最佳（`stage_sp` 更高，或同 `stage_sp` 下 `score` 更高）则覆盖保存，返回 `{ ok: true, season_id, slot_index, stage_sp, total_sp, rank_tier, rank, new_best }`。
  3. `GET /api/v1/season/leaderboard`（支持 `?season_id=...&limit=100`，可选 Bearer 鉴权）：
     - 按 `SUM(stage_sp) DESC, SUM(score) DESC, MAX(updated_at) ASC` 聚合当季全球排位天梯，返回前 100 名玩家的 `rank`、`user_id`、`username`、`featured_title`、`dan_badge`、`total_sp`、`rank_tier` 与三槽位分项 `[vanguard_sp, mid_sp, boss_sp]`，并附带请求者 `my_rank`。

### 1.3 客户端排位大厅、结算晋升演出与主菜单徽章 (`src/scene/scene_arena.*`, `src/scene/scene_menu.*`, `src/scene/scene_result.*`)
- **`SceneArena` 双枢模式升级（全 `0.0~1.0` 归一化布局）**：
  - 顶部置入流光模式切换栏：**「樱华排位赛 (Season Ranked)」** 与 **「多人对战房 (Arena Rooms)」**。
  - 排位赛视图左侧/上部展示当季徽章纹章、玩家当前段位（青铜樱 ～ 极星神乐）、总 `SP` 进度槽及先锋/中坚/大将三张课题谱面卡片（显示曲绘缩略图、小数定数、个人单题最佳分与 `SP`，点击「出击」直接携带赛季标记进入 `SceneGame`）；右侧展示当季全球 SP 排行榜，点击玩家行可无缝打开 `v2.5` 的 `ProfileCardModal` 查看名片或加为劲敌。
- **`SceneMenu` 与 `SceneResult` 闭环**：
  - 主菜单右上角生涯信息区及「竞技场」入口按钮同步渲染当季排位段位铭牌；
  - 赛季课题完赛进入 `SceneResult` 时自动调用 `SubmitSeasonScoreAsync`（离线时基于本地赛季存档计算离线段位并在恢复联网后同步），动态滚动展示本题获得 `SP`、`+ΔSP` 刷新提示及跨段位晋升（如 `黄金华 → 绯红霞`）樱瓣爆发庆典动效。

---

## 2. 官方精选原创曲包 · Vol.05 · 镜花里章 (`scripts/build_pack_vol5.py`, `resources/charts/*`)

### 2.1 铁律：严禁任何程序化合成占位音频（STRICT ZERO SYNTHETIC PLACEHOLDER AUDIO）
- **100% 真实 Suno 第二版编曲音源**：
  - 新增可复现制谱管线脚本 `scripts/build_pack_vol5.py`，严格读取 `download/songs/` 目录中**尚未使用的 6 首真实 Suno 第二版编曲（2nd-Take VIP / Omega Arrangements）`.mp3` 原声母带**，**严禁生成任何正弦波/合成器占位音频文件**：

  | 序号 | `chart_id` | 真实音源文件 (`download/songs/`) | 游戏内曲名 | 难度配置与精确小数定数 |
  |---|---|---|---|---|
  | 1 | `petal_storm_vip` | `Petal Storm_ Overdrive (1).mp3` | `Petal Storm: Overdrive [VIP]` | `Normal 6.5` / `Hard 10.5` / `Expert 14.5` |
  | 2 | `instant_bloom_vip` | `一瞬花绽 (1).mp3` | `一瞬花绽 · 里绽` | `Normal 5.0` / `Hard 8.5` / `Expert 12.0` |
  | 3 | `distant_star_vip` | `遥远的星 (1).mp3` | `遥远的星 · 回响` | `Easy 3.5` / `Normal 6.0` / `Hard 9.5` |
  | 4 | `sakura_breath_vip` | `樱花落下的呼吸 (1).mp3` | `樱花落下的呼吸 · 终章` | `Normal 5.5` / `Hard 9.0` / `Expert 12.5` |
  | 5 | `sakura_sprint_vip` | `樱语疾行 (1).mp3` | `樱语疾行 · 极速` | `Normal 6.5` / `Hard 10.0` / `Expert 13.5` |
  | 6 | `shattered_threshold_omega` | `破碎阈值 (1).mp3` | `破碎阈值 · Ω` | `Hard 11.0` / `Expert 13.8` / **`Master 15.0`**（终极巅峰魔王谱 `master.json`） |

### 2.2 音频转码、STFT/HPSS 双区 Chart v4 精制与全曲库总表更新
- **高保真可复现 Vorbis 转码**：
  - 统一通过 `ffmpeg`（`-v error -nostdin -y -i <src.mp3> -map 0:a:0 -vn -map_metadata -1 -fflags +bitexact -flags:a +bitexact -ac 2 -ar 44100 -c:a libvorbis -q:a 4.5`）输出 `resources/charts/<chart_id>/music.ogg`。
- **STFT HPSS 节拍对齐与双区 Chart Format v4 全特性注入**：
  - 对每首真实音源执行短时傅里叶变换与谐波/打击乐分离（STFT HPSS），提取底鼓/军鼓/踩镲瞬态通量与主旋律音高质心，自相关锁定精确 BPM 与首拍相位偏移 `grid0`，并通过平滑能量峰值检测写入高潮试听起点 `preview_start_ms`（直接驱动 `v2.4` 双碟 22 秒无缝循环预听）。
  - 左手键盘区（4K + `Space` 横梁）：根据局部乐句音高走向与踩镲密度编排交互、顺逆阶梯、受控纵连防误触保护、强拍双押和弦与大拇指 `lane 4` 重音横梁；
  - 右手鼠标区（Circle + 曲线 Slider）：严格执行 $\Delta t$ 自适应空间距离与接近圈防重叠几何约束（`0.16 ~ 0.84` 安全舞台边界内平滑双锚点弧线滑条）；
  - 跨区协同与舞台演出：注入跨区同押 `link_id`、键鼠连携 `require_lane`、副歌动态 SV 变速、对齐小节强拍的 4 类 `stage_events`（`pulse` / `petal_burst` / `tint` / `vignette`）以及契合「镜花里章」世界观的双语同步歌词 `lyrics`；
  - **`Master 15.0` 巅峰魔王谱（`shattered_threshold_omega/master.json`）**：作为全游唯一封顶 `15.0` 定数的终极试炼谱面，设计高强度 16 分复合流、密集跨区同押与高潮段华丽弧线滑条协同，同时严格通过最小同轨间隔与鼠标移动速度上限校验，确保人类高阶玩家真实可攻克且 AutoPlay 100% All Perfect。
- **专属曲绘与 `album.json` 汇总校验**：
  - 为 6 首新曲程序化精绘「镜花里章」镜面暗金与绯樱琉璃视觉主题专属高清 `cover.png`；
  - 更新 `resources/charts/album.json`，使官方内置曲库达到 **21 首 100% 真实 Suno 原创曲目、共 61 张多难度 Chart v4 谱面**（原 `15 首 / 43 谱` + Vol.05 `6 首 / 18 谱` = `21 首 / 61 谱`），并同步被每日挑战与樱华赛季排位课题池纳管。

---

## 3. `v3.0.0` 终极收官文档同步与验收清单

### 3.1 全仓库核心文档升级至 `v3.0.0` 终态
- 同步更新以下核心文档，消除任何旧版本统计口径残留：
  1. `README.md`：更新核心特性矩阵（低延迟热插拔音频后端、10 类双区打击音 + 12 复音防爆池、玩家名片与劲敌系统、樱华赛季排位赛、21 首真实 Suno 原创曲目 / 61 张 Chart v4 谱面）；
  2. `doc/ARCHITECTURE.md`：补充双碟 `AudioManager` 热恢复状态机、`ProfileCardModal` / 劲敌社交数据流、`Season` 跨平台排位模块与服务端 `user_follows` / `season_scores` 表结构说明；
  3. `doc/PLAYER_GUIDE.md`：新增音频缓冲延迟调优指南、劲敌锁定与幽灵挑战教程、樱华五阶赛季段位（青铜樱 ～ 极星神乐）晋升规则及 Vol.05「镜花里章」（含 `Master 15.0`）曲目导览；
  4. `doc/ROADMAP.md` 与 `doc/roadmap/AUTOPILOT_PLAYBOOK.md`：完整登记 `v2.4.0`、`v2.5.0`、`v3.0.0` 里程碑闭环状态。

### 3.2 互斥文件域并行建议
1. **赛季核心逻辑域**：独占 `src/game/season.*`、根 `CMakeLists.txt`。
2. **曲包 Vol.05 生成与谱面资产域**：独占 `scripts/build_pack_vol5.py`、`resources/charts/*`。
3. **服务端赛季接口域**：独占 `server/src/*`、`server/tests/*`。
4. **客户端网络与排位 UI 场景域**：独占 `src/net/api_client.*`、`src/scene/scene_arena.*`、`src/scene/scene_menu.*`、`src/scene/scene_result.*`。
5. **全量审计单测与 3.0 终态文档域**：独占 `tests/*`、`README.md`、`doc/*`。

### 3.3 门禁验收标准
- `tests/` 新增：
  - `Season` 月度课题确定性轮换（先锋/中坚/大将定数区间不重叠断言）、`CalculateStageSP` 单调性与辅助/变速拦截、5 阶段位晋升阈值边界单测；
  - 全曲库 **21 首曲目 / 61 张谱面** 完整性审计单测（断言每首 `music.ogg` 为真实 Vorbis 音频且时长充足、零合成 WAV 占位曲、`ChartLoader` 零告警加载、`ValidateChartData` 零错误、`shattered_threshold_omega` 含 `Master 15.0` 且 `PlaySession` AutoPlay 100% AP 通关）。
- `server/tests/` 与 `Sakura.exe --verify-online` 覆盖 `/api/v1/season/current`、`/api/v1/season/submit`、`/api/v1/season/leaderboard` 全流程联调。
- 通过 `autopilot_release.py --release --target-tag v3.0.0 --channel stable --bump major` 完成双产物正式发布与 `steven-lee.tech` 生产服 `v3.0.0` 终态同步。
