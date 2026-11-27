// scene_stats.cpp — 玩家统计场景实现

#include "scene_stats.h"
#include "scene_menu.h"
#include "core/paths.h"
#include "core/theme.h"
#include "data/database.h"
#include "game/chart_utils.h"
#include "net/api_client.h"
#include "ui/toast.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <SDL3_image/SDL_image.h>

#if __has_include("version.h")
#  include "version.h"
#else
#  define SAKURA_VERSION_STRING "0.0.0"
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace sakura::scene
{

constexpr std::array<const char*, 6> kGradeLabels = { "SS", "S", "A", "B", "C", "D" };

namespace
{
sakura::core::Renderer* g_activeStatsRenderer = nullptr;
std::string             g_pendingExportPath;

constexpr sakura::core::NormRect kCloseBtnAchievementsRect{ 0.66f, 0.18f, 0.16f, 0.045f };
constexpr sakura::core::NormRect kCloseBtnB30Rect{ 0.856f, 0.044f, 0.096f, 0.038f };
constexpr sakura::core::NormRect kB30ModalPanelRect{ 0.028f, 0.032f, 0.944f, 0.882f };

int GradeIndex(sakura::game::Grade grade)
{
    switch (grade)
    {
        case sakura::game::Grade::SS: return 0;
        case sakura::game::Grade::S:  return 1;
        case sakura::game::Grade::A:  return 2;
        case sakura::game::Grade::B:  return 3;
        case sakura::game::Grade::C:  return 4;
        case sakura::game::Grade::D:
        default:                      return 5;
    }
}

std::string TruncateUtf8(std::string_view text, std::size_t maxCodepoints)
{
    if (text.empty() || maxCodepoints == 0)
        return {};

    std::size_t cpCount = 0;
    std::size_t cutByte = text.size();
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if ((byte & 0xC0u) != 0x80u)
        {
            if (cpCount == maxCodepoints)
            {
                cutByte = i;
                break;
            }
            ++cpCount;
        }
    }
    if (cutByte == text.size())
        return std::string(text);
    return std::string(text.substr(0, cutByte)) + "…";
}
} // namespace

SceneStats::SceneStats(SceneManager& mgr)
    : m_manager(mgr)
{
}

void SceneStats::OnEnter()
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    m_fontTitle = rm.GetBoldFontHandle();
    m_fontBody  = rm.GetDefaultFontHandle();
    m_fontSmall = rm.GetDefaultFontHandle();

    m_showAchievements = false;
    m_showB30Overlay   = false;
    m_topBar = std::make_unique<sakura::ui::TopBar>("CAREER STATS  ·  生涯统计", m_fontBody,
        [this]() { m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SlideRight, 0.4f); });

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

    m_btnToggleB30Overlay = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.34f, 0.93f, 0.20f, 0.05f },
        "B30 潜力矩阵", m_fontBody, 0.024f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnToggleB30Overlay.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnToggleB30Overlay->SetOnClick([this]()
    {
        m_showB30Overlay = !m_showB30Overlay;
        m_showAchievements = false;
    });

    m_btnExportB30 = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.56f, 0.93f, 0.20f, 0.05f },
        "导出 B30 名片", m_fontBody, 0.024f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnExportB30.get(), sakura::ui::ButtonVariant::Primary);
    m_btnExportB30->SetOnClick([this]()
    {
        ExportCareerCardPng();
    });

    m_btnAchievements = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.78f, 0.93f, 0.18f, 0.05f },
        "成就列表", m_fontBody, 0.025f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnAchievements.get(), sakura::ui::ButtonVariant::Accent);
    m_btnAchievements->SetOnClick([this]()
    {
        m_showAchievements = true;
        m_showB30Overlay   = false;
        RefreshAchievementList();
    });

    m_btnCloseOverlay = std::make_unique<sakura::ui::Button>(
        kCloseBtnAchievementsRect,
        "关闭", m_fontBody, 0.022f, 0.010f);
    sakura::ui::VisualStyle::ApplyButton(m_btnCloseOverlay.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnCloseOverlay->SetOnClick([this]()
    {
        m_showAchievements = false;
        m_showB30Overlay   = false;
    });

    m_achievementList = std::make_unique<sakura::ui::ScrollList>(
        sakura::core::NormRect{ 0.20f, 0.24f, 0.60f, 0.52f },
        m_fontSmall, 0.055f, 0.020f);
    sakura::ui::VisualStyle::ApplyScrollList(m_achievementList.get());

    RefreshData();
}

