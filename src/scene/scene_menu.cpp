// scene_menu.cpp — 主菜单场景

#include "scene_menu.h"
#include "scene_select.h"
#include "scene_market.h"
#include "scene_course.h"
#include "scene_arena.h"
#include "data/database.h"
#include "scene_stats.h"
#include "scene_tutorial.h"
#include "scene_settings.h"
#include "scene_editor.h"
#include "scene_chart_wizard.h"
#include "audio/audio_manager.h"
#include "audio/audio_visualizer.h"
#include "core/input.h"
#include "core/config.h"
#include "core/paths.h"
#include "game/chart_loader.h"
#include "game/pp_calculator.h"
#include "game/season.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "effects/particle_system.h"
#include "effects/glow.h"
#include "ui/visual_style.h"

// version.h 由 CMake 生成
#if __has_include("version.h")
#  include "version.h"
#else
#  define SAKURA_VERSION_STRING "0.0.0"
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>

namespace sakura::scene
{

namespace
{
constexpr sakura::core::NormRect EDITOR_MODAL_RECT { 0.240f, 0.140f, 0.520f, 0.700f };
constexpr sakura::core::NormRect EDITOR_LIST_RECT  { 0.268f, 0.270f, 0.464f, 0.408f };
constexpr float EDITOR_ROW_X          = 0.276f;
constexpr float EDITOR_ROW_Y0         = 0.278f;
constexpr float EDITOR_ROW_H          = 0.072f;
constexpr float EDITOR_ROW_STRIDE     = 0.080f;
constexpr float EDITOR_ROW_W_SCROLL   = 0.434f;
constexpr float EDITOR_ROW_W_FULL     = 0.448f;
constexpr float EDITOR_SCROLL_TRACK_X = 0.718f;
constexpr float EDITOR_SCROLL_TRACK_Y = 0.278f;
constexpr float EDITOR_SCROLL_TRACK_W = 0.006f;
constexpr float EDITOR_SCROLL_TRACK_H = 0.392f;
constexpr float EDITOR_DIFF_LABEL_X   = 0.268f;
constexpr float EDITOR_DIFF_X0        = 0.340f;
constexpr float EDITOR_DIFF_Y         = 0.690f;
constexpr float EDITOR_DIFF_W         = 0.392f;
constexpr float EDITOR_DIFF_H         = 0.032f;

std::string FormatEditorNumber(float value, int precision = 0)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

constexpr sakura::core::NormRect ACCOUNT_MODAL_RECT      { 0.220f, 0.145f, 0.560f, 0.710f };
constexpr sakura::core::NormRect ACCOUNT_SUMMARY_RECT    { 0.248f, 0.686f, 0.270f, 0.074f };
constexpr sakura::core::NormRect ACCOUNT_SAVED_BOX_RECT  { 0.534f, 0.304f, 0.222f, 0.456f };
constexpr sakura::core::NormRect ACCOUNT_MSG_BANNER_RECT { 0.248f, 0.778f, 0.508f, 0.052f };
constexpr float SAVED_SLOT_X      = 0.542f;
constexpr float SAVED_SLOT_Y0     = 0.314f;
constexpr float SAVED_SLOT_W      = 0.206f;
constexpr float SAVED_SLOT_H      = 0.076f;
constexpr float SAVED_SLOT_STRIDE = 0.087f;

sakura::core::NormRect GetSavedAccountSlotRect(int slot)
{
    return {
        SAVED_SLOT_X,
        SAVED_SLOT_Y0 + static_cast<float>(slot) * SAVED_SLOT_STRIDE,
        SAVED_SLOT_W,
        SAVED_SLOT_H
    };
}

sakura::core::NormRect GetSavedAccountRemoveRect(int slot)
{
    const float rowY = SAVED_SLOT_Y0 + static_cast<float>(slot) * SAVED_SLOT_STRIDE;
    return {
        SAVED_SLOT_X + SAVED_SLOT_W - 0.030f,
        rowY + 0.008f,
        0.022f,
        0.026f
    };
}

bool ContainsNormPoint(const sakura::core::NormRect& rect, float x, float y)
{
    return x >= rect.x && x <= (rect.x + rect.width) &&
           y >= rect.y && y <= (rect.y + rect.height);
}

std::string TruncateUtf8Codepoints(std::string_view sv, std::size_t maxChars)
{
    std::size_t count = 0;
    std::size_t idx   = 0;
    while (idx < sv.size() && count < maxChars)
    {
        const unsigned char lead = static_cast<unsigned char>(sv[idx]);
        std::size_t step = 1;
        if ((lead & 0x80u) == 0u)        step = 1;
        else if ((lead & 0xE0u) == 0xC0u) step = 2;
        else if ((lead & 0xF0u) == 0xE0u) step = 3;
        else if ((lead & 0xF8u) == 0xF0u) step = 4;
        if (idx + step > sv.size())
            break;
        idx += step;
        ++count;
    }
    if (idx < sv.size())
        return std::string(sv.substr(0, idx)) + "…";
    return std::string(sv);
}

std::string FormatEditorLevel(float level)
{
    const float rounded = std::round(level);
    if (std::abs(level - rounded) < 0.05f)
        return FormatEditorNumber(rounded, 0);
    return FormatEditorNumber(level, 1);
}
} // namespace

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneMenu::SceneMenu(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneMenu::OnEnter()
{
    LOG_INFO("[SceneMenu] 进入主菜单");

    // 获取字体
    auto& rm      = sakura::core::ResourceManager::GetInstance();
    m_fontTitle   = rm.GetDefaultFontHandle();
    m_fontSub     = rm.GetDefaultFontHandle();
    m_fontButton  = rm.GetDefaultFontHandle();
    m_topBar      = std::make_unique<sakura::ui::TopBar>("SAKURA  ·  月夜墨樱", m_fontSub);
    sakura::audio::AudioManager::GetInstance().EnsureMenuBgm();

    // 初始化入场动画
    m_enterTimer            = 0.0f;
    m_anim.titleOffsetX     = -0.15f;
    m_anim.titleTimer       = 0.0f;
    m_anim.done             = false;
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        m_anim.btnOffsetX[i] = -0.25f;
        m_anim.btnTimers[i]  = 0.0f;
    }

    // 重置状态
    *m_alive = true;
    m_showExitConfirm       = false;
    m_showEditorMenu        = false;
    m_showAccountModal      = false;
    m_authBusy              = false;
    m_changePasswordMode    = false;
    m_hoveredSavedSlot      = -1;
    m_hoveredSavedRemove    = -1;
    m_savedAccounts.clear();
    m_authMessage.clear();
    m_selectedChartIdx      = -1;
    m_selectedEditorDiffIdx = 0;
    m_editorScrollRow       = 0;
    m_hoveredEditorRow      = -1;
    m_editorWheelAccum      = 0.0f;
    m_draggingEditorScroll  = false;
    m_customCharts.clear();

    // ── 特效初始化 ────────────────────────────────────────────────────────────
    m_particles.Clear();
    m_glowPhase = 0.0f;
    m_clickRings.clear();

    // 樱花飘落（伪3D三层）
    auto cfgFG = sakura::effects::ParticlePresets::SakuraPetalForeground();
    m_sakuraPetalEmitterFG = m_particles.EmitContinuous(0.5f, -0.05f, 2.0f, cfgFG);

    auto cfgMG = sakura::effects::ParticlePresets::SakuraPetalMidground();
    m_sakuraPetalEmitterMG = m_particles.EmitContinuous(0.5f, -0.05f, 5.0f, cfgMG);

    auto cfgBG = sakura::effects::ParticlePresets::SakuraPetalBackground();
    m_sakuraPetalEmitterBG = m_particles.EmitContinuous(0.5f, -0.05f, 10.0f, cfgBG);

    // 背景微粒：全屏随机漂浮（3/s，低透明度）
    auto bgCfg = sakura::effects::ParticlePresets::BackgroundFloat();
    m_bgFloatEmitter = m_particles.EmitContinuous(0.5f, 0.5f, 3.0f, bgCfg);

    // 创建按钮
    SetupButtons();
    SetupConfirmButtons();
    SetupEditorMenuButtons();
    SetupAccountModal();
    RefreshSavedAccounts();
    RefreshAccountCapsuleText(true);
    UpdateAccountModalLayout();
}

// ── SetupButtons ──────────────────────────────────────────────────────────────

void SceneMenu::SetupButtons()
{
    const char* labels[BUTTON_COUNT] = {
        "开始旅程   /   PLAY", "初次见面   /   教程", "我的记录   /   统计", "谱面工房   /   编辑器", "偏好设置   /   SETTINGS", "离开游戏"
    };

    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        float y = BTN_Y0 + i * BTN_GAP;
        sakura::core::NormRect bounds = { BTN_X, y, BTN_W, BTN_H };
        m_buttons[i] = std::make_unique<sakura::ui::Button>(
            bounds, labels[i], m_fontButton, 0.028f, 0.012f);
        sakura::ui::VisualStyle::ApplyButton(
            m_buttons[i].get(),
            i == 0 ? sakura::ui::ButtonVariant::Primary
                   : (i == BUTTON_COUNT - 1 ? sakura::ui::ButtonVariant::Danger
                                            : sakura::ui::ButtonVariant::Secondary));
        m_buttons[i]->SetTextAlign(sakura::core::TextAlign::Left);
        m_buttons[i]->SetTextPadding(0.02f);
    }

    // 开始游戏 → 切换到选歌场景
    m_buttons[0]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：开始游戏");
        m_manager.SwitchScene(
            std::make_unique<SceneSelect>(m_manager),
            TransitionType::SakuraSweep, 0.45f);
    });

    // 教程 → 新手教程场景
    m_buttons[1]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：教程");
        m_manager.SwitchScene(
            std::make_unique<SceneTutorial>(m_manager, false),
            TransitionType::SilkShutter, 0.42f);
    });

    // 统计 → 进入统计场景
    m_buttons[2]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：统计");
        m_manager.SwitchScene(
            std::make_unique<SceneStats>(m_manager),
            TransitionType::SilkShutter, 0.42f);
    });

    // 谱面编辑器 → 显示编辑器子菜单
    m_buttons[3]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：谱面编辑器");
        ScanCustomCharts();
        m_showEditorMenu = true;
    });

    // 设置 → 切换到设置场景
    m_buttons[4]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：设置");
        m_manager.SwitchScene(
            std::make_unique<SceneSettings>(m_manager),
            TransitionType::SilkShutter, 0.42f);
    });

    // 退出 → 弹出确认对话框
    m_buttons[5]->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：退出（显示确认框）");
        m_showExitConfirm = true;
    });
}

// ── SetupEditorMenuButtons ────────────────────────────────────────────────────

void SceneMenu::SetupEditorMenuButtons()
{
    // 面板区域：x=0.24, y=0.14, w=0.52, h=0.70；底部操作按钮横排
    m_btnEditorOpen = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.268f, 0.738f, 0.205f, 0.054f },
        "打开选中谱面", m_fontButton, 0.023f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnEditorOpen.get(), sakura::ui::ButtonVariant::Primary);
    m_btnEditorOpen->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnEditorOpen->SetOnClick([this]()
    {
        if (m_customCharts.empty())
        {
            LOG_INFO("[SceneMenu] 暂无可编辑谱面，无法打开");
        }
        else
        {
            const int idx = (m_selectedChartIdx >= 0 &&
                             m_selectedChartIdx < static_cast<int>(m_customCharts.size()))
                            ? m_selectedChartIdx : 0;
            OpenEditorForChart(idx);
        }
    });

    m_btnEditorNew = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.485f, 0.738f, 0.135f, 0.054f },
        "新建谱面", m_fontButton, 0.023f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnEditorNew.get(), sakura::ui::ButtonVariant::Accent);
    m_btnEditorNew->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnEditorNew->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 点击：新建谱面向导");
        m_showEditorMenu       = false;
        m_draggingEditorScroll = false;
        m_manager.SwitchScene(
            std::make_unique<SceneChartWizard>(m_manager),
            TransitionType::SlideLeft, 0.4f);
    });

    m_btnEditorCancel = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.632f, 0.738f, 0.100f, 0.054f },
        "关 闭", m_fontButton, 0.022f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnEditorCancel.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnEditorCancel->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnEditorCancel->SetOnClick([this]()
    {
        m_showEditorMenu       = false;
        m_draggingEditorScroll = false;
    });
}

// ── ScanCustomCharts ──────────────────────────────────────────────────────────

