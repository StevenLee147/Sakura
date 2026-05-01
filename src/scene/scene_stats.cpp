// scene_stats.cpp — 玩家统计场景实现

#include "scene_stats.h"

#include "scene_menu.h"
#include "data/database.h"
#include "ui/visual_style.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace sakura::scene
{

constexpr std::array<const char*, 6> kGradeLabels = { "SS", "S", "A", "B", "C", "D" };

SceneStats::SceneStats(SceneManager& mgr)
    : m_manager(mgr)
{
}

void SceneStats::OnEnter()
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontTitle = rm.GetDefaultFontHandle();
    m_fontBody  = rm.GetDefaultFontHandle();
    m_fontSmall = rm.GetDefaultFontHandle();

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.04f, 0.93f, 0.18f, 0.05f },
        "返回", m_fontBody, 0.025f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnBack.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnBack->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
    });

    m_btnAchievements = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.78f, 0.93f, 0.18f, 0.05f },
        "成就列表", m_fontBody, 0.025f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAchievements.get(), sakura::ui::ButtonVariant::Accent);
    m_btnAchievements->SetOnClick([this]()
    {
        m_showAchievements = true;
        RefreshAchievementList();
    });

    m_btnCloseOverlay = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.66f, 0.18f, 0.16f, 0.045f },
        "关闭", m_fontBody, 0.022f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnCloseOverlay.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnCloseOverlay->SetOnClick([this]()
    {
        m_showAchievements = false;
    });

    m_achievementList = std::make_unique<sakura::ui::ScrollList>(
        sakura::core::NormRect{ 0.20f, 0.24f, 0.60f, 0.52f },
        m_fontSmall, 0.055f, 0.020f);
    sakura::ui::VisualStyle::ApplyScrollList(m_achievementList.get());

    RefreshData();
}

void SceneStats::OnExit()
{
}

void SceneStats::OnUpdate(float dt)
{
    if (m_btnBack) m_btnBack->Update(dt);
    if (m_btnAchievements) m_btnAchievements->Update(dt);

    if (m_showAchievements)
    {
        if (m_btnCloseOverlay) m_btnCloseOverlay->Update(dt);
        if (m_achievementList) m_achievementList->Update(dt);
    }
}

