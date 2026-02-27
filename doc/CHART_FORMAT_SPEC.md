# Sakura-樱 谱面格式规范 v2

> 基于旧版格式扩展，新增 SV 变速、BPM 变化、多难度支持等功能

## 谱面目录结构

```
resources/charts/{chart_id}/
├── info.json              # 谱面元信息（包含多难度信息）
├── easy.json              # Easy 难度谱面数据（可选）
├── normal.json            # Normal 难度谱面数据
├── hard.json              # Hard 难度谱面数据
├── expert.json            # Expert 难度谱面数据（可选）
├── music.ogg              # 音乐文件（推荐 OGG/FLAC）
├── cover.png              # 封面图片（建议 512x512 或 1024x1024）
└── bg.png                 # 游戏内背景图片（可选）
```

---

## info.json — 谱面元信息

```json
{
    "version": 2,
    "id": "unique-chart-id",
    "title": "曲目名称",
    "artist": "曲师/歌手",
    "charter": "谱面作者",
    "source": "来源（专辑/游戏等，可选）",
    "tags": ["electronic", "vocaloid"],
    
    "music_file": "music.ogg",
    "cover_file": "cover.png",
    "background_file": "bg.png",
    "preview_time": 30000,
    
    "bpm": 180.0,
    "offset": 0,
    
    "difficulties": [
        {
            "name": "Normal",
            "level": 5,
            "chart_file": "normal.json",
            "note_count": 342,
            "hold_count": 28,
            "mouse_note_count": 156
        },
        {
            "name": "Hard",
            "level": 8,
            "chart_file": "hard.json",
            "note_count": 687,
            "hold_count": 53,
            "mouse_note_count": 294
        },
        {
            "name": "Expert",
            "level": 11,
            "chart_file": "expert.json",
            "note_count": 1024,
            "hold_count": 87,
            "mouse_note_count": 412
        }
    ]
}
```

### 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `version` | int | 是 | 格式版本号，当前为 2 |
| `id` | string | 是 | 唯一标识符 |
| `title` | string | 是 | 曲目名称 |
| `artist` | string | 是 | 曲师/歌手 |
| `charter` | string | 是 | 谱面作者 |
| `source` | string | 否 | 来源信息 |
| `tags` | string[] | 否 | 标签列表 |
| `music_file` | string | 是 | 音乐文件路径（相对于谱面目录） |
| `cover_file` | string | 否 | 封面图片路径 |
| `background_file` | string | 否 | 背景图片路径 |
| `preview_time` | int | 否 | 选歌界面预览起始时间（毫秒） |
| `bpm` | float | 是 | 基础 BPM |
| `offset` | int | 是 | 音频偏移（毫秒），正值表示谱面整体延后 |
| `difficulties` | array | 是 | 难度列表 |

### 难度对象字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 难度名称（Easy/Normal/Hard/Expert 或自定义） |
| `level` | int | 是 | 难度等级（1-15） |
| `chart_file` | string | 是 | 对应的谱面数据文件 |
| `note_count` | int | 否 | 键盘端音符总数 |
| `hold_count` | int | 否 | Hold 音符总数 |
| `mouse_note_count` | int | 否 | 鼠标端音符总数 |

---

## 谱面数据文件 (chart.json)

每个难度的谱面数据文件包含键盘端和鼠标端的所有音符，以及 BPM 变化和 SV 变速事件。

```json
{
    "version": 2,
    
    "timing_points": [
        {
            "time": 0,
            "bpm": 180.0,
            "time_signature": [4, 4]
        },
        {
            "time": 45000,
            "bpm": 200.0,
            "time_signature": [4, 4]
        }
    ],
    
    "sv_points": [
        {
            "time": 12000,
            "speed": 1.5,
            "easing": "linear"
        },
        {
            "time": 15000,
            "speed": 1.0,
            "easing": "ease_out"
        }
    ],
    
    "keyboard_notes": [
        {
            "time": 1000,
            "lane": 0,
            "type": "tap"
        },
        {
            "time": 2000,
            "lane": 1,
            "type": "hold",
            "duration": 500
        },
        {
            "time": 3000,
            "lane": 2,
            "type": "drag",
            "drag_to_lane": 3
        }
    ],
    
    "mouse_notes": [
        {
            "time": 1500,
            "x": 0.3,
            "y": 0.4,
            "type": "circle"
        },
        {
            "time": 3000,
            "x": 0.5,
            "y": 0.3,
            "type": "slider",
            "slider_duration": 1500,
            "slider_path": [
                [0.6, 0.2],
                [0.7, 0.4],
                [0.5, 0.6]
            ]
        }
    ]
}
```

---

## 字段详细说明

### timing_points — BPM 变化点

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 触发时间（毫秒） |
| `bpm` | float | 是 | 新的 BPM 值 |
| `time_signature` | int[2] | 否 | 拍号 [分子, 分母]，默认 [4, 4] |

### sv_points — 变速点 (Scroll Velocity)

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 触发时间（毫秒） |
| `speed` | float | 是 | 速度倍率（1.0 = 基准速度） |
| `easing` | string | 否 | 缓动类型，默认 `"linear"`。可选：`"linear"` / `"ease_in"` / `"ease_out"` / `"ease_in_out"` |

### keyboard_notes — 键盘端音符

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 触发时间（毫秒） |
| `lane` | int | 是 | 轨道编号（0-3，对应 A/S/D/F） |
| `type` | string | 是 | 音符类型：`"tap"` / `"hold"` / `"drag"` |
| `duration` | int | Hold 专用 | Hold 音符持续时间（毫秒） |
| `drag_to_lane` | int | Drag 专用 | Drag 音符目标轨道 |

### mouse_notes — 鼠标端音符

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 触发时间（毫秒） |
| `x` | float | 是 | X 归一化坐标（0.0~1.0，指鼠标操作区域的相对位置） |
| `y` | float | 是 | Y 归一化坐标（0.0~1.0，指鼠标操作区域的相对位置） |
| `type` | string | 是 | 音符类型：`"circle"` / `"slider"` |
| `slider_duration` | int | Slider 专用 | Slider 持续时间（毫秒） |
| `slider_path` | float[][] | Slider 专用 | Slider 曲线路径点 [[x, y], ...] |

---

## 坐标系统说明

- 键盘端 `lane` 为 0-3 整数值，对应左到右 4 个轨道
- 鼠标端 `x`、`y` 为 0.0~1.0 归一化坐标，表示在**鼠标操作区域内**的相对位置
  - `(0.0, 0.0)` = 操作区域左上角
  - `(1.0, 1.0)` = 操作区域右下角
- 实际渲染时根据窗口尺寸和操作区域位置/大小动态计算像素坐标

---

## 格式版本历史

| 版本 | 变更 |
|------|------|
| v1 | 初始版本，chart.json + mousechart.json + info.json 分离 |
| v2 | 合并键盘/鼠标音符到单文件，新增 timing_points / sv_points / 多难度支持 |