void SceneMenu::ScanCustomCharts()
{
    m_customCharts.clear();
    m_selectedChartIdx      = -1;
    m_selectedEditorDiffIdx = 0;
    m_editorScrollRow       = 0;
    m_hoveredEditorRow      = -1;
    m_editorWheelAccum      = 0.0f;
    m_draggingEditorScroll  = false;

    // 确保谱面目录存在
    std::error_code ec;
    std::filesystem::create_directories(sakura::core::Paths::User("charts"), ec);

    sakura::game::ChartLoader loader;
    auto userCharts    = loader.ScanCharts(sakura::core::Paths::User("charts"));
    auto builtinCharts = loader.ScanCharts(CUSTOM_CHARTS_PATH);

    auto sortByTitle = [](const sakura::game::ChartInfo& a, const sakura::game::ChartInfo& b)
    {
        const std::string& ta = a.title.empty() ? a.id : a.title;
        const std::string& tb = b.title.empty() ? b.id : b.title;
        if (ta != tb) return ta < tb;
        return a.id < b.id;
    };
    std::stable_sort(userCharts.begin(), userCharts.end(), sortByTitle);
    std::stable_sort(builtinCharts.begin(), builtinCharts.end(), sortByTitle);

    auto appendChart = [this](sakura::game::ChartInfo& ci, bool isUser)
    {
        ChartEntry entry;
        entry.id           = ci.id;
        entry.folderPath   = ci.folderPath;
        entry.title        = ci.title.empty() ? ci.id : ci.title;
        entry.artist       = ci.artist.empty() ? "Unknown Artist" : ci.artist;
        entry.charter      = ci.charter;
        entry.bpm          = ci.bpm;
        entry.isUserChart  = isUser;
        entry.difficulties = std::move(ci.difficulties);
        if (entry.difficulties.empty())
        {
            sakura::game::DifficultyInfo fallback;
            fallback.name      = "Normal";
            fallback.level     = 1.0f;
            fallback.chartFile = "normal.json";
            entry.difficulties.push_back(std::move(fallback));
        }
        m_customCharts.push_back(std::move(entry));
    };

    for (auto& ci : userCharts)
        appendChart(ci, true);

    for (auto& ci : builtinCharts)
    {
        const bool existsInUser = std::any_of(
            m_customCharts.begin(), m_customCharts.end(),
            [&](const ChartEntry& other) { return !ci.id.empty() && other.id == ci.id; });
        if (!existsInUser)
            appendChart(ci, false);
    }

    if (!m_customCharts.empty())
    {
        int initialIdx = 0;
        const std::string lastChart = sakura::core::Config::GetInstance().Get<std::string>("library.last_chart", "");
        if (!lastChart.empty())
        {
            for (int i = 0; i < static_cast<int>(m_customCharts.size()); ++i)
            {
                if (m_customCharts[i].id == lastChart)
                {
                    initialIdx = i;
                    break;
                }
            }
        }
        SelectEditorChart(initialIdx, true);
        if (!lastChart.empty() && m_customCharts[initialIdx].id == lastChart)
        {
            const int lastDiff = sakura::core::Config::GetInstance().Get<int>("library.last_difficulty", 0);
            m_selectedEditorDiffIdx = std::clamp(
                lastDiff, 0, static_cast<int>(m_customCharts[initialIdx].difficulties.size()) - 1);
        }
    }

    LOG_INFO("[SceneMenu] 扫描可编辑谱面: {} 首", m_customCharts.size());
}

void SceneMenu::SelectEditorChart(int idx, bool ensureVisible)
{
    if (m_customCharts.empty())
    {
        m_selectedChartIdx      = -1;
        m_selectedEditorDiffIdx = 0;
        m_editorScrollRow       = 0;
        return;
    }

    const int total   = static_cast<int>(m_customCharts.size());
    const int clamped = std::clamp(idx, 0, total - 1);
    if (clamped != m_selectedChartIdx)
    {
        m_selectedChartIdx      = clamped;
        m_selectedEditorDiffIdx = 0;
    }
    else
    {
        m_selectedEditorDiffIdx = std::clamp(
            m_selectedEditorDiffIdx, 0,
            static_cast<int>(m_customCharts[m_selectedChartIdx].difficulties.size()) - 1);
    }

    if (ensureVisible)
    {
        if (m_selectedChartIdx < m_editorScrollRow)
            m_editorScrollRow = m_selectedChartIdx;
        else if (m_selectedChartIdx >= m_editorScrollRow + EDITOR_VISIBLE_ROWS)
            m_editorScrollRow = m_selectedChartIdx - EDITOR_VISIBLE_ROWS + 1;
    }

    const int maxScroll = std::max(0, total - EDITOR_VISIBLE_ROWS);
    m_editorScrollRow = std::clamp(m_editorScrollRow, 0, maxScroll);
}

void SceneMenu::ScrollEditorListBy(int deltaRows)
{
    const int total     = static_cast<int>(m_customCharts.size());
    const int maxScroll = std::max(0, total - EDITOR_VISIBLE_ROWS);
    m_editorScrollRow   = std::clamp(m_editorScrollRow + deltaRows, 0, maxScroll);
}

void SceneMenu::UpdateEditorScrollFromMouseY(float normY)
{
    const int total     = static_cast<int>(m_customCharts.size());
    const int maxScroll = std::max(0, total - EDITOR_VISIBLE_ROWS);
    if (maxScroll <= 0)
    {
        m_editorScrollRow = 0;
        return;
    }

    const float thumbH = std::max(
        0.042f,
        EDITOR_SCROLL_TRACK_H * static_cast<float>(EDITOR_VISIBLE_ROWS) / static_cast<float>(total));
    const float usable = std::max(0.001f, EDITOR_SCROLL_TRACK_H - thumbH);
    const float ratio  = std::clamp(
        (normY - EDITOR_SCROLL_TRACK_Y - thumbH * 0.5f) / usable, 0.0f, 1.0f);
    m_editorScrollRow = static_cast<int>(std::round(ratio * static_cast<float>(maxScroll)));
}

// ── OpenEditorForChart ────────────────────────────────────────────────────────

void SceneMenu::OpenEditorForChart(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_customCharts.size())) return;

    const auto& entry = m_customCharts[idx];
    const int diffCount = static_cast<int>(entry.difficulties.size());
    const int diffIdx   = diffCount > 0
        ? std::clamp(m_selectedEditorDiffIdx, 0, diffCount - 1)
        : 0;
    const std::string diffFile = (diffCount > 0 && !entry.difficulties[diffIdx].chartFile.empty())
        ? entry.difficulties[diffIdx].chartFile
        : "normal.json";

    if (!entry.id.empty())
    {
        sakura::core::Config::GetInstance().Set("library.last_chart", entry.id);
        sakura::core::Config::GetInstance().Set("library.last_difficulty", diffIdx);
    }

    LOG_INFO("[SceneMenu] 打开谱面编辑器: {} ({})", entry.folderPath, diffFile);
    m_showEditorMenu       = false;
    m_draggingEditorScroll = false;
    m_manager.SwitchScene(
        std::make_unique<SceneEditor>(m_manager,
                                      entry.folderPath,
                                      diffFile,
                                      diffIdx),
        TransitionType::SlideLeft, 0.4f);
}

// ── SetupConfirmButtons ───────────────────────────────────────────────────────

void SceneMenu::SetupConfirmButtons()
{
    // 确认框内两个按钮，居中布局
    // 面板: x=0.32 y=0.35 w=0.36 h=0.26
    m_btnConfirmYes = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.348f, 0.505f, 0.13f, 0.055f },
        "确认退出", m_fontButton, 0.026f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnConfirmYes.get(), sakura::ui::ButtonVariant::Danger);
    m_btnConfirmYes->SetOnClick([]()
    {
        LOG_INFO("[SceneMenu] 确认退出");
        SDL_Event quitEvent;
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    });

    m_btnConfirmNo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.502f, 0.505f, 0.13f, 0.055f },
        "取 消", m_fontButton, 0.026f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnConfirmNo.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnConfirmNo->SetOnClick([this]()
    {
        LOG_INFO("[SceneMenu] 取消退出");
        m_showExitConfirm = false;
    });
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneMenu::RefreshSavedAccounts()
{
    auto accounts = sakura::net::APIClient::GetInstance().GetSavedAccounts();
    if (accounts.size() > static_cast<std::size_t>(MAX_SAVED_ACCOUNT_SLOTS))
        accounts.resize(static_cast<std::size_t>(MAX_SAVED_ACCOUNT_SLOTS));
    m_savedAccounts = std::move(accounts);
    if (m_hoveredSavedSlot >= static_cast<int>(m_savedAccounts.size()))
        m_hoveredSavedSlot = -1;
    if (m_hoveredSavedRemove >= static_cast<int>(m_savedAccounts.size()))
        m_hoveredSavedRemove = -1;
}

void SceneMenu::RefreshAccountCapsuleText(bool forceSample)
{
    if (!m_btnAccountCapsule) return;
    auto& api = sakura::net::APIClient::GetInstance();
    auto& db  = sakura::data::Database::GetInstance();

    static uint64_t s_lastDbSampleMs = 0;
    static double   s_cachedLocalRating = 0.0;
    static std::string s_cachedCourseBadge;
    const uint64_t nowMs = SDL_GetTicks();
    if (forceSample || m_enterTimer < 0.05f || s_lastDbSampleMs == 0 || (nowMs - s_lastDbSampleMs) >= 500)
    {
        s_lastDbSampleMs = nowMs == 0 ? 1 : nowMs;
        s_cachedLocalRating = sakura::game::PPCalculator::CalculateSakuraRating(
            db.GetAllBestScores(), db.GetRecentScores(20)).sakuraRating;
        const auto highest = db.GetHighestClearedCourse();
        s_cachedCourseBadge = (highest && highest->cleared) ? highest->badgeText : std::string{};
    }

    const auto authState = api.GetAuthState();
    const auto user      = api.GetCurrentUser();
    m_lastAuthState      = authState;
    m_lastAuthUserId     = user.id;
    m_lastAuthUsername   = user.username;
    m_lastAuthIsGuest    = user.isGuest;
    m_lastAuthRating     = user.sakuraRating;
    m_lastAuthBadge      = user.danBadge;

    const std::string badge = !s_cachedCourseBadge.empty() ? s_cachedCourseBadge : user.danBadge;
    const std::string badgePrefix = badge.empty() ? "" : ("[" + badge + "] ");
    const double effectiveRating  = std::max(s_cachedLocalRating, user.sakuraRating);
    const std::string ratingStr   = "★" + sakura::game::PPCalculator::FormatRating(effectiveRating);
    const std::string localRating = "★" + sakura::game::PPCalculator::FormatRating(s_cachedLocalRating);

    auto colors = sakura::ui::VisualStyle::ButtonColorsFor(sakura::ui::ButtonVariant::Secondary);
    switch (authState)
    {
    case sakura::net::AuthState::AuthenticatedOnline:
    {
        const std::string uname = TruncateUtf8Codepoints(
            user.username.empty() ? std::string("Player") : user.username, 12);
        m_btnAccountCapsule->SetText(badgePrefix + uname + " · " + ratingStr);
        colors.border = { 108, 236, 176, 195 };
        colors.text   = { 244, 252, 248, 255 };
        break;
    }
    case sakura::net::AuthState::AuthenticatedOffline:
    {
        const std::string uname = TruncateUtf8Codepoints(
            user.username.empty() ? std::string("Player") : user.username, 10);
        m_btnAccountCapsule->SetText(badgePrefix + uname + " · " + ratingStr + " · 离线保活");
        colors.border = { 250, 204, 112, 198 };
        colors.text   = { 252, 238, 204, 255 };
        break;
    }
    case sakura::net::AuthState::SessionExpired:
    {
        std::string expName = user.username;
        if (expName.empty() && !m_savedAccounts.empty())
            expName = m_savedAccounts.front().username;
        const std::string uname = TruncateUtf8Codepoints(
            expName.empty() ? std::string("在线会话") : expName, 12);
        m_btnAccountCapsule->SetText(badgePrefix + uname + " · 需重登");
        colors.border = { 255, 118, 136, 212 };
        colors.text   = { 255, 224, 230, 255 };
        break;
    }
    case sakura::net::AuthState::Unauthenticated:
    default:
    {
        const auto pending = api.GetPendingScoreCount();
        const std::string label = pending > 0
            ? (badgePrefix + "登录 / 在线账号 · 待传 " + std::to_string(pending))
            : (badgePrefix + localRating + " · 登录 / 在线账号");
        m_btnAccountCapsule->SetText(label);
        break;
    }
    }
    m_btnAccountCapsule->SetColors(colors);
}

