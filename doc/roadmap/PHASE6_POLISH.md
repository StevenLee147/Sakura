# Phase 6 — 打磨优化 (v0.7)

> 性能优化、背景视频、练习模式、自动更新、最终打磨。

---

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
