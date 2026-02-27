# Sakura-樱 开发路线图 v2

> 每个 Step 都是一个可以直接交给 Copilot 的任务。按顺序执行即可。
> 标记 `[PROMPT]` 的是建议直接复制给 Copilot 的提示词。
> 质量优先，稳步推进。在线功能推迟到 v2.0。

---

## 项目概况

| 项目 | 说明 |
|------|------|
| **名称** | Sakura-樱 |
| **类型** | 混合模式音乐节奏游戏（键盘4K下落 + 鼠标点击双操作区域） |
| **技术栈** | C++20 / SDL3 / SDL3 GPU API / CMake / vcpkg / MSVC |
| **目标平台** | Windows 10/11 (64-bit) |
| **美术风格** | 日系动漫 + 樱花核心视觉 |
| **难度范围** | 1.0 ~ 15.0（含小数） |
| **音符类型** | Tap / Hold / Drag / Circle / Slider（5种） |

### 版本规划总览

```
v0.1 Foundation ──→ v0.2 Playable ──→ v0.3 Editor ──→ v0.4 Visual
        ↓                                                    ↓
v0.5 Content ──→ v0.6 Data ──→ v0.7 Polish ──→ v1.0 Release
                                                     ↓
                                              v2.0 Online (另行规划)
```

| 版本 | 阶段 | 核心目标 |
|------|------|----------|
| v0.1 | Phase 0 — 技术基建 | 引擎骨架、渲染管线、基础系统 |
| v0.2 | Phase 1 — 核心玩法 | 完整可玩流程：选歌→游戏→结算 |
| v0.3 | Phase 2 — 编辑器与设置 | 谱面编辑器、设置系统、本地存储 |
| v0.4 | Phase 3 — 视觉与音频 | 粒子/Shader/主题/音效/背景视频 |
| v0.5 | Phase 4 — 内容与体验 | 教程、测试谱面、成就、统计、PP |
| v0.6 | Phase 5 — 数据与回放 | Replay系统、数据导出导入、详细报告 |
| v0.7 | Phase 6 — 打磨优化 | 性能、背景视频、自动更新、打包 |
| v1.0 | Phase 7 — 发布 | 最终测试、安装程序、正式发布 |
| v2.0 | Phase 8 — 在线功能 | 账号、排行榜、谱面市场、云同步 |

---

## Phase 0 — 技术基建 (v0.1)

> 搭建骨架，验证渲染管线，建立所有基础系统。
> 完成后应拥有一个能渲染图形/文字、处理输入、管理场景的完整引擎框架。

### Step 0.1 — 项目骨架

**前置条件：** 已完成 QUICKSTART.md。

完成后的目录：
```
Sakura/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── .gitignore
├── .github/
│   ├── workflows/build.yml
│   └── instructions/sakura.instructions.md
├── resources/
│   └── fonts/
│       ├── NotoSansSC-Regular.ttf
│       └── NotoSansSC-Bold.ttf
└── src/
    └── main.cpp              ← 最小 SDL3 窗口
```

> [PROMPT] 根据项目根目录的 CMakeLists.txt、CMakePresets.json、vcpkg.json 配置构建。在 src/main.cpp 中写一个最小 SDL3 程序：创建窗口"Sakura-樱"(1920x1080)、用 SDL_CreateGPUDevice 创建 GPU device、SDL_ClaimWindowForGPUDevice、每帧清屏为深蓝色(RGB 15,15,35)、处理 SDL_EVENT_QUIT。确保能编译运行。

**验收：** 窗口弹出深蓝色背景，按 X 关闭。

---

### Step 0.2 — 日志系统

创建：`src/utils/logger.h`, `src/utils/logger.cpp`

> [PROMPT] 创建 sakura::utils::Logger 类。用 spdlog 实现。
> - Init(logFilePath) — 创建 stdout color sink + rotating file sink (5MB, 3个文件)
> - 全局宏：LOG_INFO(...), LOG_WARN(...), LOG_ERROR(...), LOG_DEBUG(...)
> - 在 main.cpp 初始化时调用 Logger::Init("logs/sakura.log")
> - 遵循编码规范：PascalCase 函数名，m_ 成员前缀，Allman 大括号，#pragma once

**验收：** 编译通过，控制台和 logs/sakura.log 都有彩色/带时间戳日志输出。

---

### Step 0.3 — App 类与主循环

创建：`src/core/app.h`, `src/core/app.cpp`, `src/core/timer.h`, `src/core/timer.cpp`

> [PROMPT] 创建 sakura::core::App 类（Initialize, Run, Shutdown）。
> 主循环：固定时间步长更新(60Hz, FIXED_TIMESTEP=1.0/60.0) + 可变帧率渲染。
> Timer 类用 SDL_GetPerformanceCounter 实现，提供 GetDeltaTime(), GetElapsedTime(), GetFPS()。
> 将 main.cpp 中的 SDL 初始化移入 App::Initialize()。main.cpp 只剩：
> ```cpp
> int main(int argc, char* argv[]) { App app; if(app.Initialize()) app.Run(); app.Shutdown(); return 0; }
> ```

**验收：** 程序正常运行，FPS 日志输出正确。

---

### Step 0.4 — 窗口管理

创建：`src/core/window.h`, `src/core/window.cpp`

> [PROMPT] 创建 sakura::core::Window 类封装 SDL_Window。
> - Create(title, width, height) — 创建窗口，默认 1920x1080
> - ToggleFullscreen() — 全屏/窗口切换
> - GetWidth()/GetHeight() — 当前窗口像素尺寸
> - HandleResize(SDL_Event&) — 窗口大小变化时更新尺寸
> - F11 热键全屏切换
> - App 类持有 Window 成员。

**验收：** 窗口可拖拽调整大小，F11 切换全屏/窗口。

---

### Step 0.5 — GPU 渲染器

创建：`src/core/renderer.h`, `src/core/renderer.cpp`

> [PROMPT] 创建 sakura::core::Renderer 封装 SDL3 GPU API：
>
> 数据类型：
> ```cpp
> struct Color { uint8_t r, g, b, a; static Color White, Black, Red, ...; };
> struct NormRect { float x, y, width, height; SDL_FRect ToPixel(int sw, int sh) const; };
> ```
>
> 方法：
> 1. Initialize(SDL_Window*) — SDL_CreateGPUDevice, claim window
> 2. BeginFrame() — acquire command buffer + swapchain texture, begin render pass
> 3. EndFrame() — end render pass, submit
> 4. Clear(Color) — 设置清屏色
> 5. DrawFilledRect(NormRect, Color) — 画填充矩形（归一化坐标转像素）
> 6. DrawRectOutline(NormRect, Color, float thickness)
> 7. ToPixelX/Y/W/H(float) — 归一化→像素
> 8. GetScreenWidth/Height()
>
> 在 App 中使用。画一个白色矩形 {0.1, 0.1, 0.2, 0.2} 验证坐标系统。

**验收：** 深色背景 + 左上角白色矩形，缩放窗口时矩形比例保持。

---

### Step 0.6 — 输入系统

创建：`src/core/input.h`, `src/core/input.cpp`