void SceneMenu::UpdateAccountModalLayout()
{
    auto& api = sakura::net::APIClient::GetInstance();
    const auto authState = api.GetAuthState();
    const auto user      = api.GetCurrentUser();
    const bool hasActiveIdentity  = (authState == sakura::net::AuthState::AuthenticatedOnline ||
                                     authState == sakura::net::AuthState::AuthenticatedOffline);
    const bool isGuestActive      = hasActiveIdentity && user.isGuest;
    const bool isRegisteredActive = hasActiveIdentity && !user.isGuest;

    if (!isRegisteredActive)
        m_changePasswordMode = false;

    if (m_inputServerUrl)
    {
        m_inputServerUrl->SetVisible(true);
        m_inputServerUrl->SetEnabled(!m_authBusy && !m_changePasswordMode);
        if (m_changePasswordMode)
            m_inputServerUrl->SetFocused(false);
    }
    if (m_inputUsername)
    {
        m_inputUsername->SetVisible(true);
        m_inputUsername->SetEnabled(!m_authBusy && !m_changePasswordMode);
        if (m_changePasswordMode)
            m_inputUsername->SetFocused(false);
        m_inputUsername->SetPlaceholder(
            isGuestActive ? "期望正式用户名 (3~24 字符，战绩保留)" : "昵称 / 用户名 (3~24 字符)");
    }
    if (m_inputPassword)
    {
        m_inputPassword->SetVisible(true);
        m_inputPassword->SetEnabled(!m_authBusy);
        if (m_changePasswordMode)
            m_inputPassword->SetPlaceholder("输入当前旧密码");
        else if (isGuestActive)
            m_inputPassword->SetPlaceholder("设置正式登录密码 (至少 6 位)");
        else if (authState == sakura::net::AuthState::SessionExpired)
            m_inputPassword->SetPlaceholder("输入密码重新验证登录");
        else
            m_inputPassword->SetPlaceholder("密码 (至少 6 位，访客免填)");
    }
    if (m_inputNewPassword)
    {
        m_inputNewPassword->SetVisible(m_changePasswordMode);
        m_inputNewPassword->SetEnabled(!m_authBusy && m_changePasswordMode);
        if (!m_changePasswordMode)
            m_inputNewPassword->SetFocused(false);
    }

    if (m_changePasswordMode)
    {
        if (m_btnAuthGuest)         m_btnAuthGuest->SetVisible(false);
        if (m_btnAuthUpgradeGuest)  m_btnAuthUpgradeGuest->SetVisible(false);
        if (m_btnAuthLogin)         m_btnAuthLogin->SetVisible(false);
        if (m_btnAuthRegister)      m_btnAuthRegister->SetVisible(false);

        if (m_btnAuthSubmitPassword)
        {
            m_btnAuthSubmitPassword->SetBounds({ 0.248f, 0.574f, 0.131f, 0.044f });
            m_btnAuthSubmitPassword->SetVisible(true);
            m_btnAuthSubmitPassword->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthToggleChangePwd)
        {
            m_btnAuthToggleChangePwd->SetBounds({ 0.387f, 0.574f, 0.131f, 0.044f });
            m_btnAuthToggleChangePwd->SetText("取消改密");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthToggleChangePwd.get(), sakura::ui::ButtonVariant::Secondary);
            m_btnAuthToggleChangePwd->SetVisible(true);
            m_btnAuthToggleChangePwd->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthLogout)
        {
            m_btnAuthLogout->SetBounds({ 0.248f, 0.628f, 0.131f, 0.042f });
            m_btnAuthLogout->SetVisible(true);
            m_btnAuthLogout->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthClose)
        {
            m_btnAuthClose->SetBounds({ 0.387f, 0.628f, 0.131f, 0.042f });
            m_btnAuthClose->SetVisible(true);
            m_btnAuthClose->SetEnabled(true);
        }
        return;
    }

    if (m_btnAuthSubmitPassword)
        m_btnAuthSubmitPassword->SetVisible(false);

    if (isGuestActive)
    {
        if (m_btnAuthGuest)            m_btnAuthGuest->SetVisible(false);
        if (m_btnAuthToggleChangePwd)  m_btnAuthToggleChangePwd->SetVisible(false);
        if (m_btnAuthUpgradeGuest)
        {
            m_btnAuthUpgradeGuest->SetBounds({ 0.248f, 0.504f, 0.270f, 0.046f });
            m_btnAuthUpgradeGuest->SetVisible(true);
            m_btnAuthUpgradeGuest->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthLogin)
        {
            m_btnAuthLogin->SetBounds({ 0.248f, 0.560f, 0.131f, 0.044f });
            m_btnAuthLogin->SetText("登录已有号");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthLogin.get(), sakura::ui::ButtonVariant::Secondary);
            m_btnAuthLogin->SetVisible(true);
            m_btnAuthLogin->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthRegister)
        {
            m_btnAuthRegister->SetBounds({ 0.387f, 0.560f, 0.131f, 0.044f });
            m_btnAuthRegister->SetText("新建独立号");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthRegister.get(), sakura::ui::ButtonVariant::Secondary);
            m_btnAuthRegister->SetVisible(true);
            m_btnAuthRegister->SetEnabled(!m_authBusy);
        }
    }
    else if (isRegisteredActive)
    {
        if (m_btnAuthGuest)        m_btnAuthGuest->SetVisible(false);
        if (m_btnAuthUpgradeGuest) m_btnAuthUpgradeGuest->SetVisible(false);
        if (m_btnAuthToggleChangePwd)
        {
            m_btnAuthToggleChangePwd->SetBounds({ 0.248f, 0.504f, 0.270f, 0.046f });
            m_btnAuthToggleChangePwd->SetText("✦ 修改登录密码");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthToggleChangePwd.get(), sakura::ui::ButtonVariant::Accent);
            m_btnAuthToggleChangePwd->SetVisible(true);
            m_btnAuthToggleChangePwd->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthLogin)
        {
            m_btnAuthLogin->SetBounds({ 0.248f, 0.560f, 0.131f, 0.044f });
            m_btnAuthLogin->SetText("重新登录");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthLogin.get(), sakura::ui::ButtonVariant::Primary);
            m_btnAuthLogin->SetVisible(true);
            m_btnAuthLogin->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthRegister)
        {
            m_btnAuthRegister->SetBounds({ 0.387f, 0.560f, 0.131f, 0.044f });
            m_btnAuthRegister->SetText("注册新号");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthRegister.get(), sakura::ui::ButtonVariant::Secondary);
            m_btnAuthRegister->SetVisible(true);
            m_btnAuthRegister->SetEnabled(!m_authBusy);
        }
    }
    else
    {
        if (m_btnAuthUpgradeGuest)    m_btnAuthUpgradeGuest->SetVisible(false);
        if (m_btnAuthToggleChangePwd) m_btnAuthToggleChangePwd->SetVisible(false);
        if (m_btnAuthGuest)
        {
            m_btnAuthGuest->SetBounds({ 0.248f, 0.504f, 0.270f, 0.046f });
            m_btnAuthGuest->SetVisible(true);
            m_btnAuthGuest->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthLogin)
        {
            m_btnAuthLogin->SetBounds({ 0.248f, 0.560f, 0.131f, 0.044f });
            m_btnAuthLogin->SetText(authState == sakura::net::AuthState::SessionExpired ? "验证重登" : "登录账号");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthLogin.get(), sakura::ui::ButtonVariant::Primary);
            m_btnAuthLogin->SetVisible(true);
            m_btnAuthLogin->SetEnabled(!m_authBusy);
        }
        if (m_btnAuthRegister)
        {
            m_btnAuthRegister->SetBounds({ 0.387f, 0.560f, 0.131f, 0.044f });
            m_btnAuthRegister->SetText("注册账号");
            sakura::ui::VisualStyle::ApplyButton(m_btnAuthRegister.get(), sakura::ui::ButtonVariant::Secondary);
            m_btnAuthRegister->SetVisible(true);
            m_btnAuthRegister->SetEnabled(!m_authBusy);
        }
    }

    if (m_btnAuthLogout)
    {
        m_btnAuthLogout->SetBounds({ 0.248f, 0.616f, 0.131f, 0.042f });
        m_btnAuthLogout->SetVisible(true);
        m_btnAuthLogout->SetEnabled(!m_authBusy && authState != sakura::net::AuthState::Unauthenticated);
    }
    if (m_btnAuthClose)
    {
        m_btnAuthClose->SetBounds({ 0.387f, 0.616f, 0.131f, 0.042f });
        m_btnAuthClose->SetVisible(true);
        m_btnAuthClose->SetEnabled(true);
    }
}

void SceneMenu::OpenAccountModal()
{
    auto& api            = sakura::net::APIClient::GetInstance();
    m_showAccountModal   = true;
    m_authBusy           = false;
    m_changePasswordMode = false;
    m_hoveredSavedSlot   = -1;
    m_hoveredSavedRemove = -1;
    RefreshSavedAccounts();

    if (m_inputServerUrl)   m_inputServerUrl->SetText(api.GetServerUrl());
    if (m_inputPassword)    m_inputPassword->SetText("");
    if (m_inputNewPassword) m_inputNewPassword->SetText("");

    const auto authState = api.GetAuthState();
    const auto user      = api.GetCurrentUser();
    if (authState == sakura::net::AuthState::SessionExpired)
    {
        std::string prefillName = user.username;
        if (prefillName.empty() && !m_savedAccounts.empty())
            prefillName = m_savedAccounts.front().username;
        if (m_inputUsername)
            m_inputUsername->SetText(prefillName);
        m_authMessage = "登录凭据已过期，请重新输入密码验证";
        UpdateAccountModalLayout();
        if (m_inputPassword)
            m_inputPassword->SetFocused(true);
    }
    else if (authState == sakura::net::AuthState::AuthenticatedOnline ||
             authState == sakura::net::AuthState::AuthenticatedOffline)
    {
        if (m_inputUsername && !user.username.empty())
            m_inputUsername->SetText(user.username);
        m_authMessage = (authState == sakura::net::AuthState::AuthenticatedOffline)
            ? "当前处于离线保活模式，本地身份与成绩已受保护"
            : "";
        UpdateAccountModalLayout();
    }
    else
    {
        m_authMessage.clear();
        UpdateAccountModalLayout();
    }
}

void SceneMenu::CloseAccountModal()
{
    if (m_inputServerUrl)   m_inputServerUrl->SetFocused(false);
    if (m_inputUsername)    m_inputUsername->SetFocused(false);
    if (m_inputPassword)    m_inputPassword->SetFocused(false);
    if (m_inputNewPassword) m_inputNewPassword->SetFocused(false);
    m_changePasswordMode = false;
    m_hoveredSavedSlot   = -1;
    m_hoveredSavedRemove = -1;
    m_showAccountModal   = false;
}

