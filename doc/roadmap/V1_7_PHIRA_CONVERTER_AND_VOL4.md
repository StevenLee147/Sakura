# Phase v1.7 详细规格：Phira / osu! / StepMania 智能谱面转换器与曲包 Vol.04

覆盖版本：`v1.7.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 跨格式双区智能谱面转换引擎 (`src/game/chart_converter.*`)

在纯 C++20 跨平台层（纳入 `sakura-game-logic`）实现外部音游谱面包一键转换为 Sakura Chart v4 双区谱面：

### 1.1 Phira `.pez` 原生解析与双区映射（核心重点）
- 支持直接读取 `.pez` ZIP 归档：
  - 解析 `info.yml` / `info.txt` / `info.csv` 提取曲名、曲师、谱师、音乐文件、曲绘文件与定级。
  - 双格式谱面兼容解析：
    1. **Phigros Official JSON (`formatVersion` 1 / 3)**：解析 `judgeLineList`、`bpm`、`speedEvents`、`notesAbove` / `notesBelow`；
    2. **RPE JSON (Re:PhiEdit)**：解析 `BPMList`（拍数转毫秒）、`judgeLineList` 事件层与 `notes`（`type` 1=Tap, 2=Hold, 3=Flick, 4=Drag，`startTime`/`endTime` 带分数三元组 `[bar, num, den]`）。
  - **双区智能分流策略**：
    - 主节奏 Tap / Hold 按归一化横坐标量化映射至左屏 4K 轨道，多押重音与双押 Flick 智能注入 `Space` 樱枝横梁；
    - Drag / Flick 流、副旋律点位及运动判定线高光音符映射至右屏鼠标 Circle / Slider，并自动过一遍 `v1.3` 的 $\Delta t$ 自适应间距防重叠优化器与跨区同押 `linkId` 绑定，确保转换后谱面兼具原作神韵与极佳双区手感。

### 1.2 osu! `.osz` 与 StepMania `.sm` 转换
- `.osz`：解析 `.osu`（`osu!mania` 4K~7K 轨道折叠/分流映射 + `osu!standard` HitCircle/Slider 右屏映射与左屏节拍伴奏生成）。
- `.sm`：解析 `#BPM`、`#OFFSET`、`#NOTES`（`dance-single` 四轨直映 + 强拍跨区鼠标协同点缀）。

### 1.3 拖拽与向导无缝接入
- 支持将 `.pez` / `.osz` / `.sm` 直接拖入 `SceneSelect` 窗口或通过 `SceneChartWizard` 选择文件一键导入用户曲库，立即可玩。

---

## 2. 官方精选原创曲包 Vol.04（6 首 Chart v4 新曲）

- 新增 `scripts/build_pack_vol4.py`（确定性多声部立体声合成 + $\Delta t$ 自适应制谱 + Chart v4 演出事件 + 矢量封面），扩充 6 首涵盖 Artcore、J-Core、Future Bass、 Oriental Electro 曲风的三难度新曲，使内置总曲库突破 21 首。

---

## 3. 验收清单
- `tests/` 新增 Phira Official JSON、Phira RPE JSON（分数拍号与多判定线）、`.osz`、`.sm` 内存包端到端转换与合法性校验单测。
- 自动发布 `v1.7.0` 并验证安装包体积与全曲库可玩性审计通过。