void SceneStats::OnExit()
{
    g_activeStatsRenderer = nullptr;
    g_pendingExportPath.clear();
    m_topBar.reset();
}

void SceneStats::OnUpdate(float dt)
{
    if (m_topBar) m_topBar->Update(dt);
    if (m_btnBack) m_btnBack->Update(dt);
    if (m_btnToggleB30Overlay) m_btnToggleB30Overlay->Update(dt);
    if (m_btnExportB30) m_btnExportB30->Update(dt);
    if (m_btnAchievements) m_btnAchievements->Update(dt);

    if (m_showB30Overlay)
    {
        if (m_btnCloseOverlay)
        {
            m_btnCloseOverlay->SetBounds(kCloseBtnB30Rect);
            m_btnCloseOverlay->Update(dt);
        }
    }
    else if (m_showAchievements)
    {
        if (m_btnCloseOverlay)
        {
            m_btnCloseOverlay->SetBounds(kCloseBtnAchievementsRect);
            m_btnCloseOverlay->Update(dt);
        }
        if (m_achievementList) m_achievementList->Update(dt);
    }
}

void SceneStats::OnRender(sakura::core::Renderer& renderer)
{
    g_activeStatsRenderer = &renderer;
    if (!g_pendingExportPath.empty())
    {
        const std::string pending = std::move(g_pendingExportPath);
        g_pendingExportPath.clear();
        ExportCareerCardPng(pending);
    }

    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);

    const float invAspect = static_cast<float>(renderer.GetScreenHeight()) /
                            static_cast<float>(std::max(1, renderer.GetScreenWidth()));

    sakura::ui::VisualStyle::DrawBadgePill(renderer, { 0.04f, 0.026f, 0.13f, 0.026f }, "PLAYER PROFILE",
        m_fontSmall, 0.013f, { 36, 28, 56, 195 }, palette.gildedHairline, palette.accent);
    renderer.DrawText(m_fontTitle, "生涯统计 · RECORDS",
        0.04f, 0.058f, 0.038f, palette.text, sakura::core::TextAlign::Left);
    renderer.DrawText(m_fontSmall, "Sakura Rating 潜力星阶 · 表现分 PP · B30 矩阵 · 荣誉成就",
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
        0.065f, 0.156f, 0.024f, palette.accent, sakura::core::TextAlign::Left);
    if (!m_danBadge.empty())
    {
        sakura::ui::VisualStyle::DrawBadgePill(
            renderer,
            { 0.345f, 0.153f, 0.110f, 0.029f },
            "[" + m_danBadge + "]",
            m_fontSmall, 0.0145f,
            { 54, 38, 70, 224 }, palette.gildedHairline, palette.accent);
    }
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.065f, 0.455f, 0.194f, 0.68f);

    {
        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        renderer.DrawRadialGradient(0.145f, 0.242f, 0.10f, 0.052f,
            { palette.accent.r, palette.accent.g, palette.accent.b, 52 },
            { palette.accent.r, palette.accent.g, palette.accent.b, 0 });
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

        const std::string ratingText = "★ "
            + sakura::game::PPCalculator::FormatRating(m_ratingSummary.sakuraRating)
            + "  " + m_ratingSummary.tierName;
        sakura::ui::VisualStyle::DrawTextFit(
            renderer, m_fontTitle, ratingText,
            0.065f, 0.204f, 0.0285f, 0.252f, palette.accent);

        const std::string b30R10Text = "B30 "
            + sakura::game::PPCalculator::FormatRating(m_ratingSummary.b30Avg)
            + " / R10 "
            + sakura::game::PPCalculator::FormatRating(m_ratingSummary.r10Avg);
        renderer.DrawText(m_fontSmall, b30R10Text,
            0.065f, 0.248f, 0.0165f, palette.primary, sakura::core::TextAlign::Left);

        std::ostringstream ppSS;
        ppSS << std::fixed << std::setprecision(2) << m_stats.totalPP << " PP";
        renderer.DrawText(m_fontTitle, ppSS.str(),
            0.455f, 0.206f, 0.027f, palette.text, sakura::core::TextAlign::Right);
        renderer.DrawText(m_fontSmall, "综合表现 PP",
            0.455f, 0.248f, 0.015f, palette.textDim, sakura::core::TextAlign::Right);
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
    if (m_btnToggleB30Overlay) m_btnToggleB30Overlay->Render(renderer);
    if (m_btnExportB30) m_btnExportB30->Render(renderer);
    if (m_btnAchievements) m_btnAchievements->Render(renderer);

    if (m_showB30Overlay)
    {
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.72f);
        sakura::ui::VisualStyle::DrawPanel(renderer, kB30ModalPanelRect, true, true);
        RenderB30MatrixContent(renderer, false);
        if (m_btnCloseOverlay)
        {
            m_btnCloseOverlay->SetBounds(kCloseBtnB30Rect);
            m_btnCloseOverlay->Render(renderer);
        }
        if (m_btnToggleB30Overlay) m_btnToggleB30Overlay->Render(renderer);
        if (m_btnExportB30) m_btnExportB30->Render(renderer);
    }
    else if (m_showAchievements)
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
        if (m_btnCloseOverlay)
        {
            m_btnCloseOverlay->SetBounds(kCloseBtnAchievementsRect);
            m_btnCloseOverlay->Render(renderer);
        }
        if (m_achievementList) m_achievementList->Render(renderer);
    }
    if (m_topBar && !m_showB30Overlay)
    {
        m_topBar->Render(renderer);
        m_topBar->RenderDrawer(renderer);
    }
}

