# Phase 4 — 内容与体验 (v0.5)

> 新手引导、测试谱面、成就系统、PP 评分、玩家统计、音频可视化。

---

### Step 4.1 — 新手教程

创建：`src/scene/scene_tutorial.h`, `src/scene/scene_tutorial.cpp`

> [PROMPT] SceneTutorial 分 5 课：
> 1. 键盘 Tap — 4个超慢大间隔, 大判定窗口(±200ms), 箭头+按键标注
> 2. 键盘 Hold — 3个, 持续按住进度可视化
> 3. 键盘 Drag — 2个, 箭头指示方向
> 4. 鼠标 Circle — 5个大间隔, 大距离容差(0.1)
> 5. 综合配合 — Tap+Circle 简单交替
> 每课：说明文字→空格开始→失败可重试→进度"1/5"→完成祝贺→回主菜单。
> 首次运行弹出"是否进入教程?"

**验收：** 5 课全部完成，引导清晰。

---

### Step 4.2 — 官方测试谱面

> [PROMPT] resources/charts/ 创建 5 首测试谱面(严格遵循 CHART_FORMAT_SPEC.md v2)：
> 1. **tutorial_song** (BPM100) — Easy Lv.1.0, 纯 Tap 20个
> 2. **spring_breeze** (BPM130) — Normal Lv.3.0(40个) + Hard Lv.5.5(80个)
> 3. **cherry_blossom** (BPM155) — Normal Lv.4.0(50个) + Hard Lv.7.0(100个全类型) + Expert Lv.10.0(200个+SV)
> 4. **digital_dream** (BPM175) — Hard Lv.8.5(120个Drag+Slider) + Expert Lv.12.0(250个+BPM变化)
> 5. **sakura_storm** (BPM200) — Expert Lv.14.0(300+个极限+复杂SV+BPM变化)
> 音乐用静音 WAV 占位(长度匹配)，每首配占位 cover.png。

**验收：** 全部谱面可加载并正常游玩，难度梯度合理。

---

### Step 4.3 — 成就系统

创建：`src/game/achievement_manager.h`, `src/game/achievement_manager.cpp`

> [PROMPT] AchievementManager 单例。achievements.json 定义成就：
> first_play/first_fc/first_ap/combo_100/combo_500/combo_1000 (一次性)
> play_10/play_50/play_100 (累积) / all_s/all_charts (收集) / accuracy_99/score_999k (一次性)
> LoadAchievements + CheckAndUnlock(GameResult) + GetAll/GetUnlocked/GetProgress。
> 解锁时 Toast 金色通知 + 音效 + 写入 Database。

**验收：** 首次游玩触发 first_play，Toast 正确弹出。

---

### Step 4.4 — PP 评分系统

创建：`src/game/pp_calculator.h`, `src/game/pp_calculator.cpp`

> [PROMPT] PPCalculator：
> 单谱 PP = diffLevel^2.5 × 10 × (accuracy/100)^4 × (score/1000000)^2
> 加成：FC×1.05, AP×1.10, ≥99%准确率×1.02
> 总 PP = Σ pp[i] × 0.95^i (降序加权, 同 osu!)
> CalculatePP(GameResult, level) / RecalculateTotal / GetTotalPP / GetBestPlays(limit=20)
> 结算显示本局 PP，统计页显示总 PP。

**验收：** PP 与难度/准确率正相关，总 PP 加权正确。

---

### Step 4.5 — 玩家统计场景

创建：`src/scene/scene_stats.h`, `src/scene/scene_stats.cpp`

> [PROMPT] SceneStats : Scene。从 Database 查询。
> - 左上卡片：总PP/游玩次数/时长/音符数/平均准确率/FC次数/AP次数
> - 右上评级分布：水平柱状图 SS/S/A/B/C/D
> - 左下准确率趋势：折线图最近20局
> - 右下 PP 排行：Top 10 最高 PP (曲名+难度+准确率+PP)
> - 成就入口按钮→弹出成就 ScrollList 覆盖层
> 从主菜单添加"统计"按钮进入。

**验收：** 统计数据正确展示，图表渲染正常。

---

### Step 4.6 — 音频可视化

创建：`src/audio/audio_visualizer.h`, `src/audio/audio_visualizer.cpp`

> [PROMPT] AudioVisualizer：SDL_mixer postmix callback 获取 PCM → 简单频带分析(16~32带) → 快攻慢释平滑。
> 渲染模式：BarMode(竖条) / CircleMode(环形) / WaveMode(波形线条)。
> 集成：SceneGame 判定线下方微弱频谱 / SceneSelect 底部 / SceneMenu 背景装饰。

**验收：** 音频播放时频谱动态跟随节奏。

---

**🎯 Phase 4 检查点：** 教程+5首谱面+成就+PP+统计图表+音频可视化。内容丰富，体验完整。