void SceneMenu::SetupAccountModal()
{
    auto& api = sakura::net::APIClient::GetInstance();
    m_btnAccountCapsule = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.658f, 0.036f, 0.290f, 0.044f },
        "登录 / 在线账号", m_fontButton, 0.0175f, 0.014f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAccountCapsule.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnAccountCapsule->SetTextAlign(sakura::core::TextAlign::Left);
    m_btnAccountCapsule->SetTextPadding(0.026f);
    m_btnAccountCapsule->SetVisible(false);
    m_btnAccountCapsule->SetOnClick([this]() { if (m_topBar) m_topBar->OpenAccountDrawer(); else OpenAccountModal(); });

    m_btnMarket = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.695f, 0.088f, 0.253f, 0.042f },
        "✦ 在线谱面市场  /  MARKET", m_fontButton, 0.019f, 0.012f);
    sakura::ui::VisualStyle::ApplyButton(m_btnMarket.get(), sakura::ui::ButtonVariant::Accent);
    m_btnMarket->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnMarket->SetOnClick([this]()
    {
        m_manager.SwitchScene(std::make_unique<SceneMarket>(m_manager), TransitionType::SakuraSweep, 0.42f);
    });

    m_btnDojo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.695f, 0.138f, 0.253f, 0.042f },
        "✦ 樱华道场  /  DAN DOJO", m_fontButton, 0.019f, 0.012f);
    sakura::ui::VisualStyle::ApplyButton(m_btnDojo.get(), sakura::ui::ButtonVariant::Primary);
    m_btnDojo->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnDojo->SetOnClick([this]()
    {
        m_manager.SwitchScene(std::make_unique<SceneCourse>(m_manager), TransitionType::SakuraSweep, 0.42f);
    });

    m_btnArena = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.695f, 0.188f, 0.253f, 0.042f },
        "✦ 排位竞技  /  SEASON & ARENA", m_fontButton, 0.0180f, 0.012f);
    sakura::ui::VisualStyle::ApplyButton(m_btnArena.get(), sakura::ui::ButtonVariant::Accent);
    m_btnArena->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnArena->SetOnClick([this]()
    {
        m_manager.SwitchScene(std::make_unique<SceneArena>(m_manager), TransitionType::SakuraSweep, 0.42f);
    });

    m_inputServerUrl = std::make_unique<sakura::ui::TextInput>(
        sakura::core::NormRect{ 0.248f, 0.304f, 0.270f, 0.042f }, m_fontButton, 0.0185f, 128);
    m_inputServerUrl->SetText(api.GetServerUrl());
    m_inputServerUrl->SetPlaceholder("服务器地址，默认 https://steven-lee.tech:233");

    m_inputUsername = std::make_unique<sakura::ui::TextInput>(
        sakura::core::NormRect{ 0.248f, 0.376f, 0.270f, 0.042f }, m_fontButton, 0.0185f, 32);
    m_inputUsername->SetPlaceholder("昵称 / 用户名 (3~24 字符)");

    m_inputPassword = std::make_unique<sakura::ui::TextInput>(
        sakura::core::NormRect{ 0.248f, 0.448f, 0.270f, 0.042f }, m_fontButton, 0.0185f, 64);
    m_inputPassword->SetPlaceholder("密码 (至少 6 位，访客免填)");
    m_inputPassword->SetPasswordMode(true);

    m_inputNewPassword = std::make_unique<sakura::ui::TextInput>(
        sakura::core::NormRect{ 0.248f, 0.520f, 0.270f, 0.042f }, m_fontButton, 0.0185f, 64);
    m_inputNewPassword->SetPlaceholder("输入新密码 (至少 6 位)");
    m_inputNewPassword->SetPasswordMode(true);
    m_inputNewPassword->SetVisible(false);

    auto handleAuthResult = [this](const sakura::net::AuthResult& res)
    {
        m_authBusy = false;
        RefreshSavedAccounts();
        RefreshAccountCapsuleText(true);
        UpdateAccountModalLayout();
        if (res.ok)
        {
            if (m_inputPassword)    m_inputPassword->SetText("");
            if (m_inputNewPassword) m_inputNewPassword->SetText("");
            m_authMessage = "已登录：" + res.user.username;
            sakura::ui::ToastManager::Instance().Show(
                "欢迎回来，" + res.user.username, sakura::ui::ToastType::Success, 3.0f);
            CloseAccountModal();
        }
        else
        {
            m_authMessage = res.error.empty() ? "认证请求未成功" : res.error;
        }
    };

    m_btnAuthGuest = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.248f, 0.504f, 0.270f, 0.046f },
        "一键访客登录", m_fontButton, 0.020f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthGuest.get(), sakura::ui::ButtonVariant::Accent);
    m_btnAuthGuest->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthGuest->SetOnClick([this, handleAuthResult]()
    {
        if (m_authBusy) return;
        auto& client = sakura::net::APIClient::GetInstance();
        client.SetServerUrl(m_inputServerUrl->GetText());
        m_authBusy    = true;
        m_authMessage = "正在创建访客通行证...";
        UpdateAccountModalLayout();
        auto alive = m_alive;
        client.LoginGuestAsync(m_inputUsername->GetText(), [alive, handleAuthResult](sakura::net::AuthResult r)
        { if (*alive) handleAuthResult(r); });
    });

    m_btnAuthUpgradeGuest = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.248f, 0.504f, 0.270f, 0.046f },
        "✦ 升级为正式账号（保留战绩）", m_fontButton, 0.0185f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthUpgradeGuest.get(), sakura::ui::ButtonVariant::Primary);
    m_btnAuthUpgradeGuest->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthUpgradeGuest->SetOnClick([this]()
    {
        if (m_authBusy) return;
        const std::string desiredUser = m_inputUsername ? m_inputUsername->GetText() : "";
        const std::string desiredPwd  = m_inputPassword ? m_inputPassword->GetText() : "";
        if (desiredUser.size() < 3)
        {
            m_authMessage = "请输入 3~24 字符的期望正式用户名";
            return;
        }
        if (desiredPwd.size() < 6)
        {
            m_authMessage = "升级正式账号需设置至少 6 位登录密码";
            return;
        }
        auto& client = sakura::net::APIClient::GetInstance();
        client.SetServerUrl(m_inputServerUrl->GetText());
        m_authBusy    = true;
        m_authMessage = "正在原地升级为正式账号（保留全部战绩与 Rating）...";
        UpdateAccountModalLayout();
        auto alive = m_alive;
        client.UpgradeGuestAsync(desiredUser, desiredPwd, [this, alive](sakura::net::AuthResult r)
        {
            if (!*alive) return;
            m_authBusy = false;
            RefreshSavedAccounts();
            RefreshAccountCapsuleText(true);
            UpdateAccountModalLayout();
            if (r.ok)
            {
                if (m_inputUsername) m_inputUsername->SetText(r.user.username);
                if (m_inputPassword) m_inputPassword->SetText("");
                m_authMessage = "已成功升级为正式账号：" + r.user.username;
                sakura::ui::ToastManager::Instance().Show(
                    "账号转正成功！全部战绩与 Rating 已无损保留", sakura::ui::ToastType::Success, 3.6f);
            }
            else
            {
                m_authMessage = r.error.empty() ? "游客升级正式账号失败" : r.error;
            }
        });
    });

    m_btnAuthToggleChangePwd = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.248f, 0.504f, 0.270f, 0.046f },
        "✦ 修改登录密码", m_fontButton, 0.0195f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthToggleChangePwd.get(), sakura::ui::ButtonVariant::Accent);
    m_btnAuthToggleChangePwd->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthToggleChangePwd->SetOnClick([this]()
    {
        if (m_authBusy) return;
        m_changePasswordMode = !m_changePasswordMode;
        if (m_inputPassword)    m_inputPassword->SetText("");
        if (m_inputNewPassword) m_inputNewPassword->SetText("");
        m_authMessage = m_changePasswordMode
            ? "请输入当前旧密码与至少 6 位新密码"
            : "";
        UpdateAccountModalLayout();
        if (m_changePasswordMode && m_inputPassword)
            m_inputPassword->SetFocused(true);
    });

    m_btnAuthSubmitPassword = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.248f, 0.574f, 0.131f, 0.044f },
        "确认改密", m_fontButton, 0.019f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthSubmitPassword.get(), sakura::ui::ButtonVariant::Primary);
    m_btnAuthSubmitPassword->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthSubmitPassword->SetOnClick([this]()
    {
        if (m_authBusy) return;
        const std::string oldPwd = m_inputPassword    ? m_inputPassword->GetText()    : "";
        const std::string newPwd = m_inputNewPassword ? m_inputNewPassword->GetText() : "";
        if (oldPwd.empty())
        {
            m_authMessage = "请输入当前旧密码";
            return;
        }
        if (newPwd.size() < 6)
        {
            m_authMessage = "新密码长度至少需要 6 位";
            return;
        }
        m_authBusy    = true;
        m_authMessage = "正在校验旧密码并轮换会话令牌...";
        UpdateAccountModalLayout();
        auto alive = m_alive;
        sakura::net::APIClient::GetInstance().ChangePasswordAsync(
            oldPwd, newPwd, [this, alive](sakura::net::AuthResult r)
            {
                if (!*alive) return;
                m_authBusy = false;
                if (r.ok)
                {
                    m_changePasswordMode = false;
                    if (m_inputPassword)    m_inputPassword->SetText("");
                    if (m_inputNewPassword) m_inputNewPassword->SetText("");
                    RefreshSavedAccounts();
                    RefreshAccountCapsuleText(true);
                    UpdateAccountModalLayout();
                    m_authMessage = "密码已更新，当前设备令牌已安全轮换";
                    sakura::ui::ToastManager::Instance().Show(
                        "密码修改成功，其余设备旧会话已吊销", sakura::ui::ToastType::Success, 3.4f);
                }
                else
                {
                    UpdateAccountModalLayout();
                    m_authMessage = r.error.empty() ? "修改密码失败" : r.error;
                }
            });
    });

    m_btnAuthLogin = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.248f, 0.560f, 0.131f, 0.044f },
        "登录账号", m_fontButton, 0.0195f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthLogin.get(), sakura::ui::ButtonVariant::Primary);
    m_btnAuthLogin->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthLogin->SetOnClick([this, handleAuthResult]()
    {
        if (m_authBusy) return;
        auto& client = sakura::net::APIClient::GetInstance();
        client.SetServerUrl(m_inputServerUrl->GetText());
        m_authBusy    = true;
        m_authMessage = "正在登录验证...";
        UpdateAccountModalLayout();
        auto alive = m_alive;
        client.LoginAsync(m_inputUsername->GetText(), m_inputPassword->GetText(),
            [alive, handleAuthResult](sakura::net::AuthResult r)
            { if (*alive) handleAuthResult(r); });
    });

    m_btnAuthRegister = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.387f, 0.560f, 0.131f, 0.044f },
        "注册账号", m_fontButton, 0.0195f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthRegister.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnAuthRegister->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthRegister->SetOnClick([this, handleAuthResult]()
    {
        if (m_authBusy) return;
        auto& client = sakura::net::APIClient::GetInstance();
        client.SetServerUrl(m_inputServerUrl->GetText());
        m_authBusy    = true;
        m_authMessage = "正在注册新账号...";
        UpdateAccountModalLayout();
        auto alive = m_alive;
        client.RegisterAsync(m_inputUsername->GetText(), m_inputPassword->GetText(),
            [alive, handleAuthResult](sakura::net::AuthResult r)
            { if (*alive) handleAuthResult(r); });
    });

    m_btnAuthLogout = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.248f, 0.616f, 0.131f, 0.042f },
        "退出登录", m_fontButton, 0.019f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthLogout.get(), sakura::ui::ButtonVariant::Danger);
    m_btnAuthLogout->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthLogout->SetOnClick([this]()
    {
        if (m_authBusy) return;
        m_authBusy           = true;
        m_changePasswordMode = false;
        m_authMessage        = "正在注销当前设备会话...";
        UpdateAccountModalLayout();
        auto alive = m_alive;
        sakura::net::APIClient::GetInstance().LogoutAsync([this, alive]()
        {
            if (!*alive) return;
            m_authBusy = false;
            if (m_inputPassword)    m_inputPassword->SetText("");
            if (m_inputNewPassword) m_inputNewPassword->SetText("");
            RefreshSavedAccounts();
            RefreshAccountCapsuleText(true);
            UpdateAccountModalLayout();
            m_authMessage = "已退出在线账号";
        });
    });

    m_btnAuthClose = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.387f, 0.616f, 0.131f, 0.042f },
        "关 闭", m_fontButton, 0.019f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAuthClose.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnAuthClose->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnAuthClose->SetOnClick([this]() { CloseAccountModal(); });
}

void SceneMenu::OnExit()
{
    *m_alive = false;
    LOG_INFO("[SceneMenu] 退出主菜单");
    m_particles.StopEmitter(m_sakuraPetalEmitterFG);
    m_particles.StopEmitter(m_sakuraPetalEmitterMG);
    m_particles.StopEmitter(m_sakuraPetalEmitterBG);
    m_particles.StopEmitter(m_bgFloatEmitter);
    m_sakuraPetalEmitterFG = -1;
    m_sakuraPetalEmitterMG = -1;
    m_sakuraPetalEmitterBG = -1;
    m_bgFloatEmitter     = -1;
    m_particles.Clear();
    m_savedAccounts.clear();
    for (auto& btn : m_buttons) btn.reset();
    m_btnAccountCapsule.reset();
    m_btnMarket.reset();
    m_btnDojo.reset();
    m_btnArena.reset();
    m_inputServerUrl.reset();
    m_inputUsername.reset();
    m_inputPassword.reset();
    m_inputNewPassword.reset();
    m_btnAuthGuest.reset();
    m_btnAuthUpgradeGuest.reset();
    m_btnAuthToggleChangePwd.reset();
    m_btnAuthSubmitPassword.reset();
    m_btnAuthLogin.reset();
    m_btnAuthRegister.reset();
    m_btnAuthLogout.reset();
    m_btnAuthClose.reset();
    m_btnConfirmYes.reset();
    m_btnConfirmNo.reset();
    m_btnEditorOpen.reset();
    m_btnEditorNew.reset();
    m_btnEditorCancel.reset();
    m_topBar.reset();
}

// ── UpdateEnterAnimation ──────────────────────────────────────────────────────

