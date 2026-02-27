# Sakura-樱 开发路线图 (Vibe-Coding 版)

> 每个 Step 都是一个可以直接交给 Copilot 的任务。按顺序执行即可。
> 标记 `[PROMPT]` 的是建议直接复制给 Copilot 的提示词。

---

## 项目概况

| 项目 | 说明 |
|------|------|
| **名称** | Sakura-樱 |
| **类型** | 混合模式音乐节奏游戏（键盘4K下落 + 鼠标点击） |
| **技术栈** | C++20 / SDL3 / SDL3 GPU API / CMake / vcpkg / MSVC |
| **目标平台** | Windows 10/11 (64-bit) |
| **美术风格** | 日系动漫 + 樱花核心视觉 |

```
v0.1 Foundation ──→ v0.2 Playable ──→ v0.3 Editor ──→ v0.5 Content ──→ v0.8 Online ──→ v1.0 Release
```

---

## Phase 0 — 技术基建 (v0.1)

> 搭建骨架，验证渲染管线，建立所有基础系统。

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
>
> **验收：** 深色背景 + 左上角白色矩形，缩放窗口时矩形比例保持。

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

---

### Step 0.10 — 缓动函数

创建：`src/utils/easing.h`（header-only）

> [PROMPT] sakura::utils::Easing 命名空间，所有函数 constexpr float Func(float t)，t∈[0,1]：
> Linear, EaseInQuad, EaseOutQuad, EaseInOutQuad, EaseInCubic, EaseOutCubic, EaseInOutCubic, EaseInExpo, EaseOutExpo, EaseInOutExpo, EaseInBack, EaseOutBack, EaseInOutBack, EaseInElastic, EaseOutElastic, EaseInBounce, EaseOutBounce

---

### Step 0.11 — 精灵渲染

扩展 Renderer。

> [PROMPT] 在 Renderer 添加：
> - DrawSprite(TextureHandle, NormRect dest, float rotation=0, Color tint=White, float alpha=1.0)
> - DrawCircleOutline(float cx, float cy, float radius, Color, float thickness) — 全部归一化坐标
> - DrawCircleFilled(float cx, float cy, float radius, Color)
> - DrawLine(float x1, float y1, float x2, float y2, Color, float thickness)
>
> 加载测试 PNG 显示到屏幕验证。

---

### Step 0.12 — CI/CD

> [PROMPT] 将 .github/workflows/build.yml 配置好。确认 CMakePresets.json preset 与 workflow 一致。推送到 GitHub 验证绿勾。

---

**🎯 Phase 0 检查点：** 窗口 + GPU渲染 + 文字 + 输入 + 资源 + 场景管理 + 缓动 + 日志 + CI/CD 全部可用。可以开始写游戏逻辑了。

---

## Phase 1 — 核心玩法 (v0.2)

> 可以完整玩一首歌的全部流程。

### Step 1.1 — 音频管理器

创建：`src/audio/audio_manager.h`, `src/audio/audio_manager.cpp`

> [PROMPT] sakura::audio::AudioManager 单例：
> - Initialize() — SDL3_mixer 初始化
> - PlayMusic(MusicHandle, loops=-1) / PauseMusic / ResumeMusic / StopMusic
> - SetMusicPosition(double seconds) / GetMusicPosition() → double
> - PlaySFX(SoundHandle, channel=-1)
> - SetMusicVolume/SFXVolume/MasterVolume (float 0.0~1.0)
> - Shutdown()

---

### Step 1.2 — 谱面数据结构

创建：`src/game/note.h`, `src/game/chart.h`

> [PROMPT] 按 CHART_FORMAT_SPEC.md 定义 sakura::game 内的所有结构体：
> NoteType枚举(Tap/Hold/Drag/Circle/Slider), JudgeResult枚举(Perfect/Great/Good/Bad/Miss/None), Grade枚举(SS/S/A/B/C/D),
> TimingPoint{time,bpm,timeSig}, SVPoint{time,speed,easing},
> KeyboardNote{time,lane,type,duration,dragToLane,isJudged,result,renderY},
> MouseNote{time,x,y,type,sliderDuration,sliderPath,isJudged,result,approachScale},
> DifficultyInfo{name,level,chartFile,...}, ChartInfo{version,id,title,artist,...,difficulties},
> ChartData{version,timingPoints,svPoints,keyboardNotes,mouseNotes}

