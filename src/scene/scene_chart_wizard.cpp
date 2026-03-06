// scene_chart_wizard.cpp — 新建谱面向导实现

#include "scene_chart_wizard.h"
#include "scene_menu.h"
#include "scene_editor.h"
#include "core/resource_manager.h"
#include "utils/logger.h"
#include "ui/toast.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

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
    constexpr float FORM_W     = 0.33f;  // 输入框宽度
    constexpr float FIELD_H    = 0.044f;
    constexpr float FIELD_Y0   = 0.175f;
    constexpr float FIELD_STEP = 0.068f;

    const char* placeholders[FIELD_COUNT] = {
        "必填：歌曲标题（如 Cherry Blossoms）",
        "可选：作曲家/艺术家",
        "必填：BPM（如 120 或 120.5）",
        "可选：初始偏移（毫秒，如 0）",
        "必填：难度名称（如 Normal）",
        "可选：点击右侧添加，选择音乐源文件",
        "可选：点击右侧添加，选择封面源文件",
        "可选：点击右侧添加，选择背景源文件",
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
    m_fields[FIELD_BPM]->SetText("120");
    m_fields[FIELD_OFFSET]->SetText("0");
    m_fields[FIELD_DIFF_NAME]->SetText("Normal");

    // 当曲名变化时，自动更新输出目录
    m_fields[FIELD_TITLE]->SetOnChange([this](const std::string& title) {
        if (!title.empty())
        {
            std::string slug = Slugify(title);
            m_fields[FIELD_OUTPUT_DIR]->SetText("resources/charts/" + slug);
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
        "创建谱面", m_fontTitle, 0.022f, 0.010f);
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
        "取消", m_fontTitle, 0.022f, 0.010f);
    m_btnCancel->SetColors(cancelColors);
    m_btnCancel->SetOnClick([this]() {
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
    });

    // 资源添加按钮（音乐/封面/背景）
    const float btnX[3] = { 0.72f, 0.72f, 0.72f };
    const float btnY[3] = { 0.515f, 0.583f, 0.651f };
    for (int i = 0; i < 3; ++i)
    {
        m_btnPickResource[i] = std::make_unique<sakura::ui::Button>(
            sakura::core::NormRect{ btnX[i], btnY[i], 0.08f, 0.044f },
            "添加", m_fontLabel, 0.018f, 0.007f);
        m_btnPickResource[i]->SetOnClick([this, i]()
        {
            OpenResourceFileDialog(FIELD_MUSIC_SRC + i);
        });
    }
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
    for (auto& btn : m_btnPickResource)
        if (btn) btn->Update(dt);

    if (m_errorTimer > 0.0f)
        m_errorTimer -= dt;

    ApplyPendingDialogResults();
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
        "曲名 *", "作曲/艺术家", "BPM *", "偏移 (ms)", "难度名称 *",
        "音乐源文件", "封面源文件", "背景源文件", "输出目录"
    };
    constexpr float LABEL_X    = 0.37f;
    constexpr float FIELD_H    = 0.044f;
    constexpr float LABEL_SIZE = 0.020f;
    constexpr float FIELD_Y0   = 0.175f;
    constexpr float FIELD_STEP = 0.068f;

    bool required[FIELD_COUNT] = { true, false, true, false, true, false, false, false, false };

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
                LABEL_X, y + (FIELD_H - LABEL_SIZE) * 0.5f, LABEL_SIZE,
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
    for (auto& btn : m_btnPickResource)
        if (btn) btn->Render(renderer);
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
    for (auto& btn : m_btnPickResource)
        if (btn) btn->HandleEvent(event);

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
    if (!m_fields[FIELD_TITLE] || !m_fields[FIELD_BPM]
        || !m_fields[FIELD_DIFF_NAME] || !m_fields[FIELD_OUTPUT_DIR])
        return false;

    // 验证必填字段
    std::string title          = m_fields[FIELD_TITLE]->GetText();
    std::string artist         = m_fields[FIELD_ARTIST] ? m_fields[FIELD_ARTIST]->GetText() : "";
    std::string bpmStr         = m_fields[FIELD_BPM]->GetText();
    std::string offsetStr      = m_fields[FIELD_OFFSET] ? m_fields[FIELD_OFFSET]->GetText() : "0";
    std::string diffName       = m_fields[FIELD_DIFF_NAME]->GetText();
    std::string musicSource    = m_fields[FIELD_MUSIC_SRC] ? m_fields[FIELD_MUSIC_SRC]->GetText() : "";
    std::string coverSource    = m_fields[FIELD_COVER_SRC] ? m_fields[FIELD_COVER_SRC]->GetText() : "";
    std::string backgroundSource = m_fields[FIELD_BG_SRC] ? m_fields[FIELD_BG_SRC]->GetText() : "";
    std::string outFolder      = m_fields[FIELD_OUTPUT_DIR]->GetText();

    if (title.empty())
    {
        ShowError("曲名不能为空");
        FocusField(FIELD_TITLE);
        return false;
    }

    float bpm = 120.0f;
    try { bpm = std::stof(bpmStr); } catch (...) {}
    if (bpm < 10.0f || bpm > 999.0f)
    {
        ShowError("BPM 必须在 10~999 之间");
        FocusField(FIELD_BPM);
        return false;
    }

    int offsetMs = 0;
    try { offsetMs = std::stoi(offsetStr); } catch (...) {}

    if (diffName.empty())
    {
        ShowError("难度名称不能为空");
        FocusField(FIELD_DIFF_NAME);
        return false;
    }

    if (outFolder.empty())
    {
        outFolder = "resources/charts/" + Slugify(title);
    }

    // 生成难度文件名
    std::string diffFile = Slugify(diffName) + ".json";

    if (!CreateChartFiles(outFolder, title, artist, bpm, offsetMs, diffName,
                          musicSource, coverSource, backgroundSource))
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
                                        const std::string& musicSourceFile,
                                        const std::string& coverSourceFile,
                                        const std::string& backgroundSourceFile)
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
    std::string musicFileName;
    std::string coverFileName;
    std::string backgroundFileName;

    if (!CopyResourceToChartFolder(
            musicSourceFile, folderPath, "music", ".ogg", "音乐文件",
            { ".ogg", ".mp3", ".wav", ".flac" }, musicFileName))
        return false;
    if (!CopyResourceToChartFolder(
            coverSourceFile, folderPath, "cover", ".png", "封面文件",
            { ".png", ".jpg", ".jpeg", ".webp", ".bmp" }, coverFileName, true))
        return false;
    if (!CopyResourceToChartFolder(
            backgroundSourceFile, folderPath, "bg", ".png", "背景文件",
            { ".png", ".jpg", ".jpeg", ".webp", ".bmp" }, backgroundFileName, true))
        return false;

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
        info["music_file"]      = musicFileName;
        info["cover_file"]      = coverFileName;
        info["background_file"] = backgroundFileName;
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

