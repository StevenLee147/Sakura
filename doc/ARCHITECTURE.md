# Sakura-樱 架构设计文档 (v2.0)

> C++20 / SDL3 / miniaudio + WSOLA / SQLite3 / WinHTTP + TypeScript Hono Online Stack

---

## 1. 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                          main.cpp                            │
│         Application Entry / CLI Smoke & Record Runner        │
└──────────────────────────────┬───────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────┐
│                         App (core/)                          │
│  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌───────────────────┐ │
│  │ Window  │ │ Renderer │ │  Input  │ │ Timer / ClockSync │ │
│  └─────────┘ └──────────┘ └─────────┘ └───────────────────┘ │
│  ┌─────────────────┐ ┌──────────────┐ ┌───────────────────┐ │
│  │ ResourceManager │ │ SkinManager  │ │ Updater v2 HotSwap│ │
│  └─────────────────┘ └──────────────┘ └───────────────────┘ │
└──────────────────────────────┬───────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────┐
│                    SceneManager (scene/)                     │
│  Menu / Select / Game / Result / Course / Arena / Market /   │
│  Stats (B30 Card) / Editor / Tutorial / Calibration / Settings│
└──────────────────────────────┬───────────────────────────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         ▼                     ▼                     ▼
┌────────────────────┐ ┌──────────────┐ ┌────────────────────┐
│  Game Logic (game/)│ │  UI (ui/)    │ │ Effects (effects/) │
│ • PlaySession/Judge│ │ • Button/Tab │ │ • Particles/Trails │
│ • Chart v4 & Lyrics│ │ • ScrollList │ │ • VideoBGA (MP4)   │
│ • CourseSession    │ │ • TextInput  │ │ • Stage v4 Events  │
│ • GhostRunner      │ │ • VisualStyle│ │ • Shader / Glow    │
│ • ChartConverter   │ │ • Toast/Panel│ │ • ScreenShake      │
│ • PPCalculator B30 │ │              │ │ • Moonlit BG       │
└─────────┬──────────┘ └──────────────┘ └────────────────────┘
          │
          ▼
┌────────────────────┐ ┌────────────────────┐ ┌──────────────┐
│   Audio (audio/)   │ │ Data/Zip(data/util)│ │ Net & Server │
│ • AudioManager     │ │ • SQLite3 Database │ │ • HttpClient │
│ • WSOLA Stretcher  │ │ • PKZIP + Deflate  │ │ • APIClient  │
│ • 6x Hitsound SFX  │ │ • FIPS SHA-256/CRC │ │ • Hono Server│
└────────────────────┘ └────────────────────┘ └──────────────┘
```

---

## 2. 核心模块详细设计

### 2.1 App (core/app.h)

应用程序的最顶层类，负责初始化和主循环。

```cpp
namespace sakura::core
{
    class App
    {
    public:
        bool Initialize();
        void Run();          // 主循环
        void Shutdown();

    private:
        void ProcessEvents();
        void Update(float deltaTime);
        void Render();

        Window m_window;
        Renderer m_renderer;
        Input m_input;
        Timer m_timer;
        SceneManager m_sceneManager;
        ResourceManager m_resourceManager;
        AudioManager m_audioManager;

        bool m_running = false;
    };
}
```

**主循环设计：** 固定时间步长更新 + 可变帧率渲染

```
while (running)
{
    ProcessEvents();
    
    // 固定时间步长更新（游戏逻辑 60Hz）
    accumulator += deltaTime;
    while (accumulator >= FIXED_TIMESTEP)
    {
        Update(FIXED_TIMESTEP);
        accumulator -= FIXED_TIMESTEP;
    }
    
    // 可变帧率渲染
    Render();
}
```

### 2.2 Renderer (core/renderer.h)

封装 SDL3 GPU API，提供 2D 渲染接口。

```cpp
namespace sakura::core
{
    class Renderer
    {
    public:
        bool Initialize(SDL_Window* window);
        void Shutdown();

        // 帧管理
        void BeginFrame();
        void EndFrame();