void SceneStats::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);

    renderer.DrawText(m_fontTitle, "玩家统计",
        0.06f, 0.04f, 0.040f, { 230, 220, 255, 240 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_fontSmall, "PP / 成绩 / 趋势 / 成就",
        0.06f, 0.085f, 0.020f, { 180, 170, 210, 220 }, sakura::core::TextAlign::Left);

    const sakura::core::NormRect cardTopLeft  { 0.04f, 0.14f, 0.42f, 0.28f };
    const sakura::core::NormRect cardTopRight { 0.52f, 0.14f, 0.42f, 0.28f };
    const sakura::core::NormRect cardBottomLeft { 0.04f, 0.46f, 0.42f, 0.36f };
    const sakura::core::NormRect cardBottomRight { 0.52f, 0.46f, 0.42f, 0.36f };

    sakura::ui::VisualStyle::DrawPanel(renderer, cardTopLeft, false, true);
    sakura::ui::VisualStyle::DrawPanel(renderer, cardTopRight, false, true);
    sakura::ui::VisualStyle::DrawPanel(renderer, cardBottomLeft);
    sakura::ui::VisualStyle::DrawPanel(renderer, cardBottomRight);

    renderer.DrawText(m_fontBody, "总览",
        0.07f, 0.16f, 0.026f, { 245, 220, 150, 240 }, sakura::core::TextAlign::Left);
    {
        std::ostringstream ppSS;
        ppSS << std::fixed << std::setprecision(2) << m_stats.totalPP;
        renderer.DrawText(m_fontTitle, ppSS.str(),
            0.07f, 0.20f, 0.044f, { 255, 235, 160, 245 }, sakura::core::TextAlign::Left);
        renderer.DrawText(m_fontSmall, "总 PP",
            0.07f, 0.248f, 0.018f, { 188, 176, 220, 220 }, sakura::core::TextAlign::Left);
    }

    const std::array<std::pair<const char*, std::string>, 6> summaryRows = {{
        { "游玩次数", std::to_string(m_stats.playCount) },
        { "总时长", FormatDuration(m_stats.playTimeSeconds) },
        { "音符数", std::to_string(m_stats.totalNotes) },
        { "平均准确率", ([&]() { std::ostringstream ss; ss << std::fixed << std::setprecision(2) << m_stats.averageAccuracy << "%"; return ss.str(); })() },
        { "FC 次数", std::to_string(m_stats.fullComboCount) },
        { "AP 次数", std::to_string(m_stats.allPerfectCount) },
    }};
    for (int index = 0; index < static_cast<int>(summaryRows.size()); ++index)
    {
        float y = 0.30f + static_cast<float>(index % 3) * 0.06f;
        float x = index < 3 ? 0.07f : 0.26f;
        renderer.DrawText(m_fontSmall, summaryRows[index].first,
            x, y, 0.018f, { 160, 155, 195, 220 }, sakura::core::TextAlign::Left);
        renderer.DrawText(m_fontBody, summaryRows[index].second,
            x, y + 0.025f, 0.023f, { 235, 235, 245, 235 }, sakura::core::TextAlign::Left);
    }

    renderer.DrawText(m_fontBody, "评级分布",
        0.55f, 0.16f, 0.026f, { 245, 220, 150, 240 }, sakura::core::TextAlign::Left);
    int maxGradeCount = *std::max_element(m_stats.gradeDistribution.begin(), m_stats.gradeDistribution.end());
    maxGradeCount = std::max(maxGradeCount, 1);
    for (int index = 0; index < static_cast<int>(kGradeLabels.size()); ++index)
    {
        float y = 0.21f + index * 0.032f;
        float ratio = static_cast<float>(m_stats.gradeDistribution[index]) / static_cast<float>(maxGradeCount);
        renderer.DrawText(m_fontSmall, kGradeLabels[index],
            0.56f, y, 0.019f, { 210, 205, 240, 220 }, sakura::core::TextAlign::Left);
        renderer.DrawFilledRect({ 0.61f, y - 0.004f, 0.26f, 0.018f }, { 30, 28, 52, 220 });
        renderer.DrawFilledRect({ 0.61f, y - 0.004f, 0.26f * ratio, 0.018f },
            { static_cast<uint8_t>(120 + index * 18), static_cast<uint8_t>(200 - index * 20), 180, 235 });
        renderer.DrawText(m_fontSmall, std::to_string(m_stats.gradeDistribution[index]),
            0.89f, y, 0.019f, { 235, 235, 245, 235 }, sakura::core::TextAlign::Left);
    }

    renderer.DrawText(m_fontBody, "最近 20 局准确率趋势",
        0.07f, 0.49f, 0.024f, { 245, 220, 150, 240 }, sakura::core::TextAlign::Left);
    renderer.DrawFilledRect({ 0.07f, 0.55f, 0.36f, 0.20f }, { 20, 20, 42, 210 });
    renderer.DrawLine(0.07f, 0.75f, 0.43f, 0.75f, { 110, 110, 155, 220 }, 0.0012f);
    renderer.DrawLine(0.07f, 0.55f, 0.07f, 0.75f, { 110, 110, 155, 220 }, 0.0012f);
    renderer.DrawText(m_fontSmall, "100%", 0.065f, 0.54f, 0.016f, { 160, 160, 190, 220 }, sakura::core::TextAlign::Right);
    renderer.DrawText(m_fontSmall, "0%", 0.065f, 0.742f, 0.016f, { 160, 160, 190, 220 }, sakura::core::TextAlign::Right);

    if (!m_recentScores.empty())
    {
        std::vector<sakura::game::GameResult> trend = m_recentScores;
        std::reverse(trend.begin(), trend.end());
        for (std::size_t index = 1; index < trend.size(); ++index)
        {
            float x1 = 0.07f + (static_cast<float>(index - 1) / static_cast<float>(std::max<std::size_t>(1, trend.size() - 1))) * 0.36f;
            float x2 = 0.07f + (static_cast<float>(index) / static_cast<float>(std::max<std::size_t>(1, trend.size() - 1))) * 0.36f;
            float y1 = 0.75f - std::clamp(trend[index - 1].accuracy / 100.0f, 0.0f, 1.0f) * 0.20f;
            float y2 = 0.75f - std::clamp(trend[index].accuracy / 100.0f, 0.0f, 1.0f) * 0.20f;
            renderer.DrawLine(x1, y1, x2, y2, { 120, 220, 255, 235 }, 0.0020f);
            renderer.DrawCircleFilled(x2, y2, 0.004f, { 255, 220, 160, 235 }, 18);
        }
    }

    renderer.DrawText(m_fontBody, "PP 排行 Top 10",
        0.55f, 0.49f, 0.024f, { 245, 220, 150, 240 }, sakura::core::TextAlign::Left);
    for (int index = 0; index < std::min<int>(10, static_cast<int>(m_topPPPlays.size())); ++index)
    {
        const auto& play = m_topPPPlays[index];
        std::ostringstream line;
        line << index + 1 << ". " << play.result.chartTitle << " [" << play.result.difficulty << "] ";
        line << std::fixed << std::setprecision(2) << play.result.accuracy << "%  ";
        line << std::fixed << std::setprecision(2) << play.pp << "PP";
        renderer.DrawText(m_fontSmall, line.str(),
            0.55f, 0.54f + index * 0.025f, 0.0175f,
            index == 0 ? sakura::core::Color{ 255, 230, 150, 240 } : sakura::core::Color{ 225, 225, 235, 230 },
            sakura::core::TextAlign::Left);
    }

    if (m_btnBack) m_btnBack->Render(renderer);
    if (m_btnAchievements) m_btnAchievements->Render(renderer);

    if (m_showAchievements)
    {
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.62f);
        sakura::ui::VisualStyle::DrawPanel(renderer, { 0.16f, 0.16f, 0.68f, 0.64f }, true, true);
        renderer.DrawText(m_fontTitle, "成就列表",
            0.20f, 0.18f, 0.032f, { 255, 232, 165, 245 }, sakura::core::TextAlign::Left);
        if (m_btnCloseOverlay) m_btnCloseOverlay->Render(renderer);
        if (m_achievementList) m_achievementList->Render(renderer);
    }
}

