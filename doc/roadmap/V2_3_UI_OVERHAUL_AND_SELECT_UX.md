# Phase v2.3 详细规格：选歌上下文记忆根治、全局磨砂顶栏、选歌交互革新与超宽屏/输入法适配

覆盖版本：`v2.3.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 选歌界面「上次游玩谱面与视图状态」丢失 Bug 根因与彻底修复 (`src/scene/scene_select.*`)

### 1.1 精确代码级根因定位
在现有 `src/scene/scene_select.cpp` 中，`SceneSelect::Scan()` 的实现存在致命的读写时序倒置：
```cpp
// 现状缺陷代码（src/scene/scene_select.cpp）：
m_selected = -1; Filter();
const auto last = Config::GetInstance().Get<std::string>("library.last_chart", "");
for (int i : m_visible)
    if (m_charts[i].id == last) { Select(i, Config::GetInstance().Get<int>("library.last_difficulty", 0)); break; }
```
1. **首行覆盖灾难**：`Scan()` 先置 `m_selected = -1` 并调用 `Filter()`；而 `Filter()` 发现 `m_selected` 不在 `m_visible` 中，立即执行 `if (!m_visible.empty()) Select(m_visible.front());`；`Select()` 内部又无条件执行：
   ```cpp
   Config::GetInstance().Set("library.last_chart", c.id);
   Config::GetInstance().Set("library.last_difficulty", m_difficulty);
   ```
   导致在 `Scan()` 读取 `library.last_chart` 与 `library.last_difficulty` **之前**，配置值就已经被列表第一首歌（`m_visible.front()`）和默认难度 `0` 彻底覆盖！
2. **跨场景跳转与筛选遮蔽缺失**：
   - `m_collectionFilter`（曲单筛选）、`m_sort`（排序方式）与列表滚动位置未持久化；
   - `SceneSelect::Start()` 进入 `SceneGame` 时未立即执行 `Config::Save()`；
   - 当上次选中的曲目被当前激活的收藏/曲单筛选隐藏时，没有任何回退展示策略。

### 1.2 完整状态恢复与双重持久化修复方案
- **扩展上下文记忆字段**（同时维护进程内静态会话快照 `s_lastSelectContext` 与 `Config` 持久化键，兼顾零延迟场景往返与冷启动恢复）：
  - `library.last_chart`（`std::string`：谱面唯一 `id`）
  - `library.last_difficulty`（`int`：难度索引）
  - `library.last_difficulty_name`（`std::string`：难度名称兜底，防止谱面增删难度后索引偏移）
  - `library.last_collection_filter`（`int`：`0=全部, 1=已收藏, >=2=自定义曲单`）
  - `library.last_sort`（`int`：排序模式）
  - `library.last_scroll_norm`（`float`：连续滚动偏移量）
- **严格的「先快照、禁写过滤、智能回退、落盘锁定」四步曲**：
  1. **读取前置**：在 `OnEnter()` 与 `Scan()` 开头（调用任何 `Filter()` 或 `Select()` 之前），先从进程内快照或 `Config` 完整读取目标 `targetChartId`、`targetDiffIdx`、`targetDiffName`、`targetCollectionFilter`、`targetSort` 与 `targetScrollNorm` 到局部变量。
  2. **抑制初始化误写**：为 `Filter(bool allowFallbackSelect = true)` 增加控制参数，并引入成员标志 `bool m_restoringContext = false`。在 `Scan()` 触发的首次 `Filter(false)` 期间，禁止自动调用 `Select(m_visible.front())`，且在 `m_restoringContext == true` 期间禁止 `Select()` 覆写 `Config` 中的 `library.last_*`。
  3. **筛选遮蔽回退机制**：
     - 优先在当前筛选后的 `m_visible` 中查找 `targetChartId`；
     - 若未在 `m_visible` 中找到，但该谱面确实存在于全量 `m_charts` 中（说明被当前曲单筛选、等级/通关灯筛选或搜索词隐藏），自动将筛选回退至「全部曲目」（清空遮蔽筛选条件）并重新执行 `Filter(false)`，确保精准定位并选中 `targetChartId` 与对应难度（优先匹配索引，若名称不符则按 `ResolveMatchingDifficultyIndex` 匹配 `targetDiffName`）；
     - 仅当 `targetChartId` 为空或谱面已从磁盘删除时，才回退选中 `m_visible.front()`。
  4. **全出口即时持久化**：恢复完成后解除 `m_restoringContext` 并写入一次规范上下文，同时恢复平滑滚动位置使选中项处于可视区舒适位置。在 `SceneSelect::Start()`、`SceneSelect::OnExit()` 以及跳转至 `SceneMarket`、`SceneCourse`、`SceneSettings`、`SceneEditor`、`SceneChartWizard`、`SceneMenu` 前均主动调用 `PersistSelectContext()` + `Config::GetInstance().Save()`。
- **强一致性保证**：无论玩家从结算页（`SceneResult`）、局内暂停退出（`SceneGame`）、谱面市场（`SceneMarket`）、樱华道场（`SceneCourse`）、设置页（`SceneSettings`）、谱面工房（`SceneEditor`）、主菜单（`SceneMenu`）返回，还是直接重启游戏，100% 精确回到上次浏览或游玩的同一首曲目与同一难度。

---

## 2. 全局磨砂状态顶栏 `TopBar` 与滑出式账号抽屉 `AccountDrawer`

### 2.1 全局顶栏组件 (`src/ui/top_bar.h` / `src/ui/top_bar.cpp`)
- 命名空间：`sakura::ui`，所有坐标与字号完全基于 `0.0~1.0` 相对比例与 `Layout::FontSizeFromShortSide` 动态计算，严禁任何硬编码像素值。
- **覆盖场景**：统一集成至 7 个非演奏核心场景——`SceneMenu`、`SceneSelect`、`SceneMarket`、`SceneCourse`、`SceneArena`、`SceneStats`、`SceneSettings`（`SceneGame` 与 `SceneEditor` 保持各自专属全屏工作区不挂载全局顶栏）。
- **归一化布局与功能分区**（顶栏区域 `NormRect{ 0.0f, 0.0f, 1.0f, 0.052f }`，月夜墨樱半透明毛玻璃底板 + 底部鎏金细分割线）：
  1. **左侧场景导航区**：返回按钮（非主菜单场景显示 `← 返回`）+ 当前场景中英双语徽章标题。
  2. **中右侧实时网络与任务遥测区**：
     - **在线连接指示灯**：根据 `APIClient::GetAuthState()` 渲染状态圆点与简短标签（翠绿 `在线` / 琥珀 `离线保活` / 绯红 `凭据过期` / 灰白 `未登录`）；
     - **离线待同步徽章**：当 `OfflineQueue::GetPendingCount() > 0` 时显示琥珀胶囊 `待同步 N`，点击可立即触发一次后台冲刷；
     - **后台传输迷你进度条**：当 `APIClient` 传输线程池或 `Updater` 存在活跃下载任务时，平滑展开紧凑进度胶囊（展示当前任务名与 `0%~100%` 进度条）。
  3. **最右侧玩家身份胶囊**：展示 `[段位徽章] 用户名 · ★ XX.XX`（根据 Sakura Rating 星阶自动渲染青樱/银月/鎏金/绯虹/极星渐变描边），点击展开或收起 `AccountDrawer`。

### 2.2 滑出式账号抽屉 (`src/ui/account_drawer.h` / `src/ui/account_drawer.cpp`)
- **动画与模态交互**：
  - 点击顶栏玩家胶囊后，背景叠加柔和暗化遮罩，抽屉面板从屏幕右侧带阻尼缓动滑入（安全视口右对齐宽度 `0.32f`，高度 `0.948f`）；点击遮罩外部或按 `Esc` 平滑收起。
  - 展开期间优先拦截处理鼠标点击、滚轮与键盘输入事件，避免穿透触发底层场景按钮。
- **统一账号中心能力**：
  - 收拢原本散落在 `SceneMenu` 弹窗中的所有账号操作，使得玩家在选歌、市场、道场或联机大厅任意界面无需退回主菜单即可完成：
    1. 查看个人档案摘要（Sakura Rating、B30/R10 概览、总 PP、最高段位徽章、手动「立即云端同步」按钮）；
    2. 登录已有账号 / 注册新账号 / 创建游客身份；
    3. **游客原地转正表单**（对接 `v2.1` `UpgradeGuestAsync`）；
    4. **最近 5 个已保存账号一键切换列表**与移除按钮；
    5. **修改密码**与**安全退出登录**。

---

## 3. `SceneSelect` 阻尼平滑滚动、双区 NPS 密度图与高级筛选

### 3.1 连续浮点阻尼滚动与可拖拽滚动条
- **连续滚动物理模型**：
  - 将离散行号 `int m_scroll` 升级为连续浮点 `float m_scrollPos = 0.0f` 与目标值 `float m_scrollTarget = 0.0f`（单位：归一化卡片行距）。
  - 在 `OnUpdate(float dt)` 中采用帧率无关指数阻尼插值：`m_scrollPos += (m_scrollTarget - m_scrollPos) * (1.0f - std::exp(-18.0f * dt))`，支持鼠标滚轮高精度步进、触控板连续滑动以及键盘 `↑` / `↓` / `PageUp` / `PageDown` / `Home` / `End` 自动平滑跟随。
  - 使用渲染器裁剪矩形（Clip Rect）严格限定曲目列表视口区域，使上下边缘滑入滑出的卡片平滑裁切而不与顶部搜索栏或底部按钮重叠。
- **可拖拽比例滚动条**：
  - 在曲目列表右侧边缘绘制纤细圆角轨道与动态高度滑块（滑块高度按 `visibleRows / totalRows` 比例计算并设最小高度下限 `0.06f`），支持悬停高亮、点击轨道快速跳转及鼠标左键按住连续拖拽。

### 3.2 双区实时 NPS 密度曲线与练习区间拖拽手柄
- **双区密度统计算法 (`src/game/chart_utils.h` / `src/game/chart_utils.cpp`)**：
  - 在 `sakura-game-logic` 中新增纯计算函数：
    ```cpp
    struct DualZoneNpsProfile
    {
        std::vector<float> keyboardNps; // 左半场 4K + 拇指键每秒音符密度序列
        std::vector<float> mouseNps;    // 右半场鼠标点击/长按/拖拽/连携每秒密度序列
        float              peakTotalNps = 0.0f;
        float              avgTotalNps  = 0.0f;
        int                durationMs   = 0;
    };

    DualZoneNpsProfile ComputeDualZoneNpsCurve(const ChartData& chart, int bucketCount = 48);
    ```
- **可视化与交互式练习区间手柄**：
  - 在 `SceneSelect` 右侧详情面板绘制双层半透明折线/面积密度图（**鎏金色曲线**代表键盘 4K 区密度，**绯樱粉曲线**代表鼠标区密度），右上角标注 `Peak XX.X NPS`。
  - 当切换到「自由练习 (`PlayMode::Practice`)」模式时，在 NPS 曲线图上直接叠加**左、右两个可鼠标拖拽的区间游标手柄（起点 `startMs` 与终点 `endMs`）**及高亮区间遮罩：
    - 拖拽游标手柄实时同步更新 `m_startInput` 与 `m_endInput` 秒数文本；
    - 手动在输入框修改秒数时亦反向同步游标位置，方便玩家直观框选高密度难点段落反复循环特训。

### 3.3 定数范围、通关灯徽章筛选与 `F2` 随机选曲轮盘
- **高级筛选栏**：
  - **定数区间筛选**：支持 `1.0 ~ 15.0+` 双端等级范围过滤（步进 `0.5`），仅保留所选难度（或任一难度）定数落在 `[minLevel, maxLevel]` 内的曲目。
  - **通关灯（Lamp）状态筛选与卡片徽章**：
    - 定义四档通关灯态：`Unplayed`（未游玩）、`Clear`（已通关，最佳评级 `>= C`）、`FC`（Full Combo 全连，鎏金樱花灯）、`AP`（All Perfect 理论值，虹彩极星灯）。
    - 在 `Scan()` 及成绩更新时一次性从 `Database::GetInstance()` 批量构建内存通关灯索引表（严禁在 `OnRender` 或每次按键时逐曲查询 SQLite），在曲目列表每张卡片右侧直观渲染对应通关灯徽章，并支持按 `全部 / 未游玩 / 已通关 / FC / AP` 一键过滤。
- **`F2` 随机选曲轮盘**：
  - 新增顶部/底部快捷按钮「随机选曲 `F2`」及键盘 `F2` 快捷键（非输入框聚焦态触发）：在当前激活的筛选结果集 `m_visible` 中以均匀分布随机抽取目标曲目，播放 `0.55s` 由快至慢的列表高亮滚动轮盘动画与清脆切换音效后定格选中。

---

## 4. `TextInput` 密码遮罩 / CJK 输入法跟随与超宽屏安全布局

### 4.1 `TextInput` 密码模式与剪贴板防泄露 (`src/ui/text_input.h` / `src/ui/text_input.cpp`)
- 新增接口：
  ```cpp
  void SetPasswordMode(bool enabled);
  bool IsPasswordMode() const { return m_passwordMode; }
  void SetPasswordRevealed(bool revealed);
  bool IsPasswordRevealed() const { return m_passwordRevealed; }
  ```
- **渲染遮罩与显隐切换**：
  - 当 `m_passwordMode && !m_passwordRevealed` 时，按 UTF-8 码点数量将文本替换为等长圆点符号 `•`（`\xE2\x80\xA2`）进行绘制与光标像素偏移测算，底层 `m_text` 保持真实字符不变。
  - 密码模式下在输入框右侧内边距区域自动渲染小型「显/隐」切换按钮，点击切换明文预览与圆点遮罩。
- **剪贴板安全封锁**：当 `m_passwordMode == true` 时，严格拦截并禁止 `Ctrl+C`（复制）与 `Ctrl+X`（剪切）将输入框内容写入系统剪贴板（保留 `Ctrl+A` 全选、`Ctrl+V` 粘贴与退格删除）。

### 4.2 SDL3 中文输入法（CJK IME）候选框跟随与预编辑下划线
- **按需启停文本输入模式**：
  - 仅当某个 `TextInput` 获得焦点时调用 `SDL_StartTextInput(window)`，当所有 `TextInput` 均失去焦点时调用 `SDL_StopTextInput(window)`，彻底杜绝打歌或快捷键操作时被系统中文输入法拦截按键。
- **候选词窗口精准跟随 (`SDL_SetTextInputArea`)**：
  - 在 `SetFocused(true)`、光标位置变化及窗口尺寸变化时，根据当前渲染分辨率将输入框归一化矩形与光标偏移换算为窗口像素 `SDL_Rect`，调用 `SDL_SetTextInputArea(window, &imeRect, cursorPxOffset)`，使微软拼音、搜狗、Rime 等输入法候选框精准停靠在输入框正下方。
- **预编辑拼音串内联渲染 (`SDL_EVENT_TEXT_EDITING`)**：
  - 监听 `SDL_EVENT_TEXT_EDITING` 事件维护 `m_compositionText` 与 `m_compositionCursor`；在当前光标处内联渲染尚未上屏的拼音/假名预编辑串，并在其下方绘制樱花粉虚线/高亮底线，收到 `SDL_EVENT_TEXT_INPUT` 上屏或失焦时自动清空预编辑态。

### 4.3 多宽高比与超宽屏自适应布局 (`src/core/layout.h`)
在 `sakura::core::Layout` 中新增无 GPU 依赖的纯数学视口映射工具 `SafeAspectLayout`，全面适配 `16:9`（1080p / 1440p / 4K）、`16:10`（1920×1200 / 2560×1600）与 `21:9` / `32:9`（3440×1440 等超宽带鱼屏）：
```cpp
struct SafeAspectLayout
{
    NormRect fullViewport = { 0.0f, 0.0f, 1.0f, 1.0f }; // 全屏铺满层（背景插画、BGA、樱花粒子、TopBar 底板）
    NormRect safeViewport = { 0.0f, 0.0f, 1.0f, 1.0f }; // 核心交互安全区（超宽屏自动水平居中限宽）
    float    aspectRatio  = 16.0f / 9.0f;