> [PROMPT] 创建 sakura::core::Input 静态类：
> - ProcessEvent(SDL_Event&) — 更新内部状态
> - IsKeyPressed/Held/Released(SDL_Scancode)
> - IsMouseButtonPressed/Held/Released(int button)
> - GetMousePosition() → {float x, float y} 归一化坐标 (0.0~1.0)
> - GetMousePixelPosition() → {int x, int y}
> - Update() — 每帧末尾重置 pressed/released 状态
>
> 内部用两个 bool 数组存上帧和当帧状态。App::ProcessEvents 中调 ProcessEvent，Update 末尾调 Input::Update。

**验收：** 按键/鼠标状态正确检测，日志输出确认 pressed/held/released 区分正常。

---

### Step 0.7 — 资源管理器

创建：`src/core/resource_manager.h`, `src/core/resource_manager.cpp`

> [PROMPT] 创建 sakura::core::ResourceManager 单例：
> - LoadTexture(path) → optional<TextureHandle>（SDL3_image → SDL_GPUTexture）
> - LoadFont(path, ptSize) → optional<FontHandle>（SDL3_ttf）
> - LoadSound(path) → optional<SoundHandle>（Mix_LoadWAV）
> - LoadMusic(path) → optional<MusicHandle>（Mix_LoadMUS）
> - Get<Type>(handle) 获取原始指针
> - ReleaseAll()
>
> Handle = uint32_t，内部 map<string, Resource> 防重复加载。失败时 LOG_ERROR + return nullopt。
> 初始化时加载 resources/fonts/NotoSansSC-Regular.ttf 作为默认字体。

**验收：** 字体加载成功，重复加载同一资源返回相同 handle。

---

### Step 0.8 — 文字渲染

扩展 Renderer。

> [PROMPT] 在 Renderer 添加：
> DrawText(FontHandle, string_view text, float normX, float normY, float normFontSize, Color, TextAlign = Left)
> - normFontSize = 字号相对屏幕高度比例（0.03 = 高度的3%）
> - TextAlign: Left / Center / Right
> - 内部用 TTF_RenderText_Blended → surface → GPU texture → blit
> - 支持 UTF-8 中文
>
> 测试：屏幕中央显示 "Sakura-樱"，中文正常。

**验收：** 屏幕中央正确渲染中英文混排。

---

### Step 0.9 — 场景管理

创建：`src/scene/scene.h`, `src/scene/scene_manager.h`, `src/scene/scene_manager.cpp`

> [PROMPT] 场景系统（参照架构设计文档）：
>
> Scene 基类（纯虚）：OnEnter, OnExit, OnUpdate(float dt), OnRender(Renderer&), OnEvent(SDL_Event&)
>
> SceneManager：
> - SwitchScene(unique_ptr<Scene>, TransitionType)
> - PushScene / PopScene
> - Update/Render/HandleEvent 委托当前场景
> - 过渡动画：TransitionType 枚举(None/Fade/SlideLeft/SlideRight/SlideUp/SlideDown/Scale/CircleWipe)
> - 过渡期间渲染半透明覆盖层，根据类型做不同效果，持续 400~700ms
>
> 创建 TestScene 画彩色矩形+文字验证。

**验收：** 两个 TestScene 之间可以用 Fade 和 SlideLeft 切换，过渡动画流畅。

---

### Step 0.10 — 缓动函数

创建：`src/utils/easing.h`（header-only）

> [PROMPT] sakura::utils::Easing 命名空间，所有函数 constexpr float Func(float t)，t∈[0,1]：
> Linear, EaseInQuad, EaseOutQuad, EaseInOutQuad, EaseInCubic, EaseOutCubic, EaseInOutCubic, EaseInExpo, EaseOutExpo, EaseInOutExpo, EaseInBack, EaseOutBack, EaseInOutBack, EaseInElastic, EaseOutElastic, EaseInBounce, EaseOutBounce

**验收：** 编译通过，单元测试或视觉测试确认曲线正确。

---

### Step 0.11 — 精灵渲染与几何图形

扩展 Renderer。

> [PROMPT] 在 Renderer 添加：
> - DrawSprite(TextureHandle, NormRect dest, float rotation=0, Color tint=White, float alpha=1.0)
> - DrawSpriteEx(TextureHandle, NormRect src, NormRect dest, float rotation, Color tint, float alpha) — 支持精灵图集裁切
> - DrawCircleOutline(float cx, float cy, float radius, Color, float thickness) — 全部归一化坐标
> - DrawCircleFilled(float cx, float cy, float radius, Color)
> - DrawLine(float x1, float y1, float x2, float y2, Color, float thickness)
> - DrawArc(float cx, float cy, float radius, float startAngle, float endAngle, Color, float thickness) — 弧线
> - DrawRoundedRect(NormRect, float cornerRadius, Color, bool filled) — 圆角矩形
> - SetBlendMode(BlendMode) — None/Alpha/Additive/Multiply
>
> 加载测试 PNG 显示到屏幕验证。

**验收：** 精灵正确渲染，圆形/弧线/圆角矩形均正确，混合模式有效。

---

### Step 0.12 — 配置系统

创建：`src/core/config.h`, `src/core/config.cpp`

> [PROMPT] 创建 sakura::core::Config 单例类，管理全局配置：
> - Load(path) — 加载 config/settings.json（不存在则创建默认）
> - Save() — 保存当前配置
> - Get<T>(key, defaultValue) / Set<T>(key, value) — 类型安全读写
> - 预定义配置项：
>   - general.note_speed (float, 0.5~3.0, default 1.0)
>   - general.offset (int, ms, default 0)
>   - audio.music_volume (float, 0.0~1.0, default 0.8)
>   - audio.sfx_volume (float, 0.0~1.0, default 0.8)
>   - audio.master_volume (float, 0.0~1.0, default 1.0)
>   - audio.hitsound (string, default "default")
>   - display.fullscreen (bool, default false)
>   - display.fps_limit (int, 60/120/144/240/0无限, default 144)
>   - display.vsync (bool, default true)
>   - input.key_lane_0~3 (SDL_Scancode, default A/S/D/F)
>   - game.judge_offset (int, ±ms, default 0)
> - 内部用 nlohmann::json 存储，修改时自动标记 dirty

**验收：** 程序退出时配置自动保存，重启后读取正确。

---

### Step 0.13 — CI/CD

> [PROMPT] 将 .github/workflows/build.yml 配置好：
> - 触发条件：push/PR 到 main/develop 分支
> - 矩阵：Debug + Release
> - vcpkg 缓存
> - 构建产物存档
> 确认 CMakePresets.json preset 与 workflow 一致。推送到 GitHub 验证绿勾。

**验收：** GitHub Actions 绿勾，Debug 和 Release 均编译通过。

---

**🎯 Phase 0 检查点：** 窗口 + GPU渲染(矩形/圆/弧/圆角/精灵/文字) + 输入 + 资源 + 配置 + 场景管理 + 缓动 + 日志 + CI/CD 全部可用。引擎框架完备，可以开始写游戏逻辑。
---

## Phase 1 — 核心玩法 (v0.2)

> 实现完整可玩流程：启动→主菜单→选歌→游戏→结算。
> 键盘+鼠标双模式判定、计分、评级全部工作。

### Step 1.1 — 音频管理器

创建：`src/audio/audio_manager.h`, `src/audio/audio_manager.cpp`

