#pragma once

// scene_chart_wizard.h — 新建谱面向导
// 单页表单：填写谱面信息并导入资源源文件，创建时自动复制到谱面目录

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "ui/input_field.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sakura::scene
{

// SceneChartWizard — 新建谱面向导
// 使用步骤：
//   1. 填写谱面信息（曲名、作曲、BPM、偏移、难度名称）
//   2. 点击"添加"从系统文件管理器选择音乐/封面/背景源文件
//   3. 选择输出目录（默认 resources/charts/{folder-name}）
//   4. 点击"创建谱面"→ 建目录+复制资源+写入 info.json/难度文件 → 切换到编辑器
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
    // [0] 曲名 [1] 作曲/艺术家 [2] BPM [3] 偏移(ms) [4] 难度名
    // [5] 音乐源文件 [6] 封面源文件 [7] 背景源文件 [8] 输出目录
    static constexpr int FIELD_TITLE      = 0;
    static constexpr int FIELD_ARTIST     = 1;
    static constexpr int FIELD_BPM        = 2;
    static constexpr int FIELD_OFFSET     = 3;
    static constexpr int FIELD_DIFF_NAME  = 4;
    static constexpr int FIELD_MUSIC_SRC  = 5;
    static constexpr int FIELD_COVER_SRC  = 6;
    static constexpr int FIELD_BG_SRC     = 7;
    static constexpr int FIELD_OUTPUT_DIR = 8;
    static constexpr int FIELD_COUNT      = 9;
    std::array<std::unique_ptr<sakura::ui::InputField>, FIELD_COUNT> m_fields;

    // 当前聚焦字段
    int m_focusedField = 0;

    // ── 按钮 ──────────────────────────────────────────────────────────────────
    std::unique_ptr<sakura::ui::Button> m_btnCreate;   // 创建谱面
    std::unique_ptr<sakura::ui::Button> m_btnCancel;   // 取消
    std::array<std::unique_ptr<sakura::ui::Button>, 3> m_btnPickResource; // 选择音乐/封面/背景

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
                          const std::string& musicSourceFile,
                          const std::string& coverSourceFile,
                          const std::string& backgroundSourceFile);
    bool CopyResourceToChartFolder(const std::string& sourcePath,
                                   const std::string& folderPath,
                                   const std::string& standardBaseName,
                                   const std::string& defaultExtension,
                                   const std::string& resourceLabel,
                                   std::string& outFileName);
    void OpenResourceFileDialog(int fieldIndex);
    void QueueResourceFileSelection(int fieldIndex, const std::string& filePath);
    void QueueResourceFileError(const std::string& errorMsg);
    void ApplyPendingDialogResults();

    struct PendingDialogResult
    {
        int fieldIndex = -1;
        std::string filePath;
        std::string errorMessage;
    };
    std::mutex m_pendingDialogMutex;
    std::vector<PendingDialogResult> m_pendingDialogResults;
};

} // namespace sakura::scene