---

### Step 1.3 — 谱面加载器

创建：`src/game/chart_loader.h`, `src/game/chart_loader.cpp`

> [PROMPT] ChartLoader 类：
> - LoadChartInfo(infoJsonPath) → optional<ChartInfo>
> - LoadChartData(chartJsonPath) → optional<ChartData>
> - ScanCharts("resources/charts/") → vector<ChartInfo>
> - 用 nlohmann::json 解析，缺失字段用默认值，格式错误 LOG_ERROR 跳过
> - 加载后按 time 排序 notes
>
> 在 resources/charts/test/ 创建测试 info.json + normal.json（5-10个 Tap 音符，BPM 120），验证加载。

---

### Step 1.4 — 游戏状态

创建：`src/game/game_state.h`, `src/game/game_state.cpp`

> [PROMPT] GameState 类管理一局游戏：
> - Start(chartInfo, difficulty) — 加载谱面+音乐
> - Update(dt) — 推进音乐时间
> - Pause/Resume
> - GetCurrentTime() → int ms
> - GetActiveKeyboardNotes/MouseNotes() — 当前时间窗口(time-500ms ~ time+2000ms)内的音符
> - IsFinished() → bool
> - 用二分查找定位活跃窗口

---

### Step 1.5 — 判定系统

创建：`src/game/judge.h`, `src/game/judge.cpp`

> [PROMPT] Judge 类：
> - JudgeWindows { perfect=25, great=50, good=80, bad=120, miss=150 } ±ms
> - JudgeKeyboardNote(note&, hitTime) → JudgeResult（按 P→Gr→Go→B→M 检查）
> - JudgeMouseNote(note&, hitTime, hitX, hitY) → JudgeResult（额外距离检查，归一化容差 0.05）
> - CheckMisses(notes, currentTime) — 超时标 Miss
> - Hold 判定：按下判头部，持续 tick(每100ms)，释放判尾部，综合加权

---

### Step 1.6 — 计分系统

创建：`src/game/score.h`, `src/game/score.cpp`

> [PROMPT] ScoreCalculator 类：
> - Initialize(totalNoteCount) — 每音符基础分 = 1000000/total
> - OnJudge(result) — P:100%, Gr:70%, Go:40%, B:10%, M:0%。连击加成：combo*0.1%(上限10%)
> - GetScore()→int, GetAccuracy()→float, GetCombo/MaxCombo, GetGrade, IsFC/IsAP
> - GetResult() → GameResult 结构体（全部数据打包）

---

### Step 1.7 — 游戏场景

创建：`src/scene/scene_game.h`, `src/scene/scene_game.cpp`

> [PROMPT] SceneGame : Scene。核心游戏场景。
>
> **归一化布局：**
> - 键盘轨道区：x=0.05, y=0.0, w=0.35, h=1.0（4轨每轨宽0.0875）
> - 判定线：y=0.85
> - 鼠标区域：x=0.45, y=0.05, w=0.50, h=0.90
> - HUD：分数(0.96, 0.02右对齐), 连击(0.225, 0.05居中), 准确率(0.96, 0.06)
>
> OnEnter：加载谱面、音乐、背景，初始化 GameState/Judge/ScoreCalculator，播放音乐
> OnUpdate：更新 GameState，检查 Miss，计算每个音符的 renderY:
> ```
> timeDiff = (note.time - currentTime) / 1000.0f;
> noteSpeed = 0.8f; // 可调
> note.renderY = JUDGE_LINE_Y - timeDiff * noteSpeed;
> ```
> OnEvent：ASDF 按下→对应轨道最近未判定音符→JudgeKeyboardNote→OnJudge；鼠标左键→最近鼠标音符→JudgeMouseNote；ESC→暂停
> OnRender：背景→4轨道(半透明竖条)→判定线→键盘音符(Tap矩形/Hold长条/Drag箭头)→鼠标音符(Circle+接近圈/Slider+路径)→HUD→判定文字闪现
>
> **验收：** 测试谱面能玩，音符下落，按键判定，分数显示。

