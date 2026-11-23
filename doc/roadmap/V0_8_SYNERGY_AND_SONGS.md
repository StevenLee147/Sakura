# Phase v0.8 详细规格：空格横梁、键鼠组合技与官方曲包扩充

覆盖版本：`v0.8.0-alpha`（玩法进化 & Chart v3）与 `v0.8.1-alpha`（通用音频分析制谱管线 & 官方精选曲包）。

---

## Release 1：`v0.8.0-alpha` — 空格横梁 + 键鼠组合技系统 & Chart Format v3

### 1.1 Chart Format v3 数据规范（100% 向后兼容 v1/v2）

在 `src/game/note.h`、`src/game/chart.h`、`src/game/chart_loader.cpp` 与 `doc/CHART_FORMAT_SPEC.md` 中扩展：

- **格式版本**：`version: 3`（加载器继续无损支持 `version: 1` 与 `version: 2`）。
- **第五轨道大拇指横梁 (`lane = 4`)**：
  - `keyboard_notes` 的 `lane` 取值范围由 `0..3` 扩展为 `0..4`。
  - `lane == 4` 代表横跨左侧四轨底部的 **樱枝横梁（Thumb Bar）**，默认绑定 `SDL_SCANCODE_SPACE`（配置键 `input.key_thumb`，支持 `SceneSettings` 操作页自定义按键与冲突检测）。
  - 支持 `type: "tap"` 与 `type: "hold"`（含 `duration`）。
- **跨区连携同押音符 (`link_id`)**：
  - `KeyboardNote` 与 `MouseNote` 均新增可选整型字段 `linkId = 0`（JSON 字段 `"link_id"`，`>0` 表示属于同一连携组）。
  - 同一 `linkId` 绑定同时间戳（容差 `<= 5ms`）的左区键盘音符与右区鼠标音符。
  - 渲染时用 `Renderer::DrawCatmullRomRibbon` 绘制横跨左右操作区的鎏金樱粉能量丝带；当同一 `linkId` 内所有关联音符均取得 `Great` 及以上判定时，触发 **Link Resonance（双区共鸣）** 额外奖分与跨区绽放粒子。
- **樱绽组合技音符 (`require_lane` / `synergy`)**：
  - `MouseNote` 新增可选字段 `requireLane = -1`（JSON 字段 `"require_lane"`，取值 `-1`=无要求，`0..3`=需按住对应 A/S/D/F 轨，`4`=需按住 Space 横梁，`5`=鼠标右键协同触发）。
  - 渲染时在鼠标音符圆心上方/内圈绘制清晰的键位徽记（`A`/`S`/`D`/`F`/`SPACE`/`RMB`）与鎏金双层缩圈。
  - 判定时若对应修饰键处于按下状态（或在 `±65ms` 协同窗口内同步按下），触发 **Sakura Burst（樱绽连携）**，获得组合技加分与专属命中音效；若未按对应修饰键仅单点鼠标，最高降级为 `Good` 并提示修饰键漏按，保证高阶挑战性与可读性平衡。

### 1.2 Sakura Bloom 满开槽（Fever 系统）

在 `PlaySession` 与 `ScoreCalculator` 中实现纯确定性（不依赖帧率或随机数）的满开状态机：
- **充能规则**：每次 `Perfect` / `Great` 增加基础充能，完成 `Link` 共鸣或 `Synergy` 樱绽组合技获得双倍充能；`Miss` 扣除部分未激活充能。
- **激活与表现**：充能达 `100%` 时自动进入持续 `8000ms`（谱面时钟）的 **Sakura Bloom（樱满开）** 状态：
  - 期间连击加分倍率额外提升（总分仍通过理论满分归一化封顶 `1,000,000`，确保全 AP 恒等于 `1,000,000` 分，不会溢出）。
  - `SceneGame` 渲染水墨鎏金舞台边框呼吸辉光与双倍樱瓣飘落（若开启「减少动态效果」则仅显示柔和静态金边指示条）。

### 1.3 回放 v2、教程第 6 课、PP 与成就

- **Replay v2 (`src/game/replay.h/.cpp`)**：
  - `SessionInput` 支持 `lane = 4`（Space 横梁）及 `MouseRightDown` / `MouseRightUp`，序列化版本号升至 `2`，完整兼容读取 v1 `.skr` 回放并保持原谱面与原回放分数逐位一致。
- **新手教程第 6 课 (`src/game/tutorial_data.cpp` & `src/scene/scene_tutorial.cpp`)**：
  - 在现有 5 课基础上新增第 6 课「樱枝横梁与键鼠组合技」，依次引导玩家体验 Space 横梁、左右手 Link 连携同押与 `SPACE + Circle` 樱绽组合技。