        // 2D 渲染接口
        void DrawSprite(const Texture& texture, const Rect& dest, 
                       float rotation = 0.0f, Color tint = Color::White);
        void DrawRect(const Rect& rect, Color color, bool filled = true);
        void DrawCircle(float cx, float cy, float radius, Color color);
        void DrawLine(float x1, float y1, float x2, float y2, Color color);
        void DrawText(const Font& font, std::string_view text, 
                     float x, float y, Color color);

        // 坐标转换（归一化 → 像素）
        float ToPixelX(float normalizedX) const;
        float ToPixelY(float normalizedY) const;
        float ToPixelW(float normalizedW) const;
        float ToPixelH(float normalizedH) const;

        // 屏幕尺寸
        int GetScreenWidth() const;
        int GetScreenHeight() const;

    private:
        SDL_GPUDevice* m_device = nullptr;
        SDL_Window* m_window = nullptr;
        // Shader 管线、批量渲染器等
    };
}
```

### 2.3 Scene (scene/scene.h)

场景基类与场景管理器。

```cpp
namespace sakura::scene
{
    class Scene
    {
    public:
        virtual ~Scene() = default;

        virtual void OnEnter() {}       // 进入场景时调用
        virtual void OnExit() {}        // 离开场景时调用
        virtual void OnUpdate(float dt) = 0;
        virtual void OnRender(Renderer& renderer) = 0;
        virtual void OnEvent(const SDL_Event& event) = 0;

    protected:
        SceneManager* m_manager = nullptr;
        friend class SceneManager;
    };

    enum class TransitionType
    {
        None,
        Fade,
        SlideLeft,
        SlideRight,
        SlideUp,
        SlideDown,
        Scale,
        CircleWipe
    };

    class SceneManager
    {
    public:
        void PushScene(std::unique_ptr<Scene> scene, 
                      TransitionType transition = TransitionType::Fade);
        void PopScene(TransitionType transition = TransitionType::Fade);
        void SwitchScene(std::unique_ptr<Scene> scene, 
                        TransitionType transition = TransitionType::Fade);

        void Update(float dt);
        void Render(Renderer& renderer);
        void HandleEvent(const SDL_Event& event);

    private:
        std::vector<std::unique_ptr<Scene>> m_sceneStack;
        // 过渡动画状态
        TransitionState m_transition;
    };
}
```

### 2.4 Note, Chart v4, Judge & Rating (game/)

双区音符、Chart Format v4 舞台事件与潜力值系统设计（跨平台纯逻辑层 `sakura-game-logic`）。

```cpp
namespace sakura::game
{
    // --- 音符类型（Chart v1~v4 向后兼容） ---
    struct KeyboardNote
    {
        int time;            // 触发时间 (ms)
        int lane;            // 轨道 (0-3 为 A/S/D/F，4 为 Space 樱枝横梁)
        NoteType type;       // Tap / Hold
        int duration = 0;    // Hold 持续时间
        int linkId = 0;      // 跨区同押连携 ID

        // 运行时状态
        bool isJudged = false;
        JudgeResult result = JudgeResult::Miss;
    };

    struct MouseNote
    {
        int time;
        float x, y;          // 归一化坐标 (0.0~1.0)
        NoteType type;       // Circle / Slider
        int sliderDuration = 0;
        int linkId = 0;      // 跨区连携 ID
        int modifierKey = -1;// 樱绽组合技协同修饰键
        std::vector<std::pair<float, float>> sliderPath;

        bool isJudged = false;
        JudgeResult result = JudgeResult::Miss;
    };

    // --- 判定系统 ---
    class Judge
    {
    public:
        struct JudgeWindows
        {
            int perfect = 25;   // ±25ms
            int great = 50;     // ±50ms
            int good = 80;      // ±80ms
            int bad = 120;      // ±120ms
            int miss = 150;     // >150ms
        };

        JudgeResult Evaluate(int noteTime, int hitTime) const;
        void SetWindows(const JudgeWindows& windows);

    private:
        JudgeWindows m_windows;
    };

    // --- 计分系统 ---
    class ScoreCalculator
    {
    public:
        void OnJudge(JudgeResult result);
        
