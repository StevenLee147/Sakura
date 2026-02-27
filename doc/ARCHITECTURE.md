# Sakura-樱 架构设计文档

> C++20 / SDL3 / SDL3 GPU API / CMake / vcpkg / MSVC

---

## 1. 整体架构

```
┌──────────────────────────────────────────────────────┐
│                     main.cpp                         │
│                  Application Entry                   │
└────────────────────────┬─────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────┐
│                    App (core/)                       │
│  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ │
│  │ Window  │ │ Renderer │ │  Input  │ │  Timer   │ │
│  └─────────┘ └──────────┘ └─────────┘ └──────────┘ │
│  ┌──────────────────┐  ┌────────────────────┐       │
│  │ ResourceManager  │  │    Logger (spdlog) │       │
│  └──────────────────┘  └────────────────────┘       │
└────────────────────────┬─────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────┐
│               SceneManager (scene/)                  │
│                                                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐             │
│  │  Menu    │ │  Select  │ │   Game   │   ...       │
│  │  Scene   │ │  Scene   │ │  Scene   │             │
│  └──────────┘ └──────────┘ └──────────┘             │
│  每个场景继承 Scene 基类                              │
└────────────────────────┬─────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         ▼               ▼               ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│  Game Logic  │ │   UI System  │ │   Effects    │
│  (game/)     │ │   (ui/)      │ │  (effects/)  │
│              │ │              │ │              │
│ • Note       │ │ • Button     │ │ • Particles  │
│ • Judge      │ │ • ScrollList │ │ • Glow       │
│ • Score      │ │ • InputField │ │ • Trail      │
│ • ChartLoad  │ │ • Panel      │ │ • Shader     │
│ • GameState  │ │ • ProgressBar│ │ • ScreenShake│
└──────────────┘ └──────────────┘ └──────────────┘
         │
         ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│    Audio     │ │   Database   │ │   Network    │
│  (audio/)    │ │  (SQLite3)   │ │   (net/)     │
│              │ │              │ │              │
│ • Manager    │ │ • Scores     │ │ • APIClient  │
│ • Visualizer │ │ • Settings   │ │ • NetManager │
│              │ │ • Achievment │ │              │
└──────────────┘ └──────────────┘ └──────────────┘
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

### 2.4 Note & Judge (game/)

音符和判定系统设计。

```cpp
namespace sakura::game
{
    // --- 音符类型 ---
    struct KeyboardNote
    {
        int time;            // 触发时间 (ms)
        int lane;            // 轨道 (0-3)
        NoteType type;       // Tap/Hold/Drag
        int duration = 0;    // Hold 持续时间
        int dragToLane = -1; // Drag 目标轨道

        // 运行时状态
        bool isJudged = false;
        JudgeResult result = JudgeResult::Miss;
    };

    struct MouseNote
    {
        int time;
        float x, y;          // 归一化坐标 (0.0~1.0)
        NoteType type;       // Circle/Slider
        int sliderDuration = 0;
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

-- 玩家统计
CREATE TABLE statistics (
    key TEXT PRIMARY KEY,
    value_int INTEGER DEFAULT 0,
    value_real REAL DEFAULT 0.0
);
```

---

## 6. 网络层设计 (预留)

```cpp
namespace sakura::net
{
    // API 客户端（预留接口，后期实现）
    class APIClient
    {
    public:
        void SetBaseURL(std::string_view url);
        void SetAuthToken(std::string_view token);

        // 异步请求
        std::future<APIResponse> Get(std::string_view endpoint);
        std::future<APIResponse> Post(std::string_view endpoint, 
                                      const nlohmann::json& body);

        // 具体 API
        std::future<APIResponse> Login(std::string_view username, 
                                       std::string_view password);
        std::future<APIResponse> UploadScore(const ScoreData& score);
        std::future<APIResponse> GetLeaderboard(std::string_view chartId, 
                                                 std::string_view difficulty);
        std::future<APIResponse> DownloadChart(std::string_view chartId);
    };
}
```

---

## 7. 场景流程图

```
                    ┌──────────────┐
                    │   启动画面   │
                    │   (Splash)   │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
              ┌─────│   主菜单     │─────┐
              │     │   (Menu)     │     │
              │     └──────┬───────┘     │
              │            │             │
    ┌─────────▼──┐  ┌──────▼───────┐  ┌──▼──────────┐
    │   设置     │  │   选歌       │  │   编辑器    │
    │ (Settings) │  │  (Select)    │  │  (Editor)   │
    └────────────┘  └──────┬───────┘  └─────────────┘
                           │
                    ┌──────▼───────┐
                    │   游戏       │
                    │   (Game)     │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │   结算       │
                    │  (Result)    │
                    └──────┬───────┘
                           │
                    返回选歌/重玩
```

---

## 8. 关键设计模式

| 模式 | 应用位置 | 说明 |
|------|----------|------|
| 状态机 | SceneManager | 场景切换管理 |
| 观察者 | 事件系统 | 输入事件分发 |
| 单例 | ResourceManager, AudioManager | 全局唯一资源 |
| 命令 | 编辑器撤销/重做 | Command 模式 |
| 对象池 | 粒子系统 | 固定大小粒子池 |
| 工厂 | 音符创建 | 根据类型创建对应音符 |
| 策略 | 缓动函数 | 可替换的缓动算法 |