void SceneStats::OnEvent(const SDL_Event& event)
{
    if (!m_showB30Overlay && !m_showAchievements && m_topBar && m_topBar->HandleEvent(event))
        return;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        if (m_showB30Overlay || m_showAchievements)
        {
            m_showB30Overlay   = false;
            m_showAchievements = false;
            return;
        }

        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
        return;
    }

    if (m_showB30Overlay)
    {
        if (m_btnCloseOverlay)
        {
            m_btnCloseOverlay->SetBounds(kCloseBtnB30Rect);
            if (m_btnCloseOverlay->HandleEvent(event)) return;
        }
        if (m_btnToggleB30Overlay && m_btnToggleB30Overlay->HandleEvent(event)) return;
        if (m_btnExportB30 && m_btnExportB30->HandleEvent(event)) return;
        return;
    }

    if (m_showAchievements)
    {
        if (m_btnCloseOverlay)
        {
            m_btnCloseOverlay->SetBounds(kCloseBtnAchievementsRect);
            m_btnCloseOverlay->HandleEvent(event);
        }
        if (m_achievementList) m_achievementList->HandleEvent(event);
        return;
    }

    if (m_btnBack) m_btnBack->HandleEvent(event);
    if (m_btnToggleB30Overlay) m_btnToggleB30Overlay->HandleEvent(event);
    if (m_btnExportB30) m_btnExportB30->HandleEvent(event);
    if (m_btnAchievements) m_btnAchievements->HandleEvent(event);
}

