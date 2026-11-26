# Phase v2.4 详细规格：低延迟音频后端热插拔自愈、10 类双区打击音 12 复音防爆池与双碟无缝预听环境音

覆盖版本：`v2.4.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 低延迟音频后端与设备热插拔零崩溃自愈 (`src/audio/audio_manager.*`, `src/audio/audio_backend.cpp`, `src/core/config.*`, `src/scene/scene_settings.*`, `src/scene/scene_game.*`)

### 1.1 缓冲延迟档位预设 (`audio.latency_mode`) 与实时指标遥测
- **配置键扩展 (`src/core/config.h` / `config.cpp`)**：
  - `ConfigKeys::kAudioLatencyMode = "audio.latency_mode"`（`string`，默认 `"balanced"`）：
    - `"ultra_low"`：`128` PCM 帧（极致竞技低延迟，适合现代独立声卡/板载 WASAPI 低缓冲环境）；
    - `"balanced"`：`256` PCM 帧（默认出厂推荐，兼顾超低击打反馈延迟与多任务抗抖动稳定性）；
    - `"safe"`：`512` PCM 帧（蓝牙耳机、USB 扩展坞或高负载直播推流环境防爆音安全模式）。
  - `ConfigKeys::kAudioOutputDevice = "audio.output_device"`（`string`，默认 `"default"`，表示跟随操作系统默认播放端点）。
- **底层 `ma_device_config` + `ma_engine_config` 显式装配**：
  - 当 `m_offlineClock == false` 时，由 `AudioManager` 维护受控 `ma_context*` 与 `ma_device*`：
    - 通过 `ma_device_config_init(ma_device_type_playback)` 显式设置 `periodSizeInFrames = 128 | 256 | 512`、`periods = 2`、`performanceProfile = ma_performance_profile_low_latency`（Windows WASAPI 后端启用低延迟共享流调度）；
    - 初始化 `ma_device` 成功后读取硬件协商落地的真实 `sampleRate` 与单周期帧数，再将该 `ma_device*` 注入 `ma_engine_config.pDevice` 完成 `ma_engine_init`。
  - 当 `m_offlineClock == true`（离线视频录制 `--record` 或无声环境单测）时，严格保持 `engineConfig.noDevice = MA_TRUE`（固定虚拟 `48000 Hz` 双声道），确保 Linux CI 与无音频硬件容器 100% 零依赖通过。
- **实时诊断接口与 UI 展示（全 `0.0~1.0` 归一化相对比例布局）**：
  - `AudioManager` 新增查询与热切换 API：
    - `bool SetLatencyMode(std::string_view mode)` / `std::string GetLatencyMode() const`；
    - `int GetActualSampleRate() const`、`int GetPeriodFrames() const`、`float GetEstimatedOutputLatencyMs() const`（按实际协商周期帧数与采样率计算硬件缓冲延迟毫秒值）。
  - `SceneSettings`「音频」页新增「音频缓冲延迟模式」下拉/分段选择器，并在控件旁实时渲染当前音频管线遥测状态（例如 `48000 Hz · 256 frames (~5.3 ms)`），切换档位即时触发引擎无缝重建。
  - 全局与局内 `F3` 性能诊断浮层同步追加音频状态指标行，实时展示当前活动输出设备简称、采样率、周期帧数与缓冲延迟（`ms`）。

### 1.2 播放设备枚举 (`GetPlaybackDevices()`)、输出设备选择与热插拔自愈
- **输出设备枚举与持久化切换**：
  - 定义跨平台设备描述结构：
    ```cpp
    struct AudioDeviceInfo
    {
        std::string id;        // "default" 或编码后的设备唯一标识
        std::string name;      // UTF-8 人类可读设备名
        bool        isDefault = false;
    };
    ```
  - `std::vector<AudioDeviceInfo> GetPlaybackDevices() const` 调用 `ma_context_get_devices` 枚举当前系统全部可用播放端点（列表首项固定为 `id = "default"`「系统默认设备」）。
  - `bool SetOutputDevice(std::string_view deviceId)` 将选择写入 `audio.output_device` 并即时热迁移当前音乐与音效池至目标设备。
- **设备拔出 / 系统默认音频路由变更自动热恢复（Hot-Swap Recovery）**：
  - 在 `ma_device_config.notificationCallback` 中监听 `ma_device_notification_type_stopped`、`ma_device_notification_type_rerouted` 与 `ma_device_notification_type_interruption_ended`；回调内**严禁直接调用 `ma_device_uninit`**，仅通过 `std::atomic<bool> m_deviceRebuildPending{false}` 置位异步恢复标记。
  - 在主线程 `AudioManager::Update(float dt)` 中检测恢复标记（配合防抖间隔与失败退避重连，若原指定设备已离线则自动回退系统默认端点并通过 UI `Toast` 温和提示）：
    1. 完整快照当前活跃音乐碟片的路径、精确播放游标秒数（`GetMusicPosition()`）、循环起止区间、暂停态、播放倍速（含 WSOLA 倍率）及当前 `m_hitsoundSetName`；
    2. 按逆序安全销毁全部预解码 `ma_sound` 复音槽位、音乐流解码器、`ma_sound_group`、`ma_engine` 与失效 `ma_device`；
    3. 基于新端点重新初始化 `ma_device`、`ma_engine` 与独立音量分组；
    4. 重新挂载 12 复音 SFX 缓存池，按快照游标精确恢复背景音乐播放/循环进度并重绑 `AudioVisualizer`，实现拔插耳机/蓝牙重连全程**零崩溃、零死锁、音乐无缝续播**。

---

## 2. 10 类双区精细打击音与 12 复音防削波混音池 (`src/audio/sfx_generator.*`, `src/audio/audio_manager.*`, `src/core/skin_manager.*`, `src/scene/scene_game.*`)

### 2.1 扩展 `HitsoundType`（10 类演奏打击音 + 2 类舞台反馈音）与 `cache/sfx-v5` 六套主题音色
- **枚举体系升级 (`src/audio/audio_manager.h`)**：
  - 将 `HitsoundType` 扩展为完整覆盖左手 4K+空格与右手圆环+滑条及跨区协同的 **10 类核心演奏打击音 + 2 类局内舞台关键状态音效**：
    ```cpp
    enum class HitsoundType
    {
        Tap,          // tap.wav           — 左手 4K 常规单键敲击
        HoldStart,    // hold_start.wav    — 左手长按起手冲击
        HoldTick,     // hold_tick.wav     — 左手长按持续节拍脉冲
        HoldEnd,      // hold_end.wav      — 左手长按尾判收束泛音
        ThumbBar,     // thumb_bar.wav     — Space 樱枝横梁重音低频共鸣
        Circle,       // circle.wav        — 右手鼠标圆环清脆晶响
        SliderStart,  // slider_start.wav  — 右手曲线滑条起手扫频
        SliderTick,   // slider_tick.wav   — 右手滑条锚点/轨迹追踪细密滴答
        LinkChord,    // link_chord.wav    — 跨区同押 link_id 双域和弦共鸣
        SynergyBurst, // synergy_burst.wav — 键鼠协同 require_lane 樱绽爆发华彩
        FeverTrigger, // fever_trigger.wav — Fever 满开槽开启升腾扫频和弦
        ComboBreak,   // combo_break.wav   — 高连击中断低频碎裂警示
        Count
    };
    ```
- **`SfxGenerator` 六套主题全量合成升级 (`userdata/cache/sfx-v5`)**：
  - 将合成缓存根目录由 `cache/sfx-v4` 升级至 `cache/sfx-v5`，确保 `default`（墨樱晶石）、`cyber`（霓虹 FM 电音）、`pure`（电竞极简机械木鱼）、`ukiyo`（浮世竹韵太鼓）、`soft`（柔光绒音）、`drum`（重鼓瞬态）全部 **6 套预设**完整生成上述 12 个 `.wav` 文件及 5 档判定音。
  - 针对新增音色特征设计差异化合成算法：
    - `ThumbBar`：叠加八度下潜基频与紧凑瞬态鼓皮冲击，凸显大拇指横梁重拍分量；
    - `HoldEnd` / `SliderTick`：短促干净的上扬泛音收尾与低侵入高频微脉冲，连续触发不掩盖主旋律；
    - `LinkChord` / `SynergyBurst` / `FeverTrigger`：纯五度/大三和弦多振子叠加配合包络扫频，赋予双区合击与满开爆发强烈听觉正反馈；
    - `ComboBreak`：快速下潜非谐波瞬态阻尼，仅在实际断连（如断前 `combo >= 20`）时触发。
- **皮肤音效覆盖 (`SkinManager`) 与局内事件精准派发 (`SceneGame` / `PlaySession`)**：
  - `SkinManager::ResolveCustomHitsoundFile()` 原生支持自定义皮肤包 `hitsounds/` 目录对上述全部 12 个标准文件名的逐项覆盖，未提供的条目自动回退至皮肤声明的 `hitsoundSet` 目录乃至 `default`。
  - `SceneGame` 在音符命中、长按维持/松开、滑条推进、跨区 `linkId` 结对完成、`requireLane` 协同激活、Fever 开启与高连击中断各时机精确派发对应 `HitsoundType`。

### 2.2 12 复音预解码池、微淡出抢占防爆音、人性化微调与防削波保护
- **12 复音池与 Oldest-Finished-First + Micro-Fade Steal**：
  - 将每个缓存音效的预解码通道数从 `4` 提升至 `12`（`kVoicesPerSfx = 12`），每个槽位记录 `uint64_t playOrder` 与抢占状态。
  - 分配策略：
    1. 优先挑选 `!ma_sound_is_playing(voice)` 中最先结束（最小 `playOrder`）的空闲槽位（Oldest-Finished-First），复位至第 `0` 帧并施加 `1~2 ms` 极短微淡入平滑起播；
    2. 若极端高速纵连/全屏多押下 12 路复音全部繁忙，挑选最早启动的槽位执行 **Micro-Fade Steal**（对旧声部施加 `4 ms` 线性微淡出至静音，由独立过渡槽或零交叉安全切换承接新声部），彻底根除硬 `stop + seek(0)` 造成的波形截断爆音（Clicks/Pops）。
- **可选 `±1.5%` 音高/增益人性化微扰 (`audio.sfx_humanize`)**：
  - 新增 `ConfigKeys::kSfxHumanize = "audio.sfx_humanize"`（`bool`，默认 `true`）。
  - 开启时，对演奏类 `HitsoundType` 每次触发施加 `[-1.5%, +1.5%]`（即倍率 `0.985f ~ 1.015f`）的轻量音高与响度随机微扰，消除高速十六分交互连打的机械听觉疲劳；延迟校准节拍音（`CalibrationBeat` / `CalibrationHit`）始终保持严格恒定频率与增益。
- **多复音并发增益缩放与软限幅（Polyphonic Gain Scaling & Soft Limiter）**：
  - 维护最近 `35 ms` 时间窗内的并发打击音触发计数 $N$；当 $N > 2$（如四押 + 空格或跨区同押爆发）时，按动态衰减曲线 $\text{gainScale} = 1.0 / \sqrt{1.0 + 0.22 \times (N - 2)}$ 自动收敛新触发复音增益（下限保底 `0.55f`），配合合成器峰值余量控制与软饱和限幅，杜绝多音符叠加时的数字削波失真。
- **独立打击音与界面音效音量滑条**：
  - 新增 `ConfigKeys::kHitsoundVolume = "audio.hitsound_volume"`（`float`，默认 `0.85f`）与 `ConfigKeys::kUiSfxVolume = "audio.ui_sfx_volume"`（`float`，默认 `0.75f`）。
  - `AudioManager` 内部拆分 `m_hitsoundGroup` 与 `m_uiSfxGroup` 两个子 `ma_sound_group`（总增益分别为 `master * sfx * hitsound` 与 `master * sfx * ui_sfx`），并在 `SceneSettings` 音频页提供独立滑条与即时试听反馈。

---

## 3. 双碟无缝选歌交叉淡入淡出与真实 Suno 菜单环境 BGM (`src/audio/audio_manager.*`, `src/scene/scene_select.*`, `src/scene/scene_menu.*`, `src/scene/scene_stats.*`, `src/scene/scene_course.*`, `src/scene/scene_market.*`)

### 3.1 双 `ma_sound` 碟片混音架构 (`CrossfadeMusic`) 与 22 秒副歌无缝循环预听
- **A/B 双碟片底层重构**：
  - 将单实例 `m_music` 升级为 `std::array<MusicDeck, 2> m_decks`（每碟独立管理 `ma_sound*`、`ma_decoder*`、OGG 内存字节缓冲、循环区间 `[loopStartSec, loopEndSec]`、淡入淡出计时器及目标音量），由 `m_activeDeckIndex` 指向当前主碟，既有 `PlayMusic`、`StopMusic`、`GetMusicPosition` 等全部外部调用语义完全兼容。
- **新增核心交叉混音接口**：
  ```cpp
  bool CrossfadeMusic(const std::string& path,
                      double startSec,
                      double loopStartSec,
                      double loopEndSec,
                      int fadeMs = 280);
  ```
  - **切歌无缝交叉淡变**：切歌时当前主碟调用 `ma_sound_set_fade_in_milliseconds` 在 `280 ms` 内平滑淡出并在 `Update(dt)` 归零后自动回收资源；副碟同步加载新曲、`seek` 至 `startSec`、以 `280 ms` 线性淡入起播并晋升为新主碟，同步切换 `AudioVisualizer` 数据源，快速上下滚轮切歌丝滑无卡顿、无爆音。
  - **`SceneSelect` 22 秒高潮段无缝循环预听**：
    - 读取谱面元数据 `preview_start_ms`（若为 `0` 则取曲长 `25%` 处），设定 `loopStartSec = previewStartSec`、`loopEndSec = std::min(duration - 0.5, previewStartSec + 22.0)`；
    - 当 `AudioManager::Update(dt)` 监测到当前预听游标到达 `loopEndSec - fadeMs / 1000.0` 时，自动在空闲副碟从 `loopStartSec` 预热同曲并执行 `280 ms` 碟间自循环交叉淡入淡出，彻底消除传统硬 `seek` 回跳产生的断音感。

### 3.2 真实 Suno 菜单环境 BGM (`resources/sound/bgm/moonlit_menu.ogg`) 与全大厅律动耦合
- **真实音源转码入库（零程序化合成 BGM）**：
  - 使用 `ffmpeg` 将仓库内真实创作的 Suno 氛围曲源 `download/songs/【Intro】 (1).mp3` 转码为 `44100 Hz` 双声道 Vorbis `resources/sound/bgm/moonlit_menu.ogg`（`-vn -map_metadata -1 -fflags +bitexact -flags:a +bitexact -ac 2 -ar 44100 -c:a libvorbis -q:a 4.5`），确保音质纯净且构建哈希可复现。
- **配置开关与四大外围场景无缝驻留**：
  - 新增 `ConfigKeys::kMenuBgmEnabled = "audio.menu_bgm_enabled"`（`bool`，默认 `true`），在 `SceneSettings` 音频页提供实时开关（关闭时 `280 ms` 柔和淡出停止，开启时即时淡入恢复）。
  - 在 `SceneMenu`（主菜单）、`SceneStats`（生涯战绩）、`SceneCourse`（樱华道场）与 `SceneMarket`（在线谱面市场）的 `OnEnter()` 中统一调用幂等驻留接口 `AudioManager::EnsureMenuBgm()`：
    - 若当前主碟已在播放 `moonlit_menu.ogg`，跨上述四个场景切换时**保持原播放进度连续不断流**；
    - 若从 `SceneSelect` / `SceneGame` / `SceneEditor` 返回主菜单体系，则通过 `280 ms` `CrossfadeMusic` 自然过渡回 `moonlit_menu.ogg` 全曲循环。
- **`AudioVisualizer` 视觉脉冲耦合**：
  - `moonlit_menu.ogg` 播放期间持续驱动 `AudioVisualizer` 实时频谱与低频节拍脉冲，联动主菜单与外围大厅的背景樱瓣飘落微加速、标题徽章呼吸辉光及底部装饰频谱条律动（当开启无障碍 `reduced_motion` 时自动抑制缩放脉冲幅度）。

---

## 4. 并行子智能体切分与 `v2.4.0` 验收清单

### 4.1 互斥文件域并行建议
1. **音频核心与合成器域**：独占 `src/audio/*`、`resources/sound/bgm/moonlit_menu.ogg`。
2. **配置与皮肤域**：独占 `src/core/config.*`、`src/core/skin_manager.*`。
3. **设置页、选歌与大厅场景域**：独占 `src/scene/scene_settings.*`、`src/scene/scene_select.*`、`src/scene/scene_menu.*`、`src/scene/scene_stats.*`、`src/scene/scene_course.*`、`src/scene/scene_market.*`。
4. **局内打击音事件与 F3 诊断域**：独占 `src/scene/scene_game.*`、`src/game/play_session.*`。
5. **单测与文档域**：独占 `tests/*`、`doc/*`。

### 4.2 门禁验收标准
- `tests/` 新增：
  - `sfx-v5` 六套主题 × 12 类打击/舞台音效 WAV 生成完整性、RIFF 头合法性与非静音能量断言测试；
  - `SkinManager` 12 类自定义 `hitsounds/*.wav` 逐文件覆盖与多级回退单测；
  - 离线时钟模式下 `CrossfadeMusic` 游标推进、22 秒区间循环衔接、多复音并发增益缩放曲线与新增 `audio.*` 配置项序列化回归测试。
- 本地 Release 构建、全量 `ctest`、`Sakura.exe --smoke-test`（覆盖大厅 BGM、选歌切曲预听、局内全类型打击音触发与设置页音频遥测截图校验）及 `--verify-online` 全绿。
- 通过 `autopilot_release.py --release --target-tag v2.4.0 --channel stable --bump minor` 完成双产物打包发布与 `steven-lee.tech` 在线服同步。