    static SafeAspectLayout Compute(int screenWidth, int screenHeight,
                                    float maxInteractiveAspect = 18.0f / 9.0f);
    NormRect Map(NormRect localNorm) const;
    std::pair<float, float> UnmapPoint(float screenNormX, float screenNormY) const;
};
```
- **双层渲染准则**：
  - **背景氛围层（Full-Bleed）**：场景动态背景、谱面模糊曲绘、樱花飘落粒子与 `TopBar` 玻璃底板铺满 `fullViewport`（`0.0~1.0`），在 `21:9` 超宽屏上无任何生硬黑边。
  - **核心 UI 与游玩交互层（Safe-Centered）**：当物理宽高比超过上限阈值（如 `21:9` `3440×1440`）时，`safeViewport` 自动在水平方向居中收敛至标准宽屏安全带，防止按钮被横向拉扁、模态弹窗过度拉伸或右侧鼠标判定圆变形为椭圆；在 `16:10` 屏幕上保持完整宽度并基于 `ShortSideRatio` 自适应字号与圆形几何比例。

---

## 5. 测试矩阵与验收清单

### 5.1 C++ 单元测试 (`tests/test_v230_ui_and_select_ux.cpp`)
在 `SakuraTests` 中新增无头可运行单测，覆盖：
1. **选歌上下文记忆回归测试**：
   - 构造多谱面、多难度、多曲单集合，选中第 3 首歌的第 2 个难度后触发重新扫描（模拟进入结算页/市场/设置再返回及冷启动），断言选中曲目与难度**绝不被 `m_visible.front()` 覆盖**；
   - 模拟上次曲目被激活曲单隐藏，断言自动回退至「全部曲目」并精准恢复该曲目；
   - 模拟上次谱面文件被删除，断言安全回退至首曲且不越界崩溃。
2. **双区 NPS 曲线计算测试**：验证空谱面、纯键盘 4K 谱面、纯鼠标谱面及双区混合同押谱面的 `ComputeDualZoneNpsCurve` 分桶密度、峰值 NPS 与总时长计算准确。
3. **筛选器与 `F2` 随机算法测试**：验证等级区间 `[minLevel, maxLevel]` 边界、四态通关灯（`Unplayed/Clear/FC/AP`）过滤谓词，以及固定随机种子下 `F2` 抽取结果始终落在当前 `m_visible` 集合内。
4. **`SafeAspectLayout` 多分辨率几何测试**：分别输入 `1920×1080`（16:9）、`2560×1600`（16:10）、`3440×1440`（21:9）与 `3840×2160`（4K），断言 `fullViewport` 恒为 `{0,0,1,1}`、`21:9` 下 `safeViewport` 居中对称且 `UnmapPoint(Map(pt))` 往返误差 `< 1e-6f`。
5. **`TextInput` 密码遮罩与防复制测试**：验证中英混合密码在遮罩模式下生成正确数量的 `•` 字符，且密码模式开启时复制/剪切拦截逻辑生效。
