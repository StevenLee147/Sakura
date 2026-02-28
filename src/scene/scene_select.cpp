// scene_select.cpp — 选歌场景

#include "scene_select.h"
#include "scene_menu.h"
#include "scene_game.h"
#include "core/input.h"
#include "utils/logger.h"
#include "utils/easing.h"
#include "audio/audio_manager.h"
#include "game/chart_loader.h"
#include "data/database.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <memory>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneSelect::SceneSelect(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneSelect::OnEnter()
{
    LOG_INFO("[SceneSelect] 进入选歌场景");

    m_selectedChart    = -1;
    m_selectedDifficulty = 0;
    m_previewTimer     = 0.0f;
    m_previewPlaying   = false;
    m_lastPreviewChart = -1;
    m_coverTexture     = sakura::core::INVALID_HANDLE;

    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontUI    = rm.GetDefaultFontHandle();
    m_fontSmall = rm.GetDefaultFontHandle();

    // 扫描谱面
    sakura::game::ChartLoader loader;
    m_charts = loader.ScanCharts("resources/charts/");
    LOG_INFO("[SceneSelect] 找到 {} 首曲目", static_cast<int>(m_charts.size()));

    // 建立 UI
    SetupUI();

    // 填充列表内容
    UpdateSongList();

    // 若有谱面则默认选中第一首
    if (!m_charts.empty())
    {
        m_songList->SetSelectedIndex(0);
        OnSongSelected(0);
    }
}

// ── SetupUI ───────────────────────────────────────────────────────────────────

void SceneSelect::SetupUI()
{
    // 歌曲列表 (0.02, 0.10, 0.45, 0.80)
    m_songList = std::make_unique<sakura::ui::ScrollList>(
        sakura::core::NormRect{ 0.02f, 0.10f, 0.45f, 0.80f },
        m_fontUI, 0.065f, 0.026f);

    m_songList->SetBgColor({ 15, 12, 30, 200 });
    m_songList->SetNormalColor({ 25, 20, 50, 200 });
    m_songList->SetHoverColor({ 50, 40, 90, 220 });
    m_songList->SetSelectedColor({ 85, 60, 135, 240 });

    m_songList->SetOnSelectionChanged([this](int idx) { OnSongSelected(idx); });
    m_songList->SetOnDoubleClick([this](int idx)
    {
        m_selectedChart = idx;
        if (m_btnStart) m_btnStart->SetEnabled(true);
        LOG_INFO("[SceneSelect] 双击确认: {}", m_charts[idx].title);
        // Step 1.11 完成后：切换到 SceneGame
    });

    // 返回按钮
    sakura::ui::ButtonColors btnColors;
    btnColors.normal  = { 35, 30, 65, 210 };
    btnColors.hover   = { 60, 50, 105, 230 };
    btnColors.pressed = { 20, 15, 45, 240 };
    btnColors.text    = sakura::core::Color::White;

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.04f, 0.926f, 0.18f, 0.055f },
        "返回", m_fontUI, 0.026f, 0.010f);
    m_btnBack->SetColors(btnColors);
    m_btnBack->SetOnClick([this]()
    {
        StopPreview();
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
    });

    // 开始按钮
    sakura::ui::ButtonColors startColors;
    startColors.normal  = { 100, 55, 155, 220 };
    startColors.hover   = { 130, 80, 190, 235 };
    startColors.pressed = { 70, 35, 120, 240 };
    startColors.text    = sakura::core::Color::White;

    m_btnStart = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.78f, 0.926f, 0.18f, 0.055f },
        "开始游戏", m_fontUI, 0.026f, 0.010f);
    m_btnStart->SetColors(startColors);
    m_btnStart->SetEnabled(!m_charts.empty());
    m_btnStart->SetOnClick([this]()
    {
        if (m_selectedChart < 0 || m_selectedChart >= static_cast<int>(m_charts.size()))
            return;
        LOG_INFO("[SceneSelect] 开始游戏: {} [{}]",
                 m_charts[m_selectedChart].title, m_selectedDifficulty);
        StopPreview();
        m_manager.SwitchScene(
            std::make_unique<SceneGame>(m_manager,
                m_charts[m_selectedChart], m_selectedDifficulty),
            TransitionType::Fade, 0.5f);
    });
}

