# Phase v1.5 详细规格：自定义皮肤/打击音包与 Chart v4 舞台演出系统

覆盖版本：`v1.5.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 自定义皮肤与打击音包系统 (`src/core/skin_manager.*`)

### 1.1 激活 `graphics.skin_path` 完整管线
- 实现跨平台 `SkinManager`（纯配置解析与配色回退逻辑纳入 `sakura-game-logic`）：
  - 扫描内置 `resources/skins/` 与用户目录 `userdata/skins/` 下的 `skin.json`。
  - 支持自定义：左右外轨/内轨晶石配色、大拇指横梁色、鼠标圆圈与滑条流光色、同押横梁色、接近圈样式、判定文字配色，以及独立 `hitsounds/` WAV 覆盖。
- 内置 4 套官方主题皮肤与音效包：
  1. **墨樱流光 (Moonlit Ink-Sakura)**：经典夜樱粉金琉璃 + 清脆晶石打击音
  2. **霓虹赛博 (Neon Cyber-Pulse)**：电光青紫高对比度 + 电子合成瞬态打击音
  3. **竞技极简 (Esports Pure)**：高辨识纯色无干扰几何块 + 干脆木鱼机械轴打击音
  4. **浮世金翠 (Ukiyo Jade-Gold)**：翡翠鎏金古风配色 + 竹韵太鼓打击音
- `SceneSettings`「视觉」页新增皮肤选择器与实时色板/打击音试听预览，热切换即时生效。

---

## 2. Chart Format v4：舞台演出事件与双语同步歌词

### 2.1 数据结构与加载器向后兼容 (`version: 4`)
- `ChartLoader` 完全兼容 `v1 / v2 / v3 / v4`：
  - `stage_events` 数组：支持 `pulse`（BPM 镜头微缩放脉冲）、`vignette`（暗角律动）、`tint`（舞台氛围色调平滑过渡）、`petal_burst`（副歌漫天樱瓣雨速率倍率）。
  - `lyrics` 数组：`{ "time": 12400, "end_time": 16200, "text": "原词", "sub_text": "译文" }`。
- `SceneGame`：
  - 在底部通栏上方优雅渲染渐入渐出双语时间轴歌词（可在设置中开关）；
  - 按 `stage_events` 驱动舞台背景与边框流光演出（开启 `reduced_motion` 时自动抑制脉冲缩放）。
- `SceneEditor`（谱面工房）：新增舞台事件与歌词节点快捷插入/编辑支持，并通过 `ValidateChartData` 严格校验。

---

## 3. 验收清单
- `tests/` 新增 `SkinManager` 扫描/缺失字段回退测试、Chart v4 序列化/反序列化与 v1~v3 兼容性测试。
- 为代表性官方曲目补充 Chart v4 高潮樱瓣演出事件，通过本地四重门禁并发布 `v1.5.0`。