---

### Step 1.8 — 结算场景

创建：`src/scene/scene_result.h`, `src/scene/scene_result.cpp`

> [PROMPT] SceneResult : Scene。
> 接收 GameResult 数据。
> **归一化布局：**
> 标题"RESULT"(0.5,0.08), 评级大字(0.5,0.25,字号0.15), 曲名(0.5,0.40), 分数(0.5,0.50,滚动动画0→目标值1.5s), 准确率(0.3,0.62), 最大连击(0.7,0.62), 5行判定数(0.5,0.72~0.88), FC/AP标记(金色), 按钮"重玩"(0.35,0.93)/"返回"(0.65,0.93)。
> 入场：各元素依次淡入间隔0.1s。

---

### Step 1.9 — 主菜单

创建：`src/scene/scene_menu.h`, `src/scene/scene_menu.cpp`

> [PROMPT] SceneMenu : Scene。
> **归一化布局：**
> 标题"Sakura-樱"(0.5,0.20,字号0.08), 副标题(0.5,0.30),
> 按钮垂直居中："开始游戏"(0.5,0.48)→SlideLeft到Select, "编辑器"(0.5,0.56), "设置"(0.5,0.64), "退出"(0.5,0.72)。
> 版本号(0.5,0.95)。按钮悬停变色+点击缩放。背景纯色渐变。

---

### Step 1.10 — 选歌场景

创建：`src/scene/scene_select.h`, `src/scene/scene_select.cpp`

> [PROMPT] SceneSelect : Scene。OnEnter 用 ChartLoader::ScanCharts 扫描谱面。
> **归一化布局：**
> 标题"SELECT SONG"(0.5,0.04), 歌曲列表(0.02,0.10,0.45,0.80, 每项高0.08, 滚轮滚动),
> 右侧详情面板(0.50,0.10,0.48,0.80)：封面(0.74,0.15,0.20,0.20), 曲名/曲师/作者/BPM(0.52,0.38~0.53左对齐),
> 难度标签(0.52,0.60横排), 星级+音符数(0.52,0.67), 最佳成绩(0.52,0.74)。
> 底部"返回"(0.15,0.93)→SlideRight, "开始"(0.85,0.93)→CircleWipe到Game。Enter/双击也可开始。

---

### Step 1.11 — UI 组件

创建：`src/ui/ui_base.h`, `src/ui/button.h`, `src/ui/button.cpp`, `src/ui/scroll_list.h`, `src/ui/scroll_list.cpp`

> [PROMPT] sakura::ui 组件库，所有坐标归一化：
>
> UIBase 基类：NormRect bounds, isVisible, isEnabled
>
> Button : UIBase：text, colors(normal/hover/press/text), fontSize, onClick(function<void()>)。
> 悬停变色(150ms缓动), 点击缩放到0.95再弹回(100ms)。HandleEvent + Render。
>
> ScrollList : UIBase：items 列表, selectedIndex, scrollOffset。
> 每项高度 itemHeight(归一化), 鼠标滚轮平滑滚动, 点击选中高亮, 双击触发 onDoubleClick。
>
> 在 SceneMenu 和 SceneSelect 中使用。

---

### Step 1.12 — 暂停菜单

创建：`src/scene/scene_pause.h`, `src/scene/scene_pause.cpp`

> [PROMPT] ScenePause : Scene，Push 到栈上作为覆盖层。
> 半透明黑遮罩全屏(0,0,0,128)。居中面板(0.3,0.25,0.4,0.5)。
> "PAUSED"(0.5,0.32), "继续"(0.5,0.45)→Pop, "重新开始"(0.5,0.55)→Pop+Switch新Game, "返回选歌"(0.5,0.65)→Pop+Switch到Select。
> ESC → 继续。OnRender 先绘制下层场景再绘制遮罩。

