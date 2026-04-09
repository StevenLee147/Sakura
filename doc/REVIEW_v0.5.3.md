# Sakura-樱 工作区全面审查报告 (v0.5.3-alpha)

> 审查日期：2026-04-08
> 当前版本：v0.5.3-alpha | 目标版本：v1.0.0
> 代码规模：~26,000 行 C++20 / 94 个源文件

---

## 一、总体评价

| 维度 | 评分 | 说明 |
|------|------|------|
| **架构设计** | ⭐⭐⭐⭐ | 模块划分清晰，场景状态机 + 单例管理器模式适合项目规模 |
| **代码规范** | ⭐⭐⭐⭐ | 命名规范整体一致，坐标系正确使用相对比例 |
| **错误处理** | ⭐⭐⭐ | 核心路径有保护，但部分边界条件缺失 |
| **性能** | ⭐⭐⭐ | 可运行，但渲染层存在每帧分配的性能瓶颈 |
| **测试覆盖** | ⭐⭐⭐ | 游戏逻辑有单元测试，场景/UI/渲染层无测试覆盖 |
| **安全性** | ⭐⭐⭐ | 本地应用风险较低，但数据库查询应使用参数化语句 |

### 整改进度更新（2026-04-09）

- **已修复**：C1, C2, C3, H1, H2, H3, H4, H5, M1, M3, M4, M6, M7, L2, L3, L6
- **已确认报告需更新**：M2（`Config::Set()` 仅标记 dirty，不会在每次写值时立即刷盘），L4（`ScreenShake::Trigger()` 已使用持久化随机引擎）
- **仍待处理**：M5, L1, L5，以及 Data 模块“SQL 参数化全覆盖审计”和 Phase 5~7 的发布前工作

---

## 二、按严重程度分类的问题清单

### 🔴 严重 (CRITICAL) — 必须在 1.0.0 前修复

#### C1. 结算界面"重玩"按钮丢失难度选择（已修复）
- **文件**: `src/scene/scene_result.cpp:72`
- **问题**: 重玩按钮硬编码 `difficultyIndex = 0`，玩家在非第一难度重玩时会跳回第一难度
- **修复建议**: 在 `GameResult` 中添加 `difficultyIndex` 字段，或在 `SceneResult` 中保存并传递原始难度索引

#### C2. 渲染器每帧创建/销毁文字纹理（已修复）
- **文件**: `src/core/renderer.cpp:261-342`
- **问题**: 每次 `DrawText()` 调用都会创建 SDL_Surface → SDL_Texture → 渲染 → 销毁。HUD 文字每帧重建造成严重性能浪费
- **修复建议**: 实现文字纹理缓存层（LRU 缓存，key = text + fontSize + color），仅在内容变化时重建

#### C3. 测试框架头文件末尾有未完成的宏定义（已修复）
- **文件**: `tests/test_framework.h:141`
- **问题**: `REQUIRE_THAT` 宏定义以反斜杠结尾但无后续实现体，编译器产生 `backslash-newline at end of file` 警告
- **修复建议**: 补全宏实现或移除未完成的宏定义

---

### 🟠 高优先级 (HIGH) — 建议在 1.0.0 前修复

#### H1. SceneEditor::OnEnter 未重置 m_shiftHeld（已修复）
- **文件**: `src/scene/scene_editor.cpp:45-53`
- **问题**: `OnEnter()` 重置了 `m_ctrlHeld` 但未重置 `m_shiftHeld`，从其他场景切换回编辑器时可能残留错误的 Shift 状态
- **说明**: 成员变量有默认值 `false` 初始化器，首次进入不受影响，但场景重入时可能出错

#### H2. AudioManager 的 PlayMusicFromHandle / PlaySFXFromHandle 未实现（已修复）
- **文件**: `src/audio/audio_manager.cpp`
- **问题**: 关键 API 标记为 stub / 不支持，限制了资源管理器的 Handle 模式使用
- **影响**: 资源管理器加载音频后无法通过 Handle 播放

