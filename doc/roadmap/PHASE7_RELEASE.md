# Phase 7 — 发布 (v1.0)

---

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