bool SceneChartWizard::CopyResourceToChartFolder(const std::string& sourcePath,
                                                 const std::string& folderPath,
                                                 const std::string& standardBaseName,
                                                 const std::string& defaultExtension,
                                                 const std::string& resourceLabel,
                                                 const std::vector<std::string>& allowedExtensions,
                                                 std::string& outFileName,
                                                 bool convertToPng)
{
    auto normalizeExtension = [](const std::string& ext, const std::string& fallback) -> std::string
    {
        std::string lower = ext.empty() ? fallback : ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.empty()) lower = fallback;
        if (lower.empty() || lower[0] != '.') lower.insert(lower.begin(), '.');
        if (lower.size() <= 1) return fallback;
        for (size_t i = 1; i < lower.size(); ++i)
        {
            if (!std::isalnum(static_cast<unsigned char>(lower[i])))
                return fallback;
        }
        return lower;
    };

    // 如果需要转 PNG，始终输出 .png 文件名
    if (convertToPng)
        outFileName = standardBaseName + ".png";
    else
        outFileName = standardBaseName + normalizeExtension("", defaultExtension);
    if (sourcePath.empty()) return true;

    fs::path source = fs::u8path(sourcePath);
    if (!fs::exists(source) || !fs::is_regular_file(source))
    {
        ShowError(resourceLabel + "不存在或不可读");
        return false;
    }

    std::string ext = normalizeExtension(source.extension().string(), defaultExtension);
    if (!allowedExtensions.empty())
    {
        bool matched = std::any_of(allowedExtensions.begin(), allowedExtensions.end(),
            [&ext](const std::string& allowed)
            {
                return ext == allowed;
            });
        if (!matched)
        {
            ShowError(resourceLabel + "格式不支持: " + ext);
            return false;
        }
    }
    if (!convertToPng)
        outFileName = standardBaseName + ext;
    // convertToPng 情况下 outFileName 已在前面设置为 standardBaseName + ".png"

    fs::path target = fs::path(folderPath) / outFileName;
    try
    {
        if (convertToPng)
        {
            // 通过 SDL_image 加载再保存，去除所有特殊元数据，确保与 SDL_Renderer 兼容
            SDL_Surface* surface = IMG_Load(source.string().c_str());
            if (surface)
            {
                bool saved = IMG_SavePNG(surface, target.string().c_str());
                SDL_DestroySurface(surface);
                if (saved)
                {
                    LOG_INFO("[SceneChartWizard] 已转换{}为 PNG: {} -> {}", resourceLabel, sourcePath, target.string());
                    return true;
                }
                LOG_WARN("[SceneChartWizard] IMG_SavePNG 失败 ({}), 回退到直接复制", SDL_GetError());
            }
            else
            {
                LOG_WARN("[SceneChartWizard] IMG_Load 失败 ({}), 回退到直接复制", SDL_GetError());
            }
            // 回退：直接复制（保留原扩展名）
            outFileName = standardBaseName + ext;
            target = fs::path(folderPath) / outFileName;
        }

        if (fs::exists(target) && fs::equivalent(source, target))
            return true;

        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
        LOG_INFO("[SceneChartWizard] 已复制{}: {} -> {}", resourceLabel, sourcePath, target.string());
    }
    catch (const fs::filesystem_error& e)
    {
        ShowError("复制" + resourceLabel + "失败: " + std::string(e.what()));
        return false;
    }
    return true;
}