// ── UpdateSongList ────────────────────────────────────────────────────────────

void SceneSelect::UpdateSongList()
{
    if (!m_songList) return;

    std::vector<std::string> items;
    items.reserve(m_charts.size());
    for (const auto& info : m_charts)
        items.push_back(FormatListItem(info));

    m_songList->SetItems(items);
}

std::string SceneSelect::FormatListItem(const sakura::game::ChartInfo& info) const
{
    // 找最高难度
    float maxLevel = 0.0f;
    for (const auto& d : info.difficulties)
        if (d.level > maxLevel) maxLevel = d.level;

    std::ostringstream oss;
    oss << info.title << "  -  " << info.artist;
    if (maxLevel > 0.0f)
    {
        oss << "  [Lv ";
        // 若是整数则不显示小数
        if (std::floor(maxLevel) == maxLevel)
            oss << static_cast<int>(maxLevel);
        else
            oss << std::fixed << std::setprecision(1) << maxLevel;
        oss << "]";
    }
    return oss.str();
}

// ── RefreshDifficultyButtons ──────────────────────────────────────────────────

void SceneSelect::RefreshDifficultyButtons()
{
    m_diffButtons.clear();
    if (m_selectedChart < 0 || m_selectedChart >= static_cast<int>(m_charts.size()))
        return;

    const auto& chart = m_charts[m_selectedChart];
    int diffCount = std::min(static_cast<int>(chart.difficulties.size()),
                             MAX_DIFF_BUTTONS);

    // 难度按钮从右侧详情面板 (0.50~0.98) 展示，y=0.66
    float totalW      = std::min(static_cast<float>(diffCount) * 0.080f, 0.44f);
    float startX      = 0.50f + (0.48f - totalW) * 0.5f;
    float btnW        = 0.072f;
    float btnH        = 0.038f;
    float btnY        = 0.66f;
    float gap         = 0.080f;

    for (int i = 0; i < diffCount; ++i)
    {
        const auto& diff = chart.difficulties[i];
        std::ostringstream label;
        label << diff.name << " ";
        if (std::floor(diff.level) == diff.level)
            label << static_cast<int>(diff.level);
        else
            label << std::fixed << std::setprecision(1) << diff.level;

        float x = startX + i * gap;
        sakura::core::NormRect bounds = { x, btnY, btnW, btnH };

        sakura::ui::ButtonColors c;
        if (i == m_selectedDifficulty)
        {
            c.normal  = { 120, 70, 180, 235 };
            c.hover   = { 140, 90, 200, 245 };
            c.pressed = { 90, 50, 150, 245 };
        }
        else
        {
            c.normal  = { 35, 30, 65, 200 };
            c.hover   = { 60, 50, 105, 220 };
            c.pressed = { 20, 15, 45, 235 };
        }
        c.text = sakura::core::Color::White;

        auto btn = std::make_unique<sakura::ui::Button>(
            bounds, label.str(), m_fontSmall, 0.018f, 0.008f);
        btn->SetColors(c);

        int idx = i;
        btn->SetOnClick([this, idx]()
        {
            m_selectedDifficulty = idx;
            RefreshDifficultyButtons();
        });

        m_diffButtons.push_back(std::move(btn));
    }
}

// ── OnSongSelected ────────────────────────────────────────────────────────────