> [PROMPT] sakura::audio::AudioManager 单例：
> - Initialize() — SDL3_mixer 初始化，支持 WAV/FLAC/OGG/MP3 格式
> - PlayMusic(MusicHandle, loops=-1) / PauseMusic / ResumeMusic / StopMusic / FadeOutMusic(ms)
> - SetMusicPosition(double seconds) / GetMusicPosition() → double
> - PlaySFX(SoundHandle, channel=-1) — 自动选择空闲通道
> - SetMusicVolume/SFXVolume/MasterVolume (float 0.0~1.0) — 从 Config 读取初始值
> - SetPlaybackSpeed(float speed) — 变速播放（编辑器/练习用）
> - IsPlaying() / IsPaused() → bool
> - GetMusicDuration() → double seconds
> - Shutdown()

**验收：** 播放一段 OGG/WAV 音乐，音量调节生效，暂停/恢复正常。

---

### Step 1.2 — 谱面数据结构

创建：`src/game/note.h`, `src/game/chart.h`

> [PROMPT] 按 CHART_FORMAT_SPEC.md 定义 sakura::game 内的所有结构体和枚举：
>
> ```cpp
> enum class NoteType { Tap, Hold, Drag, Circle, Slider };
> enum class JudgeResult { Perfect, Great, Good, Bad, Miss, None };
> enum class Grade { SS, S, A, B, C, D };
>
> struct TimingPoint { int time; float bpm; int timeSigNumerator=4; int timeSigDenominator=4; };
> struct SVPoint { int time; float speed; std::string easing="linear"; };
>
> struct KeyboardNote {
>     int time; int lane; NoteType type;
>     int duration=0; int dragToLane=-1;
>     bool isJudged=false; JudgeResult result=JudgeResult::None;
>     float renderY=0; float alpha=1.0f;
> };
>
> struct MouseNote {
>     int time; float x, y; NoteType type;
>     int sliderDuration=0;
>     std::vector<std::pair<float,float>> sliderPath;
>     bool isJudged=false; JudgeResult result=JudgeResult::None;
>     float approachScale=2.0f; float alpha=1.0f;
> };
>
> struct DifficultyInfo {
>     std::string name; float level; std::string chartFile;
>     int noteCount=0; int holdCount=0; int mouseNoteCount=0;
> };
>
> struct ChartInfo {
>     int version=2; std::string id, title, artist, charter, source;
>     std::vector<std::string> tags;
>     std::string musicFile, coverFile, backgroundFile;
>     int previewTime=0; float bpm; int offset=0;
>     std::vector<DifficultyInfo> difficulties;
> };
>
> struct ChartData {
>     int version=2;
>     std::vector<TimingPoint> timingPoints;
>     std::vector<SVPoint> svPoints;
>     std::vector<KeyboardNote> keyboardNotes;
>     std::vector<MouseNote> mouseNotes;
> };
> ```
>
> 注意 level 字段使用 float 支持小数难度(如 7.5)。

**验收：** 编译通过，所有结构体可正常构造和赋值。

---

### Step 1.3 — 谱面加载器

创建：`src/game/chart_loader.h`, `src/game/chart_loader.cpp`

> [PROMPT] ChartLoader 类：
> - LoadChartInfo(infoJsonPath) → optional<ChartInfo>
> - LoadChartData(chartJsonPath) → optional<ChartData>
> - ScanCharts("resources/charts/") → vector<ChartInfo> — 递归扫描所有含 info.json 的目录
> - ValidateChartData(ChartData&) → bool — 检查音符时间合法性、轨道范围、坐标范围
> - 用 nlohmann::json 解析，缺失字段用默认值，格式错误 LOG_ERROR + 跳过
> - 加载后按 time 排序所有 notes
> - 支持版本兼容：v1 格式自动转换为 v2
>
> 在 resources/charts/test/ 创建测试谱面：
> - info.json：BPM 120, title "Test Song"
> - normal.json：10个 Tap + 2个 Hold + 3个 Circle，覆盖键盘和鼠标端

**验收：** 加载测试谱面成功，日志输出音符数量正确。

---

### Step 1.4 — 游戏状态管理

创建：`src/game/game_state.h`, `src/game/game_state.cpp`

> [PROMPT] GameState 类管理一局游戏的完整状态：
> - Start(chartInfo, difficultyIndex) — 加载谱面数据+音乐资源
> - Update(dt) — 基于音乐播放位置同步游戏时间（而非累加 dt，避免音画不同步）
> - Pause() / Resume()
> - GetCurrentTime() → int ms（精确到毫秒）
> - GetActiveKeyboardNotes() → span — 当前窗口内(time-500ms ~ time+2000ms)的键盘音符
> - GetActiveMouseNotes() → span — 当前窗口内的鼠标音符
> - GetCurrentSVSpeed(int time) → float — 当前 SV 速度倍率（SV 变速插值）
> - GetCurrentBPM(int time) → float — 当前 BPM
> - IsFinished() → bool — 所有音符已判定且音乐结束
> - GetProgress() → float 0.0~1.0 — 歌曲进度
> - 用二分查找定位活跃音符窗口，避免遍历全部音符
> - 应用 Config 中的全局 offset

**验收：** 游戏时间与音乐播放完全同步，暂停/恢复不丢帧。

---

### Step 1.5 — 判定系统

创建：`src/game/judge.h`, `src/game/judge.cpp`

> [PROMPT] sakura::game::Judge 类：
>
> **判定窗口（可通过 Config 微调 ±5ms）：**
> ```cpp
> struct JudgeWindows {
>     int perfect = 25;   // ±25ms
>     int great   = 50;   // ±50ms
>     int good    = 80;   // ±80ms
>     int bad     = 120;  // ±120ms
>     int miss    = 150;  // >±150ms 自动 Miss
> };
> ```
>
> - JudgeKeyboardNote(note&, hitTimeMs) → JudgeResult — 从 Perfect 到 Bad 逐级检查
> - JudgeMouseNote(note&, hitTimeMs, hitX, hitY) → JudgeResult — 时间+距离判定（归一化欧氏距离容差 0.06）
> - CheckMisses(notes, currentTime) — 超过 miss 窗口的未判定音符标记 Miss
> - **Hold 判定：** 按下判头部 40% 权重 + 持续 tick(每100ms) 60% 权重，中途松开后续 tick 全 Miss
> - **Drag 判定：** 起始轨道按下判定时间 + 结束轨道按下判定完成
> - **Slider 判定：** 头部点击 30% 权重 + 路径跟踪采样(容差 0.08) 70% 权重
> - GetHitError(noteTime, hitTime) → int ms — 返回偏差（正值=偏早，负值=偏晚）

**验收：** 各种音符类型的判定逻辑正确，Hold 中途松开能正确生成 Miss tick。

---

### Step 1.6 — 计分系统

创建：`src/game/score.h`, `src/game/score.cpp`

