# Phase 5 — 数据与回放 (v0.6)

> Replay 系统、数据导出导入、详细结算报告。

---

### Step 5.1 — Replay 录制

创建：`src/game/replay.h`, `src/game/replay.cpp`

> [PROMPT] ReplayFrame {timeMs, type(KeyDown/KeyUp/MouseDown/MouseUp/MouseMove), lane, mouseX, mouseY}。
> ReplayData {chartId, difficulty, replayVersion, noteSpeed, offset, playedAt, result, frames}。
> ReplayRecorder: BeginRecording / RecordEvent / EndRecording → ReplayData。
> SaveReplay(.skr 二进制+zlib) / LoadReplay。SceneGame 自动录制，结算保存最佳回放。

**验收：** 游玩后生成 .skr 文件，数据完整。

---

### Step 5.2 — Replay 回放

创建：`src/scene/scene_replay.h`, `src/scene/scene_replay.cpp`

> [PROMPT] SceneReplay：复用 SceneGame 渲染，按 ReplayFrame 回放输入(观看模式)。
> HUD "REPLAY" 标记 + 可拖拽进度条。
> 空格暂停/继续, 左右±5s, 上下变速(0.5x/1x/2x), ESC退出。
> 从结算"查看回放"或统计"最佳回放"进入。

**验收：** 回放还原操作，判定结果一致。

---

### Step 5.3 — 数据导出/导入

> [PROMPT] 设置→数据管理→导出: sakura_export.zip(scores/settings/achievements/statistics.json + replays/)。
> 导入: 选择 zip→确认覆盖→解压写入→Toast + 重载。
> 编辑器谱面导出: .sakura 包(zip+自定义扩展名) / 导入到 charts/。

**验收：** 导出→另一实例导入→数据完整恢复。

---

### Step 5.4 — 详细结算报告

> [PROMPT] 结算"详细报告"按钮→全屏覆盖层：
> - 偏差散点图(X=音符时间, Y=偏差ms, Perfect区高亮) + 平均偏差/标准差
> - 判定分布饼图(P/Gr/Go/B/M 环形)
> - 连击走势折线图(标注 Miss 断连)
> - 分段准确率柱状图(4段, 标注薄弱)
> - 键盘/鼠标分离统计 + 各轨道准确率细分
> ESC 关闭。

**验收：** 图表正确渲染，数据与实际一致。

---

**🎯 Phase 5 检查点：** Replay 录制+回放 + 数据导出导入 + 详细报告。完整数据记录和分析。
