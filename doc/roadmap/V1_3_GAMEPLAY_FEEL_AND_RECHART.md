# Phase v1.3 详细规格：核心手感重塑、自适应谱面重制与显示一致性修复

覆盖版本：`v1.3.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 全场景难度与小数等级显示一致性修复

### 1.1 根因定位
- `SceneSelect` 左侧列表卡片固定读取 `c.difficulties.back()`，未随当前选中难度同步；且双击未选中曲目时 `Select(idx)` 将难度重置为 `0`。
- 等级数值格式化不统一：`SceneSelect` 用 `setprecision(0)` 四舍五入（`14.5 -> 15`），`SceneGame` 用 `static_cast<int>` 截断（`14.5 -> 14`），`SceneResult` 抹去 `.5` 定数。

### 1.2 实施规范
- 在 `src/game/chart_utils.h/.cpp` 新增统一格式化函数：
  - `std::string FormatDifficultyLevel(float level)`：若 `|level - round(level)| < 0.05f` 输出整数（如 `7`），否则保留一位小数（如 `7.5`、`14.5`）。
  - `std::string FormatDifficultyBadge(std::string_view name, float level)`：统一输出 `<Name> · Lv.<Level>`。
- `SceneSelect`：
  - 选中曲目卡片实时展示当前选中 `m_difficulty` 的难度名与 `FormatDifficultyLevel`；非选中卡片展示当前排序锚点或最高难度，并在切换选中曲目时按就近难度档位（同名难度优先，其次最接近等级）平滑承接，杜绝双击开局跳回 Easy/Normal 的割裂感。
  - 右侧难度胶囊按钮统一使用 `FormatDifficultyLevel`。
- `SceneGame` 与 `SceneResult`（含 `Tab` 全屏深度分析页）：全部改用 `FormatDifficultyLevel`，确保选曲、局内 HUD、结算三处难度名与等级 100% 一致。

---

## 2. `R` 键三连击防误触重开机制

- 在 `SceneGame` 新增 `m_retryTapCount`（`0..3`）与 `m_retryWindowTimer`（`1.5s` 滑动时间窗）：
  - 第 1 次按下 `kKeyRetry`（默认 `R`）：`m_retryTapCount = 1`，重置 `1.5s` 计时器，顶部 HUD 胶囊浮现提示 `再按 2 次 R 确认重开 (1/3)` 并播放轻提示音。
  - `1.5s` 内第 2 次按下：`m_retryTapCount = 2`，刷新计时器，提示更新为 `再按 1 次 R 确认重开 (2/3)`。
  - `1.5s` 内第 3 次按下：立即调用 `Retry()` 重新开始当前谱面。
  - 超过 `1.5s` 未继续按下则自动归零隐藏提示，彻底消除高速游玩误触重开。

---

## 3. 消除左手命中下落残影 & 双区强打击感升级

### 3.1 左手锁线晶化消散（根治下落残影）
- `SceneGame::RenderKeyboardNotes`：
  - 当 `note.type == NoteType::Tap && note.isJudged`：
    - 若 `note.result != JudgeResult::Miss`（即有效命中）：渲染纵坐标强制锁死在 `JUDGE_LINE_Y`，严禁继续随 `CalcNoteRenderY` 穿过判定线下坠；同时在 `80ms` 内执行横向延展（`1.0x -> 1.38x`）、纵向压扁（`1.0x -> 0.25x`）的高亮金粉闪光消散。
    - 仅 `JudgeResult::Miss` 的漏击音符保持原轨迹暗化下坠，提供清晰的「接住 vs 漏掉」物理反馈。

### 3.2 左右手强打击视觉特效 (`SceneGame` + `ParticlePresets`)
- 新增 `HitShockwave` 瞬态冲击波结构（归一化坐标渲染）：
  - 左手命中：在对应轨道判定线处生成双层扩散光环 + 轨道瞬态上冲光柱（高度 `0.26f`，`90ms` 衰减）+ 判定线局部弹性高亮脉冲 + `24~34` 颗高初速晶屑与樱瓣迸射。
  - 右手命中：在圆心生成急速外扩涟漪双环 + 中心十字星芒闪光（Starburst）+ `26~38` 颗径向爆裂粒子；`Perfect` 与 `Synergy` 附带微幅高频清脆镜头反馈（受 `reduced_motion` 开关约束）。

### 3.3 多瞬态清脆打击音效 (`SfxGenerator`)
- 升级 `src/audio/sfx_generator.cpp` 合成器（提升音频缓存版本号自动重生成）：
  - 叠加 `0~4ms` 高频木槌/晶石敲击瞬态头（Transient Click, `2.4kHz~4.8kHz` 指数包络）+ 基频八度泛音共鸣体 + 短促立体感空气噪声尾音，使左右手 Tap / Circle / ThumbBar / Synergy 各自拥有干净利落、穿透BGM的打击反馈。

---

## 4. 左手多压与跨区同押视觉提示

- **左手同押横梁（Chord Tie-Bar）**：在 `RenderKeyboardNotes` 预扫描活跃窗口内时间差 `<= 6ms` 的未判定键盘音符组（2 押及以上），在音符间底部绘制鎏金连接横梁，并为同押音符外框叠加醒目的双层暖金高亮描边。
- **左右手同拍双押自动连线**：即使谱面未显式标记 `linkId`，只要左手音符与右手音符判定时间差 `<= 8ms`，自动绘制跨区半透明流光同步弧线，并在右手圆圈外围绘制同押双环标记。
- **右手连续跟随引导线**：当相邻两个未判定鼠标音符间隔 `<= 900ms` 时，按时间先后绘制渐隐流光虚线箭头，辅助视线提前落位。

---

## 5. 右手 $\Delta t$ 自适应空间排布算法与全曲库重制

- 重构 `scripts/build_song_pack.py`、`scripts/build_starter_album.py` 与 `scripts/build_sakura_breath.py`：
  - **右手跳距随间隔正相关舒展**：设与上一鼠标音符结束时间间隔为 $\Delta t$（ms），目标归一化跳距为：
    $$d(\Delta t) = \operatorname{clamp}\left(0.14 + 0.50 \cdot \left(1 - e^{-\Delta t / 620}\right),\; 0.14,\; 0.68\right)$$
    短间隔快连点保持舒适微移，长间隔大拍跨度自动拉开至全屏远端，彻底消除扎堆拥挤。
  - **接近圈防重叠排斥**：新音符候选点对前 `1200ms` 内仍处于预读期的音符执行最小欧氏距离排斥（`dist >= 0.18`），全区域（`x, y in [0.16, 0.84]`）均匀覆盖。
  - **左手键型人性化**：依据鼓点/旋律能量生成自然的阶梯（Stairs）、交互（Trills）、强拍双押（Chords）与副歌大拇指横梁，严禁非预期的同轨死连纵连。
- 全量重生成 15 首内置曲目谱面，并在 `tests/test_chart_quality.cpp` 新增全曲库可玩性门禁测试。

---

## 6. 验收清单
- 新增单测：`FormatDifficultyLevel` 边界测试、`R` 键三连击状态机测试、同押分组检测测试、全曲库 15 首谱面空间间距与元数据一致性审计测试。
- 通过 `autopilot_release.py --release --target-tag v1.3.0 --channel stable --bump minor` 完成 GitHub Release 双产物发布与线上服同步校验。
