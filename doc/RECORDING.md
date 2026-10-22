# 离线逐帧录制（`--record`）

录制模式按脚本依次渲染若干「镜头」（主菜单、曲库、自动演示 → 结算、谱面工房、设置、教程……），以任意分辨率（默认 3840 × 2160）和固定帧率（默认 60）逐帧输出视频，用于制作宣传片等离线素材。

- **虚拟时间**：每一帧所有游戏逻辑与音乐时钟都精确前进 `1/fps` 秒，与渲染、读回、编码耗时无关；音符、判定、动画与音乐位置逐帧对齐，同一脚本在不同分辨率下得到完全相同的时间轴。
- **不出声、不占用音频设备**：音乐只推进虚拟播放头（仍会加载以获得时长与频谱可视化），击打音效不播放但保留可视化冲击效果。成片的音轨请在剪辑时按 manifest 中的对齐信息放入原曲。
- **不受桌面操作影响**：录制期间真实的键盘、鼠标、触控、手柄输入以及窗口焦点、最小化、尺寸变化都会被丢弃，可以继续使用电脑；脚本动作直接注入游戏。窗口只显示缩小的实时预览（左上角状态条只出现在预览中，不会进入成片）。
- **编码**：读回的原始帧通过管道交给 ffmpeg 子进程编码，ffmpeg 需要在 `PATH` 中或在脚本里指定路径。

## 命令行

```powershell
.\build\release\Release\Sakura.exe --record D:\promo\shots.json
.\build\release\Release\Sakura.exe --record D:\promo\shots.json --record-out D:\promo\takes
```

| 参数 | 说明 |
|---|---|
| `--record <script.json>` | 录制脚本路径 |
| `--record-out <dir>` | 输出目录，可选；默认是脚本所在目录下的 `record-out` |

`--record` 不能与 `--smoke-test`、`--verify-play`、`--verify-audio` 等检查模式同时使用。Sakura 是窗口程序，PowerShell 中可用 `Start-Process ... -Wait -PassThru` 等待并读取退出码：

| 退出码 | 含义 |
|---|---|
| `0` | 全部镜头成功 |
| `1` | 存在失败或中止的镜头（详见 manifest） |
| `2` | 脚本无效或输出目录不可用（错误写入 `record-manifest.json`） |

录制使用独立的用户目录 `<输出目录>/userdata`，**每次运行都会删除并重建**（目录内有 `.sakura-record-userdata` 标记；若同名目录不是录制模式创建的且非空，录制会拒绝运行）。其中的配置固定为：窗口为预览尺寸、非全屏、关闭垂直同步、主音量 0、跳过首次教程提示、不显示 FPS，然后再叠加脚本 `config`。运行日志在 `userdata/logs/sakura.log`。

## 脚本格式

脚本是 JSON（允许 `//` 注释）。示例：

```json
{
  "width": 3840, "height": 2160, "fps": 60,
  "preview_width": 1280, "preview_height": 720,
  "ffmpeg": "ffmpeg",
  "video_args": ["-vf", "scale=out_color_matrix=bt709:out_range=tv,setparams=color_primaries=bt709:color_trc=bt709",
                 "-c:v", "h264_nvenc", "-preset", "p7", "-tune", "hq", "-rc", "vbr", "-cq", "14", "-b:v", "0",
                 "-maxrate", "400M", "-bufsize", "800M", "-g", "60", "-bf", "0", "-pix_fmt", "yuv420p",
                 "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
                 "-movflags", "+faststart"],
  "config": { "gameplay.note_speed": 6.0 },
  "shots": [
    { "name": "menu", "scene": "menu", "seconds": 10,
      "actions": [ {"at": 3.0, "key": "Down"}, {"at": 4.0, "move": [0.2, 0.3]},
                   {"at": 5.0, "click": [0.5, 0.5]}, {"at": 6.0, "text": "abc"} ] },
    { "name": "select", "scene": "select", "chart": "spring_breeze", "difficulty": 1, "seconds": 8 },
    { "name": "play", "scene": "game", "chart": "spring_breeze", "difficulty": 1, "mode": "auto",
      "start_ms": 0, "end_ms": 0, "rate": 1.0, "until": "result", "tail_seconds": 5, "max_seconds": 400 },
    { "name": "editor", "scene": "editor", "chart": "spring_breeze", "difficulty": 0, "seconds": 8,
      "actions": [ {"at": 0.6, "click": [0.95, 0.868]}, {"at": 1.0, "key": "Space"} ] },
    { "name": "settings_visual", "scene": "settings", "page": 4, "seconds": 6 },
    { "name": "tutorial", "scene": "tutorial", "seconds": 8 },
    { "name": "calibration", "scene": "calibration", "seconds": 8 },
    { "name": "stats", "scene": "stats", "seconds": 5 }
  ]
}
```

