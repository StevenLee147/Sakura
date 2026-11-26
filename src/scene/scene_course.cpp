// scene_course.cpp — 樱华道场 · 段位认定场景实现 (v1.4.0)

#include "scene_course.h"
#include "scene_game.h"
#include "scene_menu.h"
#include "audio/audio_manager.h"
#include "core/input.h"
#include "core/paths.h"
#include "data/database.h"
#include "game/chart_loader.h"
#include "game/chart_utils.h"
#include "ui/toast.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace sakura::scene
{

using namespace sakura::core;
using namespace sakura::ui;
using namespace sakura::game;

namespace
{
constexpr Color kPink { 246, 178, 206, 255 };
constexpr Color kGold { 250, 216, 157, 255 };
constexpr Color kCyan { 138, 218, 248, 255 };
constexpr Color kMint { 152, 236, 194, 255 };
constexpr Color kCoral{ 255, 134, 150, 255 };
constexpr Color kDim  { 168, 178, 204, 255 };
constexpr Color kWhite{ 248, 242, 248, 255 };

constexpr int   VISIBLE_COURSE_ROWS = 6;
constexpr float COURSE_CARD_X       = 0.04f;
constexpr float COURSE_CARD_Y0      = 0.20f;
constexpr float COURSE_CARD_W       = 0.42f;
constexpr float COURSE_CARD_H       = 0.098f;
constexpr float COURSE_CARD_STRIDE  = 0.112f;

std::string FmtFloat(double val, int prec = 1)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << val;
    return oss.str();
}

std::string FormatScore(int score)
{
    const bool neg = score < 0;
    const long long absVal = neg ? -static_cast<long long>(score) : static_cast<long long>(score);
    const std::string raw = std::to_string(absVal);
    std::string out;
    out.reserve(raw.size() + raw.size() / 3 + 1);
    int count = 0;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it)
    {
        if (count > 0 && count % 3 == 0)
            out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    if (neg)
        out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

const ChartInfo* FindChartById(const std::vector<ChartInfo>& charts, std::string_view chartId)
{
    for (const auto& c : charts)
    {
        if (c.id == chartId)
            return &c;
    }
    return nullptr;
}
} // namespace

SceneCourse::SceneCourse(SceneManager& mgr,
                         std::optional<CourseRecord> finishedSummary)
    : m_manager(mgr)
    , m_finishedSummary(std::move(finishedSummary))
    , m_showSummaryModal(m_finishedSummary.has_value())
{
    if (m_finishedSummary.has_value() && !m_finishedSummary->courseId.empty())
    {
        const auto& courses = GetOfficialCourses();
        for (int i = 0; i < static_cast<int>(courses.size()); ++i)
        {
            if (courses[i].id == m_finishedSummary->courseId)
            {
                m_selected = i;
                break;
            }
        }
    }
}

void SceneCourse::OnEnter()
{
    LOG_INFO("[SceneCourse] 进入樱华道场 · 段位认定");

    m_font = ResourceManager::GetInstance().GetDefaultFontHandle();
    m_time = 0.0f;

    m_particles.Clear();
    m_petalEmitter = m_particles.EmitContinuous(
        0.5f, 0.0f, 2.8f, sakura::effects::ParticlePresets::SakuraPetal());

    ChartLoader loader;
    m_allCharts = loader.ScanCharts("resources/charts");
    auto userCharts = loader.ScanCharts(Paths::User("charts"));
    for (auto& c : userCharts)
    {
        auto existing = std::find_if(m_allCharts.begin(), m_allCharts.end(),
            [&](const ChartInfo& other) { return !c.id.empty() && other.id == c.id; });
        if (existing == m_allCharts.end())
            m_allCharts.push_back(std::move(c));
        else
            *existing = std::move(c);
    }

    RefreshRecords();

    const int totalCourses = static_cast<int>(GetOfficialCourses().size());
    if (totalCourses > 0)
    {
        m_selected = std::clamp(m_selected, 0, totalCourses - 1);
        if (m_selected < m_scroll)
            m_scroll = m_selected;
        if (m_selected >= m_scroll + VISIBLE_COURSE_ROWS)
            m_scroll = m_selected - (VISIBLE_COURSE_ROWS - 1);
        m_scroll = std::clamp(m_scroll, 0, std::max(0, totalCourses - VISIBLE_COURSE_ROWS));
    }
    else
    {
        m_selected = 0;
        m_scroll   = 0;
    }

    m_btnBack = std::make_unique<Button>(
        NormRect{ 0.04f, 0.908f, 0.13f, 0.056f },
        "返回主页", m_font, 0.021f, 0.010f);
    m_btnBack->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(m_btnBack.get(), ButtonVariant::Secondary);
    m_btnBack->SetOnClick([this]()
    {
        CourseSession::GetInstance().Abort();
        m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.38f);
    });

    m_btnStart = std::make_unique<Button>(
        NormRect{ 0.73f, 0.908f, 0.23f, 0.056f },
        "开始段位考核  →", m_font, 0.022f, 0.010f);
    m_btnStart->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(m_btnStart.get(), ButtonVariant::Primary);
    m_btnStart->SetOnClick([this]() { StartSelectedCourse(); });

    m_btnCloseSummary = std::make_unique<Button>(
        NormRect{ 0.42f, 0.68f, 0.16f, 0.050f },
        "确 认", m_font, 0.021f, 0.010f);
    m_btnCloseSummary->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(m_btnCloseSummary.get(), ButtonVariant::Primary);
    m_btnCloseSummary->SetOnClick([this]()
    {
        m_showSummaryModal = false;
        RefreshRecords();
    });
}