void SceneStats::RefreshData()
{
    auto& db = sakura::data::Database::GetInstance();

    const auto allBestScores = db.GetAllBestScores();
    sakura::game::PPCalculator calculator;
    calculator.RecalculateTotal(allBestScores);

    m_stats.totalPP = calculator.GetTotalPP();
    m_stats.playCount = db.GetTotalPlayCount();
    m_stats.playTimeSeconds = db.GetTotalPlayTimeSeconds();
    m_stats.totalNotes = db.GetTotalNotesJudged();
    m_stats.averageAccuracy = db.GetAverageAccuracy();
    m_stats.fullComboCount = db.GetFullComboCount();
    m_stats.allPerfectCount = db.GetAllPerfectCount();
    m_stats.gradeDistribution = db.GetGradeDistribution();

    m_recentScores = db.GetRecentScores(20);
    m_ratingSummary = sakura::game::PPCalculator::CalculateSakuraRating(allBestScores, m_recentScores);
    const auto highestDan = db.GetHighestClearedCourse();
    m_danBadge = (highestDan && highestDan->cleared)
        ? highestDan->badgeText
        : sakura::net::APIClient::GetInstance().GetCurrentUser().danBadge;

    m_topPPPlays = calculator.GetBestPlays(10);
    m_achievements = sakura::game::AchievementManager::GetInstance().GetAll();
    RefreshAchievementList();
}