未识别的字段只记录警告（写入 manifest 的 `warnings`），其余错误会带字段路径报出，例如 `shots[2].chart: game / editor 镜头必须指定谱面 id`。

### 顶层字段

| 字段 | 默认值 | 说明 |
|---|---|---|
| `width` / `height` | `3840` / `2160` | 录制分辨率（16～8192，必须为偶数）。整帧按此尺寸布局与渲染 |
| `fps` | `60` | 帧率（1～240），也是虚拟时间步长 `1/fps` |
| `preview_width` / `preview_height` | `1280` / `720` | 预览窗口尺寸（960～7680 × 540～4320），不影响成片 |
| `ffmpeg` | `"ffmpeg"` | ffmpeg 可执行文件。不含路径分隔符时按 `PATH` 查找；相对路径以脚本目录为基准 |
| `video_args` | 见下文 | 位于 `-i -` 与输出文件之间的 ffmpeg 参数 |
| `config` | `{}` | 录制前写入的配置项，可用点号键（`"gameplay.note_speed": 6.0`）或嵌套对象 |
| `userdata_template` | 无 | 可选目录，录制前整体复制到 `userdata`（例如带成绩的 `data/sakura.db`、自制谱面 `charts/`），相对路径以脚本目录为基准 |
| `shots` | 必填 | 镜头数组，按顺序录制 |

### 镜头字段

| 字段 | 默认值 | 说明 |
|---|---|---|
| `name` | `shot01`… | 输出文件名前缀（不能含 `< > : " / \ | ? *`，不区分大小写不可重复） |
| `scene` | 必填 | `splash` / `menu` / `select` / `settings` / `tutorial` / `stats` / `editor` / `game` / `calibration` / `create` |
| `seconds` | — | 录制时长（秒）。未指定 `until` 时必填 |
| `until` | 无 | `"result"`：一直录到结算界面完全出现（转场结束），再追加 `tail_seconds` |
| `tail_seconds` | `3` | `until: "result"` 时结算出现后继续录制的秒数 |
| `max_seconds` | `600` | `until: "result"` 的安全上限；超过仍未出现结算则镜头失败（已录制部分保留） |
| `warmup_seconds` | `0` | 进入场景后先模拟这么久再开始录制（跳过入场动画、倒计时等） |
| `chart` | — | 谱面 id（`info.json` 的 `id`）。在 `resources/charts` 与录制用户目录 `charts/` 中查找，同 id 时用户谱面优先；`game` / `editor` 必填，`select` 可选（预选曲目） |
| `difficulty` | `0` | 难度索引（`info.json` 中 `difficulties` 的下标） |
| `mode` | `auto` | 仅 `game`：`auto`（自动演示）/ `practice`（练习，需脚本按键）/ `replay`（回放）/ `standard`（正式演奏） |
| `start_ms` / `end_ms` | `0` / `0` | 仅 `game`：演奏区间（谱面时间，毫秒）；`end_ms` 为 0 表示到曲终。`auto` 与 `practice` 都会在 `end_ms` 处结束并进入结算 |
| `rate` | `1.0` | 仅 `game`：播放速度 0.5～2.0（回放模式使用回放文件中的速度） |
| `replay` | — | `mode: "replay"` 时的回放文件（`.skr`），相对路径以脚本目录为基准 |
| `page` | `0` | 仅 `settings`：页面 0～5（游玩 / 声音 / 操作 / 显示 / 视觉 / 数据） |
| `actions` | `[]` | 脚本输入，见下文 |