void SceneCourse::OnExit()
{
    if (m_petalEmitter >= 0)
    {
        m_particles.StopEmitter(m_petalEmitter);
        m_petalEmitter = -1;
    }
    m_particles.Clear();
    m_btnBack.reset();
    m_btnStart.reset();
    m_btnCloseSummary.reset();
}

void SceneCourse::RefreshRecords()
{
    const auto& courses = GetOfficialCourses();
    m_records.clear();
    m_records.reserve(courses.size());
    auto& db = sakura::data::Database::GetInstance();
    for (const auto& c : courses)
    {
        m_records.push_back(db.GetCourseRecord(c.id));
    }
}

void SceneCourse::StartSelectedCourse()
{
    const auto& courses = GetOfficialCourses();
    if (m_selected < 0 || m_selected >= static_cast<int>(courses.size()))
        return;

    const auto& course = courses[m_selected];
    auto& session = CourseSession::GetInstance();
    if (!session.Start(course, m_allCharts))
    {
        LOG_WARN("[SceneCourse] 无法启动段位 {}：缺少课题谱面", course.id);
        ToastManager::Instance().Show("缺少该段位所需的本地课题谱面，无法开始考核", ToastType::Error, 3.5f);
        return;
    }

    ChartInfo stage0Chart;
    int stage0DiffIdx = 0;
    if (!session.GetCurrentStageChart(stage0Chart, stage0DiffIdx))
    {
        session.Abort();
        ToastManager::Instance().Show("段位首曲解析失败，无法开始考核", ToastType::Error, 3.5f);
        return;
    }

    PlayOptions opts;
    opts.mode         = PlayMode::Standard;
    opts.isCourseMode = true;

    LOG_INFO("[SceneCourse] 开始段位考核: {} [{}]", course.title, course.badgeText);
    m_manager.SwitchScene(
        std::make_unique<SceneGame>(m_manager, stage0Chart, stage0DiffIdx, opts),
        TransitionType::SakuraSweep, 0.42f);
}

void SceneCourse::OnUpdate(float dt)
{
    m_time += dt;
    m_particles.Update(dt);

    if (m_showSummaryModal && m_finishedSummary.has_value())
    {
        if (m_btnCloseSummary)
            m_btnCloseSummary->Update(dt);
        return;
    }

    const auto& courses = GetOfficialCourses();
    if (m_btnBack)
        m_btnBack->Update(dt);
    if (m_btnStart)
    {
        m_btnStart->SetEnabled(!courses.empty() && m_selected >= 0 && m_selected < static_cast<int>(courses.size()));
        m_btnStart->Update(dt);
    }
}