> [PROMPT] sakura::game::ScoreCalculator 类：
>
> - Initialize(totalNoteCount) — 每音符基础分 = 1,000,000 / totalNoteCount
> - OnJudge(JudgeResult result) — P:100%, Gr:70%, Go:40%, B:10%, M:0%。连击加成：combo×0.1%(上限10%)
> - GetScore() → int (0 ~ 1,000,000+)
> - GetAccuracy() → float (0.0 ~ 100.0%) — 加权平均
> - GetCombo() / GetMaxCombo() → int
> - GetPerfectCount/GreatCount/GoodCount/BadCount/MissCount() → int
> - GetGrade() → Grade — SS:≥99%且全P/Gr, S:≥95%, A:≥90%, B:≥80%, C:≥70%, D:<70%
> - IsFullCombo() → bool — 0 Miss 0 Bad
> - IsAllPerfect() → bool
> - GetResult() → GameResult 结构体（打包全部数据：chartId, difficulty, score, accuracy, maxCombo, grade, 各判定计数, isFC, isAP, playedAt, hitErrors）

**验收：** 各种判定组合计算得分和准确率正确。

---

### Step 1.7 — UI 组件库

创建：`src/ui/ui_base.h`, `src/ui/button.h`, `src/ui/button.cpp`, `src/ui/scroll_list.h`, `src/ui/scroll_list.cpp`, `src/ui/label.h`, `src/ui/label.cpp`, `src/ui/progress_bar.h`, `src/ui/progress_bar.cpp`

> [PROMPT] sakura::ui 组件库，**所有坐标归一化**：
>
> **UIBase 基类：** NormRect bounds, bool isVisible/isEnabled, float opacity。virtual HandleEvent/Render/Update。
>
> **Label：** text, fontHandle, fontSize(归一化), color, align(Left/Center/Right)。支持文字阴影。
>
> **Button : UIBase：** text, colors{normal/hover/pressed/disabled/text}, fontSize, cornerRadius, onClick 回调。悬停平滑变色(150ms EaseOutQuad), 点击缩放 0.95 弹回(100ms EaseOutBack)。支持 disabled 状态。
>
> **ScrollList : UIBase：** items 列表, selectedIndex, scrollOffset, itemHeight(归一化)。鼠标滚轮平滑滚动(EaseOutCubic), 点击选中高亮, 双击 onDoubleClick。惯性滚动+边界回弹。
>
> **ProgressBar : UIBase：** value(0.0~1.0), bgColor, fillColor, interpolatedValue(平滑过渡)。可选百分比文字。

**验收：** 所有组件在测试场景中正确渲染和响应交互。

---

### Step 1.8 — 启动画面与加载场景

创建：`src/scene/scene_splash.h`, `src/scene/scene_splash.cpp`, `src/scene/scene_loading.h`, `src/scene/scene_loading.cpp`

> [PROMPT] **SceneSplash 启动画面：**
> - "Sakura-樱" logo 居中(字号 0.12)
> - 淡入(0.8s)→停留(1.5s)→淡出(0.8s)
> - 底部 "Loading..." 闪烁
> - 期间异步预加载全局资源（字体、通用音效、UI纹理）
> - 完成后 Fade 到主菜单
>
> **SceneLoading 通用加载场景：**
> - 可复用的加载界面，接收加载任务列表
> - 居中旋转动画 + 进度条(0.3, 0.55, 0.4, 0.03) + 百分比 + 随机 tips
> - 加载完成后回调跳转目标场景

**验收：** Splash→Loading→主菜单，过渡流畅。

---

### Step 1.9 — 主菜单

创建：`src/scene/scene_menu.h`, `src/scene/scene_menu.cpp`

> [PROMPT] SceneMenu : Scene。
> **归一化布局：**
> - 标题 "Sakura-樱" (0.5, 0.22, 字号 0.08, 居中)
> - 副标题 "Mixed-Mode Rhythm Game" (0.5, 0.31, 字号 0.025, 半透明)
> - 按钮垂直排列(宽 0.22, 高 0.055, 圆角)：
>   "开始游戏"(0.5, 0.48)→SlideLeft 到 Select / "编辑器"(0.5, 0.56) / "设置"(0.5, 0.64) / "退出"(0.5, 0.72)
> - 版本号 (0.5, 0.95, 从 version.h 读取)
> - 入场动画：标题从上滑入(0.3s), 按钮依次从右滑入(间隔 0.1s)

**验收：** 主菜单显示正确，按钮交互流畅，入场动画效果好。

---

### Step 1.10 — 选歌场景

创建：`src/scene/scene_select.h`, `src/scene/scene_select.cpp`

> [PROMPT] SceneSelect : Scene。OnEnter 用 ChartLoader::ScanCharts 扫描谱面。
> **归一化布局：**
> - 标题 "SELECT SONG" (0.5, 0.04)
> - 左侧歌曲列表 ScrollList (0.02, 0.10, 0.45, 0.80)：每项曲名+曲师+难度指示点，键盘上下切换
> - 右侧详情面板 (0.50, 0.10, 0.48, 0.80)：封面图+曲名+曲师+谱师+BPM+难度标签横排(点击切换)+音符数+最佳成绩
> - 底部 "返回"(0.15, 0.93)→SlideRight / "开始"(0.85, 0.93)→Loading→Game
> - 歌曲预览：选中 0.5s 后播放 previewTime 位置音乐片段(淡入/淡出)

**验收：** 显示测试谱面列表，选中可查看详情，预览音乐正常。

---

### Step 1.11 — 游戏场景（核心）

创建：`src/scene/scene_game.h`, `src/scene/scene_game.cpp`

> [PROMPT] SceneGame : Scene。核心游戏场景。
>
> **归一化布局：**
> - 背景层：歌曲背景图(透明度30%)，无则默认渐变
> - 键盘轨道区：x=0.05, y=0.0, w=0.35, h=1.0（4轨每轨宽0.0875，半透明交替明暗，顶部渐变消失）
> - 判定线：y=0.85, 横跨键盘轨道区, 白色发光
> - 鼠标区域：x=0.45, y=0.05, w=0.50, h=0.90（半透明边框）
> - HUD：分数(0.96,0.02右对齐) / 连击(0.225,0.05居中,combo≥10显示) / 准确率(0.96,0.06) / 进度条(底部) / 时间(0.02,0.96)
>
> **OnEnter：** 加载资源→倒计时 3-2-1(缩放动画)→播放音乐
>
> **OnUpdate：** 同步音乐时间→获取活跃音符→CheckMisses→计算 renderY(带 SV 变速)→计算 approachScale→已判定淡出→歌曲结束→结算
>
> **OnEvent：** ASDF 按下→最近未判定音符→Judge→Score→判定闪现 / 鼠标左键→鼠标音符判定 / Hold/Slider 持续 tick / ESC→暂停
>
> **OnRender：** 背景→轨道+判定线→键盘音符(Tap矩形/Hold长条/Drag箭头)→鼠标音符(Circle+接近圈/Slider+路径)→HUD→判定文字(0.5s淡出)

**验收：** 测试谱面可完整游玩，音符下落/接近、按键判定、分数连击全部正常。

---

### Step 1.12 — 暂停菜单

创建：`src/scene/scene_pause.h`, `src/scene/scene_pause.cpp`

> [PROMPT] ScenePause : Scene，Push 到栈上。进入时暂停音乐。
> 半透明黑遮罩(0,0,0,160) + 居中面板(0.3,0.25,0.4,0.5 圆角)。
> "PAUSED"(0.5,0.32) / "继续"(0.5,0.45)→Pop / "重新开始"(0.5,0.55) / "返回选歌"(0.5,0.65)。
> ESC→继续。OnRender 先绘制下层场景再绘制遮罩。