结算界面由 `game` 镜头自然进入，不需要单独的场景；`splash` 会依次经过加载页进入主菜单。

### 动作

每个动作必须有 `at`（秒）和下列**一种**类型。`at` 是成片中的时间（录制首帧为 0）；预热阶段的动作可以用负值（不早于 `-warmup_seconds`）。坐标均为归一化的 `[x, y]`（0～1，相对整个画面），注入时换算为窗口坐标并带上真实窗口 ID，所以工房时间轴、全曲轴等依赖窗口坐标的输入同样有效。

| 类型 | 示例 | 说明 |
|---|---|---|
| `key` | `{"at": 3, "key": "Down", "mod": "ctrl", "hold": 0.2}` | 按下并松开。键名使用 SDL 扫描码名称（`Down`、`Return`、`Space`、`Escape`、`Tab`、`A`、`F5`…）；`mod` 可为 `ctrl` / `shift` / `alt` 或用 `+` 组合，会先按下对应的左侧修饰键；`hold` 为按住秒数，默认在下一帧松开 |
| `key_down` / `key_up` | `{"at": 3.15, "key_down": "S"}` | 单独按下 / 松开（用于 Hold 音符、练习模式），同样支持 `mod` |
| `move` | `{"at": 4, "move": [0.2, 0.3], "duration": 0.4}` | 移动指针；`duration` > 0 时逐帧平滑移动 |
| `click` | `{"at": 5, "click": [0.5, 0.5], "button": "left", "hold": 0.1}` | 移动后按下并松开；`button` 为 `left` / `middle` / `right`，`clicks` 可设为 2 表示双击 |
| `mouse_down` / `mouse_up` | `{"at": 5, "mouse_down": [0.5, 0.5]}` | 单独按下 / 松开（可配合 `move` 拖动） |
| `wheel` | `{"at": 2, "wheel": -1, "pos": [0.25, 0.4]}` | 滚轮（正值向上），`pos` 可选 |
| `text` | `{"at": 6, "text": "abc"}` | 文本输入（输入框需先获得焦点，例如曲库的 `Ctrl+F`） |

输入在两帧之间注入，与真实输入的到达时机一致：`at = t` 的动作在时间 `t` 的那一帧更新之前送达，因此该帧就能看到效果。场景切换动画进行中游戏本身会忽略输入，脚本动作也一样。

## 时间轴

- 镜头开始时以无转场方式切换到目标场景；场景 `OnEnter` 后的首帧是第 0 帧（预热时是预热第 0 帧）。
- 每帧依次执行：注入到期的脚本动作 → 音乐虚拟播放头前进 `dt × 播放速度` → 场景更新 → 渲染 → 读回编码。
- `seconds: 10` 在 60 fps 下恰好输出 600 帧；`until: "result"` 输出到「结算完全出现的那一帧 + tail_seconds」为止。
- 同一镜头内场景可以自然切换（演奏 → 结算、启动画面 → 菜单），录制不会中断。
- 镜头之间会清空场景栈、停止音乐、松开所有脚本按住的键并清除通知与屏幕震动，镜头互不影响。

## 输出

| 文件 | 内容 |
|---|---|
| `<name>.mp4` | 成片 |
| `<name>.frames.csv` | 逐帧数据：`frame,shot_time_s,scene,music_pos_s,music_playing,game_time_ms,game_phase` |
| `<name>.ffmpeg.log` | ffmpeg 的标准错误输出 |
| `record-manifest.json` | 每个镜头结束后更新的汇总 |
| `userdata/` | 本次录制的隔离用户目录（日志、配置、复制出的谱面等） |

`frames.csv` 中 `scene` 为当前场景（`splash`、`loading`、`menu`、`select`、`settings`、`tutorial`、`stats`、`editor`、`game`、`pause`、`result`、`calibration`、`create`），`game_time_ms` 与 `game_phase`（`countdown` / `playing` / `paused` / `finished`）只在演奏场景中填写。