void SceneSelect::OnSongSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(m_charts.size())) return;

    m_selectedChart    = index;
    m_selectedDifficulty = 0;
    m_previewTimer     = 0.0f;   // 重置预览计时

    // 加载封面
    const auto& chart = m_charts[index];
    if (!chart.coverFile.empty())
    {
        std::string coverPath = chart.folderPath + "/" + chart.coverFile;
        auto& rm = sakura::core::ResourceManager::GetInstance();
        auto hOpt = rm.LoadTexture(coverPath);
        m_coverTexture = hOpt.value_or(sakura::core::INVALID_HANDLE);
    }
    else
    {
        m_coverTexture = sakura::core::INVALID_HANDLE;
    }

    RefreshDifficultyButtons();
    LOG_DEBUG("[SceneSelect] 选中: {}", chart.title);
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneSelect::OnExit()
{
    LOG_INFO("[SceneSelect] 退出选歌场景");
    StopPreview();
    m_songList.reset();
    m_btnBack.reset();
    m_btnStart.reset();
    m_diffButtons.clear();
}

// ── StartPreview / StopPreview ────────────────────────────────────────────────

void SceneSelect::StartPreview()
{
    if (m_selectedChart < 0 || m_selectedChart >= static_cast<int>(m_charts.size()))
        return;

    const auto& chart = m_charts[m_selectedChart];
    if (chart.musicFile.empty()) return;

    std::string musicPath = chart.folderPath + "/" + chart.musicFile;
    auto& audio = sakura::audio::AudioManager::GetInstance();

    audio.PlayMusic(musicPath, 0);   // loops=0: 播放一次
    audio.SetMusicPosition(static_cast<double>(chart.previewTime) / 1000.0);

    m_previewPlaying   = true;
    m_lastPreviewChart = m_selectedChart;
    LOG_DEBUG("[SceneSelect] 开始预览: {}", chart.title);
}

void SceneSelect::StopPreview()
{
    if (!m_previewPlaying) return;
    sakura::audio::AudioManager::GetInstance().FadeOutMusic(400);
    m_previewPlaying = false;
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneSelect::OnUpdate(float dt)
{
    // 预览计时
    if (m_selectedChart >= 0 && !m_previewPlaying)
    {
        m_previewTimer += dt;
        if (m_previewTimer >= PREVIEW_DELAY)
        {
            StartPreview();
        }
    }

    // 检测音乐预览是否播完
    if (m_previewPlaying)
    {
        auto& audio = sakura::audio::AudioManager::GetInstance();
        if (!audio.IsPlaying())
        {
            m_previewPlaying = false;
        }
    }

    // 键盘上下切换
    int listSize = static_cast<int>(m_charts.size());
    if (listSize > 0)
    {
        if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_UP) ||
            sakura::core::Input::IsKeyPressed(SDL_SCANCODE_W))
        {
            int newIdx = std::max(0, (m_selectedChart < 0 ? 0 : m_selectedChart) - 1);
            m_songList->SetSelectedIndex(newIdx);
            m_songList->ScrollToIndex(newIdx);
            OnSongSelected(newIdx);
        }
        if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_DOWN) ||
            sakura::core::Input::IsKeyPressed(SDL_SCANCODE_S))
        {
            int newIdx = std::min(listSize - 1,
                (m_selectedChart < 0 ? 0 : m_selectedChart) + 1);
            m_songList->SetSelectedIndex(newIdx);
            m_songList->ScrollToIndex(newIdx);
            OnSongSelected(newIdx);
        }
        // Enter / Space → 开始
        if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_RETURN) ||
            sakura::core::Input::IsKeyPressed(SDL_SCANCODE_SPACE))
        {
            if (m_btnStart && m_btnStart->IsEnabled() &&
                m_selectedChart >= 0 &&
                m_selectedChart < static_cast<int>(m_charts.size()))
            {
                LOG_INFO("[SceneSelect] 键盘确认选曲");
                StopPreview();
                m_manager.SwitchScene(
                    std::make_unique<SceneGame>(m_manager,
                        m_charts[m_selectedChart], m_selectedDifficulty),
                    TransitionType::Fade, 0.5f);
            }
        }
    }

    // 返回键
    if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_ESCAPE))
    {
        StopPreview();
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
        return;
    }

    // 更新 UI 组件
    if (m_songList)  m_songList->Update(dt);
    if (m_btnBack)   m_btnBack->Update(dt);
    if (m_btnStart)  m_btnStart->Update(dt);
    for (auto& btn : m_diffButtons) if (btn) btn->Update(dt);
}