**验收：** ESC 弹出暂停，继续恢复流畅，音乐同步无偏差。

---

### Step 1.13 — 结算场景

创建：`src/scene/scene_result.h`, `src/scene/scene_result.cpp`

> [PROMPT] SceneResult : Scene。接收 GameResult。
> **归一化布局：**
> - "RESULT"(0.5,0.06) / 评级大字(0.5,0.22,字号0.15,对应颜色) / FC/AP标记
> - 曲名+难度(0.5,0.38) / 分数(0.5,0.47,字号0.06,**数字滚动动画**0→目标值1.5s EaseOutExpo)
> - 准确率(0.3,0.58) / 最大连击(0.7,0.58)
> - 5行判定统计(0.5,0.67~0.84)：P紫/Gr蓝/Go绿/B黄/M红
> - 偏差分布图(0.5,0.89,0.6,0.05)：横轴-150ms~+150ms 点状分布，Perfect区高亮
> - "重玩"(0.35,0.95) / "返回"(0.65,0.95)
> - 入场：元素从上到下依次淡入(间隔0.08s)

**验收：** 分数滚动动画流畅，偏差图显示正确。

---

**🎯 Phase 1 检查点：** Splash→主菜单→选歌(预览)→倒计时→游戏(双端判定+SV)→暂停→结算(偏差图) 完整流程可跑通。
---

## Phase 2 — 编辑器与设置 (v0.3)

> 谱面编辑器和完整设置系统。玩家可调游戏参数，谱师可制作谱面。

### Step 2.1 — 额外 UI 组件

创建：`src/ui/slider.h/.cpp`, `src/ui/toggle.h/.cpp`, `src/ui/dropdown.h/.cpp`, `src/ui/text_input.h/.cpp`, `src/ui/tab_bar.h/.cpp`, `src/ui/toast.h/.cpp`

> [PROMPT] 新增 UI 组件（全归一化坐标）：
> - **Slider 滑块：** min/max/value/step, 拖拽/点击调整, onChange 回调, 可选标签+数值
> - **Toggle 开关：** on/off 滑动动画(200ms EaseOutQuad), onChange 回调
> - **Dropdown 下拉框：** 收起/展开, z-order 最高, onChange 回调
> - **TextInput 输入框：** 单行, 光标闪烁, 支持退格/左右/全选/复制粘贴, placeholder, maxLength
> - **TabBar 标签栏：** 水平排列, 选中高亮+底部指示条滑动, onChange 回调
> - **Toast 通知：** 右下角滑入/停留3s/滑出, info/success/warning/error 四种, ToastManager 队列管理

**验收：** 每个组件在测试场景中正确渲染和交互。

---

### Step 2.2 — 设置场景

创建：`src/scene/scene_settings.h`, `src/scene/scene_settings.cpp`

> [PROMPT] SceneSettings : Scene。
> 左侧纵向 TabBar(0.03,0.12,0.15,0.75)：通用/音频/按键/显示。右侧内容区(0.22,0.12,0.73,0.75)。
> - **通用：** 流速 Slider(0.5~3.0, 步长0.1), 判定偏移 Slider(-50~+50ms), 延迟校准按钮
> - **音频：** 主/音乐/音效 音量 Slider(0~100%), Hitsound Dropdown(default/soft/drum), 调整时预览音效
> - **按键：** 轨道1-4绑定(默认ASDF, 点击监听+冲突检测), 暂停键/快速重试键, "恢复默认"
> - **显示：** 全屏 Toggle, 帧率 Dropdown(60/120/144/240/无限), VSync Toggle
> - 底部"返回"(0.5,0.93)。实时生效，自动保存到 config/settings.json

**验收：** 所有设置项正常工作，游戏中修改即时生效。

---

### Step 2.3 — SQLite 数据层

创建：`src/data/database.h`, `src/data/database.cpp`

> [PROMPT] sakura::data::Database 单例：
> - Initialize("data/sakura.db") — 创建/打开, 自动建表(scores/statistics/achievements)
> - SaveScore(GameResult) / GetBestScore(chartId, diff) / GetTopScores(chartId, diff, limit=10)
> - GetAllBestScores() → vector — 用于 PP 计算
> - IncrementStatistic(key, amount) / GetStatistic(key)
> - SaveAchievement / GetAchievements
> - GetTotalPlayCount / GetTotalPlayTimeSeconds
> - 集成到结算(SaveScore)和选歌(GetBestScore)

**验收：** 结算后成绩存库，选歌显示最佳成绩。

---

### Step 2.4 — 延迟校准

创建：`src/scene/scene_calibration.h`, `src/scene/scene_calibration.cpp`

> [PROMPT] SceneCalibration : Scene。BPM 120 tick 循环 + 脉冲圆圈(0.5,0.45)。
> 玩家按空格对拍，收集 20 次有效偏差(忽略>200ms)，算平均+标准差。
> 显示结果 + "应用"→保存 Config(general.offset) + Toast / "重试"。
> 从设置进入。

**验收：** 校准流程完整，保存后判定时间补偿正确。

---

### Step 2.5 — 编辑器（基础）

创建：`src/scene/scene_editor.h/.cpp`, `src/editor/editor_core.h/.cpp`, `src/editor/editor_timeline.h/.cpp`

> [PROMPT] 谱面编辑器第一版。
> **布局：** 顶部工具栏(0,0,1,0.06)音符类型/播放暂停/保存/BPM/BeatSnap
> 左侧轨道编辑区(0,0.06,0.40,0.94)：4轨时间轴 + 网格线(Beat Snap) + 时间标尺
> 右侧鼠标编辑区(0.42,0.06,0.33,0.60) + 属性面板(0.42,0.68,0.33,0.32)
> 底部全曲缩略轴(0.77,0.06,0.21,0.94)
> **快捷键：** 空格播放/暂停, 滚轮滚动, Ctrl+滚轮切换 BeatSnap(1/1~1/16), 1-5 切换音符, Ctrl+S 保存, Delete 删除
> EditorCore 管理状态, EditorTimeline 管理渲染交互。
> 先完整实现 Tap 音符放置/移动/删除/保存。

**验收：** 放置 Tap 音符，Ctrl+S 保存为 CHART_FORMAT_SPEC.md 格式 JSON。

---

### Step 2.6 — 编辑器撤销/重做

创建：`src/editor/editor_command.h`, `src/editor/editor_command.cpp`

> [PROMPT] Command 模式：EditorCommand 基类(Execute/Undo/GetDescription)。
> 子类：PlaceNoteCommand, DeleteNoteCommand, MoveNoteCommand, ModifyNoteCommand, BatchCommand。
> CommandHistory 栈(最大200步)。Ctrl+Z 撤销, Ctrl+Y 重做。工具栏按钮+tooltip。

**验收：** 放置/删除/移动多步撤销重做正确。

---

### Step 2.7 — 编辑器完善