void SceneCourse::OnRender(Renderer& renderer)
{
    VisualStyle::DrawSceneBackground(renderer);
    VisualStyle::DrawSakuraLandscape(renderer, m_time, 0.64f);
    renderer.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        { 8, 12, 26, 126 }, { 12, 16, 34, 96 },
        { 8, 12, 26, 176 }, { 10, 14, 30, 158 });
    if (m_font == INVALID_HANDLE)
        return;

    // ── 顶部标题区 ────────────────────────────────────────────────────────────
    VisualStyle::DrawBadgePill(renderer,
        { 0.04f, 0.032f, 0.155f, 0.026f },
        "SAKURA DOJO", m_font, 0.013f,
        { 48, 36, 70, 208 }, { 250, 216, 157, 158 }, kGold);
    renderer.DrawText(m_font, "DOJO / 樱华道场 · 段位认定", 0.04f, 0.062f, 0.038f, kWhite);
    renderer.DrawText(m_font, "多曲连奏继承制生命槽试炼 · 通过考核授予专属段位称号徽章", 0.04f, 0.114f, 0.0185f, kDim);

    const auto highestCleared = sakura::data::Database::GetInstance().GetHighestClearedCourse();
    {   // 右上最高认定称号铭牌
        const NormRect topBadgeCard{ 0.72f, 0.045f, 0.24f, 0.062f };
        VisualStyle::DrawGlassCard(renderer, topBadgeCard,
            { 32, 28, 58, 214 }, { 18, 18, 38, 228 },
            { 250, 216, 157, 172 }, { 246, 178, 206, 86 }, 0.013f, 0.42f);
        renderer.DrawText(m_font, "HIGHEST DAN / 最高段位", 0.738f, 0.055f, 0.0125f, kDim);
        const std::string highestText = (highestCleared && highestCleared->cleared && !highestCleared->badgeText.empty())
            ? ("✦ [" + highestCleared->badgeText + "] 已认定")
            : "○ 尚未取得段位称号";
        renderer.DrawText(m_font, highestText, 0.738f, 0.076f, 0.0185f,
            (highestCleared && highestCleared->cleared) ? kGold : kDim);
    }

    const auto& courses = GetOfficialCourses();
    const int totalCourses = static_cast<int>(courses.size());
    const auto [mx, my] = Input::GetMousePosition();

    // ── 左侧段位卷轴卡片列表 (x = 0.04 .. 0.46) ───────────────────────────────
    for (int row = 0; row < VISIBLE_COURSE_ROWS && (row + m_scroll) < totalCourses; ++row)
    {
        const int idx   = row + m_scroll;
        const auto& c   = courses[idx];
        const float y   = COURSE_CARD_Y0 + static_cast<float>(row) * COURSE_CARD_STRIDE;
        const bool sel  = (idx == m_selected);
        const bool hov  = !m_showSummaryModal &&
                          (mx >= COURSE_CARD_X && mx <= COURSE_CARD_X + COURSE_CARD_W &&
                           my >= y && my < y + COURSE_CARD_H);
        const NormRect card{ COURSE_CARD_X, y, COURSE_CARD_W, COURSE_CARD_H };

        if (sel)
        {
            VisualStyle::DrawGlassCard(renderer, card,
                { 84, 50, 90, 240 }, { 44, 32, 68, 244 },
                { 250, 216, 157, 232 }, { 246, 178, 206, 142 }, 0.013f, 0.56f);
            renderer.DrawRoundedGradientRect(
                { card.x + 0.005f, y + 0.016f, 0.0036f, COURSE_CARD_H - 0.032f },
                0.0018f, kGold, kPink, kGold, kPink);
        }
        else
        {
            VisualStyle::DrawGlassCard(renderer, card,
                hov ? Color{ 38, 46, 78, 226 } : Color{ 22, 28, 52, 208 },
                hov ? Color{ 24, 30, 54, 234 } : Color{ 14, 18, 36, 220 },
                hov ? Color{ 246, 178, 206, 145 } : Color{ 166, 176, 214, 70 },
                { 112, 122, 168, 38 }, 0.012f, 0.36f);
        }

        const bool isKaiden = (c.danRank >= 6);
        VisualStyle::DrawBadgePill(renderer,
            { card.x + 0.016f, y + 0.016f, 0.078f, 0.030f },
            "[" + c.badgeText + "]", m_font, 0.0155f,
            isKaiden ? Color{ 98, 44, 72, 232 } : (sel ? Color{ 72, 48, 86, 228 } : Color{ 38, 34, 64, 214 }),
            kGold, kGold);

        VisualStyle::DrawTextFit(renderer, m_font, c.title,
            card.x + 0.104f, y + 0.013f, 0.024f, 0.205f, kWhite);
        VisualStyle::DrawTextFit(renderer, m_font, c.subtitle,
            card.x + 0.104f, y + 0.055f, 0.015f, 0.185f,
            sel ? Color{ 226, 214, 236, 242 } : kDim);

        const std::optional<CourseRecord> rec = (idx < static_cast<int>(m_records.size()))
            ? m_records[idx]
            : std::nullopt;
        const bool cleared = rec.has_value() && rec->cleared;

        if (cleared)
        {
            const Color statusColor = isKaiden ? kGold : kMint;
            renderer.DrawText(m_font, "● 已认定",
                card.x + card.width - 0.015f, y + 0.017f, 0.016f,
                statusColor, TextAlign::Right);
            renderer.DrawText(m_font, FormatScore(rec->totalScore),
                card.x + card.width - 0.015f, y + 0.055f, 0.015f,
                kGold, TextAlign::Right);
        }
        else
        {
            renderer.DrawText(m_font, "○ 未认定",
                card.x + card.width - 0.015f, y + 0.017f, 0.016f,
                kDim, TextAlign::Right);
            const std::string subStat = rec.has_value()
                ? ("最高 " + FormatScore(rec->totalScore))
                : (std::to_string(c.stages.size()) + " 曲连奏");
            renderer.DrawText(m_font, subStat,
                card.x + card.width - 0.015f, y + 0.055f, 0.0145f,
                kDim, TextAlign::Right);
        }
    }

    renderer.DrawText(m_font,
        "↑ ↓ 选择段位   ·   Enter 开始段位考核   ·   Esc 返回主页",
        0.04f, 0.870f, 0.0155f, kDim);

    // ── 右侧段位课题详情卡片 ({0.49, 0.16, 0.47, 0.72}) ──────────────────────
    VisualStyle::DrawGlassCard(renderer,
        { 0.49f, 0.16f, 0.47f, 0.72f },
        { 24, 30, 56, 224 }, { 14, 18, 36, 238 },
        { 250, 216, 157, 182 }, { 168, 158, 214, 74 },
        0.018f, 0.56f);

    if (m_selected >= 0 && m_selected < totalCourses)
    {
        const auto& course = courses[m_selected];
        const std::optional<CourseRecord> rec = (m_selected < static_cast<int>(m_records.size()))
            ? m_records[m_selected]
            : std::nullopt;
        const bool cleared = rec.has_value() && rec->cleared;

        VisualStyle::DrawBadgePill(renderer,
            { 0.515f, 0.178f, 0.118f, 0.028f },
            "[" + course.badgeText + "]  DAN " + std::to_string(course.danRank),
            m_font, 0.0135f,
            { 58, 40, 82, 220 }, kGold, kGold);

        renderer.DrawText(m_font,
            cleared ? "● 已获段位认定" : ("○ " + std::to_string(course.stages.size()) + " 曲继承血槽考核"),
            0.935f, 0.182f, 0.0155f,
            cleared ? kMint : kPink, TextAlign::Right);

        VisualStyle::DrawTextFit(renderer, m_font, course.title,
            0.515f, 0.214f, 0.032f, 0.420f, kWhite);
        VisualStyle::DrawTextFit(renderer, m_font, course.subtitle,
            0.515f, 0.254f, 0.017f, 0.420f, kDim);
        VisualStyle::DrawGildedDivider(renderer, 0.515f, 0.935f, 0.286f, 0.72f);

        // ── 生命槽法则 ────────────────────────────────────────────────────────
        renderer.DrawText(m_font, "GAUGE RULES / 继承制血槽法则", 0.515f, 0.296f, 0.0135f, kDim);
        renderer.DrawText(m_font,
            "Good -" + FmtFloat(course.goodDamage, 1) + "%  ·  Bad -" + FmtFloat(course.badDamage, 1) + "%",
            0.935f, 0.296f, 0.013f, kDim, TextAlign::Right);

        const char* ruleLabels[4] = { "初始血量", "Perfect 回复", "Miss 扣血", "曲间回复" };
        const std::string ruleVals[4] = {
            FmtFloat(course.initialLife, 0) + "%",
            "+" + FmtFloat(course.perfectHeal, 2) + "%",
            "-" + FmtFloat(course.missDamage, 1) + "%",
            "+" + FmtFloat(course.stageClearHeal, 0) + "%"
        };
        const Color ruleColors[4] = { kGold, kMint, kCoral, kCyan };

        for (int i = 0; i < 4; ++i)
        {
            const float rx = 0.515f + static_cast<float>(i) * 0.107f;
            const NormRect ruleBox{ rx, 0.318f, 0.099f, 0.060f };
            renderer.DrawRoundedRect(ruleBox, 0.009f, { 16, 22, 42, 198 }, true);
            renderer.DrawRoundedRectOutlineGradient(ruleBox, 0.009f,
                { 250, 216, 157, 92 }, { 130, 142, 188, 42 }, 0.0011f);
            renderer.DrawText(m_font, ruleLabels[i],
                rx + 0.0495f, 0.326f, 0.013f, kDim, TextAlign::Center);
            renderer.DrawText(m_font, ruleVals[i],
                rx + 0.0495f, 0.347f, 0.0185f, ruleColors[i], TextAlign::Center);
        }

        // ── 课题曲目列表 (3 ~ 4 首) ───────────────────────────────────────────
        renderer.DrawText(m_font, "COURSE STAGES / 考核课题曲目", 0.515f, 0.392f, 0.0135f, kDim);
        const int stageCount = std::min(4, static_cast<int>(course.stages.size()));
        for (int s = 0; s < stageCount; ++s)
        {
            const auto& stage = course.stages[s];
            const float sy    = 0.414f + static_cast<float>(s) * 0.068f;
            const NormRect stageCard{ 0.515f, sy, 0.420f, 0.060f };

            renderer.DrawRoundedGradientRect(stageCard, 0.010f,
                { 20, 26, 48, 210 }, { 26, 30, 56, 210 },
                { 14, 18, 36, 224 }, { 18, 22, 42, 224 });
            renderer.DrawRoundedRectOutlineGradient(stageCard, 0.010f,
                { 246, 178, 206, 98 }, { 140, 150, 196, 42 }, 0.0011f);

            VisualStyle::DrawBadgePill(renderer,
                { 0.525f, sy + 0.014f, 0.068f, 0.032f },
                "STAGE " + std::to_string(s + 1), m_font, 0.013f,
                { 48, 36, 72, 220 }, { 250, 216, 157, 145 }, kGold);

            const ChartInfo* resolved = FindChartById(m_allCharts, stage.chartId);
            const std::string songTitle = (resolved && !resolved->title.empty())
                ? resolved->title
                : stage.chartId;
            const std::string songMeta = resolved
                ? ((resolved->artist.empty() ? "Unknown Artist" : resolved->artist) +
                   "  ·  " + FmtFloat(resolved->bpm, 0) + " BPM")
                : ("谱面 ID: " + stage.chartId);

            VisualStyle::DrawTextFit(renderer, m_font, songTitle,
                0.604f, sy + 0.008f, 0.020f, 0.186f, kWhite);
            VisualStyle::DrawTextFit(renderer, m_font, songMeta,
                0.604f, sy + 0.034f, 0.0135f, 0.186f, resolved ? kDim : kCoral);

            const std::string diffBadge = FormatDifficultyBadge(stage.difficultyName, stage.expectedLevel);
            VisualStyle::DrawBadgePill(renderer,
                { 0.798f, sy + 0.014f, 0.127f, 0.032f },
                diffBadge, m_font, 0.0135f,
                { 38, 44, 74, 215 }, { 246, 178, 206, 155 }, kPink);
        }

        // ── 个人最佳段位记录卡片 ──────────────────────────────────────────────
        renderer.DrawText(m_font, "PERSONAL BEST / 个人最佳认定记录", 0.515f, 0.696f, 0.0135f, kDim);
        const NormRect bestCard{ 0.515f, 0.718f, 0.420f, 0.140f };
        VisualStyle::DrawGlassCard(renderer, bestCard,
            cleared ? Color{ 32, 52, 62, 220 } : Color{ 20, 26, 48, 214 },
            { 14, 18, 36, 230 },
            cleared ? kMint : Color{ 250, 216, 157, 128 },
            { 138, 148, 194, 54 }, 0.012f, 0.38f);

        if (rec.has_value())
        {
            const std::string badgeLabel = !rec->badgeText.empty() ? rec->badgeText : course.badgeText;
            const std::string recHeader = rec->cleared
                ? ("● 段位认定合格  ·  已授称号 [" + badgeLabel + "]")
                : "○ 历史最佳尝试（尚未达成合格）";
            renderer.DrawText(m_font, recHeader,
                0.532f, 0.732f, 0.0165f, rec->cleared ? kMint : kPink);

            renderer.DrawText(m_font, "总成绩", 0.532f, 0.772f, 0.013f, kDim);
            renderer.DrawText(m_font, FormatScore(rec->totalScore), 0.532f, 0.795f, 0.025f, kWhite);

            renderer.DrawText(m_font, "平均准确率", 0.695f, 0.772f, 0.013f, kDim);
            renderer.DrawText(m_font, FmtFloat(rec->avgAccuracy, 2) + "%", 0.695f, 0.795f, 0.025f, kGold);

            renderer.DrawText(m_font, "剩余生命", 0.835f, 0.772f, 0.013f, kDim);
            renderer.DrawText(m_font, FmtFloat(rec->remainingLife, 1) + "%", 0.835f, 0.795f, 0.025f,
                rec->cleared ? kMint : kCoral);
        }
        else
        {
            renderer.DrawText(m_font, "尚未留下该段位考核记录",
                0.725f, 0.758f, 0.020f, kWhite, TextAlign::Center);
            renderer.DrawText(m_font,
                "完奏全部课题曲目且生命槽保持大于 0% 即可获授 [" + course.badgeText + "] 称号",
                0.725f, 0.800f, 0.0145f, kDim, TextAlign::Center);
        }
    }
    else
    {
        renderer.DrawText(m_font, "暂无可用段位课题",
            0.725f, 0.50f, 0.024f, kDim, TextAlign::Center);
    }

    if (m_btnBack)  m_btnBack->Render(renderer);
    if (m_btnStart) m_btnStart->Render(renderer);

    m_particles.Render(renderer);

    // ── 段位考核结算弹窗 ({0.26, 0.24, 0.48, 0.52}) ───────────────────────────
    if (m_showSummaryModal && m_finishedSummary.has_value())
    {
        const auto& sum = *m_finishedSummary;
        const CourseDefinition* def = FindOfficialCourse(sum.courseId);
        const std::string badgeText = !sum.badgeText.empty()
            ? sum.badgeText
            : (def ? def->badgeText : "");

        VisualStyle::DrawScrim(renderer, 0.72f);
        VisualStyle::DrawGlassCard(renderer,
            { 0.26f, 0.24f, 0.48f, 0.52f },
            sum.cleared ? Color{ 38, 44, 74, 244 } : Color{ 44, 28, 54, 244 },
            { 14, 18, 36, 248 },
            sum.cleared ? kGold : kCoral,
            sum.cleared ? kMint : kPink,
            0.018f, 0.66f);

        VisualStyle::DrawBadgePill(renderer,
            { 0.41f, 0.266f, 0.18f, 0.028f },
            sum.cleared ? "DAN CERTIFIED" : "DAN CHALLENGE",
            m_font, 0.013f,
            { 52, 38, 76, 220 }, sum.cleared ? kGold : kCoral,
            sum.cleared ? kGold : kPink);

        const std::string sealText = sum.cleared
            ? ("✦ 段位认定合格 · 授予称号 [" + badgeText + "]")
            : "段位考核未达成";
        renderer.DrawText(m_font, sealText,
            0.50f, 0.312f, 0.028f,
            sum.cleared ? kGold : kCoral, TextAlign::Center);

        if (def)
        {
            renderer.DrawText(m_font, def->title + "  ·  " + def->subtitle,
                0.50f, 0.362f, 0.0175f, kDim, TextAlign::Center);
        }

        VisualStyle::DrawGildedDivider(renderer, 0.30f, 0.70f, 0.402f, 0.76f);

        renderer.DrawText(m_font, "总成绩  /  TOTAL SCORE",
            0.50f, 0.426f, 0.0145f, kDim, TextAlign::Center);
        renderer.DrawText(m_font, FormatScore(sum.totalScore),
            0.50f, 0.454f, 0.042f, kWhite, TextAlign::Center);

        const NormRect accBox { 0.305f, 0.532f, 0.180f, 0.105f };
        const NormRect lifeBox{ 0.515f, 0.532f, 0.180f, 0.105f };
        renderer.DrawRoundedRect(accBox, 0.011f, { 18, 24, 46, 208 }, true);
        renderer.DrawRoundedRect(lifeBox, 0.011f, { 18, 24, 46, 208 }, true);
        renderer.DrawRoundedRectOutlineGradient(accBox, 0.011f, { 250, 216, 157, 110 }, { 140, 150, 196, 45 }, 0.0011f);
        renderer.DrawRoundedRectOutlineGradient(lifeBox, 0.011f, { 250, 216, 157, 110 }, { 140, 150, 196, 45 }, 0.0011f);

        renderer.DrawText(m_font, "平均准确率", 0.395f, 0.548f, 0.014f, kDim, TextAlign::Center);
        renderer.DrawText(m_font, FmtFloat(sum.avgAccuracy, 2) + "%", 0.395f, 0.578f, 0.028f, kGold, TextAlign::Center);

        renderer.DrawText(m_font, "剩余生命", 0.605f, 0.548f, 0.014f, kDim, TextAlign::Center);
        renderer.DrawText(m_font, FmtFloat(sum.remainingLife, 1) + "%", 0.605f, 0.578f, 0.028f,
            sum.cleared ? kMint : kCoral, TextAlign::Center);

        if (m_btnCloseSummary)
            m_btnCloseSummary->Render(renderer);
    }
}