manifest 的每个镜头包含：`file`、`frames`、`fps`、`width`、`height`、`duration_s`、`success`、`error`、`ffmpeg_exit_code`、`ffmpeg_command`、`pixel_format`，以及音画同步信息 `music_start_frame`（第一帧音乐处于播放状态的帧）、`music_start_position_s`（该帧的音乐位置）、`music_playback_rate`、`music_file`、`music_offset_in_video_s`。此外还有 `result_frame`（结算出现的帧）、`render_fps`（实际录制速度）、`readback_ms_avg` 与 `encoder_submit_ms_avg`（读回与等待编码的平均耗时）。

### 音画对齐

音乐连续播放期间，第 `f` 帧对应的音乐位置为 `music_start_position_s + (f − music_start_frame) / fps × music_playback_rate`。剪辑时把原曲（`music_file`）放在成片时间 `music_offset_in_video_s` 处即可逐帧同步；该值为负时裁掉原曲开头相应长度。变速镜头需要按 `music_playback_rate` 同时改变速度与音高（与游戏内练习变速一致）。暂停或重新开始会改变对齐关系，请以 `frames.csv` 的 `music_pos_s` 为准。

## 编码参数

默认 `video_args`（libx264，高质量，BT.709 limited range）：

```text
-vf scale=out_color_matrix=bt709:out_range=tv,setparams=color_primaries=bt709:color_trc=bt709
-c:v libx264 -preset medium -crf 12 -pix_fmt yuv420p -g 60
-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv -movflags +faststart
```

完整命令为 `ffmpeg -y -f rawvideo -pix_fmt <读回格式> -s WxH -r fps -i - <video_args...> <name>.mp4`。读回格式按像素布局映射（ARGB8888→`bgra`、ABGR8888→`rgba`、XRGB8888→`bgr0`、XBGR8888→`rgb0`、RGBA8888→`abgr`、BGRA8888→`argb`），其他格式先转换为 ARGB8888。

> 较新的 ffmpeg 以滤镜输出帧的色彩属性为准，对未标注的 RGB 输入，单独的 `-color_primaries` / `-color_trc` 不会写入码流（ffprobe 显示 `unknown`）。自定义 `video_args` 时建议像上面一样在 `-vf` 末尾加上 `setparams=color_primaries=bt709:color_trc=bt709`。

NVIDIA 显卡可改用 NVENC（示例见上文脚本），4K60 下录制速度主要受画面读回限制。参考数据（i9-13900K + RTX 5080，h264_nvenc p7）：3840 × 2160 约 31～41 帧/秒（读回约 22 ms/帧），1280 × 720 约 230～280 帧/秒。

## 预览与中止

- 预览窗口固定尺寸，左上角显示 `REC` / `WARMUP` / `PREPARING`、当前镜头、已录帧数与实时速度。
- 关闭预览窗口（或在启动 Sakura 的终端中按 Ctrl+C）会中止录制：当前镜头已录制的部分会正常封装成可播放的文件，manifest 标记 `aborted`，后续镜头记为未执行，退出码为 1。
- 从终端启动时，Sakura 会附加到该终端的控制台，ffmpeg 不会弹出新的控制台窗口；即使弹出，焦点变化也不会影响录制。

## 限制

- 成片不含音轨，需要剪辑时按上文对齐原曲；击打音效不会录入。
- 粒子等随机效果每次运行的随机数不同，时间轴（帧数、音乐位置、判定）完全一致。
- 工房时间轴的滚轮缩放 / 滚动读取系统光标位置与 Ctrl 状态，谱面创建向导的 Shift+Tab 读取系统修饰键状态，这两处脚本输入无法完全模拟；其他场景的键鼠输入均可脚本化。
- 演奏场景的鼠标判定纵横缩放按预览窗口（未获得键盘焦点时按 16:9）计算；录制分辨率不是 16:9 时，练习 / 正式模式中脚本鼠标输入的判定几何可能与画面比例不一致（自动演示与回放不受影响）。