void SceneChartWizard::OpenResourceFileDialog(int fieldIndex)
{
    struct DialogRequest
    {
        SceneChartWizard* wizard = nullptr;
        int field = -1;
    };

    auto req = std::make_unique<DialogRequest>();
    req->wizard = this;
    req->field  = fieldIndex;
    SDL_ClearError();
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            std::unique_ptr<DialogRequest> holder(static_cast<DialogRequest*>(userdata));
            if (!holder || !holder->wizard) return;
            if (!filelist)
            {
                const char* err = SDL_GetError();
                holder->wizard->QueueResourceFileError(
                    std::string("打开文件选择器失败: ") + (err ? err : "未知错误"));
                return;
            }
            if (filelist[0] == nullptr) return; // 用户主动取消，属于正常行为
            holder->wizard->QueueResourceFileSelection(holder->field, filelist[0]);
        },
        req.release(),
        nullptr, nullptr, 0, nullptr, false);
    const char* err = SDL_GetError();
    if (err && err[0] != '\0')
        QueueResourceFileError(std::string("打开文件选择器失败: ") + err);
}

void SceneChartWizard::QueueResourceFileSelection(int fieldIndex, const std::string& filePath)
{
    std::scoped_lock lock(m_pendingDialogMutex);
    m_pendingDialogResults.push_back(PendingDialogResult{ fieldIndex, filePath, "" });
}

void SceneChartWizard::QueueResourceFileError(const std::string& errorMsg)
{
    std::scoped_lock lock(m_pendingDialogMutex);
    m_pendingDialogResults.push_back(PendingDialogResult{ -1, "", errorMsg });
}

void SceneChartWizard::ApplyPendingDialogResults()
{
    std::vector<PendingDialogResult> pending;
    {
        std::scoped_lock lock(m_pendingDialogMutex);
        if (m_pendingDialogResults.empty()) return;
        pending.swap(m_pendingDialogResults);
    }

    for (const auto& item : pending)
    {
        if (!item.errorMessage.empty())
        {
            ShowError(item.errorMessage);
            continue;
        }
        if (item.fieldIndex < 0 || item.fieldIndex >= FIELD_COUNT || !m_fields[item.fieldIndex])
            continue;
        m_fields[item.fieldIndex]->SetText(item.filePath);
    }
}

} // namespace sakura::scene