void SceneCourse::OnEvent(const SDL_Event& event)
{
    if (m_showSummaryModal && m_finishedSummary.has_value())
    {
        if (m_btnCloseSummary && m_btnCloseSummary->HandleEvent(event))
            return;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE ||
                event.key.scancode == SDL_SCANCODE_RETURN ||
                event.key.scancode == SDL_SCANCODE_KP_ENTER ||
                event.key.scancode == SDL_SCANCODE_SPACE)
            {
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                m_showSummaryModal = false;
                RefreshRecords();
            }
        }
        return;
    }

    if (m_btnBack && m_btnBack->HandleEvent(event))
        return;
    if (m_btnStart && m_btnStart->HandleEvent(event))
        return;

    const auto& courses = GetOfficialCourses();
    const int totalCourses = static_cast<int>(courses.size());

    auto ensureVisible = [&]()
    {
        if (totalCourses <= 0)
            return;
        m_selected = std::clamp(m_selected, 0, totalCourses - 1);
        if (m_selected < m_scroll)
            m_scroll = m_selected;
        if (m_selected >= m_scroll + VISIBLE_COURSE_ROWS)
            m_scroll = m_selected - (VISIBLE_COURSE_ROWS - 1);
        m_scroll = std::clamp(m_scroll, 0, std::max(0, totalCourses - VISIBLE_COURSE_ROWS));
    };

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
            CourseSession::GetInstance().Abort();
            m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.38f);
            return;
        }
        if (totalCourses > 0)
        {
            if (event.key.scancode == SDL_SCANCODE_UP)
            {
                m_selected = (m_selected + totalCourses - 1) % totalCourses;
                ensureVisible();
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_DOWN)
            {
                m_selected = (m_selected + 1) % totalCourses;
                ensureVisible();
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_HOME)
            {
                m_selected = 0;
                ensureVisible();
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_END)
            {
                m_selected = totalCourses - 1;
                ensureVisible();
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_RETURN ||
                event.key.scancode == SDL_SCANCODE_KP_ENTER)
            {
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                StartSelectedCourse();
                return;
            }
        }
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL && totalCourses > 0)
    {
        const int delta = (event.wheel.y > 0.0f) ? -1 : ((event.wheel.y < 0.0f) ? 1 : 0);
        if (delta != 0)
        {
            if (totalCourses > VISIBLE_COURSE_ROWS)
            {
                m_scroll = std::clamp(m_scroll + delta, 0, totalCourses - VISIBLE_COURSE_ROWS);
            }
            else
            {
                const int next = std::clamp(m_selected + delta, 0, totalCourses - 1);
                if (next != m_selected)
                {
                    m_selected = next;
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
                }
            }
        }
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && totalCourses > 0)
    {
        const auto [mx, my] = Input::GetMousePosition();
        if (mx >= COURSE_CARD_X && mx <= COURSE_CARD_X + COURSE_CARD_W)
        {
            for (int row = 0; row < VISIBLE_COURSE_ROWS && (row + m_scroll) < totalCourses; ++row)
            {
                const float y = COURSE_CARD_Y0 + static_cast<float>(row) * COURSE_CARD_STRIDE;
                if (my >= y && my < y + COURSE_CARD_H)
                {
                    const int idx = row + m_scroll;
                    const bool wasSelected = (idx == m_selected);
                    m_selected = idx;
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    if (wasSelected && event.button.clicks >= 2)
                        StartSelectedCourse();
                    return;
                }
            }
        }
    }
}

} // namespace sakura::scene