void SceneStats::OnEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        if (m_showAchievements)
        {
            m_showAchievements = false;
            return;
        }

        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
        return;
    }

    if (m_showAchievements)
    {
        if (m_btnCloseOverlay) m_btnCloseOverlay->HandleEvent(event);
        if (m_achievementList) m_achievementList->HandleEvent(event);
        return;
    }

    if (m_btnBack) m_btnBack->HandleEvent(event);
    if (m_btnAchievements) m_btnAchievements->HandleEvent(event);
}

void SceneStats::RefreshData()
{
    auto& db = sakura::data::Database::GetInstance();

    sakura::game::PPCalculator calculator;
    calculator.RecalculateTotal(db.GetAllBestScores());

    m_stats.totalPP = calculator.GetTotalPP();
    m_stats.playCount = db.GetTotalPlayCount();
    m_stats.playTimeSeconds = db.GetTotalPlayTimeSeconds();
    m_stats.totalNotes = db.GetTotalNotesJudged();
    m_stats.averageAccuracy = db.GetAverageAccuracy();
    m_stats.fullComboCount = db.GetFullComboCount();
    m_stats.allPerfectCount = db.GetAllPerfectCount();
    m_stats.gradeDistribution = db.GetGradeDistribution();

    m_recentScores = db.GetRecentScores(20);
    m_topPPPlays = calculator.GetBestPlays(10);
    m_achievements = sakura::game::AchievementManager::GetInstance().GetAll();
    RefreshAchievementList();
}

void SceneStats::RefreshAchievementList()
{
    if (!m_achievementList)
        return;

    std::vector<std::string> items;
    items.reserve(m_achievements.size());

    for (const auto& achievement : m_achievements)
    {
        std::ostringstream line;
        line << (achievement.unlocked ? "[已解锁] " : "[进行中] ")
             << achievement.definition.title << " - "
             << achievement.definition.description << " ("
             << achievement.current << "/" << achievement.target << ")";
        items.push_back(line.str());
    }

    m_achievementList->SetItems(items);
}

std::string SceneStats::FormatDuration(double seconds)
{
    long long totalSeconds = static_cast<long long>(std::round(std::max(0.0, seconds)));
    long long hours = totalSeconds / 3600;
    long long minutes = (totalSeconds % 3600) / 60;
    long long secs = totalSeconds % 60;

    std::ostringstream ss;
    if (hours > 0)
        ss << hours << "h ";
    ss << std::setw(2) << std::setfill('0') << minutes << "m ";
    ss << std::setw(2) << std::setfill('0') << secs << "s";
    return ss.str();
}

} // namespace sakura::scene