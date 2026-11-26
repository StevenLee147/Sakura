# Sakura-樱 谱面格式规范 v4（100% 向后兼容 v1 / v2 / v3）

> 在 v3（第 5 轨 Space 樱枝横梁 `lane = 4`、跨区连携同押 `link_id`、樱绽组合技修饰 `require_lane`、可选 MP4/WebM 动态视频舞台 `video_file`、`.sakura` PKZIP 单文件分发包）基础上，新增 **舞台镜头/氛围演出事件 (`stage_events`)** 与 **双语时间轴同步歌词 (`lyrics`)**，同时不改变音符判定与回放哈希。

## 谱面目录与 `.sakura` 包结构

```
resources/charts/{chart_id}/   （或打包为单文件 {chart_id}.sakura）
├── info.json              # 谱面元信息（version: 4，兼容 1/2/3）
├── easy.json              # Easy 难度谱面数据（可选）
├── normal.json            # Normal 难度谱面数据
├── hard.json              # Hard 难度谱面数据
├── expert.json            # Expert 难度谱面数据（可选）
├── music.ogg              # 音乐文件（支持 OGG/FLAC/MP3/WAV）
├── cover.png              # 封面图片（建议 512x512 或 1024x1024）
├── bg.png                 # 游戏内静态背景图片（可选）
└── bg.mp4                 # 游戏内硬件加速循环背景视频（可选，info.json 配置 video_file）
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
| `version` | int | 是 | 格式版本号，当前推荐为 `4`（完全兼容 `1 / 2 / 3 / 4`） |
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
| `level` | float | 是 | 难度等级（1.0–15.0，支持小数定数如 `7.5`） |
| `chart_file` | string | 是 | 对应的谱面数据文件 |
| `note_count` | int | 否 | 键盘端音符总数 |
| `hold_count` | int | 否 | Hold 音符总数 |
| `mouse_note_count` | int | 否 | 鼠标端音符总数 |
| `thumb_count` | int | 否 | 空格樱枝横梁音符数（Chart v3+） |
| `synergy_count` | int | 否 | 跨区连携/组合技音符数（Chart v3+） |

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
            "type": "hold",
            "duration": 600
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
    ],

    "stage_events": [
        {
            "time": 1000,
            "duration": 800,
            "type": "pulse",
            "intensity": 0.55,
            "color": [252, 198, 222]
        },
        {
            "time": 12000,
            "duration": 4000,
            "type": "petal_burst",
            "intensity": 1.5,
            "color": [255, 186, 216]
        }
    ],

    "lyrics": [
        {
            "time": 2000,
            "end_time": 8500,
            "text": "晨光拂过沉睡的枝梢",
            "sub_text": "Morning light brushes the sleeping boughs"
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
| `lane` | int | 是 | 轨道编号（`0`–`3` 对应 A/S/D/F，`4` 对应空格樱枝横梁） |
| `type` | string | 是 | 音符类型：`"tap"` / `"hold"` |
| `duration` | int | Hold 专用 | Hold 音符持续时间（毫秒） |
| `link_id` | int | 否 | 跨区连携同押组 ID（`> 0` 时与相同 `link_id` 鼠标音符配对，v3+） |

### mouse_notes — 鼠标端音符

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 触发时间（毫秒） |
| `x` | float | 是 | X 归一化坐标（0.0~1.0，指鼠标操作区域的相对位置） |
| `y` | float | 是 | Y 归一化坐标（0.0~1.0，指鼠标操作区域的相对位置） |
| `type` | string | 是 | 音符类型：`"circle"` / `"slider"` |
| `slider_duration` | int | Slider 专用 | Slider 持续时间（毫秒） |
| `slider_path` | float[][] | Slider 专用 | Slider 曲线路径点 [[x, y], ...] |
| `link_id` | int | 否 | 跨区连携同押组 ID（v3+） |
| `require_lane` | int | 否 | 樱绽组合技修饰键（`-1` 无，`0`–`4` 对应左区五键，`5` 对应鼠标右键，v3+） |

### stage_events — 舞台镜头与氛围演出事件（Chart v4）

按 `time` 非递减排列（`ValidateChartData` 会拒绝负数时间、逆序事件、非法时长或越界强度）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 事件触发时间（毫秒，`>= 0`，按升序排列） |
| `duration` | int | 否 | 持续时长（毫秒，`> 0`，默认 `600`） |
| `type` | string | 是 | 事件类型：`"pulse"`（节拍镜头微缩放脉冲）/ `"vignette"`（边缘暗角律动）/ `"tint"`（舞台氛围色调柔染）/ `"petal_burst"`（副歌漫天樱瓣雨速率倍增） |
| `intensity` | float | 否 | 强度（`0.0 ~ 2.0`，默认 `0.5`；开启「减少动态效果」时 `pulse` 镜头缩放强制置 `0.0`） |
| `color` | int[3] | 否 | RGB 色调 `[r, g, b]`（`0`–`255`，主要供 `"tint"` 等氛围染色使用，默认 `[246, 178, 206]`） |

### lyrics — 双语时间轴同步歌词（Chart v4）

在演奏界面底部通栏上方按时间轴平滑淡入、驻留与淡出显示（可在设置中独立开关）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `time` | int | 是 | 本句歌词起始时间（毫秒，`>= 0`） |
| `end_time` | int | 是 | 本句歌词结束时间（毫秒，`> time`） |
| `text` | string | 是 | 主行歌词文本（如中文原词） |
| `sub_text` | string | 否 | 副行译文或日/英双语对照（可选） |

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
| v3 | 向后兼容扩展：新增 `lane = 4` 空格樱枝横梁、`link_id` 跨区连携同押、`require_lane` 樱绽键鼠组合技 |
| v4 | 向后兼容扩展：新增 `stage_events`（`pulse` / `vignette` / `tint` / `petal_burst` 舞台氛围演出）与 `lyrics` 双语时间轴同步歌词 |
