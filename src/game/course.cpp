#include "game/course.h"

#include "core/config.h"
#include "game/chart_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace sakura::game
{

namespace
{

long long NowUnixSeconds()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

CourseDefinition MakeCourse(std::string id,
                            std::string title,
                            std::string subtitle,
                            std::string badgeText,
                            int danRank,
                            float stageClearHeal,
                            float perfectHeal,
                            float goodDamage,
                            float badDamage,
                            float missDamage,
                            std::vector<CourseStage> stages)
{
    CourseDefinition def;
    def.id             = std::move(id);
    def.title          = std::move(title);
    def.subtitle       = std::move(subtitle);
    def.badgeText      = std::move(badgeText);
    def.danRank        = danRank;
    def.initialLife    = 100.0f;
    def.stageClearHeal = stageClearHeal;
    def.perfectHeal    = perfectHeal;
    def.greatDelta     = 0.0f;
    def.goodDamage     = goodDamage;
    def.badDamage      = badDamage;
    def.missDamage     = missDamage;
    def.stages         = std::move(stages);
    return def;
}

} // namespace

// ── CourseGauge ──────────────────────────────────────────────────────────────

CourseGauge::CourseGauge(const CourseDefinition& def)
{
    Reset(def);
}

void CourseGauge::Reset()
{
    m_life   = std::clamp(m_def.initialLife, 0.0f, 100.0f);
    m_failed = (m_life <= 0.001f);
    if (m_failed)
        m_life = 0.0f;
}

void CourseGauge::Reset(const CourseDefinition& def)
{
    m_def = def;
    Reset();
}

void CourseGauge::OnJudge(JudgeResult result)
{
    if (m_failed || result == JudgeResult::None)
        return;

    switch (result)
    {
    case JudgeResult::Perfect:
        m_life += m_def.perfectHeal;
        break;
    case JudgeResult::Great:
        m_life += m_def.greatDelta;
        break;
    case JudgeResult::Good:
        m_life -= m_def.goodDamage;
        break;
    case JudgeResult::Bad:
        m_life -= m_def.badDamage;
        break;
    case JudgeResult::Miss:
        m_life -= m_def.missDamage;
        break;
    case JudgeResult::None:
    default:
        break;
    }

    m_life = std::clamp(m_life, 0.0f, 100.0f);
    if (m_life <= 0.001f)
    {
        m_life   = 0.0f;
        m_failed = true;
    }
}

void CourseGauge::OnStageCleared()
{
    if (m_failed)
        return;
    m_life = std::clamp(m_life + m_def.stageClearHeal, 0.0f, 100.0f);
}

// ── Official Courses ─────────────────────────────────────────────────────────

const std::vector<CourseDefinition>& GetOfficialCourses()
{
    static const std::vector<CourseDefinition> kCourses = {
        MakeCourse(
            "dan_1", "初段 · 春芽", "双区启蒙 · 柔和春风试炼", "初段", 1,
            18.0f, 0.25f, 1.0f, 3.0f, 5.0f,
            {
                { "sakura_awakening", "Easy", 2.5f },
                { "starry_sky", "Normal", 3.5f },
                { "sakura_breath", "Normal", 4.0f },
            }),
        MakeCourse(
            "dan_2", "二段 · 绽樱", "键鼠初谐 · 落樱跃动试炼", "二段", 2,
            15.0f, 0.22f, 1.2f, 3.6f, 6.0f,
            {
                { "sakura_awakening", "Normal", 4.5f },
                { "distant_star", "Normal", 4.5f },
                { "flowing_petals", "Normal", 5.0f },
            }),
        MakeCourse(
            "dan_4", "四段 · 流岚", "五轨进阶 · 疾风旋舞试炼", "四段", 4,
            15.0f, 0.22f, 1.2f, 3.8f, 6.5f,
            {
                { "sidechain_dream", "Normal", 5.5f },
                { "starry_sky", "Hard", 6.5f },
                { "sakura_breath", "Hard", 7.5f },
            }),
        MakeCourse(
            "dan_6", "六段 · 月影", "连携觉醒 · 月夜残影试炼", "六段", 6,
            14.0f, 0.20f, 1.35f, 4.2f, 7.0f,
            {
                { "unfallen_light", "Hard", 8.0f },
                { "flowing_petals", "Hard", 8.5f },
                { "instant_bloom", "Hard", 9.0f },
                { "starry_sky", "Expert", 9.5f },
            }),
        MakeCourse(
            "dan_8", "八段 · 绯闪", "高速协同 · 绯红雷光试炼", "八段", 8,
            12.0f, 0.18f, 1.5f, 4.8f, 7.5f,
            {
                { "sakura_breath", "Expert", 10.5f },
                { "distant_star", "Expert", 10.5f },
                { "unfallen_light", "Expert", 11.0f },
                { "sidechain_dream", "Expert", 11.5f },
            }),
        MakeCourse(
            "dan_kaiden", "皆传 · 樱华绝响", "双域极境 · 宗师终极认定", "皆传", 10,
            8.0f, 0.15f, 1.8f, 5.5f, 9.0f,
            {
                { "petal_storm_overdrive", "Hard", 10.5f },
                { "flowing_petals", "Expert", 12.0f },
                { "instant_bloom", "Expert", 12.5f },
                { "petal_storm_overdrive", "Expert", 14.5f },
            }),
    };
    return kCourses;
}

const CourseDefinition* FindOfficialCourse(std::string_view courseId)
{
    const auto& all = GetOfficialCourses();
    for (const auto& c : all)
    {
        if (c.id == courseId)
            return &c;
    }
    return nullptr;
}

// ── CourseSession ────────────────────────────────────────────────────────────

CourseSession& CourseSession::GetInstance()
{
    static CourseSession instance;
    return instance;
}

bool CourseSession::Start(const CourseDefinition& def, const std::vector<ChartInfo>& availableCharts)
{
    if (def.stages.empty())
        return false;

    std::vector<ChartInfo> resolvedCharts;
    std::vector<int>       resolvedDiffs;
    resolvedCharts.reserve(def.stages.size());
    resolvedDiffs.reserve(def.stages.size());

    for (const auto& stage : def.stages)
    {
        auto it = std::find_if(availableCharts.begin(), availableCharts.end(),
            [&](const ChartInfo& c) { return c.id == stage.chartId; });
        if (it == availableCharts.end() || it->difficulties.empty())
            return false;

        const int diffIdx = ResolveMatchingDifficultyIndex(*it, stage.difficultyName, stage.expectedLevel);
        resolvedCharts.push_back(*it);
        resolvedDiffs.push_back(diffIdx);
    }

    m_course              = def;
    m_gauge.Reset(def);
    m_resolvedCharts      = std::move(resolvedCharts);
    m_resolvedDiffIndices = std::move(resolvedDiffs);
    m_stageResults.clear();
    m_stageIndex          = 0;
    m_active              = true;
    return true;
}

void CourseSession::Abort()
{
    m_active = false;
}

bool CourseSession::GetCurrentStageChart(ChartInfo& outChart, int& outDiffIndex) const
{
    if (!m_active || m_stageIndex < 0 || m_stageIndex >= static_cast<int>(m_resolvedCharts.size()))
        return false;
    outChart     = m_resolvedCharts[static_cast<std::size_t>(m_stageIndex)];
    outDiffIndex = m_resolvedDiffIndices[static_cast<std::size_t>(m_stageIndex)];
    return true;
}

void CourseSession::RecordStageResult(const GameResult& stageResult)
{
    m_stageResults.push_back(stageResult);
}

bool CourseSession::HasNextStage() const
{
    return m_active && !m_gauge.IsFailed() &&
           (m_stageIndex + 1 < static_cast<int>(m_resolvedCharts.size()));
}

bool CourseSession::AdvanceStage()
{
    if (!HasNextStage())
        return false;
    m_gauge.OnStageCleared();
    ++m_stageIndex;
    return true;
}

CourseRecord CourseSession::BuildFinalRecord(long long timestamp) const
{
    CourseRecord rec;
    rec.courseId      = m_course.id;
    rec.badgeText     = m_course.badgeText;
    rec.danRank       = m_course.danRank;
    rec.remainingLife = m_gauge.GetLife();

    long long sumScore = 0;
    double    sumAcc   = 0.0;
    for (const auto& sr : m_stageResults)
    {
        sumScore += sr.score;
        sumAcc   += static_cast<double>(sr.accuracy);
    }
    rec.totalScore  = static_cast<int>(std::min<long long>(sumScore, 2000000000LL));
    rec.avgAccuracy = m_stageResults.empty()
        ? 0.0f
        : static_cast<float>(sumAcc / static_cast<double>(m_stageResults.size()));
    rec.cleared = (!m_gauge.IsFailed() && !m_resolvedCharts.empty() &&
                   m_stageResults.size() == m_resolvedCharts.size());
    if (rec.cleared)
        rec.clearedAt = timestamp > 0 ? timestamp : NowUnixSeconds();
    else
        rec.clearedAt = 0;
    return rec;
}

// ── Daily Challenge (UTC+8 Deterministic Rotation) ───────────────────────────

std::string GetCurrentDateUtc8(long long unixSeconds)
{
    const long long baseSec = unixSeconds > 0 ? unixSeconds : NowUnixSeconds();
    const long long shifted = baseSec + 8LL * 3600LL;
    long long z = shifted >= 0 ? (shifted / 86400LL) : ((shifted - 86399LL) / 86400LL);
    z += 719468LL;
    const long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const unsigned  doe = static_cast<unsigned>(z - era * 146097LL);
    const unsigned  yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const long long y   = static_cast<long long>(yoe) + era * 400LL;
    const unsigned  doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned  mp  = (5u * doy + 2u) / 153u;
    const unsigned  d   = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned  m   = mp < 10u ? (mp + 3u) : (mp - 9u);
    const long long yr  = y + (m <= 2u ? 1LL : 0LL);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02u", yr, m, d);
    return std::string(buf);
}

DailyChallengeSpec ComputeOfflineDailyChallenge(std::string_view dateUtc8)
{
    struct PoolItem
    {
        const char* chartId;
        const char* chartTitle;
        const char* difficulty;
        float       level;
        const char* goalText;
        int         targetScore;
    };

    static constexpr std::array<PoolItem, 12> kPool{{
        { "sakura_awakening", "樱光初醒",   "Normal", 4.5f, "春光初醒 · 达成 950,000 分", 950000 },
        { "sakura_awakening", "樱光初醒",   "Hard",   7.0f, "樱枝觉醒 · 达成 955,000 分", 955000 },
        { "starry_sky",       "繁星",       "Hard",   6.5f, "星夜漫游 · 达成 955,000 分", 955000 },
        { "distant_star",     "遥远的星",   "Normal", 4.5f, "遥望星河 · 达成 950,000 分", 950000 },
        { "sakura_breath",    "樱落呼吸",   "Hard",   7.5f, "呼吸律动 · 达成 950,000 分", 950000 },
        { "distant_star",     "遥远的星",   "Hard",   7.5f, "星轨同押 · 达成 955,000 分", 955000 },
        { "unfallen_light",   "樱光未落",   "Hard",   8.0f, "疾风鼓点 · 达成 950,000 分", 950000 },
        { "flowing_petals",   "樱落流光",   "Normal", 5.0f, "流光滑条 · 达成 945,000 分", 945000 },
        { "sidechain_dream",  "樱落侧链梦", "Hard",   8.5f, "律动侧链 · 达成 950,000 分", 950000 },
        { "flowing_petals",   "樱落流光",   "Hard",   8.5f, "流光花影 · 达成 950,000 分", 950000 },
        { "instant_bloom",    "一瞬花绽",   "Hard",   9.0f, "一瞬满开 · 达成 945,000 分", 945000 },
        { "starry_sky",       "繁星",       "Expert", 9.5f, "星穹极境 · 达成 940,000 分", 940000 },
    }};

    const std::string resolvedDate = dateUtc8.empty() ? GetCurrentDateUtc8() : std::string(dateUtc8);
    std::uint32_t hash = 2166136261u;
    for (char ch : resolvedDate)
    {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 16777619u;
    }
    const auto& picked = kPool[static_cast<std::size_t>(hash % kPool.size())];

    DailyChallengeSpec spec;
    spec.dateUtc8        = resolvedDate;
    spec.chartId         = picked.chartId;
    spec.chartTitle      = picked.chartTitle;
    spec.difficultyName  = picked.difficulty;
    spec.difficultyLevel = picked.level;
    spec.goalText        = picked.goalText;
    spec.targetScore     = picked.targetScore;
    return spec;
}

// ── CollectionManager ────────────────────────────────────────────────────────

std::vector<SongCollection> CollectionManager::LoadFromConfig()
{
    std::vector<SongCollection> out;
    const auto raw = sakura::core::Config::GetInstance().Get<nlohmann::json>(
        "library.collections", nlohmann::json::array());
    if (!raw.is_array())
        return out;

    for (const auto& item : raw)
    {
        if (!item.is_object())
            continue;
        SongCollection col;
        col.id        = item.value("id", "");
        col.name      = item.value("name", "");
        col.updatedAt = item.value("updated_at", item.value("updatedAt", 0LL));
        if (col.id.empty() || col.name.empty())
            continue;

        const nlohmann::json* idsNode = nullptr;
        if (item.contains("chart_ids") && item["chart_ids"].is_array())
            idsNode = &item["chart_ids"];
        else if (item.contains("chartIds") && item["chartIds"].is_array())
            idsNode = &item["chartIds"];

        if (idsNode)
        {
            for (const auto& cid : *idsNode)
            {
                if (cid.is_string())
                {
                    const std::string s = cid.get<std::string>();
                    if (!s.empty() && !ContainsChart(col, s))
                        col.chartIds.push_back(s);
                }
            }
        }
        out.push_back(std::move(col));
    }
    return out;
}

void CollectionManager::SaveToConfig(const std::vector<SongCollection>& collections)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& col : collections)
    {
        if (col.id.empty() || col.name.empty())
            continue;
        arr.push_back({
            { "id", col.id },
            { "name", col.name },
            { "chart_ids", col.chartIds },
            { "updated_at", col.updatedAt }
        });
    }
    auto& cfg = sakura::core::Config::GetInstance();
    cfg.Set("library.collections", arr);
    cfg.Set("online.settings_updated_at", NowUnixSeconds());
    cfg.Save();
}

