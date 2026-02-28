// scene_chart_wizard.cpp — 新建谱面向导实现

#include "scene_chart_wizard.h"
#include "scene_menu.h"
#include "scene_editor.h"
#include "core/resource_manager.h"
#include "utils/logger.h"
#include "ui/toast.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace fs   = std::filesystem;
using     json = nlohmann::json;

namespace sakura::scene
{

// ═════════════════════════════════════════════════════════════════════════════
// 构造
// ═════════════════════════════════════════════════════════════════════════════

SceneChartWizard::SceneChartWizard(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ═════════════════════════════════════════════════════════════════════════════
// OnEnter
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::OnEnter()
{
    LOG_INFO("[SceneChartWizard] 进入新建谱面向导");

    auto& rm     = sakura::core::ResourceManager::GetInstance();
    m_fontTitle  = rm.GetDefaultFontHandle();
    m_fontLabel  = rm.GetDefaultFontHandle();

    SetupFields();
    SetupButtons();
    FocusField(0);
}

// ═════════════════════════════════════════════════════════════════════════════
// OnExit
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::OnExit()
{
    // 停止所有文本输入
    for (auto& f : m_fields)
        if (f) f->SetFocused(false);
    LOG_INFO("[SceneChartWizard] 退出向导");
}

// ═════════════════════════════════════════════════════════════════════════════
// SetupFields
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::SetupFields()
{
    // 表单区域
    constexpr float FORM_LEFT  = 0.38f;  // 输入框左边缘
    constexpr float FORM_W     = 0.42f;  // 输入框宽度
    constexpr float FIELD_H    = 0.044f;
    constexpr float FIELD_Y0   = 0.175f;
    constexpr float FIELD_STEP = 0.080f;

    const char* placeholders[FIELD_COUNT] = {
        "必填：歌曲标题（如 Cherry Blossoms）",
        "可选：作曲家/艺术家",
        "必填：BPM（如 120 或 120.5）",
        "可选：初始偏移（毫秒，如 0）",
        "必填：难度名称（如 Normal）",
        "可选：音乐文件路径（相对路径，如 music.ogg）",
        "自动生成（可修改输出目录）"
    };

    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        float y = FIELD_Y0 + i * FIELD_STEP;
        m_fields[i] = std::make_unique<sakura::ui::InputField>(
            sakura::core::NormRect{ FORM_LEFT, y, FORM_W, FIELD_H },
            placeholders[i],
            m_fontLabel,
            0.020f);
        m_fields[i]->SetMaxLength(200);
    }

    // 默认值
    m_fields[2]->SetText("120");
    m_fields[3]->SetText("0");
    m_fields[4]->SetText("Normal");

    // 当曲名变化时，自动更新输出目录
    m_fields[0]->SetOnChange([this](const std::string& title) {
        if (!title.empty())
        {
            std::string slug = Slugify(title);
            m_fields[6]->SetText("resources/charts/" + slug);
        }
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// SetupButtons
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::SetupButtons()
{
    // "创建谱面" 按钮
    sakura::ui::ButtonColors createColors;
    createColors.normal  = { 40, 120, 80,  220 };
    createColors.hover   = { 60, 160, 110, 235 };
    createColors.pressed = { 25, 90,  55,  245 };
    createColors.text    = sakura::core::Color::White;

    m_btnCreate = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.38f, 0.745f, 0.20f, 0.055f },
        "✔ 创建谱面", m_fontTitle, 0.022f, 0.010f);
    m_btnCreate->SetColors(createColors);
    m_btnCreate->SetOnClick([this]() { ValidateAndCreate(); });

    // "取消" 按钮
    sakura::ui::ButtonColors cancelColors;
    cancelColors.normal  = { 80, 40, 50,  200 };
    cancelColors.hover   = { 110, 55, 70, 220 };
    cancelColors.pressed = { 55, 25, 35,  240 };
    cancelColors.text    = sakura::core::Color::White;

    m_btnCancel = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.60f, 0.745f, 0.20f, 0.055f },
        "✕ 取消", m_fontTitle, 0.022f, 0.010f);
    m_btnCancel->SetColors(cancelColors);
    m_btnCancel->SetOnClick([this]() {
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// FocusField
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::FocusField(int index)
{
    for (int i = 0; i < FIELD_COUNT; ++i)
        if (m_fields[i]) m_fields[i]->SetFocused(i == index);
    m_focusedField = index;
}

// ═════════════════════════════════════════════════════════════════════════════
// OnUpdate
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::OnUpdate(float dt)
{
    for (auto& f : m_fields) if (f) f->Update(dt);
    if (m_btnCreate) m_btnCreate->Update(dt);
    if (m_btnCancel) m_btnCancel->Update(dt);

    if (m_errorTimer > 0.0f)
        m_errorTimer -= dt;
}

// ═════════════════════════════════════════════════════════════════════════════
// OnRender
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::OnRender(sakura::core::Renderer& renderer)
{
    // 背景
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 8, 6, 18, 255 });

    // 标题
    if (m_fontTitle != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_fontTitle, "新建谱面向导",
            0.5f, 0.070f, 0.042f,
            sakura::core::Color{ 255, 200, 240, 230 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontTitle, "填写谱面基本信息，创建后将自动切换到编辑器",
            0.5f, 0.118f, 0.020f,
            sakura::core::Color{ 160, 150, 200, 180 },
            sakura::core::TextAlign::Center);
    }

    // 横线
    renderer.DrawLine(0.10f, 0.145f, 0.90f, 0.145f,
        sakura::core::Color{ 80, 60, 120, 120 }, 0.001f);

    // 字段标签 + 输入框
    const char* labelTexts[FIELD_COUNT] = {
        "曲名 *", "作曲/艺术家", "BPM *", "偏移 (ms)", "难度名称 *", "音乐文件", "输出目录"
    };
    constexpr float LABEL_X    = 0.37f;
    constexpr float FIELD_Y0   = 0.175f;
    constexpr float FIELD_STEP = 0.080f;

    bool required[FIELD_COUNT] = { true, false, true, false, true, false, false };

    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        float y = FIELD_Y0 + i * FIELD_STEP;

        // 标签
        if (m_fontLabel != sakura::core::INVALID_HANDLE)
        {
            sakura::core::Color labelColor = (i == m_focusedField)
                ? sakura::core::Color{ 200, 180, 255, 230 }
                : sakura::core::Color{ 140, 130, 180, 200 };
            renderer.DrawText(m_fontLabel, labelTexts[i],
                LABEL_X, y + 0.022f, 0.020f,
                labelColor,
                sakura::core::TextAlign::Right);

            // 必填红点
            if (required[i])
            {
                renderer.DrawFilledRect({ LABEL_X + 0.003f, y + 0.012f, 0.006f, 0.006f },
                    sakura::core::Color{ 220, 80, 80, 220 });
            }
        }

        // 输入框
        if (m_fields[i]) m_fields[i]->Render(renderer);
    }