// ── RenderDetailPanel ─────────────────────────────────────────────────────────

void SceneSelect::RenderDetailPanel(sakura::core::Renderer& renderer)
{
    // 面板背景
    renderer.DrawRoundedRect(
        { 0.50f, 0.10f, 0.48f, 0.80f }, 0.012f,
        sakura::core::Color{ 15, 12, 30, 210 }, true);
    renderer.DrawRoundedRect(
        { 0.50f, 0.10f, 0.48f, 0.80f }, 0.012f,
        sakura::core::Color{ 80, 60, 130, 100 }, false);

    if (m_selectedChart < 0 || m_selectedChart >= static_cast<int>(m_charts.size()))
    {
        // 未选中提示
        if (m_fontUI != sakura::core::INVALID_HANDLE)
        {
            renderer.DrawText(m_fontUI, "← 请选择曲目",
                0.74f, 0.46f, 0.030f,
                sakura::core::Color{ 150, 140, 170, 150 },
                sakura::core::TextAlign::Center);
        }
        return;
    }

    const auto& chart = m_charts[m_selectedChart];

    // 封面区 (0.52, 0.12, 0.20, 0.35)
    sakura::core::NormRect coverRect = { 0.52f, 0.12f, 0.20f, 0.35f };
    if (m_coverTexture != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawSprite(m_coverTexture, coverRect);
    }
    else
    {
        renderer.DrawRoundedRect(coverRect, 0.008f,
            sakura::core::Color{ 40, 30, 70, 180 }, true);
        renderer.DrawRoundedRect(coverRect, 0.008f,
            sakura::core::Color{ 100, 80, 150, 120 }, false);
        if (m_fontSmall != sakura::core::INVALID_HANDLE)
        {
            renderer.DrawText(m_fontSmall, "No Cover",
                0.62f, 0.28f, 0.020f,
                sakura::core::Color{ 130, 120, 150, 160 },
                sakura::core::TextAlign::Center);
        }
    }

    if (m_fontUI == sakura::core::INVALID_HANDLE) return;

    float px = 0.74f;   // 右侧信息区中心 X

    // 曲名
    renderer.DrawText(m_fontUI, chart.title,
        px, 0.13f, 0.036f,
        sakura::core::Color{ 250, 230, 255, 240 },
        sakura::core::TextAlign::Center);

    // 曲师
    renderer.DrawText(m_fontSmall, chart.artist.empty() ? "Unknown Artist" : chart.artist,
        px, 0.178f, 0.024f,
        sakura::core::Color{ 200, 185, 220, 200 },
        sakura::core::TextAlign::Center);

    // 谱师
    if (!chart.charter.empty())
    {
        renderer.DrawText(m_fontSmall, "Chart: " + chart.charter,
            px, 0.210f, 0.020f,
            sakura::core::Color{ 160, 150, 180, 170 },
            sakura::core::TextAlign::Center);
    }

    // BPM
    std::ostringstream bpmStr;
    bpmStr << "BPM: " << std::fixed << std::setprecision(1) << chart.bpm;
    renderer.DrawText(m_fontSmall, bpmStr.str(),
        px, 0.238f, 0.020f,
        sakura::core::Color{ 160, 150, 180, 170 },
        sakura::core::TextAlign::Center);

    // 分隔线
    renderer.DrawLine(0.52f, 0.285f, 0.964f, 0.285f,
        sakura::core::Color{ 80, 60, 120, 120 }, 0.001f);

    // 当前难度详情
    if (m_selectedDifficulty < static_cast<int>(chart.difficulties.size()))
    {
        const auto& diff = chart.difficulties[m_selectedDifficulty];

        // 难度名 + 等级
        std::ostringstream lvStr;
        lvStr << diff.name << "  Lv. ";
        if (std::floor(diff.level) == diff.level)
            lvStr << static_cast<int>(diff.level);
        else
            lvStr << std::fixed << std::setprecision(1) << diff.level;

        renderer.DrawText(m_fontUI, lvStr.str(),
            px, 0.298f, 0.030f,
            sakura::core::Color{ 220, 190, 255, 240 },
            sakura::core::TextAlign::Center);

        // 音符数量
        std::string noteCountStr =
            "Notes: KB=" + std::to_string(diff.noteCount) +
            "  Mouse=" + std::to_string(diff.mouseNoteCount);
        renderer.DrawText(m_fontSmall, noteCountStr,
            px, 0.338f, 0.020f,
            sakura::core::Color{ 180, 170, 200, 180 },
            sakura::core::TextAlign::Center);
    }

    // 难度标签按钮由 RefreshDifficultyButtons 渲染
    // （难度按钮在 OnRender 中统一渲染）

    // 最佳成绩 —— 从数据库查询
    {
        int diffIdx = m_selectedDifficulty;
        if (diffIdx < 0 || diffIdx >= static_cast<int>(chart.difficulties.size()))
            diffIdx = 0;

        std::string diffName = chart.difficulties.empty()
            ? "" : chart.difficulties[diffIdx].name;

        auto bestOpt = sakura::data::Database::GetInstance()
                           .GetBestScore(chart.id, diffName);

        std::string bestText;
        if (bestOpt.has_value())
        {
            const auto& best   = bestOpt.value();
            // 评级字符串
            const char* gradeStr[] = { "SS", "S", "A", "B", "C", "D" };
            int gi = static_cast<int>(best.grade);
            const char* gs = (gi >= 0 && gi <= 5) ? gradeStr[gi] : "?";
            // 分数 7 位字符串
            std::string sc = std::to_string(best.score);
            while (sc.size() < 7) sc = "0" + sc;
            std::ostringstream oss;
            oss << "Best: " << sc << "  " << gs
                << "  " << std::fixed << std::setprecision(2)
                << best.accuracy << "%";
            bestText = oss.str();
        }
        else
        {
            bestText = "Best: --  (No Record)";
        }

        renderer.DrawText(m_fontSmall, bestText,
            px, 0.720f, 0.022f,
            sakura::core::Color{ 220, 200, 140, 210 },
            sakura::core::TextAlign::Center);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneSelect::OnRender(sakura::core::Renderer& renderer)
{
    // 背景
    renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        sakura::core::Color{ 10, 8, 22, 255 });

    // 标题
    if (m_fontUI != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_fontUI, "SELECT SONG",
            0.5f, 0.027f, 0.038f,
            sakura::core::Color{ 220, 200, 255, 220 },
            sakura::core::TextAlign::Center);
    }

    // 歌曲列表
    if (m_songList) m_songList->Render(renderer);

    // 右侧详情面板
    RenderDetailPanel(renderer);

    // 难度按钮
    for (auto& btn : m_diffButtons)
        if (btn) btn->Render(renderer);

    // 底部按钮
    if (m_btnBack)  m_btnBack->Render(renderer);
    if (m_btnStart) m_btnStart->Render(renderer);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneSelect::OnEvent(const SDL_Event& event)
{
    // ESC → 返回主菜单（与"返回"按钮相同行为）
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        StopPreview();
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
        return;
    }

    if (m_songList)  m_songList->HandleEvent(event);
    if (m_btnBack)   m_btnBack->HandleEvent(event);
    if (m_btnStart)  m_btnStart->HandleEvent(event);
    for (auto& btn : m_diffButtons)
        if (btn) btn->HandleEvent(event);
}

} // namespace sakura::scene