> [PROMPT] 编辑器添加：
> - Hold(点击拖拽设长度), Drag(拖拽箭头设目标轨道), Circle(右侧2D区), Slider(依次点击路径点+双击结束+贝塞尔路径)
> - 波形可视化：读 PCM 数据, 时间轴左侧波形柱状图(半透明绿)
> - 谱面信息编辑对话框 / BPM变化点编辑 / SV变速点编辑
> - 批量操作：框选(Ctrl+拖拽)/全选/批量删除移动 / 网格吸附 / 复制粘贴(Ctrl+C/V) / 镜像(Ctrl+M)

**验收：** 5种音符均可编辑，波形与音乐对齐。

---

### Step 2.8 — 编辑器内试玩

> [PROMPT] F5 进入试玩模式：从当前位置播放，复用 SceneGame 判定/渲染。
> 半屏预览(左时间轴+右游戏画面)。ESC 退出回编辑器恢复到起始位置。
> F6 = 从选中音符 -2s 处开始。试玩结果不存库。

**验收：** 编辑器中 F5 试玩正常，ESC 退出回编辑状态。

---

### Step 2.9 — 谱面导出/导入

> [PROMPT] 新建谱面向导：选音乐文件→输入信息(曲名/BPM/偏移)→选封面→创建目录+info.json+空谱面。
> 打开(文件对话框选 info.json) / Ctrl+S 保存 / Ctrl+Shift+S 另存为。
> 难度管理：下拉切换/新建(可从已有复制)/删除。

**验收：** 从零创建完整谱面，多难度管理正确。

---

**🎯 Phase 2 检查点：** 设置完整可调、成绩持久化、编辑器可建谱/试玩/导出。
---

## Phase 3 — 视觉与音频 (v0.4)

> 粒子系统、Shader特效、主题系统、音效系统、背景系统。
> 让游戏从"能玩"升级到"好看好听"。

### Step 3.1 — 粒子系统

创建：`src/effects/particle_system.h`, `src/effects/particle_system.cpp`

> [PROMPT] ParticleSystem 类。Particle 结构体(position/velocity/acceleration/color/size/life/rotation/alpha 全归一化)。
> 对象池 MAX_PARTICLES=2000。Emit(pos, count, config) / EmitContinuous(pos, rate, config) / Update(dt) / Render / Clear。
> **预设配置：**
> - SakuraPetal — 大粒子(0.02), 粉色渐变, 慢旋转, 3~5s, 飘落
> - HitBurst — 小粒子(0.005), 快扩散, 0.2~0.5s
> - ComboMilestone — 中粒子, 向上喷射+重力, 金色
> - BackgroundFloat — 极小(0.003), 极慢, 低透明度
> - JudgeSpark(Color) — 按判定颜色火花

**验收：** 各粒子效果帧率不受明显影响。

---

### Step 3.2 — 发光与拖尾

创建：`src/effects/glow.h/.cpp`, `src/effects/trail.h/.cpp`

> [PROMPT] **GlowEffect：** DrawGlow(多层半透明圆, Additive混合), PulseGlow(sin脉冲), DrawGlowLine(发光线条)。
> **TrailEffect：** ring buffer 历史位置, 渲染 alpha 渐变线段。用于音符拖尾/Slider路径。

**验收：** 发光视觉良好，拖尾跟随平滑。

---

### Step 3.3 — 屏幕震动

创建：`src/effects/screen_shake.h`, `src/effects/screen_shake.cpp`

> [PROMPT] ScreenShake: Trigger(intensity, duration, decay) → Update(dt) → {offsetX, offsetY}。
> Renderer BeginFrame 应用全局偏移。Miss 触发轻微震动(0.003, 0.15s)。

**验收：** Miss 时屏幕轻微抖动。

---

### Step 3.4 — Shader 特效

创建：`src/effects/shader_manager.h`, `src/effects/shader_manager.cpp`

> [PROMPT] SDL3 GPU API 后处理系统：场景→offscreen render target→shader→swapchain。
> ShaderManager: LoadShader/BindShader/SetUniform/UnbindShader。
> 内置 Shader(HLSL→SPIR-V)：高斯模糊(暂停背景)、暗角、色彩校正、色差(连击特效)。
> Enable/Disable 各效果，Config 开关。

**验收：** 暂停时背景模糊，暗角增强沉浸感。

---

### Step 3.5 — 特效集成