std::vector<SongCollection> CollectionManager::MergeCollections(
    const std::vector<SongCollection>& localCols,
    const std::vector<SongCollection>& remoteCols)
{
    std::vector<SongCollection> merged = localCols;
    for (const auto& rem : remoteCols)
    {
        if (rem.id.empty() || rem.name.empty())
            continue;
        auto it = std::find_if(merged.begin(), merged.end(),
            [&](const SongCollection& c) { return c.id == rem.id; });
        if (it == merged.end())
        {
            SongCollection clean;
            clean.id        = rem.id;
            clean.name      = rem.name;
            clean.updatedAt = rem.updatedAt;
            for (const auto& cid : rem.chartIds)
            {
                if (!cid.empty() && !ContainsChart(clean, cid))
                    clean.chartIds.push_back(cid);
            }
            merged.push_back(std::move(clean));
        }
        else
        {
            if (rem.updatedAt > it->updatedAt && !rem.name.empty())
                it->name = rem.name;
            it->updatedAt = std::max(it->updatedAt, rem.updatedAt);
            for (const auto& cid : rem.chartIds)
            {
                if (!cid.empty() && !ContainsChart(*it, cid))
                    it->chartIds.push_back(cid);
            }
        }
    }
    return merged;
}

SongCollection CollectionManager::CreateCollection(std::vector<SongCollection>& cols,
                                                   std::string_view name,
                                                   long long nowSec)
{
    static int s_seq = 0;
    ++s_seq;
    const long long ts = nowSec > 0 ? nowSec : NowUnixSeconds();
    std::string baseId = "col_" + std::to_string(ts) + "_" + std::to_string(s_seq);
    while (std::any_of(cols.begin(), cols.end(), [&](const SongCollection& c) { return c.id == baseId; }))
    {
        ++s_seq;
        baseId = "col_" + std::to_string(ts) + "_" + std::to_string(s_seq);
    }

    SongCollection col;
    col.id        = std::move(baseId);
    col.name      = name.empty() ? ("精选曲单 " + std::to_string(cols.size() + 1)) : std::string(name);
    col.updatedAt = ts;
    cols.push_back(col);
    return col;
}