- **PP 与成就 (`pp_calculator.cpp`、`config/achievements.json`)**：
  - `PPCalculator` 根据谱面中 `lane 4`、`linkId`、`requireLane` 占比给予最高 `+8%` 协同复杂度系数（纯 v2 谱面系数为 `1.0`，保持历史成绩完全兼容）。
  - 新增成就：`first_synergy`（首次达成樱绽连携）、`bloom_master`（单局触发 2 次樱满开）。

### 1.4 `v0.8.0-alpha` 验收清单
- 新增 `tests/test_synergy_v3.cpp`：覆盖 Chart v3 解析/序列化、Space Tap/Hold、Link 共鸣判定、Synergy 修饰键判定、Fever 归一化百万分满分不变性、Replay v2 确定性重放。
- 升级 `src/main.cpp` `--smoke-test` 验证教程 6 课与 v3 谱面自动演示零 Miss。
- 执行 `python scripts/autopilot_release.py --release --target-tag v0.8.0-alpha --channel alpha --bump minor` 完成远端发版验证。

---

## Release 2：`v0.8.1-alpha` — 通用音频分析制谱管线 & 官方精选曲包

### 2.1 通用自动制谱与封面生成脚本 (`scripts/build_song_pack.py`)

提炼并升级 `scripts/build_sakura_breath.py` 的确定性信号处理架构，构建支持批量曲目配置的 `scripts/build_song_pack.py`：
- **音频转码与网格对齐**：调用 `ffmpeg` 将 `download/songs/` 下指定源音频转码为统一响度与采样率的 `music.ogg`，通过瞬态包络自相关与节拍追踪测定精确 `BPM` 与首拍偏移 `offset`。
- **左右手分频编排 + v3 组合技**：
  - 左手节奏（STFT + HPSS 打击乐分离）：底鼓/Sub-bass → A 轨，军鼓 → F 轨，合成器切分/踩镲 → S/D 轨，乐句重拍与副歌前奏铺垫 → **Space 樱枝横梁 (`lane = 4`)**。
  - 右手旋律（谐波显著度音高追踪）：主旋律起音 → Circle，长音延留 → 平滑多节点 Slider（移速严格受难度阈值约束）。
  - 双区协奏（Chart v3）：在 Hard / Expert 难度的 Drop 强拍自动生成跨区 `link_id` 同押，在乐句收束高潮点生成 `require_lane = 4`（Space + 鼠标）或对应轨道键的樱绽组合技音符。
- **程序化高清封面 (`cover.png`)**：基于每首曲目的色板主题（夜樱绯红、月白流光、苍蓝星轨、鎏金疾风等）程序化渲染 `1024x1024` 几何水墨樱花视觉封面。
- **全自动质量门禁**：逐张谱面运行时间单调性、同轨重叠、键鼠最大瞬时跨度与理论难度等级（Lv.2.5 ~ Lv.14.5）自动定级校验。

### 2.2 精选入库曲单（保留全部既有曲目）

从 `download/songs/` 精选 8 首风格互补曲目入库（连同已有《樱落呼吸》、4 首原创合成曲与 2 首训练曲，内置曲库达 **15 首曲目、37+ 张谱面**）：
1. `sakura_intro` — 《序章 · 夜樱初启》（源自 `【Intro】.mp3`，Easy / Normal，迎新序曲）
2. `sakura_awakening` — 《樱光初醒》（Normal / Hard / Expert）
3. `instant_bloom` — 《一瞬花绽》（Normal / Hard / Expert）
4. `unfallen_light` — 《樱光未落》（Normal / Hard / Expert）
5. `flowing_petals` — 《樱落流光》（Normal / Hard / Expert）
6. `sakura_memories` — 《Sakura Memories》（Normal / Hard / Expert）
7. `distant_star` — 《遥远的星》（Normal / Hard / Expert）
8. `sakura_rush` — 《樱语疾行》（Hard / Expert，高速进阶）
9. `petal_storm_overdrive` — 《Petal Storm: Overdrive》（Hard / Expert Lv.14+，终极魔王曲）

更新 `resources/charts/album.json` 曲包分组元数据（支持在选歌界面按「官方精选 / 经典合成 / 教学训练」清晰展示）。

### 2.3 `v0.8.1-alpha` 验收清单
- 扩展 `tests/test_chart_loader_builtin.cpp`：断言全部内置曲目 `info.json`、`music.ogg`/`wav`、`cover.png` 存在且可加载，所有 Hard/Expert 新谱均包含有效 v3 横梁与组合技音符，AutoPlay 全曲 1,000,000 满分零 Miss。
- 执行 `python scripts/autopilot_release.py --release --target-tag v0.8.1-alpha --channel alpha --bump patch` 完成远端发版验证。