---

**🎯 Phase 1 检查点：** 主菜单→选歌→游戏→结算 完整流程可跑通。键盘+鼠标双模式判定、计分、评级全部工作。

---

## Phase 2 — 编辑器与设置 (v0.3)

### Step 2.1 — 设置场景

> [PROMPT] SceneSettings : Scene。左侧分类标签(通用/音频/按键/显示), 右侧对应项。
> 通用：流速滑块(0.5~2.0), 判定窗口调节(±5ms)
> 音频：音乐/音效/全局音量滑块
> 按键：ASDF 重绑定(点击后按键录入)
> 显示：全屏切换, 帧率上限(60/120/144/无限)
> 新增 UI: Slider 滑块组件, Toggle 开关组件。
> 设置实时生效，auto-save 到 config/settings.json。

---

### Step 2.2 — SQLite 数据层

创建：`src/data/database.h`, `src/data/database.cpp`

> [PROMPT] Database 单例（sakura::data）：
> - Initialize(dbPath) — 创建/打开 SQLite，建表(scores/settings/achievements/statistics)
> - SaveScore(GameResult)
> - GetBestScore(chartId, difficulty) → optional<ScoreRecord>
> - GetTopScores(chartId, difficulty, limit=10) → vector
> - GetSetting(key)/SetSetting(key, value)
> - IncrementStatistic(key, amount)
> 在结算时 SaveScore，在选歌时读最佳成绩。

---

### Step 2.3 — 延迟校准

> [PROMPT] SceneCalibration : Scene。BPM 120 tick 音效循环播放，玩家按空格对拍。记录 20 次偏差，算平均值作为全局延迟补偿。显示进度和结果，保存到 settings。

---

### Step 2.4 — 编辑器（基础）

创建：`src/scene/scene_editor.h`, `src/scene/scene_editor.cpp`, `src/editor/editor_core.h`, `src/editor/editor_core.cpp`

> [PROMPT] 谱面编辑器第一版。
> **归一化布局：**
> 顶部工具栏(0,0,1,0.06)：音符类型/播放暂停/保存/BPM
> 左侧轨道编辑区(0,0.06,0.35,0.94)：4轨纵向时间轴，网格线(Beat Snap 1/4)，点击放置 Tap，右键删除
> 右侧音符属性面板(0.40,0.06,0.55,0.94)：选中音符的时间/轨道/类型编辑
> 空格播放/暂停，滚轮滚动时间轴，Ctrl+S 保存。
> EditorCore：管理编辑状态、当前工具、选中音符。
> 先只实现 Tap 音符放置和保存。

---

### Step 2.5 — 编辑器撤销/重做

创建：`src/editor/editor_command.h`, `src/editor/editor_command.cpp`

> [PROMPT] Command 模式：EditorCommand 基类(Execute/Undo)。子类：PlaceNoteCommand, DeleteNoteCommand, MoveNoteCommand。CommandHistory 管理栈。Ctrl+Z 撤销，Ctrl+Y 重做。

---

### Step 2.6 — 编辑器完善

> [PROMPT] 编辑器添加：Hold 音符(点击拖拽设置时长)，Circle/Slider 鼠标音符(右侧2D区域)，波形显示(读音频数据)，Beat Snap(Ctrl+滚轮切换1/1~1/8)，谱面信息编辑对话框(曲名/BPM/难度名等)。

---

**🎯 Phase 2 检查点：** 设置可调、成绩存库、编辑器可建谱。

---

## Phase 3 — 视觉与音频 (v0.4)

### Step 3.1 — 粒子系统

创建：`src/effects/particle_system.h`, `src/effects/particle_system.cpp`

> [PROMPT] ParticleSystem 类。Particle 结构体(position归一化, velocity, color, size, life, rotation)。
> 对象池固定大小 MAX_PARTICLES=1000。
> Emit(pos, count, config), Update(dt), Render(renderer)。
> 预设：SakuraPetal(大粒子慢速旋转粉色), HitBurst(小粒子快扩散短命), BackgroundFloat(极慢低透明度)。