bool CollectionManager::RenameCollection(std::vector<SongCollection>& cols,
                                         std::string_view collectionId,
                                         std::string_view newName,
                                         long long nowSec)
{
    if (newName.empty())
        return false;
    auto it = std::find_if(cols.begin(), cols.end(),
        [&](const SongCollection& c) { return c.id == collectionId; });
    if (it == cols.end())
        return false;
    it->name      = std::string(newName);
    it->updatedAt = nowSec > 0 ? nowSec : NowUnixSeconds();
    return true;
}

bool CollectionManager::DeleteCollection(std::vector<SongCollection>& cols,
                                         std::string_view collectionId)
{
    const auto oldSize = cols.size();
    std::erase_if(cols, [&](const SongCollection& c) { return c.id == collectionId; });
    return cols.size() != oldSize;
}

bool CollectionManager::ToggleChartInCollection(std::vector<SongCollection>& cols,
                                                std::string_view collectionId,
                                                std::string_view chartId,
                                                long long nowSec)
{
    if (chartId.empty())
        return false;
    auto it = std::find_if(cols.begin(), cols.end(),
        [&](const SongCollection& c) { return c.id == collectionId; });
    if (it == cols.end())
        return false;

    auto cidIt = std::find(it->chartIds.begin(), it->chartIds.end(), chartId);
    bool added = false;
    if (cidIt == it->chartIds.end())
    {
        it->chartIds.emplace_back(chartId);
        added = true;
    }
    else
    {
        it->chartIds.erase(cidIt);
        added = false;
    }
    it->updatedAt = nowSec > 0 ? nowSec : NowUnixSeconds();
    return added;
}

bool CollectionManager::ContainsChart(const SongCollection& col, std::string_view chartId)
{
    return std::find(col.chartIds.begin(), col.chartIds.end(), chartId) != col.chartIds.end();
}

} // namespace sakura::game