    // Tab 提示
    if (m_fontLabel != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_fontLabel,
            "Tab 切换字段  |  Enter 确认  |  ESC 取消",
            0.5f, 0.703f, 0.017f,
            sakura::core::Color{ 100, 90, 140, 160 },
            sakura::core::TextAlign::Center);
    }

    // 按钮
    if (m_btnCreate) m_btnCreate->Render(renderer);
    if (m_btnCancel) m_btnCancel->Render(renderer);

    // 错误信息
    if (m_errorTimer > 0.0f && !m_errorMsg.empty() && m_fontLabel != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawFilledRect({ 0.25f, 0.816f, 0.50f, 0.040f },
            sakura::core::Color{ 160, 30, 40, 200 });
        renderer.DrawText(m_fontLabel, m_errorMsg,
            0.50f, 0.836f, 0.018f,
            sakura::core::Color{ 255, 200, 200, 230 },
            sakura::core::TextAlign::Center);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// OnEvent
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::OnEvent(const SDL_Event& event)
{
    // 先分发到按钮
    if (m_btnCreate) m_btnCreate->HandleEvent(event);
    if (m_btnCancel) m_btnCancel->HandleEvent(event);

    // 全局键盘
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const SDL_Scancode sc = event.key.scancode;

        if (sc == SDL_SCANCODE_ESCAPE)
        {
            m_manager.SwitchScene(
                std::make_unique<SceneMenu>(m_manager),
                TransitionType::SlideRight, 0.4f);
            return;
        }

        if (sc == SDL_SCANCODE_TAB)
        {
            // Shift+Tab = 向前，Tab = 向后
            bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            int next = m_focusedField + (shift ? -1 : 1);
            if (next < 0)           next = FIELD_COUNT - 1;
            if (next >= FIELD_COUNT) next = 0;
            FocusField(next);
            return;
        }

        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
        {
            // 若在最后一个字段 Enter = 提交；否则移到下一字段
            if (m_focusedField == FIELD_COUNT - 1)
            {
                ValidateAndCreate();
            }
            else
            {
                FocusField(m_focusedField + 1);
            }
            return;
        }
    }

    // 分发到各输入框
    for (int i = 0; i < FIELD_COUNT; ++i)
    {
        if (!m_fields[i]) continue;
        if (m_fields[i]->HandleEvent(event))
        {
            // 输入框被点击时更新聚焦索引
            if (m_fields[i]->IsFocused())
                m_focusedField = i;
            break;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ShowError
// ═════════════════════════════════════════════════════════════════════════════

void SceneChartWizard::ShowError(const std::string& msg)
{
    m_errorMsg   = msg;
    m_errorTimer = 4.0f;
    LOG_WARN("[SceneChartWizard] 错误: {}", msg);
}

// ═════════════════════════════════════════════════════════════════════════════
// Slugify
// ═════════════════════════════════════════════════════════════════════════════

std::string SceneChartWizard::Slugify(const std::string& title)
{
    std::string result;
    for (unsigned char c : title)
    {
        if (std::isalnum(c))
            result += static_cast<char>(std::tolower(c));
        else if (std::isspace(c) || c == '-' || c == '_')
            result += '_';
        // 多字节 UTF-8 字节跳过（不影响简单 ASCII）
    }
    if (result.empty()) result = "chart";
    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateAndCreate
// ═════════════════════════════════════════════════════════════════════════════

bool SceneChartWizard::ValidateAndCreate()
{
    if (!m_fields[0] || !m_fields[2] || !m_fields[4] || !m_fields[6]) return false;

    // 验证必填字段
    std::string title     = m_fields[0]->GetText();
    std::string artist    = m_fields[1] ? m_fields[1]->GetText() : "";
    std::string bpmStr    = m_fields[2]->GetText();
    std::string offsetStr = m_fields[3] ? m_fields[3]->GetText() : "0";
    std::string diffName  = m_fields[4]->GetText();
    std::string musicFile = m_fields[5] ? m_fields[5]->GetText() : "";
    std::string outFolder = m_fields[6]->GetText();

    if (title.empty())
    {
        ShowError("曲名不能为空");
        FocusField(0);
        return false;
    }

    float bpm = 120.0f;
    try { bpm = std::stof(bpmStr); } catch (...) {}
    if (bpm < 10.0f || bpm > 999.0f)
    {
        ShowError("BPM 必须在 10~999 之间");
        FocusField(2);
        return false;
    }

    int offsetMs = 0;
    try { offsetMs = std::stoi(offsetStr); } catch (...) {}

    if (diffName.empty())
    {
        ShowError("难度名称不能为空");
        FocusField(4);
        return false;
    }

    if (outFolder.empty())
    {
        outFolder = "resources/charts/" + Slugify(title);
    }

    // 生成难度文件名
    std::string diffFile = Slugify(diffName) + ".json";

    if (!CreateChartFiles(outFolder, title, artist, bpm, offsetMs, diffName, musicFile))
        return false;

    sakura::ui::ToastManager::Instance().Show(
        "谱面已创建，正在打开编辑器...", sakura::ui::ToastType::Success);

    m_manager.SwitchScene(
        std::make_unique<SceneEditor>(m_manager, outFolder, diffFile),
        TransitionType::SlideLeft, 0.4f);
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// CreateChartFiles
// ═════════════════════════════════════════════════════════════════════════════

bool SceneChartWizard::CreateChartFiles(const std::string& folderPath,
                                        const std::string& title,
                                        const std::string& artist,
                                        float bpm, int offsetMs,
                                        const std::string& diffName,
                                        const std::string& musicFile)
{
    try
    {
        // 创建目录
        fs::create_directories(folderPath);
        LOG_INFO("[SceneChartWizard] 创建目录: {}", folderPath);
    }
    catch (const fs::filesystem_error& e)
    {
        ShowError("无法创建目录: " + std::string(e.what()));
        return false;
    }

    std::string chartId  = Slugify(title);
    std::string diffFile = Slugify(diffName) + ".json";

    // ── 写 info.json ──────────────────────────────────────────────────────────
    {
        json info;
        info["version"]         = 2;
        info["id"]              = chartId;
        info["title"]           = title;
        info["artist"]          = artist.empty() ? "Unknown" : artist;
        info["charter"]         = "Me";
        info["source"]          = "";
        info["tags"]            = json::array();
        info["music_file"]      = musicFile.empty() ? "music.ogg" : musicFile;
        info["cover_file"]      = "cover.png";
        info["background_file"] = "bg.png";
        info["preview_time"]    = 0;
        info["bpm"]             = bpm;
        info["offset"]          = offsetMs;

        json diff;
        diff["name"]             = diffName;
        diff["level"]            = 5.0f;
        diff["chart_file"]       = diffFile;
        diff["note_count"]       = 0;
        diff["hold_count"]       = 0;
        diff["mouse_note_count"] = 0;
        info["difficulties"]     = json::array({ diff });

        std::string infoPath = folderPath + "/info.json";
        std::ofstream f(infoPath);
        if (!f.is_open())
        {
            ShowError("无法写入 info.json");
            return false;
        }
        f << info.dump(4);
        f.close();
        LOG_INFO("[SceneChartWizard] 写入 info.json: {}", infoPath);
    }

    // ── 写空难度文件 ──────────────────────────────────────────────────────────
    {
        json chart;
        chart["version"] = 2;

        json tp;
        tp["time"]           = 0;
        tp["bpm"]            = bpm;
        tp["time_signature"] = json::array({ 4, 4 });
        chart["timing_points"] = json::array({ tp });

        chart["sv_points"]       = json::array();
        chart["keyboard_notes"]  = json::array();
        chart["mouse_notes"]     = json::array();

        std::string chartPath = folderPath + "/" + diffFile;
        std::ofstream f(chartPath);
        if (!f.is_open())
        {
            ShowError("无法写入难度文件: " + diffFile);
            return false;
        }
        f << chart.dump(4);
        f.close();
        LOG_INFO("[SceneChartWizard] 写入难度文件: {}", chartPath);
    }

    return true;
}

} // namespace sakura::scene
