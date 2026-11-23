# Phase v0.9 详细规格：专业音游工具链、深度结算报告与动态视频舞台

覆盖版本：`v0.9.0-beta`（变速不变调、深度结算分析、工房 v3 与 `.sakura` 谱面包）与 `v0.9.1-beta`（原生 MP4 背景视频舞台、F3 性能诊断面板与时钟抗漂移）。

---

## Release 3：`v0.9.0-beta` — WSOLA 变速不变调、全屏深度报告与工房 v3

### 3.1 自研 WSOLA 变速不变调音频管线 (`src/audio/time_stretcher.h/.cpp`)

解决 `README.md` 记录的已知痛点「练习变速会同时改变音高」：
- 实现零外部 GPL 依赖的 **WSOLA (Waveform Similarity Overlap-Add)** 双声道浮点 PCM 处理器（Hann 窗帧长 `20ms`，互相关搜索窗口 `±8ms`）。
- 支持 `0.50x ~ 2.00x` 任意速率实时时间伸缩而保持原始音高不变；速率 `1.0x` 时自动直通零开销。
- 集成进 `AudioManager` 音乐播放链路，并提供配置项 `audio.pitch_lock`（默认 `true`，可在声音设置页切换「变速锁音高 / 经典变调」）。
- 单元测试 `tests/test_time_stretch.cpp`：输入标准 `440Hz` 正弦波 PCM，在 `0.75x` 与 `1.50x` 下验证输出帧数比例误差 `< 1%` 且主频峰值仍稳定在 `440Hz ± 5Hz`。

### 3.2 全屏深度结算分析报告 (`SceneResult` 扩展)

升级 `src/game/score.h/.cpp`、`src/game/play_session.cpp` 与 `src/scene/scene_result.h/.cpp`：
- `GameResult` 补全逐音符结算遥测快照 `{timeMs, hitErrorMs, result, isKeyboard, lane, isSynergy}`。
- 在 `SceneResult` 新增「详细报告 (Tab / 按钮)」全屏毛玻璃覆盖层（全部采用 `0.0~1.0` 归一化布局）：
  1. **全曲时间轴偏差散点图**：横轴为歌曲时间，纵轴为 `±150ms` 偏差（`Perfect` 金色带高亮，Early 青蓝 / Late 绯粉着色），右上展示 Mean、SD 与 **UR (Unstable Rate = 10 × SD)**。
  2. **判定环形分布图**：`Perfect / Great / Good / Bad / Miss` 扇环比例与精确计数。
  3. **连击走势与断连折线图**：全曲连击包络曲线，红色菱形标注每处 `Bad/Miss` 断连时间点。
  4. **四分段准确率柱状图**：按歌曲时长四等分展示各段准确率，高亮最薄弱段落并提供「一键进入该段练习」快捷按钮（直接带起止时间跳转 `SceneGame` Practice 模式）。
  5. **双区与分轨明细**：独立统计左手键盘区（细分 `A / S / D / F / SPACE` 五轨）与右手鼠标区（`Circle / Slider / Synergy`）准确率。

### 3.3 谱面工房 v3 与 `.sakura` 单文件谱面包

- **工房 v3 增强 (`src/editor/` & `src/scene/scene_editor.cpp`)**：
  - 时间轴底层绘制归一化音频包络波形图，辅助精确对拍。
  - 支持第 5 轨 Space 横梁音符放置/拖拽、左右区音符框选绑定/解绑 `Link` 同押连线、鼠标音符循环切换 `Synergy` 修饰键，以及可视化添加/编辑 `SV` 与 `BPM` 节点。
- **`.sakura` 谱面包归档与拖拽导入 (`src/utils/archive.h/.cpp`)**：
  - 实现跨平台标准 ZIP 读写封装（兼容 Windows 与 `ci-linux` 单测），将谱面目录打包为 `.sakura` 文件（含 `info.json`、各难度 JSON、音频、封面与内容校验清单）。
  - 工房顶栏支持「导出 `.sakura`」；`SceneSelect` 支持文件选择导入及 **直接将 `.sakura` 文件拖入游戏窗口 (`SDL_EVENT_DROP_FILE`) 一键校验安装并刷新曲库**。

### 3.4 `v0.9.0-beta` 验收清单
- 新增 `tests/test_time_stretch.cpp`、`tests/test_sakura_archive.cpp` 与详细结算统计单测。
- 切换通道至 `beta`（`python scripts/version_hook_entry.py tag set beta`），运行 `autopilot_release.py` 发布 `v0.9.0-beta`。

---

## Release 4：`v0.9.1-beta` — 原生 MP4 背景视频舞台、F3 诊断面板与抗漂移

### 4.1 原生背景视频解码与动态舞台 (`src/effects/video_decoder.h/.cpp`、`video_renderer.h/.cpp`)

- **Windows 原生硬解管线**：基于 Windows 10/11 系统内置 Media Foundation (`IMFSourceReader`，链接系统库 `mfplat`、`mfreadwrite`、`mfuuid`，无需引入庞大第三方 FFmpeg DLL) 在独立后台线程解码 `MP4 (H.264)` / `WebM` 视频帧至 4 帧环形缓冲；非 Windows 平台提供安全空实现桩以保障跨平台构建。
- **谱面与设置集成**：`ChartInfo` 支持读取 `info.json` 中的可选 `"video_file"` 字段；设置「视觉」页新增「背景视频开关」与「舞台暗化度 (Bg Dim 20%~90%)」。
- **性能熔断与动态夜樱回退**：当曲目未附带外部视频、用户关闭视频或连续帧解码超时时，自动无缝切换至多层视差月夜墨樱动态着色背景，确保游戏帧率绝不掉帧。
- 为内置主打曲《樱落呼吸》生成一段轻量级无缝循环几何夜樱动态 `bg.mp4` 随包实测验证。

### 4.2 F3 实时性能监控叠层与音频时钟抗漂移

- **F3 诊断叠层 (`App::Render`)**：按 `F3` 切换右上角半透明琉璃监控面板，实时显示当前 FPS、帧间隔 ms、CPU 更新+提交耗时 P95、本帧顶点/索引批处理数、活跃粒子数、音频时钟与视觉时钟实时偏差 ms。
- **音频时钟软硬抗漂移 (`SceneGame`)**：维护高精单调时钟插值游标，当与音频设备播放头偏差 `5ms ~ 50ms` 时执行平滑一阶滤波软追赶（消除微卡顿抖动），`> 50ms`（如蓝牙重连/设备切歌）时立即硬对齐。

### 4.3 `v0.9.1-beta` 验收清单
- 新增视频解码回退与时钟同步测试；扩展 `--smoke-test` 验证 `960x540`、`1600x900`、`3840x2160` 及 F3 面板与视频舞台渲染；自动发布 `v0.9.1-beta`。