#### H3. GameResult 缺少 playTimeSeconds 赋值（已修复）
- **文件**: `src/game/score.cpp` GetResult() / `src/game/chart.h:102`
- **问题**: `GameResult::playTimeSeconds` 声明并初始化为 0.0，但从未在 `GetResult()` 中赋值
- **影响**: 统计面板显示的游玩时长始终为 0

#### H4. PP 公式使用魔法数字无文档说明（已修复）
- **文件**: `src/game/pp_calculator.cpp:17-26`
- **问题**: PP 计算公式中的 `2.5, 10.0, 4.0, 2.0, 1.05, 1.10, 1.02` 等常数缺少注释说明来源和设计意图
- **修复建议**: 提取为命名常量并添加公式文档

#### H5. 版本号不同步（已修复）
- **文件**: `doc/ROADMAP.md:74`, `CMakeLists.txt:3`, `vcpkg.json:2`
- **问题**:
  - ROADMAP.md 写 "当前版本：v0.4.2-alpha"（已过期）
  - CMakeLists.txt 写 `VERSION 0.5.3`（正确）
  - vcpkg.json 写 `"version-string": "0.1.0"`（严重过期）
- **修复建议**: 统一为 0.5.3

---

### 🟡 中优先级 (MEDIUM) — 影响代码质量，建议修复

#### M1. ResourceManager Unload 函数的反向查找可优化（已修复）
- **文件**: `src/core/resource_manager.cpp:164-171, 222-229, 284-291, 346-353`
- **问题**: 四个 Unload 函数中使用线性遍历进行反向查找（path → handle 的逆映射）。虽然 `erase(pit); break;` 模式是正确的（break 后不再使用失效迭代器），但效率低
- **优化建议**: 考虑维护双向映射或在 Resource 结构中存储 key

#### M2. Config::Save() 在每次 Set 时频繁刷盘（已确认报告需更新）
- **文件**: `src/core/config.cpp`
- **问题**: 报告初审时怀疑每次 `Set()` 都会触发保存，导致主题切换等批量操作多次 I/O
- **复核结果**: 当前实现中 `Set()` 仅设置内存值并标记 `m_dirty = true`，真正写盘发生在显式 `Save()` / `SaveForce()` 或程序退出时，该项不再视为当前缺陷

#### M3. Input 模块硬编码默认分辨率 1920×1080（已修复）
- **文件**: `src/core/input.cpp:19-20`
- **问题**: 静态变量 `s_screenWidth = 1920, s_screenHeight = 1080` 作为默认值，在 `SetScreenSize()` 调用前的短暂窗口期内坐标归一化使用错误的尺寸
- **修复建议**: 确保 `SetScreenSize()` 在第一次 `ProcessEvent()` 之前调用

#### M4. 粒子系统 DrawSakuraBlossom 每帧重新计算常量（已修复）
- **文件**: `src/effects/particle_system.cpp:226-247`
- **问题**: 花瓣绘制中的三角函数和常量每帧重复计算
- **优化建议**: 使用 `static constexpr` 或预计算查找表

#### M5. AudioVisualizer FFT 分析使用 O(n²) 算法
- **文件**: `src/audio/audio_visualizer.cpp`
- **问题**: 频谱分析使用暴力 DFT 而非 FFT，1024 采样点时性能不佳
- **优化建议**: 如需频谱功能，引入轻量 FFT 库（如 pffft）或减少分析精度

#### M6. Easing.h 中的 Pi 常量应使用 C++20 标准库（已修复）
- **文件**: `src/utils/easing.h:15-17`
- **问题**: 手动定义 `kPi` 等常量，C++20 提供 `std::numbers::pi_v<float>`
- **修复建议**: 替换为标准库常量

#### M7. SfxGenerator 硬编码 π 值（已修复）
- **文件**: `src/effects/sfx_generator.cpp:68`
- **问题**: 使用 `3.14159265f` 字面量而非标准库常量
- **修复建议**: 使用 `std::numbers::pi_v<float>`

---

### 🟢 低优先级 (LOW) — 代码异味和改进建议

#### L1. score.cpp GetAccuracy() 未缓存
- 每次调用重新计算准确率，可在 OnJudge() 时缓存

#### L2. editor_command.h MAX_HISTORY 硬编码（已修复）
- 撤销历史上限 200 应改为可配置项