        int GetScore() const;           // 当前分数 (0~1,000,000)
        float GetAccuracy() const;       // 准确率 (0.0~100.0)
        int GetCombo() const;
        int GetMaxCombo() const;
        Grade GetGrade() const;          // SS/S/A/B/C/D
        bool IsFullCombo() const;
        bool IsAllPerfect() const;

    private:
        int m_totalNotes = 0;
        int m_judgedNotes = 0;
        int m_combo = 0;
        int m_maxCombo = 0;
        int m_perfectCount = 0;
        int m_greatCount = 0;
        int m_goodCount = 0;
        int m_badCount = 0;
        int m_missCount = 0;
    };
}
```

---

## 3. 坐标系统设计

**核心原则：所有布局、位置、尺寸都使用 0.0~1.0 归一化坐标**

```cpp
namespace sakura::core
{
    // 归一化矩形（所有 UI 元素使用此类型定位）
    struct NormRect
    {
        float x, y;          // 左上角（0.0~1.0）
        float width, height; // 宽高比例（0.0~1.0）

        // 转换为像素矩形
        SDL_FRect ToPixelRect(int screenW, int screenH) const
        {
            return {
                x * screenW,
                y * screenH,
                width * screenW,
                height * screenH
            };
        }
    };

    // 游戏区域划分
    struct GameLayout
    {
        // 键盘轨道区域（左屏）
        static constexpr NormRect KEYBOARD_AREA = {0.0f, 0.0f, 0.45f, 1.0f};
        // 鼠标操作区域（右屏）
        static constexpr NormRect MOUSE_AREA = {0.45f, 0.0f, 0.55f, 1.0f};
        // 判定线高度（从顶部算）
        static constexpr float JUDGE_LINE_Y = 0.85f;
        // 轨道宽度比例
        static constexpr float LANE_WIDTH = 0.08f;
    };
}
```

---

## 4. 资源管理设计

```cpp
namespace sakura::core
{
    class ResourceManager
    {
    public:
        // 加载资源（同步）
        std::optional<TextureHandle> LoadTexture(std::string_view path);
        std::optional<FontHandle> LoadFont(std::string_view path, int size);
        std::optional<SoundHandle> LoadSound(std::string_view path);
        std::optional<MusicHandle> LoadMusic(std::string_view path);

        // 获取已加载的资源
        Texture* GetTexture(TextureHandle handle);
        Font* GetFont(FontHandle handle);

        // 释放资源
        void ReleaseTexture(TextureHandle handle);
        void ReleaseAll();

