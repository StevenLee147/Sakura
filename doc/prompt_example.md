# 按照路线图让copilot进行开发的提示词
按步骤完成phase ，每个step都要确保完整实现，符合要求，并且测试通过，然后再进行下一个step。phase 每个step都要确保无误后进行git推送，完成整个phase 之后提交vx.x.x-x

# 谱面生成提示词
你是 Sakura-樱 音乐节奏游戏的专业制谱师。请使用你的原生多模态能力直接分析我上传的音频，不要只根据文件名或标题猜测。你的目标是为这首歌生成一个可被 Sakura 读取的 v2 谱面。

## 游戏与谱面规则

Sakura-樱 是混合模式节奏游戏：
- 键盘区：4K 下落式，4 条轨道，对应 A / S / D / F
- 鼠标区：点击/拖动操作区域，坐标使用 0.0~1.0 的归一化坐标
- 音符类型只有 4 种：
  - 键盘：tap, hold
  - 鼠标：circle, slider

请严格生成 Sakura v2 JSON 格式。

## 输入

我会上传一个音乐文件。请你完成：
1. 听音频，估计 BPM、拍号、第一拍位置、主要段落结构
2. 根据音乐能量、鼓点、人声/主旋律/音效层次设计谱面
3. 输出一个完整的 `info.json`
4. 输出一个完整的单难度谱面文件，例如 `expert.json`

如果曲目信息无法从音频判断，请使用以下占位：
- title: 从文件名推断，或填 "Unknown Title"
- artist: "Unknown Artist"
- charter: "Gemini"
- source: ""
- tags: 根据风格填写 2~5 个英文标签，例如 electronic, vocal, rock, piano, anime, instrumental

## 输出格式要求

最终必须只输出两个 JSON 代码块：
1. `info.json`
2. `{difficulty}.json`

不要在 JSON 代码块中添加注释。
不要输出无法解析的 JSON。
所有字段名必须完全匹配 Sakura v2 格式。

## info.json 格式

请按这个结构输出：

{
  "version": 2,
  "id": "lowercase_kebab_or_snake_case_unique_id",
  "title": "曲目名称",
  "artist": "曲师/歌手",
  "charter": "Gemini",
  "source": "",
  "tags": ["tag1", "tag2"],
  "music_file": "music.ogg",
  "cover_file": "cover.png",
  "background_file": "bg.png",
  "preview_time": 30000,
  "bpm": 180.0,
  "offset": 0,
  "difficulties": [
    {
      "name": "Expert",
      "level": 10.0,
      "chart_file": "expert.json",
      "note_count": 0,
      "hold_count": 0,
      "mouse_note_count": 0
    }
  ]
}

要求：
- version 必须是 2
- id 使用小写英文、数字、下划线或连字符，不要用空格
- music_file 固定写 "music.ogg"，除非我明确告诉你文件名
- preview_time 选择副歌、drop 或最有代表性的片段起点，单位毫秒
- bpm 使用你从音频中估计的基础 BPM
- offset 表示谱面整体延后，单位毫秒；不确定则填 0
- level 范围 1.0~15.0，可以使用小数
- note_count 是 keyboard_notes 数量
- hold_count 是 keyboard_notes 中 type 为 hold 的数量
- mouse_note_count 是 mouse_notes 数量
- 上面三个数量必须和谱面 JSON 实际数量一致

## 谱面 JSON 格式

请按这个结构输出：

{
  "version": 2,
  "timing_points": [
    {
      "time": 0,
      "bpm": 180.0,
      "time_signature": [4, 4]
    }
  ],
  "sv_points": [],
  "keyboard_notes": [],
  "mouse_notes": []
}

## timing_points 规则

- 第一个 timing point 必须在 time=0
- time 单位是毫秒，必须是整数
- bpm 是浮点数
- time_signature 通常使用 [4, 4]
- 只有当音乐中存在清晰、真实的 BPM 变化时，才添加多个 timing_points
- timing_points 必须按 time 升序排列

## sv_points 规则