#### L3. database.h 硬编码数据库路径（已修复）
- `"data/sakura.db"` 应从 Config 读取或支持环境变量

#### L4. Screen_shake.cpp 每次 Trigger 创建 random_device（已确认报告需更新）
- 复核结果：当前实现已使用 `static std::mt19937 rng{ std::random_device{}() };`，无需额外修复

#### L5. 部分 UI 组件动画时长为魔法数字
- 如 Button hover 0.02f, Toast slide 0.25f/0.30f, Toggle 0.20f 等应统一管理

#### L6. 中文按钮标签间距不一致（已修复）
- "继 续" / "重新开始" / "返 回" / "重 玩" 格式不统一

---

## 三、按模块的详细评审

### Core 模块 (src/core/)
- ✅ App 生命周期管理正确，固定时间步长实现规范（带死亡螺旋保护）
- ✅ Config 系统使用 JSON 存储，支持嵌套路径访问
- ✅ Renderer 封装完整，支持基本 2D 图元和文字渲染
- ✅ 坐标系统全部使用归一化坐标 (0.0~1.0)
- ✅ 渲染器文字缓存层已实现（C2 已修复）
- ✅ ResourceManager 已补充双向映射，Unload 反查不再线性遍历（M1 已修复）

### Game 模块 (src/game/)
- ✅ 判定系统支持 Perfect/Great/Good/Bad/Miss 五级，含 Hold/Slider
- ✅ 计分系统含 Combo 加成和准确率权重
- ✅ ChartLoader 支持内置和旧版两种格式
- ✅ 成就系统支持 13 种成就，JSON 配置驱动
- ✅ PP 公式已提取命名常量并补充说明（H4 已修复）
- ✅ playTimeSeconds 已在结果构建阶段赋值（H3 已修复）

### Scene 模块 (src/scene/)
- ✅ 场景管理器支持栈式管理和淡入淡出切换动画
- ✅ 涵盖完整游戏流程：启动 → 菜单 → 选歌 → 游戏 → 结算 → 统计
- ✅ 包含编辑器、校准、教程等辅助场景
- ✅ 结算界面重玩已保留原难度索引（C1 已修复）
- ✅ 编辑器场景重入会重置 Shift 状态（H1 已修复）

### Audio 模块 (src/audio/)
- ✅ 使用 miniaudio 作为底层引擎
- ✅ 音频可视化支持 Bar/Circle/Wave 三种模式
- ✅ SFX 生成器可程序化合成打击音效
- ✅ Handle 播放 API 已通过资源路径反查实现（H2 已修复）
- ⚠️ 频谱分析性能（见 M5）

### Effects 模块 (src/effects/)
- ✅ 粒子系统支持池分配和多种预设（樱花/火花/星星等）
- ✅ ShaderManager 支持 SPIR-V + runtime 回退
- ✅ Glow / Trail / ScreenShake 效果完整
- ✅ 樱花花朵绘制已收敛为预计算局部偏移 + 单次旋转矩阵（M4 已修复）
- ⚠️ 部分魔法数字（见 L5）

### UI 模块 (src/ui/)
- ✅ 完整的组件库：Button, Slider, Toggle, Dropdown, TextInput, ScrollList, TabBar, Toast, ProgressBar, Label, InputField
- ✅ 全部使用归一化坐标
- ✅ 良好的组件化抽象

### Editor 模块 (src/editor/)
- ✅ 完整的谱面编辑器核心（Command 模式支持撤销/重做）
- ✅ 预览、时间轴、鼠标区域编辑
- ✅ 支持多难度

### Data 模块 (src/data/)
- ✅ SQLite 数据库封装完整
- ✅ 支持成绩保存/查询/统计
- ✅ 数据库路径已支持配置项与环境变量覆盖（L3 已修复）
- ⚠️ SQL 查询应确保全部使用参数化语句（当前看到有使用 `sqlite3_bind_*`，需确认全覆盖）

### Utils 模块 (src/utils/)
- ✅ 完整的缓动函数库（26+ 种曲线）
- ✅ 日志系统支持 spdlog 和 no-op 回退
- ✅ 圆周率常量已切换为 C++20 标准库 `std::numbers::pi_v<float>`（M6/M7 已修复）