> [PROMPT] 集成到各场景：
> - **SceneMenu：** 樱花飘落(5~8/s) + 标题发光脉冲 + 微浮粒子
> - **SceneGame：** 暗角 + 音符拖尾 + 判定爆发粒子(P紫#B388FF/Gr蓝#64B5F6/Go绿#81C784/B黄#FFD54F/M红#E57373) + 判定线脉冲 + 轨道按下发光 + Miss微震 + 连击里程碑(50/100/200/500/1000大特效+色差) + 判定文字弹出(缩放1.2→1.0+上浮+淡出0.5s)
> - **SceneResult：** 评级字母 EaseOutElastic 弹跳 + FC/AP 金色/彩虹粒子 + 樱花飘落

**验收：** 各场景特效丰富但>60FPS。

---

### Step 3.6 — 主题系统

创建：`src/core/theme.h`, `src/core/theme.cpp`

> [PROMPT] Theme 类从 config/theme.json 加载。颜色(primary/secondary/accent/bg/surface/text/judge_colors/lane_colors/grade_colors)、动画时长、特效开关。
> 预设三套：Sakura(深蓝+粉色)、Midnight(纯黑+紫色霓虹)、Daylight(浅灰+蓝色柔和)。
> 设置中添加主题切换。所有 UI/场景从 Theme 读颜色。

**验收：** 切换主题后全局颜色一致变化。

---

### Step 3.7 — Hitsound 系统

> [PROMPT] resources/sound/sfx/ 下按 set 分目录(default/soft/drum)，每套含 tap/hold_start/hold_tick/circle/slider_start.wav。
> AudioManager 扩展：LoadHitsoundSet(name) / PlayHitsound(NoteType)。
> UI 音效：button_hover/click/transition/result_score/result_grade/toast.wav。PlayUISFX 便捷方法。
> 判定音效：perfect/great/good/bad/miss.wav + PlayJudgeSFX。
> Button 自动播放 hover/click 音效。先用合成占位音效。

**验收：** 各交互有音效，可切换 Hitsound 集。

---

### Step 3.8 — 背景系统

创建：`src/effects/background.h`, `src/effects/background.cpp`

> [PROMPT] BackgroundRenderer：LoadImage(path)→铺满居中裁切, SetDimming(0~1.0), SetBlurEnabled(暂停用)。
> DefaultBackground：无自定义时使用缓慢渐变色+BackgroundFloat 粒子。
> SceneGame 加载谱面 background_file，无则 DefaultBackground。暗化程度可配置。

**验收：** 游戏中正确显示背景，暗化可调。

---

**🎯 Phase 3 检查点：** 粒子+发光拖尾+屏幕震动+Shader后处理+主题+Hitsound+背景 全部上线。视听体验显著提升。
---

## Phase 4 — 内容与体验 (v0.5)

> 新手引导、测试谱面、成就系统、PP 评分、玩家统计、音频可视化。

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
---

## Phase 5 — 数据与回放 (v0.6)

> Replay 系统、数据导出导入、详细结算报告。

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

---

## Phase 6 — 打磨优化 (v0.7)

> 性能优化、背景视频、练习模式、自动更新、最终打磨。

### Step 6.1 — 性能优化

> [PROMPT] 系统性优化：
> **渲染：** 纹理Atlas(小纹理合并减draw call) / 批量渲染 / 文字缓存(LRU 100项) / 只渲染可见音符 / 粒子GPU实例化
> **内存：** 延迟加载+引用计数 / 对象池(粒子/判定文字)
> **CPU：** 二分查找替代线性 / 固定时间步长 / 活跃窗口内判定
> **帧率：** 可配置上限(60/120/144/240/无限) + VSync / F3 调试面板(FPS/帧时间/draw call/粒子数/音符数/GPU内存)

**验收：** Release 1080p 稳定 144+FPS，4K 稳定 60+FPS。

---

### Step 6.2 — 背景视频支持

创建：`src/effects/video_decoder.h/.cpp`, `src/effects/video_renderer.h/.cpp`

> [PROMPT] FFmpeg(libavcodec/libavformat/libswscale) 解码视频。
> VideoDecoder: OpenVideo / SeekTo / GetNextFrame(RGBA), 独立线程解码, ring buffer 3~5帧。
> VideoRenderer: SetVideo / Update(gameTimeMs) / Render(同 BackgroundRenderer 接口)。
> info.json 新增 "video_file", SceneGame 优先使用。支持 MP4(H.264)/WebM(VP9)。
> 性能保护：CPU过高自动降级到静态背景。vcpkg 添加 ffmpeg。

**验收：** 背景视频与音乐同步播放。

---

### Step 6.3 — 游戏内快捷操作

> [PROMPT] 快速重试(` 波浪键): 游戏中/结算直接重开。
> 练习模式：选歌时可选进入, 游戏中 +/- 调速(0.5x~2.0x, 步长 0.25x), HUD 标注 "PRACTICE" + 速度, 不计正式成绩/PP。

**验收：** 快速重试即时生效，练习模式速度调节正常。

---

### Step 6.4 — 版本管理与自动更新

创建：`src/core/updater.h`, `src/core/updater.cpp`

> [PROMPT] version.h.in → CMake configure → version.h (MAJOR.MINOR.PATCH+BUILD)。
> Updater 框架(本地)：CheckForUpdate / DownloadUpdate / ApplyUpdate 预留接口。
> 启动时后台检查(当前读 update_manifest.json)，有新版 Toast + 设置→"检查更新"按钮。
> 实际网络下载 v2.0 实现。

**验收：** 版本号正确显示，更新框架预留完整。

---

### Step 6.5 — 游戏体验打磨

> [PROMPT] 最终打磨：
> **输入：** ProcessEvents 立即判定, 无缓冲延迟
> **视觉：** 连击 milestone 闪光 / 判定显示精确偏差ms / 准确率渐变色 / 分数跳动动画 / 进度条颜色渐变
> **音频同步：** 定期校正漂移(>5ms软校正渐变, >50ms硬校正)
> **流畅度：** 场景切换预加载 / 图片异步加载+占位符
> **防误操作：** 退出确认 / 编辑器未保存提示
> **图标：** resources/images/icon.ico

**验收：** 所有细节打磨到位，无明显粗糙环节。

---

**🎯 Phase 6 检查点：** 性能优化+背景视频+练习模式+更新框架+体验打磨。品质达发布标准。

---

## Phase 7 — 发布 (v1.0)

### Step 7.1 — 全面测试

> [PROMPT] 测试清单：
> **功能：** 完整流程 / 5种音符判定 / Hold+Slider中途松开 / SV+BPM变化 / 暂停同步 / 快速重试 / 所有设置 / 按键重绑 / 延迟校准 / 编辑器全流程 / 撤销重做 / 成就解锁 / PP计算 / Replay录制回放 / 数据导出导入 / 教程
> **兼容：** 1080p/1440p/4K / 全屏窗口切换 / NVIDIA/AMD/Intel GPU / 60/144/240帧率
> **边界：** 空谱面 / 超长谱面(1000+) / 缺失资源 / 格式错误JSON / 数据库损坏
> **性能：** 1080p@60稳定 / 4K@60稳定 / 内存泄漏 / 粒子满负荷

**验收：** 所有测试用例通过。

---

### Step 7.2 — 打包发布

> [PROMPT] CMake install(TARGETS+resources+config+DLLs)。
> Inno Setup 安装程序：默认 C:\Games\Sakura, 桌面快捷方式+开始菜单, 组件选择(本体/谱面/快捷方式), 卸载可保留存档。
> GitHub Actions(tag v* 触发)：Release构建→Inno Setup→GitHub Release(.exe安装包+.zip便携版+更新日志)。
> 便携版：zip 解压即玩, 数据存 data/ 子目录。

**验收：** 安装包在全新 Windows 安装运行正常。

---

**🎯 Phase 7 检查点 (v1.0 Release)：** 完整流程 + 5种音符 + 编辑器 + 粒子/Shader/主题/视频 + 教程 + 成就 + PP + Replay + 自动更新框架。

---

## Phase 8 — 在线功能 (v2.0) 🌐

> 推迟到 v1.0 稳定后开发。以下为规划大纲，详细 Step 另行编写。

### 8.1 — 后端服务（另行开发）

| 组件 | 技术 | 说明 |
|------|------|------|
| API 服务 | Go / Rust / Node.js | REST API + WebSocket |
| 数据库 | PostgreSQL | 用户/成绩/谱面 |
| 对象存储 | S3 / MinIO | 谱面文件/封面/回放 |
| CDN | Cloudflare | 静态资源加速 |
| 认证 | JWT | 令牌鉴权 |

### 8.2 — 客户端网络层

- SDL3_net + nlohmann-json 封装 REST/WebSocket
- 异步请求 + 离线缓存 + 重试 + 限流 + 超时
- 网络状态指示器

### 8.3 — 用户账号系统

- 注册/登录/个人资料/头像上传/密码修改
- 访客模式(不登录可玩，不计排行)

### 8.4 — 在线排行榜

- 全球/好友/地区排行 / 按谱面+难度
- WebSocket 实时推送 / 回放下载观看

### 8.5 — 谱面市场

- 上传/下载/搜索/筛选/评分/评论/下载量
- Ranked/Unranked 分类 / 一键下载安装

### 8.6 — 云同步

- 成绩/设置/成就/回放 云端同步 / 多设备 / 冲突取高分

### 8.7 — 自动更新（完整实现）

- 差量更新 / 后台下载+提示安装 / 回滚机制
---

## 附录 A — 技术栈依赖清单

| 库 | 用途 | vcpkg 包名 |
|----|------|------------|
| SDL3 | 窗口/输入/事件 | sdl3 |
| SDL3_image | 图片加载 | sdl3-image |
| SDL3_ttf | 字体渲染 | sdl3-ttf |
| SDL3_mixer | 音频播放 | sdl3-mixer |
| SDL3_net | 网络(v2.0) | sdl3-net |
| nlohmann/json | JSON 解析 | nlohmann-json |
| SQLite3 | 本地数据库 | sqlite3 |
| spdlog | 日志 | spdlog |
| fmt | 格式化 | fmt |
| FFmpeg | 视频解码 | ffmpeg |
| zlib | 压缩 | zlib |
| freetype | 字体(SDL3_ttf依赖) | freetype |
| Noto Sans SC | CJK 字体 | 手动下载 |

## 附录 B — 目录结构完整规划

```
Sakura/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── version.h.in
├── installer.iss              ← Inno Setup 脚本
├── .github/
│   ├── workflows/build.yml
│   └── instructions/sakura.instructions.md
├── config/
│   ├── settings.json          ← 用户设置
│   └── theme.json             ← 主题配置
├── data/
│   └── sakura.db              ← SQLite 数据库
├── logs/
│   └── sakura.log
├── replays/                   ← 回放文件 (.skr)
├── resources/
│   ├── charts/                ← 谱面
│   │   └── {chart_id}/
│   │       ├── info.json
│   │       ├── easy.json / normal.json / hard.json / expert.json
│   │       ├── music.ogg
│   │       ├── cover.png
│   │       ├── bg.png / bg.mp4
│   ├── fonts/
│   │   ├── NotoSansSC-Regular.ttf
│   │   └── NotoSansSC-Bold.ttf
│   ├── images/
│   │   ├── icon.ico
│   │   └── logo.png
│   ├── data/
│   │   └── achievements.json  ← 成就定义
│   ├── shaders/
│   │   ├── blur.hlsl / blur.spv
│   │   ├── vignette.hlsl / vignette.spv
│   │   ├── color_correction.hlsl / .spv
│   │   └── chromatic.hlsl / .spv
│   └── sound/
│       ├── music/
│       └── sfx/
│           ├── default/ soft/ drum/  ← hitsound sets
│           ├── judge/                ← 判定音效
│           └── ui/                   ← UI 音效
├── doc/
│   ├── ARCHITECTURE.md
│   ├── CHART_FORMAT_SPEC.md
│   ├── CODING_STANDARDS.md
│   ├── DEPENDENCIES.md
│   ├── QUICKSTART.md
│   └── ROADMAP.md
└── src/
    ├── main.cpp
    ├── core/
    │   ├── app.h / .cpp
    │   ├── window.h / .cpp
    │   ├── renderer.h / .cpp
    │   ├── input.h / .cpp
    │   ├── timer.h / .cpp
    │   ├── resource_manager.h / .cpp
    │   ├── config.h / .cpp
    │   ├── theme.h / .cpp
    │   └── updater.h / .cpp
    ├── scene/
    │   ├── scene.h
    │   ├── scene_manager.h / .cpp
    │   ├── scene_splash.h / .cpp
    │   ├── scene_loading.h / .cpp
    │   ├── scene_menu.h / .cpp
    │   ├── scene_select.h / .cpp
    │   ├── scene_game.h / .cpp
    │   ├── scene_pause.h / .cpp
    │   ├── scene_result.h / .cpp
    │   ├── scene_settings.h / .cpp
    │   ├── scene_calibration.h / .cpp
    │   ├── scene_editor.h / .cpp
    │   ├── scene_tutorial.h / .cpp
    │   ├── scene_stats.h / .cpp
    │   └── scene_replay.h / .cpp
    ├── game/
    │   ├── note.h
    │   ├── chart.h
    │   ├── chart_loader.h / .cpp
    │   ├── game_state.h / .cpp
    │   ├── judge.h / .cpp
    │   ├── score.h / .cpp
    │   ├── replay.h / .cpp
    │   ├── achievement_manager.h / .cpp
    │   └── pp_calculator.h / .cpp
    ├── editor/
    │   ├── editor_core.h / .cpp
    │   ├── editor_timeline.h / .cpp
    │   └── editor_command.h / .cpp
    ├── ui/
    │   ├── ui_base.h
    │   ├── label.h / .cpp
    │   ├── button.h / .cpp
    │   ├── scroll_list.h / .cpp
    │   ├── progress_bar.h / .cpp
    │   ├── slider.h / .cpp
    │   ├── toggle.h / .cpp
    │   ├── dropdown.h / .cpp
    │   ├── text_input.h / .cpp
    │   ├── tab_bar.h / .cpp
    │   └── toast.h / .cpp
    ├── audio/
    │   ├── audio_manager.h / .cpp
    │   └── audio_visualizer.h / .cpp
    ├── effects/
    │   ├── particle_system.h / .cpp
    │   ├── glow.h / .cpp
    │   ├── trail.h / .cpp
    │   ├── screen_shake.h / .cpp
    │   ├── shader_manager.h / .cpp
    │   ├── background.h / .cpp
    │   ├── video_decoder.h / .cpp
    │   └── video_renderer.h / .cpp
    ├── data/
    │   └── database.h / .cpp
    ├── net/                   ← v2.0 预留
    │   ├── api_client.h / .cpp
    │   └── net_manager.h / .cpp
    └── utils/
        ├── logger.h / .cpp
        └── easing.h           ← header-only
```

## 附录 C — 场景流程图

```
          ┌──────────────┐
          │   Splash     │ ← 程序启动
          │  (淡入淡出)   │
          └──────┬───────┘
                 │ (首次运行?)
          ┌──────┴──────────────────┐
          │                         │
    ┌─────▼──────┐           ┌──────▼───────┐
    │  Tutorial  │           │   主菜单      │
    │  (教程)    │──完成────→│   (Menu)      │
    └────────────┘           └──────┬───────┘
                                    │
              ┌──────────────┬──────┼──────────────┐
              │              │      │              │
        ┌─────▼────┐  ┌─────▼────┐ │        ┌─────▼────┐
        │  设置    │  │  统计    │ │        │  退出    │
        │Settings  │  │  Stats   │ │        └──────────┘
        │  ├校准   │  │  ├成就   │ │
        └──────────┘  └──────────┘ │
                                   │
                            ┌──────▼───────┐
                     ┌──────│   选歌        │──────┐
                     │      │  (Select)     │      │
                     │      └──────┬───────┘      │
                     │             │               │
               ┌─────▼────┐ ┌─────▼───────┐      │
               │  编辑器  │ │  Loading    │      │
               │ (Editor) │ │  (加载资源)  │      │
               │  ├试玩   │ └─────┬───────┘      │
               └──────────┘       │               │
                            ┌─────▼───────┐      │
                            │   游戏       │      │
                            │  (Game)      │      │
                            │  ├暂停(Push) │      │
                            └─────┬───────┘      │
                                  │               │
                            ┌─────▼───────┐      │
                            │   结算       │──────┘
                            │  (Result)    │ (返回选歌)
                            │  ├详细报告   │
                            │  ├查看回放   │
                            └──────────────┘
```

## 附录 D — 工时估算

| Phase | 预计工时 | 累计 |
|-------|----------|------|
| Phase 0 — 技术基建 | 25-35h | 35h |
| Phase 1 — 核心玩法 | 40-55h | 90h |
| Phase 2 — 编辑器与设置 | 35-50h | 140h |
| Phase 3 — 视觉与音频 | 30-40h | 180h |
| Phase 4 — 内容与体验 | 25-35h | 215h |
| Phase 5 — 数据与回放 | 20-30h | 245h |
| Phase 6 — 打磨优化 | 25-35h | 280h |
| Phase 7 — 发布 | 10-15h | 295h |
| **v1.0 总计** | **210-295h** | |
| Phase 8 — 在线功能 | 60-80h | 375h |

> 以上为 Vibe-Coding 辅助下的估算。实际开发时间可能因调试、迭代而浮动。