    private:
        std::unordered_map<std::string, TextureEntry> m_textures;
        std::unordered_map<std::string, FontEntry> m_fonts;
        // 引用计数管理
    };
}
```

---

## 5. 数据存储设计 (SQLite)

### 数据库表结构

```sql
-- 玩家成绩
CREATE TABLE scores (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chart_id TEXT NOT NULL,
    difficulty TEXT NOT NULL,
    score INTEGER NOT NULL,
    accuracy REAL NOT NULL,
    max_combo INTEGER NOT NULL,
    grade TEXT NOT NULL,
    perfect_count INTEGER,
    great_count INTEGER,
    good_count INTEGER,
    bad_count INTEGER,
    miss_count INTEGER,
    is_full_combo BOOLEAN DEFAULT FALSE,
    is_all_perfect BOOLEAN DEFAULT FALSE,
    played_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 玩家设置
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- 成就
CREATE TABLE achievements (
    id TEXT PRIMARY KEY,
    unlocked_at DATETIME,
    progress REAL DEFAULT 0.0
);

-- 樱华道场段位认定通关记录（幂等迁移）
CREATE TABLE course_records (
    course_id TEXT PRIMARY KEY,
    badge_text TEXT NOT NULL,
    dan_rank INTEGER NOT NULL,
    total_score INTEGER NOT NULL,
    avg_accuracy REAL NOT NULL,
    remaining_life REAL NOT NULL,
    cleared BOOLEAN DEFAULT FALSE,
    cleared_at INTEGER DEFAULT 0
);

-- 玩家统计
CREATE TABLE statistics (
    key TEXT PRIMARY KEY,
    value_int INTEGER DEFAULT 0,
    value_real REAL DEFAULT 0.0
);
```

---

## 6. 在线生态、原子热更新与双模网络架构 (`src/net/` + `src/core/updater.*` + `server/`)

- **客户端跨平台传输层 (`src/net/http_client.h/.cpp`)**：抽象纯 C++20 `IHttpTransport` 接口（纳入无 SDL 依赖的 `sakura-game-logic`）。Windows 主程序通过系统原生 `WinHttpTransport` 支持 TLS 1.2/1.3 HTTPS 与流式下载进度回调；Linux CI 与单元测试注入内存 `MockHttpTransport`。
- **异步 API 客户端 (`src/net/api_client.h/.cpp`)**：后台工作线程执行登录鉴权、防作弊成绩与 `.skr` 回放上报、每日挑战日榜提交、`SceneArena` 多人房间同步、`SceneMarket` `.sakura` 谱面包上传/检索/评分/SHA-256 校验安装及多端幂等云存档同步（`SyncCloudSave`），主线程在 `App::Update` 中通过 `PumpCallbacks()` 安全消费 UI 回调。
- **Updater v2 原子热替换自更新架构 (`src/core/updater.h/.cpp`)**：解析 GitHub Releases 资产与 `SHA256SUMS.txt`，通过 `DetectInstallMode` 自动识别安装版（存在 `unins000.exe` 时匹配 `-win64-setup.exe`）与免安装便携版（匹配 `-win64.zip`）；后台流式下载至临时暂存目录并校验 FIPS 180-4 SHA-256 摘要，校验通过后由 `BuildHotSwapPowerShellScript` 生成带当前进程 PID 等待锁、核心文件回滚备份及用户存档（`%APPDATA%` / `userdata/`）保护的 PowerShell 原子热替换脚本（或拉起 Inno Setup `/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`）完成无缝自重启。
- **仓库内建双模服务端 (`server/`)**：基于 Node.js 22 + TypeScript + Hono 构建，内置 `SqliteStorageAdapter`（零外部依赖支持本地与 CI `--verify-online` 实服闭环）与生产持久化接口，提供 PKZIP 内存解包防穿越校验、权威 PP / Sakura Rating 复算、全球排行榜、每日挑战榜、多人对战房间状态机、在线谱面市场与幂等云同步 REST API。

---

## 7. 场景流程图

```
                             ┌──────────────┐
                             │   启动画面   │
                             │   (Splash)   │
                             └──────┬───────┘
                                    │
                             ┌──────▼───────┐
     ┌───────────┬───────────│   主菜单     │───────────┬───────────┐
     │           │           │   (Menu)     │           │           │
┌────▼─────┐┌────▼─────┐     └──┬───────┬───┘     ┌─────▼────┐┌─────▼────┐
│ 个人统计 ││ 樱华道场 │        │       │         │ 多人对战 ││ 谱面市场 │
│ (Stats)  ││ (Course) │  ┌─────▼────┐┌─▼────────┐│ (Arena)  ││ (Market) │
│B30名片导出│└────┬─────┘  │   选歌   ││ 编辑器/  │└─────┬────┘└─────┬────┘
└──────────┘     │        │ (Select) ││ 设置/教程│      │           │
                 │        └─────┬────┘└──────────┘      │      一键安装跳选歌
                 └──────────────┼───────────────────────┘
                                │
                         ┌──────▼───────┐
                         │   演奏舞台   │ (支持 GhostRunner 伴跑 /
                         │   (Game)     │  CourseGauge / Arena 同步)
                         └──────┬───────┘
                                │
                         ┌──────▼───────┐
                         │ 结算 / 领奖台│
                         │   (Result)   │
                         └──────────────┘
```

---

## 8. 关键设计模式

| 模式 | 应用位置 | 说明 |
|------|----------|------|
| 状态机 | SceneManager / CourseSession | 场景栈过渡管理与多曲连奏段位状态流转 |
| 观察者 | 事件系统 / APIClient 回调泵 | 输入事件分发与主线程无锁异步通知 |
| 单例 | ResourceManager, AudioManager, SkinManager, Updater | 全局核心服务访问 |
| 命令 | 编辑器撤销/重做 (`EditorCommand`) | 可逆编辑历史栈 |
| 对象池 | 粒子系统 (`ParticleSystem`) | 固定容量预分配零 GC 抖动 |
| 策略 / 适配器 | `IHttpTransport` / `IStorageAdapter` / `Easing` | 跨平台网络 Mock 注入、双模存储与缓动曲线 |

---

## 9. 核心扩展与在线生态架构模块表 (v1.1 ~ v2.0)

| 模块 | 位置 | 职责说明 |
|------|------|----------|
| 主题皮肤与打击音管理 (`SkinManager`) | `src/core/skin_manager.h/.cpp` | 纯 C++20 零 SDL 头文件设计；内置 4 套主题色板与 6 套打击音映射，支持扫描用户 `skins/<id>/skin.json` 与 `hitsounds/*.wav` 热切换覆盖 |
| 樱华道场、日榜与曲单 (`CourseSession` / `SceneCourse`) | `src/game/course.h/.cpp`、`src/scene/scene_course.h/.cpp` | 管理 6 阶段位认定多曲连奏、继承制生命槽（`CourseGauge`）与段位徽章，内置 UTC+8 FNV-1a 确定性每日挑战算法与 `CollectionManager` 命名多曲单并集同步 |
| 幽灵伴跑与多人竞技 (`GhostRunner` / `SceneArena`) | `src/game/ghost_runner.h/.cpp`、`src/scene/scene_arena.h/.cpp` | `GhostRunner` 基于 `.skr` 回放流提供实时双向分差与幽灵光标伴跑；`SceneArena` 实现在线建房/房间码加入、谱面哈希一致性校验、局内实时名次与赛后多人领奖台 |
| 跨生态谱面转换与原生 Deflate (`ChartConverter` / `InflateRawDeflate`) | `src/game/chart_converter.h/.cpp`、`src/utils/archive.h/.cpp` | 自研零外部依赖 PKZIP Store + RFC 1951 `InflateRawDeflate` 解压与 SHA-256 校验；原生解析 Phira `.pez`（Official / RPE JSON）、osu! `.osz`（Mode 0/3）与 StepMania `.sm`，执行 $\Delta t$ 自适应防重叠布局并转换为双区 Chart v4 |
| B30/R10 Sakura Rating 与生涯名片 (`PPCalculator` / `SceneStats`) | `src/game/pp_calculator.h/.cpp`、`src/scene/scene_stats.h/.cpp` | 结合小数定数与准度曲线计算 `0.75 * B30 + 0.25 * R10` 综合潜力值（含防退化保护）与五阶星徽；`SceneStats` 提供 5×6 B30 矩阵视图并支持离线渲染导出 `1920×1080` 月夜墨樱生涯名片 PNG |
| 原子热替换自更新器 (`Updater` v2) | `src/core/updater.h/.cpp` | 自动识别便携版与安装版，后台流式下载 Release 产物并校验 `SHA256SUMS.txt`，生成带 PID 等待锁与回滚保护的 PowerShell 热替换自重启脚本或拉起静默安装升级 |
| HTTP 传输与在线客户端 (`HttpClient` / `APIClient` / `SceneMarket`) | `src/net/http_client.h/.cpp`、`src/net/api_client.h/.cpp`、`src/scene/scene_market.h/.cpp` | Windows 原生 `WinHttpTransport` + Linux CI `MockHttpTransport`；封装账号通行证、排行榜、在线谱面市场搜索/评分/一键安装、工房直传与幂等云存档同步 |
| 双模在线服务端 | `server/src/` | Node.js + TypeScript + Hono 后端，支持零配置 SQLite 与生产环境 `https://steven-lee.tech:233` 部署，统一承载鉴权、排行榜、日榜、多人房间、谱面市场与云同步 |