---

## 四、1.0.0 上线前还需要完成的内容

根据 ROADMAP 和当前代码状态，从 v0.5.3-alpha 到 v1.0.0 还需完成以下阶段：

### Phase 5 — 数据与回放 (v0.6)

| 功能 | 状态 | 说明 |
|------|------|------|
| Replay 录制系统 (.skr 格式 + zlib 压缩) | 🔲 未开始 | 逐帧记录输入事件 |
| Replay 回放场景 (含拖拽/变速/暂停) | 🔲 未开始 | 新增 SceneReplay |
| 数据导出/导入 (sakura_export.zip) | 🔲 未开始 | 成绩+设置+成就+回放打包 |
| 详细结算报告 (散点图/饼图/趋势线) | 🔲 未开始 | 增强 SceneResult |

### Phase 6 — 打磨优化 (v0.7)

| 功能 | 状态 | 说明 |
|------|------|------|
| 性能优化 (纹理图集/批渲染/文字缓存/对象池) | 🔲 未开始 | 解决 C2 等性能问题 |
| 背景视频支持 (FFmpeg H.264/VP9) | 🔲 未开始 | 需引入 FFmpeg 依赖 |
| 练习模式 (变速 0.5x~2.0x) | 🔲 未开始 | 扩展 GameState |
| 快捷重试 (~ 键) | 🔲 未开始 | SceneGame 快捷键 |
| 版本管理 / 自动更新框架 | 🔲 未开始 | HTTPS 检查 + 下载 |
| UX 精修 (输入缓冲/里程碑动画/异步加载) | 🔲 未开始 | 多项细节打磨 |

### Phase 7 — 发布 (v1.0)

| 功能 | 状态 | 说明 |
|------|------|------|
| 全功能测试清单执行 | 🔲 未开始 | 5 种音符 / SV 变速 / 编辑器 / 成就 |
| 多分辨率 / 多 GPU 兼容性测试 | 🔲 未开始 | 1080p / 1440p / 4K |
| 安装程序 (Inno Setup) | 🔲 未开始 | 含组件选择 |
| GitHub Actions 自动构建 (tag v*) | 🔲 未开始 | Release CI/CD |
| 便携版 ZIP 打包 | 🔲 未开始 | 免安装发布 |

### 本次审查发现的额外必修项（ROADMAP 未列出）

| 项目 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| 修复重玩按钮丢失难度 (C1) | 🔴 | ✅ 已修复 | 功能 Bug |
| 实现渲染器文字缓存 (C2) | 🔴 | ✅ 已修复 | 性能瓶颈 |
| 修复测试框架警告 (C3) | 🔴 | ✅ 已修复 | 编译警告 |
| 赋值 playTimeSeconds (H3) | 🟠 | ✅ 已修复 | 数据缺失 |
| 统一版本号 (H5) | 🟠 | ✅ 已修复 | 信息不一致 |
| 编辑器 m_shiftHeld 重置 (H1) | 🟠 | ✅ 已修复 | 状态残留 |
| Handle 播放 API 实现 (H2) | 🟠 | ✅ 已修复 | API 缺失 |

---

## 五、工时估算（到 v1.0.0）

| 阶段 | 预估工时 | 说明 |
|------|----------|------|
| 修复本审查 Critical/High 问题 | 8-12h | C1-C3, H1-H5 |
| Phase 5 — 数据与回放 | 40-60h | Replay 系统为主要工作量 |
| Phase 6 — 打磨优化 | 50-70h | 背景视频 + 性能优化最耗时 |
| Phase 7 — 发布 | 20-30h | 测试 + 打包 + CI/CD |
| **合计** | **~120-170h** | 从当前 v0.5.3 到 v1.0.0 |

---

## 六、建议的优先级排序

1. ✅ Critical 和 High 问题已完成修复
2. ⬜ 完成 Phase 5 (v0.6) — Replay 系统和数据功能
3. ⬜ 完成 Phase 6 (v0.7) — 性能优化和 UX 打磨
4. ⬜ 完成 Phase 7 (v1.0) — 测试和发布
