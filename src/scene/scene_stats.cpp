// scene_stats.cpp — 玩家统计场景实现

#include "scene_stats.h"
#include "scene_menu.h"
#include "core/theme.h"
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
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);

    const float invAspect = static_cast<float>(renderer.GetScreenHeight()) /
                            static_cast<float>(std::max(1, renderer.GetScreenWidth()));

    sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.04f, 0.026f, 0.13f, 0.026f }, "PLAYER PROFILE",
        m_fontSmall, 0.013f, { 36, 28, 56, 195 }, palette.gildedHairline, palette.accent);
    renderer.DrawText(m_fontTitle, "生涯统计 · RECORDS",
        0.04f, 0.058f, 0.038f, palette.text, sakura::core::TextAlign::Left);
    renderer.DrawText(m_fontSmall, "表现分 PP · 评级勋章 · 准确率丝带 · 荣誉成就",
        0.04f, 0.102f, 0.0185f, palette.textDim, sakura::core::TextAlign::Left);

    const sakura::core::NormRect cardTopLeft    { 0.04f, 0.14f, 0.44f, 0.30f };
    const sakura::core::NormRect cardTopRight   { 0.50f, 0.14f, 0.46f, 0.30f };
    const sakura::core::NormRect cardBottomLeft { 0.04f, 0.46f, 0.44f, 0.44f };
    const sakura::core::NormRect cardBottomRight{ 0.50f, 0.46f, 0.46f, 0.44f };

    sakura::ui::VisualStyle::DrawPanel(renderer, cardTopLeft, false, true);
    sakura::ui::VisualStyle::DrawPanel(renderer, cardTopRight, false, true);
    sakura::ui::VisualStyle::DrawPanel(renderer, cardBottomLeft, false, false);
    sakura::ui::VisualStyle::DrawPanel(renderer, cardBottomRight, false, false);

    // ── 卡片 1：生涯总览 ─────────────────────────────────────────────────────
    renderer.DrawText(m_fontBody, "生涯总览",
        0.065f, 0.158f, 0.024f, palette.accent, sakura::core::TextAlign::Left);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.065f, 0.455f, 0.194f, 0.68f);

    {
        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        renderer.DrawRadialGradient(0.135f, 0.255f, 0.09f, 0.05f,
            { palette.accent.r, palette.accent.g, palette.accent.b, 52 },
            { palette.accent.r, palette.accent.g, palette.accent.b, 0 });
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

        std::ostringstream ppSS;
        ppSS << std::fixed << std::setprecision(2) << m_stats.totalPP << " PP";
        renderer.DrawText(m_fontTitle, ppSS.str(),
            0.065f, 0.208f, 0.040f, palette.accent, sakura::core::TextAlign::Left);
        renderer.DrawText(m_fontSmall, "综合表现评分 (Performance Points)",
            0.065f, 0.256f, 0.016f, palette.textDim, sakura::core::TextAlign::Left);
    }

    const std::array<std::pair<const char*, std::string>, 6> summaryRows = {{
        { "游玩次数", std::to_string(m_stats.playCount) },
        { "累计时长", FormatDuration(m_stats.playTimeSeconds) },
        { "判定音符", std::to_string(m_stats.totalNotes) },
        { "平均准度", ([&]() { std::ostringstream ss; ss << std::fixed << std::setprecision(2) << m_stats.averageAccuracy << "%"; return ss.str(); })() },
        { "Full Combo", std::to_string(m_stats.fullComboCount) },
        { "All Perfect", std::to_string(m_stats.allPerfectCount) },
    }};

    for (int index = 0; index < static_cast<int>(summaryRows.size()); ++index)
    {
        const int col = index % 3;
        const int row = index / 3;
        const float tileX = 0.062f + static_cast<float>(col) * 0.135f;
        const float tileY = 0.292f + static_cast<float>(row) * 0.066f;
        renderer.DrawRoundedGradientRect({ tileX, tileY, 0.126f, 0.056f }, 0.008f,
            { 24, 28, 48, 138 }, { 24, 28, 48, 138 }, { 16, 20, 36, 162 }, { 16, 20, 36, 162 });
        renderer.DrawText(m_fontSmall, summaryRows[index].first,
            tileX + 0.010f, tileY + 0.006f, 0.015f, palette.textDim, sakura::core::TextAlign::Left);
        renderer.DrawText(m_fontBody, summaryRows[index].second,
            tileX + 0.010f, tileY + 0.026f, 0.0205f, palette.text, sakura::core::TextAlign::Left);
    }

    // ── 卡片 2：评级勋章分布 ─────────────────────────────────────────────────
    renderer.DrawText(m_fontBody, "评级分布",
        0.525f, 0.158f, 0.024f, palette.accent, sakura::core::TextAlign::Left);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.525f, 0.935f, 0.194f, 0.68f);

    int maxGradeCount = *std::max_element(m_stats.gradeDistribution.begin(), m_stats.gradeDistribution.end());
    maxGradeCount = std::max(maxGradeCount, 1);
    for (int index = 0; index < static_cast<int>(kGradeLabels.size()); ++index)
    {
        const float y = 0.208f + static_cast<float>(index) * 0.036f;
        const float ratio = static_cast<float>(m_stats.gradeDistribution[index]) / static_cast<float>(maxGradeCount);
        const sakura::core::Color gradeCol = palette.gradeColors[index];

        const auto emblem = sakura::ui::VisualStyle::GetRankEmblemTexture(kGradeLabels[index]);
        if (emblem != sakura::core::INVALID_HANDLE)
        {
            const float eh = 0.028f;
            const float ew = eh * invAspect;
            renderer.DrawSprite(emblem, { 0.530f, y - 0.004f, ew, eh });
        }
        renderer.DrawText(m_fontSmall, kGradeLabels[index],
            0.556f, y, 0.0175f, gradeCol, sakura::core::TextAlign::Left);

        renderer.DrawRoundedRect({ 0.600f, y + 0.002f, 0.275f, 0.015f }, 0.006f, { 18, 22, 38, 215 }, true);
        if (ratio > 0.0f)
        {
            sakura::core::Color brightCol = gradeCol;
            brightCol.a = 240;
            sakura::core::Color tailCol = gradeCol;
            tailCol.a = 175;
            renderer.DrawRoundedGradientRect(
                { 0.600f, y + 0.002f, std::max(0.012f, 0.275f * ratio), 0.015f }, 0.006f,
                tailCol, brightCol, tailCol, brightCol);
        }
        renderer.DrawText(m_fontSmall, std::to_string(m_stats.gradeDistribution[index]),
            0.930f, y, 0.018f, palette.text, sakura::core::TextAlign::Right);
    }

    // ── 卡片 3：最近 20 局准确率丝带曲线 ─────────────────────────────────────
    renderer.DrawText(m_fontBody, "最近 20 局准确率趋势",
        0.065f, 0.478f, 0.024f, palette.accent, sakura::core::TextAlign::Left);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.065f, 0.455f, 0.514f, 0.65f);

    const sakura::core::NormRect plotRect{ 0.088f, 0.540f, 0.365f, 0.315f };
    renderer.DrawRoundedGradientRect(plotRect, 0.010f,
        { 14, 18, 34, 215 }, { 14, 18, 34, 215 }, { 22, 20, 42, 230 }, { 22, 20, 42, 230 });
    renderer.DrawRoundedRectOutlineGradient(plotRect, 0.010f,
        { 150, 145, 195, 65 }, { 100, 110, 150, 35 }, 0.0012f);

    const std::array<float, 4> refAccs = { 100.0f, 95.0f, 90.0f, 80.0f };
    for (float refAcc : refAccs)
    {
        const float normY = std::clamp((refAcc - 75.0f) / 25.0f, 0.0f, 1.0f);
        const float gy = plotRect.y + plotRect.height - normY * plotRect.height;
        renderer.DrawLine(plotRect.x, gy, plotRect.x + plotRect.width, gy, { 145, 140, 185, 38 }, 0.001f);
        renderer.DrawText(m_fontSmall, std::to_string(static_cast<int>(refAcc)) + "%",
            plotRect.x - 0.006f, gy - 0.008f, 0.0135f, palette.textMuted, sakura::core::TextAlign::Right);
    }
    renderer.DrawText(m_fontSmall, "≤75%", plotRect.x - 0.006f, plotRect.y + plotRect.height - 0.010f,
        0.0135f, palette.textMuted, sakura::core::TextAlign::Right);

    if (!m_recentScores.empty())
    {
        std::vector<sakura::game::GameResult> trend = m_recentScores;
        std::reverse(trend.begin(), trend.end());
        std::vector<sakura::core::RibbonNode> nodes;
        nodes.reserve(trend.size());
        for (std::size_t idx = 0; idx < trend.size(); ++idx)
        {
            const float tx = (trend.size() == 1)
                ? (plotRect.x + plotRect.width * 0.5f)
                : (plotRect.x + 0.014f + (static_cast<float>(idx) / static_cast<float>(trend.size() - 1)) * (plotRect.width - 0.028f));
            const float normAcc = std::clamp((static_cast<float>(trend[idx].accuracy) - 75.0f) / 25.0f, 0.0f, 1.0f);
            const float ty = plotRect.y + plotRect.height - 0.012f - normAcc * (plotRect.height - 0.024f);
            nodes.push_back({
                tx, ty, 0.0042f,
                palette.accent,
                { palette.primary.r, palette.primary.g, palette.primary.b, 40 }
            });
        }
        if (nodes.size() >= 2)
            renderer.DrawRibbonStrip(nodes.data(), static_cast<int>(nodes.size()), true);
        for (const auto& nd : nodes)
        {
            renderer.DrawCircleFilled(nd.x, nd.y, 0.0042f, palette.primary, 18);
            renderer.DrawCircleFilled(nd.x, nd.y, 0.0022f, { 255, 250, 245, 245 }, 14);
        }
    }
    else
    {
        renderer.DrawText(m_fontSmall, "暂无最近游玩记录",
            plotRect.x + plotRect.width * 0.5f, plotRect.y + plotRect.height * 0.48f, 0.018f,
            palette.textMuted, sakura::core::TextAlign::Center);
    }

    // ── 卡片 4：PP 排行 Top 10 ───────────────────────────────────────────────
    renderer.DrawText(m_fontBody, "最佳表现 Top 10",
        0.525f, 0.478f, 0.024f, palette.accent, sakura::core::TextAlign::Left);
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.525f, 0.935f, 0.514f, 0.65f);

    if (m_topPPPlays.empty())
    {
        renderer.DrawText(m_fontSmall, "完成曲目后在此展示最高 PP 成绩",
            0.73f, 0.68f, 0.018f, palette.textMuted, sakura::core::TextAlign::Center);
    }
    for (int index = 0; index < std::min<int>(10, static_cast<int>(m_topPPPlays.size())); ++index)
    {
        const auto& play = m_topPPPlays[index];
        const float rowY = 0.528f + static_cast<float>(index) * 0.0345f;
        if (index % 2 == 0)
        {
            renderer.DrawRoundedRect({ 0.520f, rowY - 0.003f, 0.420f, 0.030f }, 0.006f,
                { 24, 28, 48, 105 }, true);
        }

        const sakura::core::Color rankCol = (index == 0)
            ? palette.accent
            : ((index == 1) ? sakura::core::Color{ 215, 224, 242, 240 }
            : ((index == 2) ? sakura::core::Color{ 232, 176, 138, 240 } : palette.textDim));

        renderer.DrawText(m_fontSmall, "#" + std::to_string(index + 1),
            0.528f, rowY + 0.002f, 0.0165f, rankCol, sakura::core::TextAlign::Left);

        const std::string titleWithDiff = play.result.chartTitle + " [" + play.result.difficulty + "]";
        sakura::ui::VisualStyle::DrawTextFit(renderer, m_fontSmall, titleWithDiff,
            0.566f, rowY + 0.002f, 0.0165f, 0.220f, index == 0 ? palette.accent : palette.text);

        std::ostringstream accSS;
        accSS << std::fixed << std::setprecision(2) << play.result.accuracy << "%";
        renderer.DrawText(m_fontSmall, accSS.str(),
            0.848f, rowY + 0.002f, 0.016f, palette.textDim, sakura::core::TextAlign::Right);

        std::ostringstream ppRowSS;
        ppRowSS << std::fixed << std::setprecision(1) << play.pp << " PP";
        renderer.DrawText(m_fontSmall, ppRowSS.str(),
            0.932f, rowY + 0.002f, 0.0165f, rankCol, sakura::core::TextAlign::Right);
    }

    if (m_btnBack) m_btnBack->Render(renderer);
    if (m_btnAchievements) m_btnAchievements->Render(renderer);

    if (m_showAchievements)
    {
        int unlockedCnt = 0;
        for (const auto& a : m_achievements) if (a.unlocked) ++unlockedCnt;

        sakura::ui::VisualStyle::DrawScrim(renderer, 0.66f);
        sakura::ui::VisualStyle::DrawPanel(renderer, { 0.16f, 0.15f, 0.68f, 0.66f }, true, true);
        renderer.DrawText(m_fontTitle, "荣誉成就殿堂",
            0.20f, 0.174f, 0.032f, palette.accent, sakura::core::TextAlign::Left);
        sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.43f, 0.180f, 0.18f, 0.034f },
            "已解锁 " + std::to_string(unlockedCnt) + " / " + std::to_string(m_achievements.size()),
            m_fontSmall, 0.016f, { 28, 32, 54, 210 }, palette.gildedHairline, palette.text);
        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.20f, 0.80f, 0.226f, 0.72f);
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