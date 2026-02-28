#pragma once

// scene_chart_wizard.h — 新建谱面向导
// 单页表单：填入音乐路径、曲名、BPM 等信息后一键创建谱面目录+文件

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "ui/input_field.h"

#include <array>
#include <memory>
#include <string>

namespace sakura::scene
{

// SceneChartWizard — 新建谱面向导
// 使用步骤：
//   1. 填写谱面信息（曲名、作曲、BPM、偏移、难度名称）
//   2. 填写音乐文件路径（绝对路径或相对于 exe 的路径）
//   3. 选择输出目录（默认 resources/charts/{folder-name}）
//   4. 点击"创建谱面"→ 建目录+info.json+空难度文件 → 切换到编辑器
class SceneChartWizard final : public Scene
{
public:
    explicit SceneChartWizard(SceneManager& mgr);

    void OnEnter() override;
    void OnExit()  override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    SceneManager& m_manager;

    // 字体
    sakura::core::FontHandle m_fontTitle  = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontLabel  = sakura::core::INVALID_HANDLE;

    // ── 表单字段 ──────────────────────────────────────────────────────────────
    // [0] 曲名  [1] 作曲/艺术家  [2] BPM  [3] 偏移(ms)  [4] 难度名  [5] 音乐路径  [6] 输出目录
    static constexpr int FIELD_COUNT = 7;
    std::array<std::unique_ptr<sakura::ui::InputField>, FIELD_COUNT> m_fields;

    // 当前聚焦字段
    int m_focusedField = 0;

    // ── 按钮 ──────────────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::Button> m_btnCreate;   // 创建谱面
    std::unique_ptr<sakura::ui::Button> m_btnCancel;   // 取消

    // ── 状态 ──────────────────────────────────────────────────────────────────
    std::string m_errorMsg;    // 显示验证错误
    float       m_errorTimer = 0.0f;

    // ── 内部方法 ──────────────────────────────────────────────────────────────
    void SetupFields();
    void SetupButtons();
    void FocusField(int index);
    bool ValidateAndCreate();    // 验证字段，创建文件，返回 true = 成功
    void ShowError(const std::string& msg);

    // 根据曲名生成文件夹名（小写+下划线，过滤特殊字符）
    static std::string Slugify(const std::string& title);
    // 在目标路径创建谱面文件
    bool CreateChartFiles(const std::string& folderPath,
                          const std::string& title,
                          const std::string& artist,
                          float bpm, int offsetMs,
                          const std::string& diffName,
                          const std::string& musicFile);
};

} // namespace sakura::scene