void SceneStats::RenderB30MatrixContent(sakura::core::Renderer& renderer, bool exportCardMode)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    const float invAspect = static_cast<float>(renderer.GetScreenHeight()) /
                            static_cast<float>(std::max(1, renderer.GetScreenWidth()));

    if (exportCardMode)
    {
        sakura::ui::VisualStyle::DrawSceneBackground(renderer);
        sakura::ui::VisualStyle::DrawSakuraLandscape(renderer, 1.35f, 0.45f);
        sakura::ui::VisualStyle::DrawGlassCard(
            renderer,
            { 0.018f, 0.020f, 0.964f, 0.960f },
            { 16, 20, 38, 224 }, { 10, 14, 28, 240 },
            { 250, 216, 157, 178 }, { 246, 172, 200, 86 },
            0.016f, 0.50f);
    }
    else
    {
        sakura::ui::VisualStyle::DrawBadgePill(
            renderer,
            { 0.044f, 0.046f, 0.145f, 0.028f },
            "POTENTIAL MATRIX",
            m_fontSmall, 0.013f,
            { 42, 30, 62, 210 }, palette.gildedHairline, palette.accent);
        renderer.DrawText(
            m_fontTitle, "B30 潜力矩阵 · 生涯名片预览",
            0.200f, 0.044f, 0.026f, palette.text, sakura::core::TextAlign::Left);
    }

    const float x0    = exportCardMode ? 0.036f : 0.044f;
    const float w     = exportCardMode ? 0.928f : 0.912f;
    const float hdrY  = exportCardMode ? 0.038f : 0.088f;
    const float hdrH  = exportCardMode ? 0.132f : 0.116f;
    const float gridY = exportCardMode ? 0.190f : 0.220f;
    const float gridH = exportCardMode ? 0.742f : 0.678f;

    // ── 顶部月夜墨樱玩家铭牌横幅 ─────────────────────────────────────────────
    sakura::ui::VisualStyle::DrawGlassCard(
        renderer,
        { x0, hdrY, w, hdrH },
        { 30, 26, 56, 232 }, { 16, 20, 40, 242 },
        { 250, 216, 157, 192 }, { 246, 178, 206, 98 },
        0.014f, 0.38f);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient(
        x0 + 0.14f, hdrY + hdrH * 0.5f, 0.16f, hdrH * 0.75f,
        { palette.primary.r, palette.primary.g, palette.primary.b, 42 },
        { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    float textStartX = x0 + 0.020f;
    const auto crestTex = sakura::ui::VisualStyle::GetSakuraCrestTexture();
    if (crestTex != sakura::core::INVALID_HANDLE)
    {
        const float ch = hdrH * 0.72f;
        const float cw = ch * invAspect;
        renderer.DrawSprite(
            crestTex,
            { x0 + 0.016f, hdrY + (hdrH - ch) * 0.5f, cw, ch },
            0.0f, sakura::core::Color::White, 0.95f);
        textStartX = x0 + 0.024f + cw;
    }

    auto& api = sakura::net::APIClient::GetInstance();
    const auto user = api.GetCurrentUser();
    const std::string username = (api.IsAuthenticated() && !user.username.empty())
        ? user.username
        : "Sakura Player";

    sakura::ui::VisualStyle::DrawBadgePill(
        renderer,
        { textStartX, hdrY + hdrH * 0.10f, 0.148f, hdrH * 0.20f },
        "SAKURA CAREER CARD",
        m_fontSmall, hdrH * 0.105f,
        { 48, 34, 68, 215 }, palette.gildedHairline, palette.accent);

    if (!m_danBadge.empty())
    {
        sakura::ui::VisualStyle::DrawBadgePill(
            renderer,
            { textStartX + 0.156f, hdrY + hdrH * 0.10f, 0.096f, hdrH * 0.20f },
            "[" + m_danBadge + "]",
            m_fontSmall, hdrH * 0.110f,
            { 68, 42, 76, 232 }, palette.accent, palette.accent);
    }

    sakura::ui::VisualStyle::DrawTextFit(
        renderer, m_fontTitle, username,
        textStartX, hdrY + hdrH * 0.34f, hdrH * 0.27f, 0.28f, palette.text);

    const std::string ratingBanner = "★ Sakura Rating "
        + sakura::game::PPCalculator::FormatRating(m_ratingSummary.sakuraRating)
        + "   ·   " + m_ratingSummary.tierName;
    sakura::ui::VisualStyle::DrawTextFit(
        renderer, m_fontBody, ratingBanner,
        textStartX, hdrY + hdrH * 0.68f, hdrH * 0.185f, 0.35f, palette.accent);

    // 右侧 6 项生涯核心指标矩阵 (3 列 × 2 行)
    std::ostringstream totalPpSS;
    totalPpSS << std::fixed << std::setprecision(1) << m_stats.totalPP;

    const std::array<std::pair<const char*, std::string>, 6> kpis = {{
        { "B30 Avg",    sakura::game::PPCalculator::FormatRating(m_ratingSummary.b30Avg) },
        { "R10 Avg",    sakura::game::PPCalculator::FormatRating(m_ratingSummary.r10Avg) },
        { "Total PP",   totalPpSS.str() },
        { "Play Count", std::to_string(m_stats.playCount) },
        { "FC",         std::to_string(m_stats.fullComboCount) },
        { "AP",         std::to_string(m_stats.allPerfectCount) },
    }};

    const float kpiAreaX = x0 + w * 0.47f;
    const float kpiAreaW = w * 0.512f;
    const float kpiGapX  = 0.008f;
    const float kpiGapY  = hdrH * 0.08f;
    const float kpiW     = (kpiAreaW - 2.0f * kpiGapX) / 3.0f;
    const float kpiH     = (hdrH * 0.78f - kpiGapY) * 0.5f;
    const float kpiY0    = hdrY + hdrH * 0.11f;

    for (int i = 0; i < 6; ++i)
    {
        const int kc = i % 3;
        const int kr = i / 3;
        const float kx = kpiAreaX + static_cast<float>(kc) * (kpiW + kpiGapX);
        const float ky = kpiY0 + static_cast<float>(kr) * (kpiH + kpiGapY);
        renderer.DrawRoundedGradientRect(
            { kx, ky, kpiW, kpiH }, 0.007f,
            { 20, 25, 46, 192 }, { 24, 28, 52, 192 },
            { 14, 18, 34, 212 }, { 16, 20, 38, 212 });
        renderer.DrawRoundedRectOutlineGradient(
            { kx, ky, kpiW, kpiH }, 0.007f,
            { 250, 216, 157, 86 }, { 148, 158, 204, 42 }, 0.001f);
        renderer.DrawText(
            m_fontSmall, kpis[i].first,
            kx + 0.009f, ky + kpiH * 0.22f, kpiH * 0.40f, palette.textDim, sakura::core::TextAlign::Left);
        renderer.DrawText(
            m_fontTitle, kpis[i].second,
            kx + kpiW - 0.009f, ky + kpiH * 0.18f, kpiH * 0.48f,
            i < 3 ? palette.accent : palette.text, sakura::core::TextAlign::Right);
    }

    sakura::ui::VisualStyle::DrawGildedDivider(
        renderer, x0, x0 + w, hdrY + hdrH + (gridY - (hdrY + hdrH)) * 0.5f, 0.72f);

    // ── 5 列 × 6 行 Best 30 潜力谱面网格 ────────────────────────────────────
    constexpr int kCols = 5;
    constexpr int kRows = 6;
    const float colGap = exportCardMode ? 0.0105f : 0.0095f;
    const float rowGap = exportCardMode ? 0.0095f : 0.0080f;
    const float tileW  = (w - static_cast<float>(kCols - 1) * colGap) / static_cast<float>(kCols);
    const float tileH  = (gridH - static_cast<float>(kRows - 1) * rowGap) / static_cast<float>(kRows);

    for (int slot = 0; slot < 30; ++slot)
    {
        const int col = slot % kCols;
        const int row = slot / kCols;
        const float tx = x0 + static_cast<float>(col) * (tileW + colGap);
        const float ty = gridY + static_cast<float>(row) * (tileH + rowGap);
        const sakura::core::NormRect tileRect{ tx, ty, tileW, tileH };

        if (slot < static_cast<int>(m_ratingSummary.best30.size()))
        {
            const auto& entry = m_ratingSummary.best30[slot];
            const auto& res   = entry.result;
            const bool isTop3 = (slot < 3);
            const bool isTop10 = (slot < 10);

            const sakura::core::Color topFill = isTop3
                ? sakura::core::Color{ 46, 32, 66, 228 }
                : (isTop10 ? sakura::core::Color{ 28, 32, 58, 222 } : sakura::core::Color{ 20, 24, 44, 214 });
            const sakura::core::Color botFill = isTop3
                ? sakura::core::Color{ 26, 20, 44, 238 }
                : sakura::core::Color{ 13, 17, 32, 232 };
            const sakura::core::Color topBorder = isTop3
                ? sakura::core::Color{ 250, 216, 157, 205 }
                : (isTop10 ? sakura::core::Color{ 246, 178, 206, 138 } : sakura::core::Color{ 148, 158, 202, 76 });

            renderer.DrawRoundedGradientRect(tileRect, 0.008f, topFill, topFill, botFill, botFill);
            renderer.DrawRoundedRectOutlineGradient(
                tileRect, 0.008f, topBorder, { 96, 108, 154, 40 }, 0.0011f);

            // #Rank 与单曲 Rating 胶囊
            const sakura::core::Color rankCol = (slot == 0)
                ? palette.accent
                : (slot == 1 ? sakura::core::Color{ 222, 230, 246, 245 }
                : (slot == 2 ? sakura::core::Color{ 236, 182, 144, 245 }
                : (isTop10 ? palette.primary : palette.textDim)));
            renderer.DrawText(
                m_fontTitle, "#" + std::to_string(slot + 1),
                tx + 0.008f, ty + tileH * 0.07f, tileH * 0.175f, rankCol, sakura::core::TextAlign::Left);

            const std::string rtText = "Rt " + sakura::game::PPCalculator::FormatRating(entry.rating);
            sakura::ui::VisualStyle::DrawBadgePill(
                renderer,
                { tx + tileW - 0.074f, ty + tileH * 0.06f, 0.066f, tileH * 0.22f },
                rtText, m_fontSmall, tileH * 0.125f,
                isTop3 ? sakura::core::Color{ 68, 44, 76, 228 } : sakura::core::Color{ 28, 34, 58, 215 },
                isTop3 ? palette.accent : palette.primary,
                isTop3 ? palette.accent : palette.text);

            // 曲名（UTF-8 安全截断 + 自适应宽度）
            const std::string rawTitle = res.chartTitle.empty() ? res.chartId : res.chartTitle;
            const std::string displayTitle = TruncateUtf8(rawTitle, 22);
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontBody, displayTitle,
                tx + 0.008f, ty + tileH * 0.31f, tileH * 0.180f, tileW - 0.016f, palette.text);

            // 难度徽标 + 评级彩色胶囊
            const std::string diffBadge = sakura::game::FormatDifficultyBadge(
                res.difficulty.empty() ? "Normal" : res.difficulty, res.difficultyLevel);
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSmall, diffBadge,
                tx + 0.008f, ty + tileH * 0.55f, tileH * 0.140f, tileW - 0.052f, palette.primary);

            const int gIdx = GradeIndex(res.grade);
            const sakura::core::Color gradeCol = palette.gradeColors[gIdx];
            sakura::ui::VisualStyle::DrawBadgePill(
                renderer,
                { tx + tileW - 0.040f, ty + tileH * 0.52f, 0.032f, tileH * 0.21f },
                kGradeLabels[gIdx], m_fontSmall, tileH * 0.125f,
                { static_cast<uint8_t>(gradeCol.r / 4), static_cast<uint8_t>(gradeCol.g / 4), static_cast<uint8_t>(gradeCol.b / 4), 220 },
                gradeCol, gradeCol);

            // 分数 + 准确率%
            std::string scoreLine = std::to_string(res.score);
            if (res.isAllPerfect) scoreLine += " · AP";
            else if (res.isFullCombo) scoreLine += " · FC";
            sakura::ui::VisualStyle::DrawTextFit(
                renderer, m_fontSmall, scoreLine,
                tx + 0.008f, ty + tileH * 0.77f, tileH * 0.142f, tileW * 0.56f, palette.textDim);

            std::ostringstream accSS;
            accSS << std::fixed << std::setprecision(2) << res.accuracy << "%";
            renderer.DrawText(
                m_fontSmall, accSS.str(),
                tx + tileW - 0.008f, ty + tileH * 0.77f, tileH * 0.148f,
                palette.accent, sakura::core::TextAlign::Right);
        }
        else
        {
            renderer.DrawRoundedGradientRect(
                tileRect, 0.008f,
                { 16, 20, 36, 132 }, { 16, 20, 36, 132 },
                { 11, 14, 26, 152 }, { 11, 14, 26, 152 });
            renderer.DrawRoundedRectOutlineGradient(
                tileRect, 0.008f,
                { 118, 126, 168, 42 }, { 82, 90, 128, 24 }, 0.0009f);
            renderer.DrawText(
                m_fontSmall, "#" + std::to_string(slot + 1),
                tx + 0.008f, ty + tileH * 0.08f, tileH * 0.155f, palette.textMuted, sakura::core::TextAlign::Left);
            renderer.DrawText(
                m_fontSmall, "— 待解锁谱面 —",
                tx + tileW * 0.5f, ty + tileH * 0.45f, tileH * 0.150f,
                palette.textMuted, sakura::core::TextAlign::Center);
        }
    }

    if (exportCardMode)
    {
        renderer.DrawText(
            m_fontSmall, "SAKURA-樱  ·  MOONLIT INK-SAKURA B30 CAREER PROFILE",
            x0, 0.946f, 0.0145f, palette.textDim, sakura::core::TextAlign::Left);
        renderer.DrawText(
            m_fontSmall, std::string("v") + SAKURA_VERSION_STRING + "  ·  4K + MOUSE HYBRID RHYTHM",
            x0 + w, 0.946f, 0.0145f, palette.accent, sakura::core::TextAlign::Right);
    }
}