void SceneMenu::UpdateEnterAnimation(float dt)
{
    if (m_anim.done) return;

    m_enterTimer += dt;

    // 标题滑入（0.3s EaseOutBack）
    m_anim.titleTimer += dt;
    float t = std::min(1.0f, m_anim.titleTimer / EnterAnim::TITLE_DURATION);
    m_anim.titleOffsetX = -0.15f * (1.0f - sakura::utils::EaseOutBack(t));

    // 按钮依次从左往右滑入（间隔 0.08s，EaseOutCubic）
    bool allDone = (t >= 1.0f);
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        // 第 i 个按钮在 titleDuration + i*stagger 后开始
        float startTime = EnterAnim::TITLE_DURATION + i * EnterAnim::BTN_STAGGER;
        float elapsed   = m_enterTimer - startTime;

        if (elapsed <= 0.0f)
        {
            m_anim.btnOffsetX[i] = -0.25f;
            allDone = false;
        }
        else
        {
            float bt = std::min(1.0f, elapsed / EnterAnim::BTN_DURATION);
            m_anim.btnOffsetX[i] = -0.25f * (1.0f - sakura::utils::EaseOutCubic(bt));
            if (bt < 1.0f) allDone = false;
        }
    }

    if (allDone) m_anim.done = true;
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneMenu::OnUpdate(float dt)
{
    if (m_topBar)
        m_topBar->Update(dt);
    UpdateEnterAnimation(sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false) ? 10.0f : dt);

    // ── 粒子与特效更新 ───────────────────────────────────────────────────────
    m_particles.Update(dt);
    m_glowPhase += dt;  // 标题脉冲相位

    for (auto it = m_clickRings.begin(); it != m_clickRings.end(); )
    {
        it->timer += dt;
        if (it->timer > 0.4f) { // 存活0.4秒
            it = m_clickRings.erase(it);
        } else {
            ++it;
        }
    }

    // 更新按钮（按带 Update 处理悬停动画）
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (m_buttons[i])
        {
            // 临时移动按钮 bounds 到动画偏移位置（X轴偏移）
            auto origBounds = m_buttons[i]->GetBounds();
            sakura::core::NormRect animBounds = origBounds;
            animBounds.x = origBounds.x + m_anim.btnOffsetX[i] + (i % 2 == 1 ? 0.006f : 0.0f);

            m_buttons[i]->SetBounds(animBounds);

            m_buttons[i]->Update(dt);

            // 恢复原始 bounds
            m_buttons[i]->SetBounds(origBounds);
        }
    }

    {
        auto& api = sakura::net::APIClient::GetInstance();
        const auto curState = api.GetAuthState();
        const auto curUser  = api.GetCurrentUser();
        const bool authChanged =
            (curState != m_lastAuthState) ||
            (curUser.id != m_lastAuthUserId) ||
            (curUser.username != m_lastAuthUsername) ||
            (curUser.isGuest != m_lastAuthIsGuest) ||
            (std::abs(curUser.sakuraRating - m_lastAuthRating) > 0.0005) ||
            (curUser.danBadge != m_lastAuthBadge);

        RefreshAccountCapsuleText(authChanged);
        if (authChanged)
        {
            RefreshSavedAccounts();
            if (curState == sakura::net::AuthState::SessionExpired)
            {
                std::string prefillName = curUser.username;
                if (prefillName.empty() && !m_savedAccounts.empty())
                    prefillName = m_savedAccounts.front().username;
                if (m_inputUsername && !m_inputUsername->IsFocused() && !prefillName.empty())
                    m_inputUsername->SetText(prefillName);
                if (m_showAccountModal && !m_authBusy)
                    m_authMessage = "登录凭据已过期，请重新输入密码验证";
            }
            UpdateAccountModalLayout();
        }
    }

    if (!m_showAccountModal && !m_showEditorMenu && !m_showExitConfirm)
    {
        if (m_btnAccountCapsule) m_btnAccountCapsule->Update(dt);
        if (m_btnMarket)         m_btnMarket->Update(dt);
        if (m_btnDojo)           m_btnDojo->Update(dt);
        if (m_btnArena)          m_btnArena->Update(dt);
    }

    if (m_showAccountModal)
    {
        const auto [mx, my] = sakura::core::Input::GetMousePosition();
        const int slotCount = static_cast<int>(m_savedAccounts.size());
        int nextHoverSlot   = -1;
        int nextHoverRemove = -1;
        if (!m_authBusy)
        {
            for (int slot = 0; slot < slotCount; ++slot)
            {
                if (ContainsNormPoint(GetSavedAccountRemoveRect(slot), mx, my))
                {
                    nextHoverRemove = slot;
                    break;
                }
                if (ContainsNormPoint(GetSavedAccountSlotRect(slot), mx, my))
                {
                    nextHoverSlot = slot;
                    break;
                }
            }
        }
        if ((nextHoverSlot != m_hoveredSavedSlot && nextHoverSlot >= 0) ||
            (nextHoverRemove != m_hoveredSavedRemove && nextHoverRemove >= 0))
        {
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
        }
        m_hoveredSavedSlot   = nextHoverSlot;
        m_hoveredSavedRemove = nextHoverRemove;

        if (m_inputServerUrl)         m_inputServerUrl->Update(dt);
        if (m_inputUsername)          m_inputUsername->Update(dt);
        if (m_inputPassword)          m_inputPassword->Update(dt);
        if (m_inputNewPassword)       m_inputNewPassword->Update(dt);
        if (m_btnAuthGuest)           m_btnAuthGuest->Update(dt);
        if (m_btnAuthUpgradeGuest)    m_btnAuthUpgradeGuest->Update(dt);
        if (m_btnAuthToggleChangePwd) m_btnAuthToggleChangePwd->Update(dt);
        if (m_btnAuthSubmitPassword)  m_btnAuthSubmitPassword->Update(dt);
        if (m_btnAuthLogin)           m_btnAuthLogin->Update(dt);
        if (m_btnAuthRegister)        m_btnAuthRegister->Update(dt);
        if (m_btnAuthLogout)          m_btnAuthLogout->Update(dt);
        if (m_btnAuthClose)           m_btnAuthClose->Update(dt);
    }

    // 编辑器子菜单悬停与按钮更新
    if (m_showEditorMenu)
    {
        const auto [mx, my] = sakura::core::Input::GetMousePosition();
        const int total = static_cast<int>(m_customCharts.size());
        const float rowMaxX = (total > EDITOR_VISIBLE_ROWS)
            ? (EDITOR_ROW_X + EDITOR_ROW_W_SCROLL + 0.006f)
            : (EDITOR_ROW_X + EDITOR_ROW_W_FULL + 0.006f);
        int newHover = -1;
        if (!m_draggingEditorScroll && total > 0 &&
            mx >= EDITOR_ROW_X && mx <= rowMaxX &&
            my >= EDITOR_ROW_Y0 && my < EDITOR_ROW_Y0 + EDITOR_VISIBLE_ROWS * EDITOR_ROW_STRIDE)
        {
            const float relY = my - EDITOR_ROW_Y0;
            const int slot   = static_cast<int>(relY / EDITOR_ROW_STRIDE);
            const float inY  = relY - static_cast<float>(slot) * EDITOR_ROW_STRIDE;
            if (slot >= 0 && slot < EDITOR_VISIBLE_ROWS && inY <= EDITOR_ROW_H)
            {
                const int idx = m_editorScrollRow + slot;
                if (idx >= 0 && idx < total)
                    newHover = idx;
            }
        }
        if (newHover != m_hoveredEditorRow)
        {
            m_hoveredEditorRow = newHover;
            if (m_hoveredEditorRow >= 0)
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
        }

        if (m_btnEditorOpen)   m_btnEditorOpen->Update(dt);
        if (m_btnEditorNew)    m_btnEditorNew->Update(dt);
        if (m_btnEditorCancel) m_btnEditorCancel->Update(dt);
    }

    // 确认框按钮更新
    if (m_showExitConfirm)
    {
        if (m_btnConfirmYes) m_btnConfirmYes->Update(dt);
        if (m_btnConfirmNo)  m_btnConfirmNo ->Update(dt);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────
void SceneMenu::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);
    sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, m_glowPhase);
    if (m_fontTitle == sakura::core::INVALID_HANDLE) return;

    const float aspect = static_cast<float>(renderer.GetScreenHeight()) / std::max(1, renderer.GetScreenWidth());
    const auto [mx, my] = sakura::core::Input::GetMousePosition();
    const float parX = (mx - 0.5f) * 0.014f;
    const float parY = (my - 0.5f) * 0.010f;

    // ── 右侧月夜樱纹星盘与律动光环 ────────────────────────────────────────────
    const float crestCX = 0.72f + parX;
    const float crestCY = 0.45f + parY;
    const float breath  = 0.5f + 0.5f * std::sin(m_glowPhase * 1.8f);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        crestCX, crestCY, 0.24f + breath * 0.02f, 0.24f + breath * 0.02f,
        { 246, 172, 200, static_cast<uint8_t>(48 + breath * 22.0f) }, { 246, 172, 200, 0 }, 48);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    sakura::audio::AudioVisualizer::GetInstance().RenderCircle(
        renderer, crestCX, crestCY, 0.145f, { 246, 178, 206, 210 }, 0.72f);

    renderer.DrawCircleOutline(crestCX, crestCY, 0.185f, { 250, 216, 157, 72 }, 0.0011f, 72);
    renderer.DrawArc(crestCX, crestCY, 0.205f, m_glowPhase * 18.0f, m_glowPhase * 18.0f + 135.0f,
                     { 250, 216, 157, 130 }, 0.0018f, 48);
    renderer.DrawArc(crestCX, crestCY, 0.205f, m_glowPhase * 18.0f + 180.0f, m_glowPhase * 18.0f + 315.0f,
                     { 246, 172, 200, 125 }, 0.0018f, 48);

    const auto crestTex = sakura::ui::VisualStyle::GetSakuraCrestTexture();
    if (crestTex != sakura::core::INVALID_HANDLE)
    {
        const float s = 0.21f * (0.98f + 0.03f * breath);
        const float w = s * aspect;
        renderer.DrawSprite(crestTex, { crestCX - w * 0.5f, crestCY - s * 0.5f, w, s },
                            m_glowPhase * 7.5f, sakura::core::Color::White, 0.94f);
    }

    // 右下角磨砂琉璃双模说明铭牌
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer,
        { 0.585f + parX * 0.5f, 0.735f + parY * 0.5f, 0.27f, 0.125f },
        { 22, 28, 52, 188 }, { 14, 18, 36, 208 },
        { 250, 216, 157, 145 }, { 164, 154, 212, 58 },
        0.015f, 0.42f);

    const auto pink = sakura::core::Color{ 246, 178, 206, 255 };
    const auto gold = sakura::core::Color{ 250, 216, 157, 245 };
    const auto dim  = sakura::core::Color{ 176, 184, 210, 235 };
    renderer.DrawText(m_fontSub, "4 KEYS  +  MOUSE  HYBRID", 0.72f + parX * 0.5f, 0.758f + parY * 0.5f, 0.019f, gold, sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.625f + parX * 0.5f, 0.815f + parX * 0.5f, 0.795f + parY * 0.5f, 0.55f);
    renderer.DrawText(m_fontSub, "左手键盘落键 · 右手丝滑轨迹", 0.72f + parX * 0.5f, 0.810f + parY * 0.5f, 0.021f, dim, sakura::core::TextAlign::Center);

    // ── 左侧导航磨砂底座与主标题 ─────────────────────────────────────────────
    const float titleX = TITLE_X + m_anim.titleOffsetX;
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer,
        { 0.052f, 0.088f, 0.345f, 0.835f },
        { 18, 24, 46, 168 }, { 10, 14, 28, 196 },
        { 250, 216, 157, 128 }, { 154, 162, 206, 48 },
        0.020f, 0.48f);

    sakura::ui::VisualStyle::DrawBadgePill(
        renderer,
        { titleX, 0.115f, 0.215f, 0.032f },
        "A DUAL RHYTHM JOURNEY",
        m_fontSub, 0.015f,
        { 46, 34, 66, 195 }, { 250, 216, 157, 145 }, gold);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        titleX + 0.12f, TITLE_Y + 0.055f, 0.15f, 0.09f,
        { 246, 172, 200, static_cast<uint8_t>(36 + breath * 22.0f) }, { 246, 172, 200, 0 }, 36);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    renderer.DrawText(m_fontTitle, "Sakura", titleX, TITLE_Y - 0.008f, 0.098f, { 250, 244, 248, 255 });
    renderer.DrawText(m_fontTitle, "樱", titleX + 0.236f, TITLE_Y + 0.015f, 0.068f, pink);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, titleX, titleX + 0.29f, 0.308f, 0.78f);
    renderer.DrawText(m_fontSub, "月华琉璃，指尖落樱成诗。", titleX, 0.324f, 0.021f, dim);

    // ── 按钮（错落阶梯感 + 鎏金键盘焦点指示）────────────────────────────────
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i] + (i % 2 == 1 ? 0.006f : 0.0f);

        if (animBounds.x + animBounds.width < 0.0f) continue;

        m_buttons[i]->SetBounds(animBounds);
        m_buttons[i]->Render(renderer);
        if (i == m_keyboardFocus)
        {
            renderer.DrawRoundedGradientRect(
                { animBounds.x - 0.009f, animBounds.y + 0.010f, 0.0034f, animBounds.height - 0.020f },
                0.0017f, gold, pink, gold, pink, 6, true);
        }
        m_buttons[i]->SetBounds(origBounds);
    }

    renderer.DrawText(m_fontSub, "↑ ↓ 选择   ·   Enter 确认   ·   Esc 退出", 0.075f, 0.876f, 0.016f, dim);

    // ── 三层景深樱瓣与点击涟漪 ───────────────────────────────────────────────
    m_particles.Render(renderer);
    for (const auto& ring : m_clickRings)
    {
        const float rt = std::clamp(ring.timer / 0.4f, 0.0f, 1.0f);
        const float rad = 0.008f + sakura::utils::EaseOutCubic(rt) * 0.045f;
        const uint8_t ra = static_cast<uint8_t>((1.0f - rt) * 195.0f);
        renderer.DrawRingGradient(ring.x, ring.y, rad * 0.78f, rad, { 250, 216, 157, ra }, { 246, 172, 200, 0 }, 40);
    }

    // ── 版本号与右上在线账号胶囊 ───────────────────────────────────────────────
    renderer.DrawText(m_fontSub, "v" SAKURA_VERSION_STRING "  ·  LUMINOUS GLASS EDITION",
        0.075f, 0.948f, 0.015f,
        sakura::core::Color{ 208, 212, 232, 168 },
        sakura::core::TextAlign::Left);
    if (m_btnAccountCapsule)
    {
        m_btnAccountCapsule->Render(renderer);
        const auto capBounds = m_btnAccountCapsule->GetBounds();
        const float dotX     = capBounds.x + 0.013f;
        const float dotY     = capBounds.y + capBounds.height * 0.5f;
        const auto  st       = sakura::net::APIClient::GetInstance().GetAuthState();
        sakura::core::Color dotCol{ 176, 184, 210, 195 };
        if (st == sakura::net::AuthState::AuthenticatedOnline)
            dotCol = { 88, 236, 164, 255 };
        else if (st == sakura::net::AuthState::AuthenticatedOffline)
            dotCol = { 250, 198, 96, 255 };
        else if (st == sakura::net::AuthState::SessionExpired)
            dotCol = { 255, 102, 122, 255 };
        renderer.DrawCircleFilled(dotX, dotY, 0.0045f, dotCol, 24);
        renderer.DrawCircleOutline(dotX, dotY, 0.0068f, dotCol.WithAlpha(92), 0.0010f, 24);
    }
    if (m_btnMarket)         m_btnMarket->Render(renderer);
    if (m_btnDojo)           m_btnDojo->Render(renderer);
    if (m_btnArena)          m_btnArena->Render(renderer);
    {
        const auto sSpec  = sakura::game::SeasonEngine::ComputeSeasonSpec();
        const auto localS = sakura::game::SeasonEngine::ComputeLocalStanding(
            sSpec, sakura::data::Database::GetInstance().GetAllBestScores());
        int effSP = localS.totalSP;
        auto& cfg = sakura::core::Config::GetInstance();
        if (cfg.Get<std::string>("online.season_id", "") == sSpec.seasonId)
            effSP = std::max(effSP, cfg.Get<int>("online.season_total_sp", 0));
        const auto tierInfo = sakura::game::SeasonEngine::EvaluateTier(effSP);
        const sakura::core::Color tCol{ tierInfo.colorR, tierInfo.colorG, tierInfo.colorB, 255 };
        sakura::ui::VisualStyle::DrawBadgePill(
            renderer,
            { 0.695f, 0.238f, 0.253f, 0.030f },
            "★ 赛季 " + sSpec.seasonId + " · 「" + tierInfo.tierName + "」 " + std::to_string(effSP) + " SP",
            m_fontSub, 0.0128f,
            { 30, 26, 56, 215 }, tCol.WithAlpha(185), tCol);
    }

    // ── 在线账号登录/转正/改密/快捷切换弹层 ───────────────────────────────────
    if (m_showAccountModal)
    {
        auto& api            = sakura::net::APIClient::GetInstance();
        const auto authState = api.GetAuthState();
        const auto curUser   = api.GetCurrentUser();
        const sakura::core::Color mint  { 132, 238, 184, 255 };
        const sakura::core::Color amber { 250, 204, 108, 255 };
        const sakura::core::Color coral { 255, 122, 138, 255 };

        sakura::ui::VisualStyle::DrawScrim(renderer, 0.72f);
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer, ACCOUNT_MODAL_RECT,
            { 26, 32, 58, 242 }, { 14, 18, 36, 248 },
            { 250, 216, 157, 198 }, { 246, 172, 200, 98 },
            0.018f, 0.64f);

        sakura::ui::VisualStyle::DrawBadgePill(
            renderer, { 0.425f, 0.160f, 0.150f, 0.025f },
            "SAKURA ACCOUNT", m_fontSub, 0.0125f,
            { 54, 38, 74, 218 }, { 250, 216, 157, 155 }, gold);
        renderer.DrawText(m_fontSub, "在线通行证与安全账号保险箱", 0.50f, 0.191f, 0.027f,
            sakura::core::Color{ 248, 242, 248, 250 }, sakura::core::TextAlign::Center);

        // 顶部四态认证横幅
        std::string stateBanner;
        sakura::core::Color stateColor = dim;
        sakura::core::Color stateFill  { 26, 32, 56, 208 };
        switch (authState)
        {
        case sakura::net::AuthState::AuthenticatedOnline:
            stateBanner = "● 在线已连接  ·  " + curUser.username +
                (curUser.isGuest ? " (游客通行证 · 可原地升级正式号)" : " (正式账号)");
            stateColor = mint;
            stateFill  = { 22, 52, 48, 214 };
            break;
        case sakura::net::AuthState::AuthenticatedOffline:
            stateBanner = "◈ 离线保活中  ·  " + curUser.username +
                "  ·  本地身份与待同步成绩已加密保留";
            stateColor = amber;
            stateFill  = { 58, 46, 28, 216 };
            break;
        case sakura::net::AuthState::SessionExpired:
            stateBanner = "! 会话需重登  ·  登录凭据已过期，请验证密码恢复在线同步";
            stateColor = coral;
            stateFill  = { 68, 28, 42, 222 };
            break;
        case sakura::net::AuthState::Unauthenticated:
        default:
            stateBanner = "○ 当前未登录  ·  支持访客通行、正式账号登录或右侧免密切换";
            stateColor = dim;
            break;
        }
        sakura::ui::VisualStyle::DrawBadgePill(
            renderer, { 0.248f, 0.230f, 0.508f, 0.028f },
            stateBanner, m_fontSub, 0.0132f,
            stateFill, stateColor.WithAlpha(168), stateColor);

        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.248f, 0.756f, 0.270f, 0.76f);

        // ── 左栏：服务器 / 用户名 / 密码 / 转正或改密表单 ─────────────────────
        renderer.DrawText(m_fontSub, "服务器地址", 0.248f, 0.283f, 0.0138f, dim);
        const bool hasIdentity   = (authState == sakura::net::AuthState::AuthenticatedOnline ||
                                    authState == sakura::net::AuthState::AuthenticatedOffline);
        const bool isGuestActive = hasIdentity && curUser.isGuest;
        renderer.DrawText(
            m_fontSub,
            m_changePasswordMode ? "当前正式账号"
                : (isGuestActive ? "升级正式用户名 (战绩无损保留)" : "账号 / 昵称"),
            0.248f, 0.355f, 0.0138f, isGuestActive ? gold : dim);
        renderer.DrawText(
            m_fontSub,
            m_changePasswordMode ? "当前旧密码"
                : (isGuestActive ? "设置正式密码 (或登录已有号密码)"
                    : (authState == sakura::net::AuthState::SessionExpired ? "验证登录密码" : "登录密码")),
            0.248f, 0.427f, 0.0138f,
            authState == sakura::net::AuthState::SessionExpired ? coral : dim);
        if (m_changePasswordMode)
        {
            renderer.DrawText(m_fontSub, "设置新密码 (至少 6 位)", 0.248f, 0.499f, 0.0138f, gold);
        }

        if (m_inputServerUrl)         m_inputServerUrl->Render(renderer);
        if (m_inputUsername)          m_inputUsername->Render(renderer);
        if (m_inputPassword)          m_inputPassword->Render(renderer);
        if (m_inputNewPassword)       m_inputNewPassword->Render(renderer);
        if (m_btnAuthGuest)           m_btnAuthGuest->Render(renderer);
        if (m_btnAuthUpgradeGuest)    m_btnAuthUpgradeGuest->Render(renderer);
        if (m_btnAuthToggleChangePwd) m_btnAuthToggleChangePwd->Render(renderer);
        if (m_btnAuthSubmitPassword)  m_btnAuthSubmitPassword->Render(renderer);
        if (m_btnAuthLogin)           m_btnAuthLogin->Render(renderer);
        if (m_btnAuthRegister)        m_btnAuthRegister->Render(renderer);
        if (m_btnAuthLogout)          m_btnAuthLogout->Render(renderer);
        if (m_btnAuthClose)           m_btnAuthClose->Render(renderer);

        // 左下当前档案概览卡片
        renderer.DrawRoundedGradientRect(
            ACCOUNT_SUMMARY_RECT, 0.011f,
            { 16, 22, 42, 210 }, { 20, 26, 48, 210 },
            { 12, 16, 32, 224 }, { 14, 18, 36, 224 }, 10, true);
        renderer.DrawRoundedRectOutlineGradient(
            ACCOUNT_SUMMARY_RECT, 0.011f,
            { 250, 216, 157, 92 }, { 140, 150, 196, 42 }, 0.0011f, 10);
        if (hasIdentity)
        {
            const std::string badgeTag = curUser.danBadge.empty() ? "" : ("[" + curUser.danBadge + "] ");
            const std::string profTop  = badgeTag + curUser.username +
                (curUser.isGuest ? " · 游客档案" : " · 正式档案");
            const std::string profBot  = "★" + sakura::game::PPCalculator::FormatRating(curUser.sakuraRating) +
                "  ·  " + std::to_string(static_cast<int>(std::round(curUser.totalPP))) + " PP  ·  待传 " +
                std::to_string(api.GetPendingScoreCount());
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSub, profTop, 0.260f, 0.697f, 0.0155f, 0.246f, gold);
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSub, profBot, 0.260f, 0.728f, 0.0135f, 0.246f, dim);
        }
        else
        {
            const std::string hintTop = (authState == sakura::net::AuthState::SessionExpired)
                ? "账号名已自动预填，输入密码即可恢复"
                : "DPAPI 系统级加密保险箱已启用";
            const std::string hintBot = "待同步本地成绩队列：" +
                std::to_string(api.GetPendingScoreCount()) + " 条";
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSub, hintTop, 0.260f, 0.698f, 0.0148f, 0.246f,
                authState == sakura::net::AuthState::SessionExpired ? coral : gold);
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSub, hintBot, 0.260f, 0.729f, 0.0135f, 0.246f, dim);
        }

        // ── 右栏：最近保存的 5 个账号快捷切换胶囊（含 'x' 移除热区）───────────
        renderer.DrawText(m_fontSub, "已保存账号 · 一键切换", 0.536f, 0.283f, 0.0138f, gold);
        renderer.DrawText(
            m_fontSub,
            std::to_string(m_savedAccounts.size()) + " / " + std::to_string(MAX_SAVED_ACCOUNT_SLOTS),
            0.754f, 0.283f, 0.0135f, dim, sakura::core::TextAlign::Right);

        renderer.DrawRoundedGradientRect(
            ACCOUNT_SAVED_BOX_RECT, 0.012f,
            { 12, 16, 32, 212 }, { 14, 18, 36, 212 },
            {  8, 11, 24, 226 }, { 10, 14, 28, 226 }, 12, true);
        renderer.DrawRoundedRectOutlineGradient(
            ACCOUNT_SAVED_BOX_RECT, 0.012f,
            { 250, 216, 157, 96 }, { 140, 150, 196, 44 }, 0.0012f, 12);

        if (m_savedAccounts.empty())
        {
            renderer.DrawText(m_fontSub, "暂无已保存账号",
                0.645f, 0.492f, 0.0175f, sakura::core::Color{ 236, 232, 244, 230 },
                sakura::core::TextAlign::Center);
            renderer.DrawText(m_fontSub, "登录后自动加密记忆最多 5 个槽位",
                0.645f, 0.528f, 0.0128f, dim, sakura::core::TextAlign::Center);
        }
        else
        {
            const int slotCount = static_cast<int>(m_savedAccounts.size());
            for (int slot = 0; slot < slotCount; ++slot)
            {
                const auto& entry    = m_savedAccounts[slot];
                const auto  slotRect = GetSavedAccountSlotRect(slot);
                const auto  rmRect   = GetSavedAccountRemoveRect(slot);
                const bool  isActive = (authState != sakura::net::AuthState::Unauthenticated &&
                                        !curUser.id.empty() && entry.userId == curUser.id);
                const bool  slotHov  = (slot == m_hoveredSavedSlot);
                const bool  rmHov    = (slot == m_hoveredSavedRemove);

                if (isActive)
                {
                    sakura::ui::VisualStyle::DrawGlassCard(
                        renderer, slotRect,
                        { 78, 48, 86, 238 }, { 42, 30, 66, 242 },
                        { 250, 216, 157, 226 }, { 246, 178, 206, 136 }, 0.010f, 0.40f);
                }
                else
                {
                    sakura::ui::VisualStyle::DrawGlassCard(
                        renderer, slotRect,
                        slotHov ? sakura::core::Color{ 38, 46, 78, 228 } : sakura::core::Color{ 20, 26, 48, 206 },
                        slotHov ? sakura::core::Color{ 24, 30, 54, 234 } : sakura::core::Color{ 14, 18, 36, 218 },
                        slotHov ? pink : sakura::core::Color{ 164, 174, 212, 68 },
                        { 112, 122, 168, 36 }, 0.010f, 0.28f);
                }

                const std::string tagText = isActive ? "当前" : (entry.isGuest ? "游客" : "正式");
                const sakura::core::Color tagCol = isActive ? mint : (entry.isGuest ? amber : gold);
                sakura::ui::VisualStyle::DrawBadgePill(
                    renderer,
                    { slotRect.x + 0.008f, slotRect.y + 0.009f, 0.042f, 0.023f },
                    tagText, m_fontSub, 0.0118f,
                    { 32, 36, 62, 216 }, tagCol.WithAlpha(175), tagCol);

                const std::string slotName = entry.username.empty() ? entry.userId : entry.username;
                sakura::ui::VisualStyle::DrawTextFit(
                    renderer, m_fontSub, slotName,
                    slotRect.x + 0.055f, slotRect.y + 0.009f, 0.0160f, 0.116f,
                    isActive ? gold : sakura::core::Color{ 246, 244, 252, 250 });

                // 'x' 移除账号记录热区按钮
                sakura::ui::VisualStyle::DrawBadgePill(
                    renderer, rmRect, "x", m_fontSub, 0.0132f,
                    rmHov ? sakura::core::Color{ 182, 48, 70, 238 } : sakura::core::Color{ 34, 40, 66, 198 },
                    rmHov ? coral : sakura::core::Color{ 164, 174, 212, 92 },
                    rmHov ? sakura::core::Color::White : dim);

                std::string metaLine = entry.isGuest ? "游客 · " : "正式 · ";
                if (!entry.danBadge.empty())
                    metaLine += "[" + entry.danBadge + "] ";
                metaLine += "★" + sakura::game::PPCalculator::FormatRating(entry.sakuraRating);
                sakura::ui::VisualStyle::DrawTextFit(
                    renderer, m_fontSub, metaLine,
                    slotRect.x + 0.010f, slotRect.y + 0.044f, 0.0130f, slotRect.width - 0.018f,
                    isActive ? sakura::core::Color{ 230, 220, 242, 242 } : dim);
            }
        }

        // ── 底部提示与错误反馈条 ───────────────────────────────────────────────
        renderer.DrawRoundedRect(ACCOUNT_MSG_BANNER_RECT, 0.010f, { 14, 18, 34, 212 }, true);
        renderer.DrawRoundedRectOutlineGradient(
            ACCOUNT_MSG_BANNER_RECT, 0.010f,
            m_authMessage.empty() ? sakura::core::Color{ 164, 174, 212, 68 } : pink,
            { 112, 122, 168, 36 }, 0.0010f, 10);
        const std::string footerMsg = m_authMessage.empty()
            ? "提示：右侧点击账号可免密切换，点击右上 'x' 移除本地槽位；重置游戏设置不会清除登录态"
            : m_authMessage;
        sakura::ui::VisualStyle::DrawTextFit(
            renderer, m_fontSub, footerMsg,
            ACCOUNT_MSG_BANNER_RECT.x + 0.014f, ACCOUNT_MSG_BANNER_RECT.y + 0.015f,
            0.0145f, ACCOUNT_MSG_BANNER_RECT.width - 0.028f,
            m_authMessage.empty() ? dim : (authState == sakura::net::AuthState::SessionExpired ? coral : pink));
    }

    // ── 编辑器子菜单 ──────────────────────────────────────────────────────────
    if (m_showEditorMenu)
    {
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.68f);
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer, EDITOR_MODAL_RECT,
            { 28, 34, 62, 240 }, { 14, 18, 36, 246 },
            { 250, 216, 157, 195 }, { 246, 172, 200, 96 },
            0.018f, 0.64f);

        sakura::ui::VisualStyle::DrawBadgePill(
            renderer, { 0.425f, 0.158f, 0.150f, 0.026f },
            "CHART ATELIER", m_fontSub, 0.0125f,
            { 54, 38, 74, 215 }, { 250, 216, 157, 155 }, gold);
        renderer.DrawText(m_fontSub, "谱面工房 · 选择或新建谱面",
            0.50f, 0.190f, 0.032f,
            sakura::core::Color{ 248, 242, 248, 250 },
            sakura::core::TextAlign::Center);

        const int totalCharts = static_cast<int>(m_customCharts.size());
        int userChartCount = 0;
        for (const auto& entry : m_customCharts)
        {
            if (entry.isUserChart) ++userChartCount;
        }
        const int builtinChartCount = totalCharts - userChartCount;
        renderer.DrawText(m_fontSub,
            "自制谱 " + std::to_string(userChartCount) + " 首  ·  内置曲库 " +
            std::to_string(builtinChartCount) + " 首（编辑内置谱自动建立副本）",
            0.268f, 0.232f, 0.0148f, dim, sakura::core::TextAlign::Left);
        if (totalCharts > 0 && m_selectedChartIdx >= 0)
        {
            renderer.DrawText(m_fontSub,
                "已选 " + std::to_string(m_selectedChartIdx + 1) + " / " + std::to_string(totalCharts),
                0.732f, 0.232f, 0.015f, gold, sakura::core::TextAlign::Right);
        }
        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.268f, 0.732f, 0.256f, 0.78f);

        // 谱面列表凹槽底板
        renderer.DrawRoundedGradientRect(
            EDITOR_LIST_RECT, 0.013f,
            { 12, 16, 32, 208 }, { 14, 18, 36, 208 },
            {  8, 11, 24, 222 }, { 10, 14, 28, 222 }, 12, true);
        renderer.DrawRoundedRectOutlineGradient(
            EDITOR_LIST_RECT, 0.013f,
            { 250, 216, 157, 92 }, { 140, 150, 196, 42 }, 0.0012f, 12);

        if (m_customCharts.empty())
        {
            renderer.DrawText(m_fontSub, "暂无可编辑谱面",
                0.50f, 0.435f, 0.025f,
                sakura::core::Color{ 248, 242, 248, 245 },
                sakura::core::TextAlign::Center);
            renderer.DrawText(m_fontSub, "点击下方「新建谱面」导入音乐创建第一张作品",
                0.50f, 0.482f, 0.017f, dim, sakura::core::TextAlign::Center);
        }
        else
        {
            const bool hasScrollbar = (totalCharts > EDITOR_VISIBLE_ROWS);
            const float baseRowW    = hasScrollbar ? EDITOR_ROW_W_SCROLL : EDITOR_ROW_W_FULL;

            for (int slot = 0; slot < EDITOR_VISIBLE_ROWS && (m_editorScrollRow + slot) < totalCharts; ++slot)
            {
                const int idx        = m_editorScrollRow + slot;
                const auto& entry    = m_customCharts[idx];
                const bool selected  = (idx == m_selectedChartIdx);
                const bool hovered   = (idx == m_hoveredEditorRow);
                const float indent   = selected ? 0.006f : (hovered ? 0.003f : 0.0f);
                const float rowY     = EDITOR_ROW_Y0 + static_cast<float>(slot) * EDITOR_ROW_STRIDE;
                const sakura::core::NormRect cardRect{
                    EDITOR_ROW_X + indent, rowY, baseRowW - indent, EDITOR_ROW_H
                };

                if (selected)
                {
                    sakura::ui::VisualStyle::DrawGlassCard(
                        renderer, cardRect,
                        { 84, 52, 90, 238 }, { 46, 34, 70, 242 },
                        { 250, 216, 157, 228 }, { 246, 178, 206, 138 }, 0.011f, 0.48f);
                    renderer.DrawRoundedGradientRect(
                        { cardRect.x + 0.0045f, rowY + 0.012f, 0.0032f, EDITOR_ROW_H - 0.024f },
                        0.0016f, gold, pink, gold, pink, 6, true);
                }
                else
                {
                    sakura::ui::VisualStyle::DrawGlassCard(
                        renderer, cardRect,
                        hovered ? sakura::core::Color{ 38, 46, 78, 226 } : sakura::core::Color{ 22, 28, 52, 204 },
                        hovered ? sakura::core::Color{ 24, 30, 54, 232 } : sakura::core::Color{ 14, 18, 36, 216 },
                        hovered ? sakura::core::Color{ 246, 178, 206, 145 } : sakura::core::Color{ 166, 176, 214, 64 },
                        { 112, 122, 168, 36 }, 0.010f, 0.30f);
                }

                renderer.DrawText(
                    m_fontSub,
                    selected ? "▶" : (entry.isUserChart ? "✦" : "♪"),
                    cardRect.x + 0.013f, rowY + 0.022f, 0.020f,
                    selected ? gold : (entry.isUserChart ? pink : dim));

                // 右上角来源标签 + 难度等级范围胶囊
                float minLv = entry.difficulties.front().level;
                float maxLv = minLv;
                for (const auto& d : entry.difficulties)
                {
                    minLv = std::min(minLv, d.level);
                    maxLv = std::max(maxLv, d.level);
                }
                const std::string lvLabel = (std::abs(maxLv - minLv) < 0.05f)
                    ? ("Lv." + FormatEditorLevel(maxLv))
                    : ("Lv." + FormatEditorLevel(minLv) + "~" + FormatEditorLevel(maxLv));

                sakura::ui::VisualStyle::DrawBadgePill(
                    renderer,
                    { cardRect.x + cardRect.width - 0.140f, rowY + 0.010f, 0.050f, 0.023f },
                    entry.isUserChart ? "自制" : "内置",
                    m_fontSub, 0.0125f,
                    entry.isUserChart ? sakura::core::Color{ 34, 64, 62, 212 } : sakura::core::Color{ 34, 42, 70, 195 },
                    entry.isUserChart ? sakura::core::Color{ 146, 232, 198, 165 } : sakura::core::Color{ 164, 174, 214, 105 },
                    entry.isUserChart ? sakura::core::Color{ 168, 245, 214, 255 } : dim);

                sakura::ui::VisualStyle::DrawBadgePill(
                    renderer,
                    { cardRect.x + cardRect.width - 0.084f, rowY + 0.010f, 0.074f, 0.023f },
                    lvLabel, m_fontSub, 0.0125f,
                    selected ? sakura::core::Color{ 104, 64, 98, 224 } : sakura::core::Color{ 36, 44, 74, 198 },
                    selected ? gold : sakura::core::Color{ 174, 184, 218, 95 },
                    selected ? gold : pink);

                const float textX     = cardRect.x + 0.034f;
                const float maxTitleW = std::max(0.10f, (cardRect.x + cardRect.width - 0.148f) - textX);
                sakura::ui::VisualStyle::DrawTextFit(
                    renderer, m_fontSub, entry.title, textX, rowY + 0.009f, 0.022f, maxTitleW,
                    selected ? sakura::core::Color{ 255, 248, 250, 255 }
                             : sakura::core::Color{ 240, 242, 250, 245 });

                std::string meta = entry.artist;
                if (!entry.charter.empty())
                    meta += "  ·  谱师 " + entry.charter;
                meta += "  ·  " + FormatEditorNumber(entry.bpm, 0) + " BPM";
                const float maxMetaW = std::max(0.10f, (cardRect.x + cardRect.width - 0.138f) - textX);
                sakura::ui::VisualStyle::DrawTextFit(
                    renderer, m_fontSub, meta, textX, rowY + 0.042f, 0.0148f, maxMetaW,
                    selected ? sakura::core::Color{ 220, 210, 232, 240 } : dim);

                if (selected)
                {
                    const int dIdx = std::clamp(
                        m_selectedEditorDiffIdx, 0, static_cast<int>(entry.difficulties.size()) - 1);
                    renderer.DrawText(
                        m_fontSub,
                        "编辑：" + entry.difficulties[dIdx].name + " (" +
                        std::to_string(entry.difficulties.size()) + " 难度)",
                        cardRect.x + cardRect.width - 0.010f, rowY + 0.043f, 0.0142f,
                        gold, sakura::core::TextAlign::Right);
                }
                else
                {
                    renderer.DrawText(
                        m_fontSub,
                        std::to_string(entry.difficulties.size()) + " 个难度",
                        cardRect.x + cardRect.width - 0.010f, rowY + 0.043f, 0.0142f,
                        dim, sakura::core::TextAlign::Right);
                }
            }

            // 右侧滚动条（超出可见行数时显示）
            if (hasScrollbar)
            {
                const sakura::core::NormRect trackRect{
                    EDITOR_SCROLL_TRACK_X, EDITOR_SCROLL_TRACK_Y,
                    EDITOR_SCROLL_TRACK_W, EDITOR_SCROLL_TRACK_H
                };
                renderer.DrawRoundedRect(trackRect, EDITOR_SCROLL_TRACK_W * 0.5f, { 22, 28, 52, 185 }, true);

                const int maxScroll = std::max(1, totalCharts - EDITOR_VISIBLE_ROWS);
                const float thumbH  = std::max(
                    0.042f,
                    EDITOR_SCROLL_TRACK_H * static_cast<float>(EDITOR_VISIBLE_ROWS) / static_cast<float>(totalCharts));
                const float thumbY  = EDITOR_SCROLL_TRACK_Y +
                    (EDITOR_SCROLL_TRACK_H - thumbH) *
                    (static_cast<float>(m_editorScrollRow) / static_cast<float>(maxScroll));
                const bool scrollHover = m_draggingEditorScroll ||
                    (mx >= EDITOR_SCROLL_TRACK_X - 0.008f &&
                     mx <= EDITOR_SCROLL_TRACK_X + EDITOR_SCROLL_TRACK_W + 0.008f &&
                     my >= EDITOR_SCROLL_TRACK_Y && my <= EDITOR_SCROLL_TRACK_Y + EDITOR_SCROLL_TRACK_H);

                const sakura::core::Color thumbTop = scrollHover ? gold : pink;
                const sakura::core::Color thumbBot = scrollHover
                    ? pink
                    : sakura::core::Color{ 156, 128, 210, 225 };
                renderer.DrawRoundedGradientRect(
                    { EDITOR_SCROLL_TRACK_X, thumbY, EDITOR_SCROLL_TRACK_W, thumbH },
                    EDITOR_SCROLL_TRACK_W * 0.5f,
                    thumbTop, thumbTop, thumbBot, thumbBot,
                    8, true);
            }
        }

        // 选中谱面的难度切换栏
        renderer.DrawText(m_fontSub, "编辑难度", EDITOR_DIFF_LABEL_X, EDITOR_DIFF_Y + 0.007f, 0.0155f, dim);
        if (m_selectedChartIdx >= 0 && m_selectedChartIdx < totalCharts)
        {
            const auto& selEntry = m_customCharts[m_selectedChartIdx];
            const int diffCount  = static_cast<int>(selEntry.difficulties.size());
            const int pageStart  = (m_selectedEditorDiffIdx / 4) * 4;
            const int visDiffs   = std::min(4, diffCount - pageStart);
            const float slotW    = EDITOR_DIFF_W / static_cast<float>(std::max(1, visDiffs));

            for (int k = 0; k < visDiffs; ++k)
            {
                const int di = pageStart + k;
                const auto& diff = selEntry.difficulties[di];
                const sakura::core::NormRect pillRect{
                    EDITOR_DIFF_X0 + static_cast<float>(k) * slotW,
                    EDITOR_DIFF_Y,
                    slotW - 0.008f,
                    EDITOR_DIFF_H
                };
                const bool diffSel = (di == m_selectedEditorDiffIdx);
                const bool diffHov = (mx >= pillRect.x && mx <= pillRect.x + pillRect.width &&
                                      my >= pillRect.y && my <= pillRect.y + pillRect.height);
                sakura::ui::VisualStyle::DrawBadgePill(
                    renderer, pillRect,
                    diff.name + "  Lv." + FormatEditorLevel(diff.level),
                    m_fontSub, 0.0142f,
                    diffSel ? sakura::core::Color{ 96, 58, 94, 232 }
                            : (diffHov ? sakura::core::Color{ 44, 52, 86, 220 }
                                       : sakura::core::Color{ 24, 30, 56, 200 }),
                    diffSel ? gold : (diffHov ? pink : sakura::core::Color{ 166, 176, 214, 95 }),
                    diffSel ? gold : (diffHov ? sakura::core::Color::White : dim));
            }
        }
        else
        {
            renderer.DrawText(m_fontSub, "新建谱面后可添加并切换多个难度",
                EDITOR_DIFF_X0, EDITOR_DIFF_Y + 0.007f, 0.015f, dim);
        }

        if (m_btnEditorOpen)
        {
            m_btnEditorOpen->SetEnabled(!m_customCharts.empty());
            m_btnEditorOpen->Render(renderer);
        }
        if (m_btnEditorNew)    m_btnEditorNew->Render(renderer);
        if (m_btnEditorCancel) m_btnEditorCancel->Render(renderer);

        renderer.DrawText(
            m_fontSub,
            "左键选谱 · 双击直接打开 · 滚轮翻页   |   ↑↓ 选谱 · ←→ 切换难度 · Enter 打开 · Esc 关闭",
            0.50f, 0.805f, 0.0142f, dim, sakura::core::TextAlign::Center);
    }

    // ── 退出确认对话框 ────────────────────────────────────────────────────────
    if (m_showExitConfirm)
    {
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.66f);
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer, { 0.315f, 0.34f, 0.37f, 0.275f },
            { 28, 34, 62, 238 }, { 16, 20, 40, 246 },
            { 250, 216, 157, 190 }, { 246, 172, 200, 92 },
            0.018f, 0.62f);

        renderer.DrawText(m_fontSub, "EXIT SAKURA",
            0.50f, 0.368f, 0.015f, gold, sakura::core::TextAlign::Center);
        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.36f, 0.64f, 0.398f, 0.72f);
        renderer.DrawText(m_fontSub, "确定要退出游戏吗？",
            0.50f, 0.422f, 0.031f,
            sakura::core::Color{ 248, 242, 248, 250 },
            sakura::core::TextAlign::Center);

        if (m_btnConfirmYes) m_btnConfirmYes->Render(renderer);
        if (m_btnConfirmNo)  m_btnConfirmNo ->Render(renderer);
    }

    if (m_topBar)
    {
        m_topBar->Render(renderer);
        m_topBar->RenderDrawer(renderer);
    }
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneMenu::OnEvent(const SDL_Event& event)
{
    // 鼠标点击全局特效
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        auto [mx, my] = sakura::core::Input::GetMousePosition();
        m_clickRings.push_back({mx, my, 0.0f});
        auto cfg = sakura::effects::ParticlePresets::ClickSpark();
        m_particles.Emit(mx, my, 5, cfg);
    }
    if (m_topBar && m_topBar->HandleEvent(event))
        return;

    // 在线账号弹层优先
    if (m_showAccountModal)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            CloseAccountModal();
            return;
        }

        if (!m_authBusy && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            const auto [mx, my] = sakura::core::Input::GetMousePosition();
            const int slotCount = static_cast<int>(m_savedAccounts.size());
            for (int slot = 0; slot < slotCount; ++slot)
            {
                if (ContainsNormPoint(GetSavedAccountRemoveRect(slot), mx, my))
                {
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    const std::string removedId   = m_savedAccounts[slot].userId;
                    const std::string removedName = m_savedAccounts[slot].username;
                    sakura::net::APIClient::GetInstance().RemoveSavedAccount(removedId);
                    RefreshSavedAccounts();
                    RefreshAccountCapsuleText(true);
                    UpdateAccountModalLayout();
                    m_authMessage = "已移除已保存账号：" + (removedName.empty() ? removedId : removedName);
                    return;
                }
                if (ContainsNormPoint(GetSavedAccountSlotRect(slot), mx, my))
                {
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    const std::string targetId   = m_savedAccounts[slot].userId;
                    const std::string targetName = m_savedAccounts[slot].username;
                    const std::string targetUrl  = m_savedAccounts[slot].serverUrl;
                    m_authBusy           = true;
                    m_changePasswordMode = false;
                    m_authMessage        = "正在切换账号：" + (targetName.empty() ? targetId : targetName) + "...";
                    UpdateAccountModalLayout();
                    auto alive = m_alive;
                    sakura::net::APIClient::GetInstance().SwitchSavedAccountAsync(
                        targetId, [this, alive, targetName, targetUrl](sakura::net::AuthResult r)
                        {
                            if (!*alive) return;
                            m_authBusy = false;
                            RefreshSavedAccounts();
                            RefreshAccountCapsuleText(true);
                            if (r.ok)
                            {
                                if (m_inputServerUrl)
                                    m_inputServerUrl->SetText(sakura::net::APIClient::GetInstance().GetServerUrl());
                                if (m_inputUsername)
                                    m_inputUsername->SetText(r.user.username);
                                if (m_inputPassword)    m_inputPassword->SetText("");
                                if (m_inputNewPassword) m_inputNewPassword->SetText("");
                                UpdateAccountModalLayout();
                                sakura::ui::ToastManager::Instance().Show(
                                    "已切换账号：" + r.user.username, sakura::ui::ToastType::Success, 2.8f);
                                CloseAccountModal();
                            }
                            else
                            {
                                if (m_inputServerUrl && !targetUrl.empty())
                                    m_inputServerUrl->SetText(targetUrl);
                                if (m_inputUsername && !targetName.empty())
                                    m_inputUsername->SetText(targetName);
                                if (m_inputPassword)
                                {
                                    m_inputPassword->SetText("");
                                    m_inputPassword->SetFocused(true);
                                }
                                UpdateAccountModalLayout();
                                m_authMessage = r.error.empty()
                                    ? ("账号「" + targetName + "」会话已过期，请输入密码重新登录")
                                    : r.error;
                            }
                        });
                    return;
                }
            }
        }

        bool consumed = false;
        if (m_inputServerUrl)   consumed = m_inputServerUrl->HandleEvent(event)   || consumed;
        if (m_inputUsername)    consumed = m_inputUsername->HandleEvent(event)    || consumed;
        if (m_inputPassword)    consumed = m_inputPassword->HandleEvent(event)    || consumed;
        if (m_inputNewPassword) consumed = m_inputNewPassword->HandleEvent(event) || consumed;
        if (consumed && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_TEXT_INPUT))
            return;
        if (m_btnAuthGuest           && m_btnAuthGuest->HandleEvent(event))           return;
        if (m_btnAuthUpgradeGuest    && m_btnAuthUpgradeGuest->HandleEvent(event))    return;
        if (m_btnAuthToggleChangePwd && m_btnAuthToggleChangePwd->HandleEvent(event)) return;
        if (m_btnAuthSubmitPassword  && m_btnAuthSubmitPassword->HandleEvent(event))  return;
        if (m_btnAuthLogin           && m_btnAuthLogin->HandleEvent(event))           return;
        if (m_btnAuthRegister        && m_btnAuthRegister->HandleEvent(event))        return;
        if (m_btnAuthLogout          && m_btnAuthLogout->HandleEvent(event))          return;
        if (m_btnAuthClose           && m_btnAuthClose->HandleEvent(event))           return;
        return;
    }

    // 编辑器子菜单优先
    if (m_showEditorMenu)
    {
        const int totalCharts = static_cast<int>(m_customCharts.size());
        const auto [mx, my]   = sakura::core::Input::GetMousePosition();

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
        {
            m_draggingEditorScroll = false;
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION && m_draggingEditorScroll)
        {
            UpdateEditorScrollFromMouseY(my);
            return;
        }
        else if (event.type == SDL_EVENT_MOUSE_WHEEL && totalCharts > 0)
        {
            m_editorWheelAccum -= event.wheel.y;
            int steps = 0;
            while (m_editorWheelAccum >= 0.85f)  { ++steps; m_editorWheelAccum -= 1.0f; }
            while (m_editorWheelAccum <= -0.85f) { --steps; m_editorWheelAccum += 1.0f; }
            if (steps != 0)
                ScrollEditorListBy(steps);
            return;
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            // 右侧滚动条点击/拖拽
            if (totalCharts > EDITOR_VISIBLE_ROWS &&
                mx >= EDITOR_SCROLL_TRACK_X - 0.008f &&
                mx <= EDITOR_SCROLL_TRACK_X + EDITOR_SCROLL_TRACK_W + 0.010f &&
                my >= EDITOR_LIST_RECT.y && my <= EDITOR_LIST_RECT.y + EDITOR_LIST_RECT.height)
            {
                m_draggingEditorScroll = true;
                UpdateEditorScrollFromMouseY(my);
                return;
            }

            // 谱面列表条目点击选中 / 双击直接进入编辑器
            const float rowMaxX = (totalCharts > EDITOR_VISIBLE_ROWS)
                ? (EDITOR_SCROLL_TRACK_X - 0.008f)
                : (EDITOR_LIST_RECT.x + EDITOR_LIST_RECT.width);
            if (totalCharts > 0 &&
                mx >= EDITOR_LIST_RECT.x && mx < rowMaxX &&
                my >= EDITOR_ROW_Y0 && my < EDITOR_ROW_Y0 + EDITOR_VISIBLE_ROWS * EDITOR_ROW_STRIDE)
            {
                const int slot = static_cast<int>((my - EDITOR_ROW_Y0) / EDITOR_ROW_STRIDE);
                const int idx  = m_editorScrollRow + slot;
                if (slot >= 0 && slot < EDITOR_VISIBLE_ROWS && idx >= 0 && idx < totalCharts)
                {
                    const bool wasSelected = (idx == m_selectedChartIdx);
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    SelectEditorChart(idx, false);
                    if (wasSelected && event.button.clicks >= 2)
                        OpenEditorForChart(idx);
                    return;
                }
            }

            // 底部难度胶囊点击切换
            if (m_selectedChartIdx >= 0 && m_selectedChartIdx < totalCharts &&
                my >= EDITOR_DIFF_Y && my <= EDITOR_DIFF_Y + EDITOR_DIFF_H &&
                mx >= EDITOR_DIFF_X0 && mx <= EDITOR_DIFF_X0 + EDITOR_DIFF_W)
            {
                const auto& selEntry = m_customCharts[m_selectedChartIdx];
                const int diffCount  = static_cast<int>(selEntry.difficulties.size());
                const int pageStart  = (m_selectedEditorDiffIdx / 4) * 4;
                const int visDiffs   = std::min(4, diffCount - pageStart);
                const float slotW    = EDITOR_DIFF_W / static_cast<float>(std::max(1, visDiffs));
                const int k          = static_cast<int>((mx - EDITOR_DIFF_X0) / slotW);
                if (k >= 0 && k < visDiffs)
                {
                    const float pillX = EDITOR_DIFF_X0 + static_cast<float>(k) * slotW;
                    if (mx <= pillX + slotW - 0.008f)
                    {
                        m_selectedEditorDiffIdx = pageStart + k;
                        sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                        return;
                    }
                }
            }
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_showEditorMenu       = false;
            m_draggingEditorScroll = false;
            return;
        }

        // 方向键与翻页键选择谱面及难度
        if (event.type == SDL_EVENT_KEY_DOWN && totalCharts > 0)
        {
            const int maxIdx = totalCharts - 1;
            if (event.key.scancode == SDL_SCANCODE_UP)
            {
                SelectEditorChart((m_selectedChartIdx <= 0) ? maxIdx : (m_selectedChartIdx - 1), true);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_DOWN)
            {
                SelectEditorChart((m_selectedChartIdx >= maxIdx) ? 0 : (m_selectedChartIdx + 1), true);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_PAGEUP)
            {
                SelectEditorChart(std::max(0, m_selectedChartIdx - EDITOR_VISIBLE_ROWS), true);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_PAGEDOWN)
            {
                SelectEditorChart(std::min(maxIdx, m_selectedChartIdx + EDITOR_VISIBLE_ROWS), true);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_HOME)
            {
                SelectEditorChart(0, true);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_END)
            {
                SelectEditorChart(maxIdx, true);
                return;
            }
            if ((event.key.scancode == SDL_SCANCODE_LEFT || event.key.scancode == SDL_SCANCODE_RIGHT) &&
                m_selectedChartIdx >= 0 && m_selectedChartIdx < totalCharts)
            {
                const int diffCount = static_cast<int>(m_customCharts[m_selectedChartIdx].difficulties.size());
                if (diffCount > 1)
                {
                    const int delta = (event.key.scancode == SDL_SCANCODE_LEFT) ? -1 : 1;
                    m_selectedEditorDiffIdx = (m_selectedEditorDiffIdx + diffCount + delta) % diffCount;
                }
                return;
            }
            if ((event.key.scancode == SDL_SCANCODE_RETURN ||
                 event.key.scancode == SDL_SCANCODE_KP_ENTER) &&
                m_selectedChartIdx >= 0)
            {
                OpenEditorForChart(m_selectedChartIdx);
                return;
            }
        }

        if (m_btnEditorOpen)   m_btnEditorOpen->HandleEvent(event);
        if (m_btnEditorNew)    m_btnEditorNew->HandleEvent(event);
        if (m_btnEditorCancel) m_btnEditorCancel->HandleEvent(event);
        return;
    }

    // 若退出确认框可见，事件只路由给确认框按钮
    if (m_showExitConfirm)
    {
        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            // ESC 取消退出确认框
            m_showExitConfirm = false;
            return;
        }
        if (m_btnConfirmYes) m_btnConfirmYes->HandleEvent(event);
        if (m_btnConfirmNo)  m_btnConfirmNo ->HandleEvent(event);
        return;
    }

    // ESC → 显示退出确认框
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_UP) { m_keyboardFocus = (m_keyboardFocus + BUTTON_COUNT - 1) % BUTTON_COUNT; return; }
        if (event.key.scancode == SDL_SCANCODE_DOWN || event.key.scancode == SDL_SCANCODE_TAB) { m_keyboardFocus = (m_keyboardFocus + 1) % BUTTON_COUNT; return; }
        if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_SPACE) { m_buttons[m_keyboardFocus]->Activate(); return; }
    }
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        m_showExitConfirm = true;
        return;
    }

    if (m_btnAccountCapsule && m_btnAccountCapsule->HandleEvent(event))
        return;
    if (m_btnMarket && m_btnMarket->HandleEvent(event))
        return;
    if (m_btnDojo && m_btnDojo->HandleEvent(event))
        return;
    if (m_btnArena && m_btnArena->HandleEvent(event))
        return;

    // 分发给按钮（带当前 X 偏移做命中测试）
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (!m_buttons[i]) continue;

        // 临时应用偏移
        auto origBounds = m_buttons[i]->GetBounds();
        sakura::core::NormRect animBounds = origBounds;
        animBounds.x = origBounds.x + m_anim.btnOffsetX[i] + (i % 2 == 1 ? 0.006f : 0.0f);

        m_buttons[i]->SetBounds(animBounds);
        m_buttons[i]->HandleEvent(event);

        m_buttons[i]->SetBounds(origBounds);
    }
}

} // namespace sakura::scene