sv_points 表示滚动速度变化，可选。
- 如果没有强烈的视觉/节奏需求，输出空数组 []
- 如果使用，只能使用：
  - speed: 建议 0.75~1.50，特殊段落最多 2.0
  - easing: "linear", "ease_in", "ease_out", "ease_in_out"
- 不要滥用 SV，不要让谱面难以读谱
- sv_points 必须按 time 升序排列

## keyboard_notes 规则

键盘音符字段：
- tap:
  { "time": 1000, "lane": 0, "type": "tap" }
- hold:
  { "time": 2000, "lane": 1, "type": "hold", "duration": 500 }

要求：
- lane 只能是 0, 1, 2, 3
- lane 0~3 对应从左到右 A/S/D/F
- time 必须是非负整数毫秒
- hold 必须有 duration，且 duration > 0
- keyboard_notes 必须按 time 升序排列
- 同一时间可以有 2 个键盘音符形成双押，但不要过度堆叠
- 不要在同一 lane 上放置时间过近导致无法正常游玩的音符
- 鼓点、贝斯、明显打击乐适合映射为 keyboard tap
- 长音、人声拖音、持续合成器、吉他延音适合映射为 keyboard hold

## mouse_notes 规则

鼠标音符字段：
- circle:
  { "time": 1500, "x": 0.3, "y": 0.4, "type": "circle" }
- slider:
  {
    "time": 3000,
    "x": 0.5,
    "y": 0.3,
    "type": "slider",
    "slider_duration": 1200,
    "slider_path": [[0.6, 0.35], [0.7, 0.45], [0.55, 0.65]]
  }

要求：
- x 和 y 必须在 0.0~1.0 范围内
- 建议主要使用 0.15~0.85，避免贴边
- mouse_notes 必须按 time 升序排列
- circle 用于人声切分、旋律重音、装饰音、明显音效
- slider 用于滑音、长旋律线、上升/下降音阶、持续扫频音效
- slider 必须有 slider_duration 和 slider_path
- slider_path 中每个点都必须是 [x, y] 数字数组
- 不要让鼠标坐标跳跃过大，除非音乐有明显跳跃感
- 鼠标音符应补充键盘节奏，不要全程和键盘完全重复

## 制谱风格要求

请做一个可玩的完整单难度谱面，不是示例片段。

难度目标：
- 如果我没有指定难度，请默认生成 Hard 或 Expert
- Hard: level 7.0~9.0
- Expert: level 10.0~12.5
- 不要超过 level 15.0

节奏设计：
- 先识别 BPM 和小节线，再把音符贴到音乐事件上
- 时间尽量贴近真实瞬态，允许使用毫秒级时间
- 主歌降低密度，副歌/drop 提高密度
- 重要鼓点优先给键盘区
- 主旋律、vocal chop、装饰音、上扬/下滑音效优先给鼠标区
- 休止、break、氛围段要适当留白
- 高潮可以使用键盘+鼠标的组合，但不要让玩家同时处理过多不可读输入
- 避免无音乐依据的随机撒音符

可玩性限制：
- 普通连续键盘 tap 间隔建议 >= 100ms
- 同一 lane 连打间隔建议 >= 150ms
- hold duration 建议 >= 250ms
- mouse circle 之间建议 >= 180ms
- slider_duration 建议 >= 400ms
- 鼠标坐标使用 2~3 位小数即可
- 所有数组必须按 time 升序排列

## 质量检查

输出前请自检：
1. JSON 是否可以被严格解析
2. 是否只有 Sakura 支持的字段和音符类型
3. timing_points 第一项是否 time=0
4. lane 是否都在 0~3
5. x/y 是否都在 0.0~1.0
6. hold 是否都有 duration > 0
7. slider 是否都有 slider_duration 和 slider_path
8. keyboard_notes 和 mouse_notes 是否分别按 time 升序排列
9. info.json 中 note_count / hold_count / mouse_note_count 是否与实际数组数量一致
10. 难度 level 是否在 1.0~15.0

## 输出要求

请严格按下面格式输出，不要添加额外解释：

```json
{
  "...": "info.json content here"
}
{
  "...": "chart json content here"
}