---

### Step 3.2 — 发光与拖尾

创建：`src/effects/glow.h`, `src/effects/trail.h` 及 .cpp

> [PROMPT] GlowEffect：DrawGlow(renderer, cx, cy, radius, color, intensity, layers=4) — 多层半透明圆。PulseGlow — 随时间脉冲 intensity。
> TrailEffect：记录历史位置点，渲染透明度渐变线条。用于音符下落拖尾和 Slider 路径。

---

### Step 3.3 — Shader 特效

> [PROMPT] 用 SDL3 GPU API shader 创建后处理效果：
> 1. 高斯模糊 — 暂停背景
> 2. 暗角 — 边缘变暗
> 3. 色彩校正 — 亮度/对比度
> ShaderManager 管理 shader 加载/编译/绑定。HLSL 编写，SDL3 工具编译。

---

### Step 3.4 — 特效集成

> [PROMPT] 集成特效到场景：
> SceneMenu：樱花飘落粒子、标题发光
> SceneGame：音符拖尾、判定爆发粒子(P紫/Gr蓝/Go绿/B黄/M红)、判定线脉冲、轨道按下发光、Miss微震、连击里程碑大特效
> SceneResult：评级字母弹跳粒子

---

### Step 3.5 — 主题系统

创建：`src/core/theme.h`, `src/core/theme.cpp`

> [PROMPT] Theme 类从 config/theme.json 加载。定义：primary/secondary/accent/bg/surface/text 颜色，判定颜色(P/Gr/Go/B/M)，轨道颜色(0~3)，动画时长，特效开关。预设 Dark/Light/Neon 三套。GetColor/GetDuration/IsEffectEnabled。

---

### Step 3.6 — 音效

> [PROMPT] 准备音效文件(先用合成音)：perfect/great/good/bad/miss.wav, button_hover/click.wav, transition.wav。
> AudioManager 添加 PlayJudgeSFX(JudgeResult), PlayUISFX(UISFXType) 便捷方法。

---

**🎯 Phase 3 检查点：** 樱花粒子+发光拖尾+Shader+主题+音效 全部上线。

---

## Phase 4 — 内容与体验 (v0.5)

### Step 4.1 — 新手教程

> [PROMPT] SceneTutorial 4课：键盘Tap → Hold → 鼠标Circle → 双端配合。每课带文字提示+慢速大窗口。首次启动自动进入。

### Step 4.2 — 官方测试谱面

> [PROMPT] resources/charts/ 下创建 3-5 首谱面(info.json+难度json, 按 CHART_FORMAT_SPEC.md)。包含 tutorial(BPM120纯Tap), easy(BPM140双难度), medium(BPM170全类型三难度)。音乐用静音 WAV 占位。

### Step 4.3 — 成就系统

> [PROMPT] 成就管理器 + JSON 定义。内置 first_play/first_fc/first_ap/combo_100/play_10/all_s。解锁时右下角 Toast 通知滑入滑出。

### Step 4.4 — 玩家统计

> [PROMPT] 个人统计页面：总游玩次数/时长、平均准确率、评级分布、常玩歌曲Top3、成就列表。从 SQLite 查询。

---

## Phase 5 — 在线功能 (v0.8)

> 后端另行开发。客户端侧：

### Step 5.1 — API 客户端

> [PROMPT] SDL3_net + nlohmann-json 封装 REST 客户端。Get/Post 异步请求，离线缓存。

### Step 5.2 — 账户/排行/市场 UI

> [PROMPT] 登录注册UI、在线排行榜查询展示、谱面下载列表。所有布局归一化坐标。

---

## Phase 6 — 打磨发布 (v1.0)

### Step 6.1 — 性能优化

> [PROMPT] 纹理Atlas减少draw call、文字缓存、粒子GPU实例化、只渲染可见音符、延迟加载。

### Step 6.2 — 打包发布

> [PROMPT] CMake install + Inno Setup 安装程序 + GitHub Actions 自动打包 Release。