bool SceneStats::ExportCareerCardPng(const std::string& outputPath)
{
    const std::string outFile = outputPath.empty()
        ? sakura::core::Paths::User("exports/sakura_b30_card.png")
        : outputPath;

    std::error_code ec;
    const std::filesystem::path outPath(outFile);
    if (outPath.has_parent_path())
        std::filesystem::create_directories(outPath.parent_path(), ec);

    if (!g_activeStatsRenderer || !g_activeStatsRenderer->IsValid())
    {
        g_pendingExportPath = outFile;
        LOG_WARN("[SceneStats] Renderer not yet bound; queued B30 card export to {}", outFile);
        return false;
    }

    sakura::core::Renderer& renderer = *g_activeStatsRenderer;
    SDL_Renderer* sdlRen = renderer.GetSDLRenderer();
    if (!sdlRen)
    {
        sakura::ui::ToastManager::Instance().Show("导出失败：渲染器不可用", sakura::ui::ToastType::Error, 3.5f);
        return false;
    }

    constexpr int kCardWidth  = 1920;
    constexpr int kCardHeight = 1080;

    SDL_Texture* prevTarget = SDL_GetRenderTarget(sdlRen);
    const bool hadViewport = SDL_RenderViewportSet(sdlRen);
    SDL_Rect prevViewport{};
    if (hadViewport)
        SDL_GetRenderViewport(sdlRen, &prevViewport);
    const bool hadClip = SDL_RenderClipEnabled(sdlRen);
    SDL_Rect prevClip{};
    if (hadClip)
        SDL_GetRenderClipRect(sdlRen, &prevClip);

    SDL_Texture* targetTex = SDL_CreateTexture(
        sdlRen, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kCardWidth, kCardHeight);
    if (!targetTex)
    {
        LOG_ERROR("[SceneStats] SDL_CreateTexture 1920x1080 failed: {}", SDL_GetError());
        sakura::ui::ToastManager::Instance().Show("导出失败：无法创建离线画布", sakura::ui::ToastType::Error, 3.5f);
        return false;
    }
    SDL_SetTextureBlendMode(targetTex, SDL_BLENDMODE_BLEND);

    if (!SDL_SetRenderTarget(sdlRen, targetTex))
    {
        LOG_ERROR("[SceneStats] SDL_SetRenderTarget failed: {}", SDL_GetError());
        SDL_DestroyTexture(targetTex);
        sakura::ui::ToastManager::Instance().Show("导出失败：无法切换渲染目标", sakura::ui::ToastType::Error, 3.5f);
        return false;
    }

    SDL_SetRenderViewport(sdlRen, nullptr);
    SDL_SetRenderClipRect(sdlRen, nullptr);
    renderer.BeginFrame();
    renderer.Clear({ 10, 14, 26, 255 });
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    RenderB30MatrixContent(renderer, true);
    renderer.Flush();

    SDL_Surface* surface = SDL_RenderReadPixels(sdlRen, nullptr);

    SDL_SetRenderTarget(sdlRen, prevTarget);
    if (hadViewport)
        SDL_SetRenderViewport(sdlRen, &prevViewport);
    else
        SDL_SetRenderViewport(sdlRen, nullptr);
    if (hadClip)
        SDL_SetRenderClipRect(sdlRen, &prevClip);
    else
        SDL_SetRenderClipRect(sdlRen, nullptr);
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    renderer.BeginFrame();

    SDL_DestroyTexture(targetTex);

    if (!surface)
    {
        LOG_ERROR("[SceneStats] SDL_RenderReadPixels failed: {}", SDL_GetError());
        sakura::ui::ToastManager::Instance().Show("导出失败：无法读取画面像素", sakura::ui::ToastType::Error, 3.5f);
        return false;
    }

    const bool saved = IMG_SavePNG(surface, outFile.c_str());
    SDL_DestroySurface(surface);

    if (saved)
    {
        LOG_INFO("[SceneStats] 已导出 1920x1080 B30 生涯名片: {}", outFile);
        sakura::ui::ToastManager::Instance().Show(
            "已导出高清 B30 名片：" + outFile,
            sakura::ui::ToastType::Success, 4.0f);
    }
    else
    {
        LOG_ERROR("[SceneStats] IMG_SavePNG failed for {}: {}", outFile, SDL_GetError());
        sakura::ui::ToastManager::Instance().Show(
            "导出 B30 名片失败，请检查目录写入权限",
            sakura::ui::ToastType::Error, 3.8f);
    }
    return saved;
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