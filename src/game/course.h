#pragma once

// course.h — 樱华道场段位认定、每日挑战确定性轮换与命名多曲单管理 (v1.4.0)

#include "game/chart.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sakura::game
{

// ── 樱华道场 · 段位认定 ────────────────────────────────────────────────────────

struct CourseStage
{
    std::string chartId;
    std::string difficultyName;
    float       expectedLevel = 1.0f;
};

struct CourseDefinition
{
    std::string              id;
    std::string              title;
    std::string              subtitle;
    std::string              badgeText;       // 如 "初段", "皆传"
    int                      danRank        = 1;
    float                    initialLife    = 100.0f;
    float                    stageClearHeal = 15.0f;
    float                    perfectHeal    = 0.22f;
    float                    greatDelta     = 0.0f;
    float                    goodDamage     = 1.20f;
    float                    badDamage      = 3.80f;
    float                    missDamage     = 6.50f;
    std::vector<CourseStage> stages;
};

struct CourseRecord
{
    std::string courseId;
    std::string badgeText;
    int         danRank       = 0;
    int         totalScore    = 0;
    float       avgAccuracy   = 0.0f;
    float       remainingLife = 0.0f;
    bool        cleared       = false;
    long long   clearedAt     = 0;
};

class CourseGauge
{
public:
    CourseGauge() = default;
    explicit CourseGauge(const CourseDefinition& def);

    void Reset();
    void Reset(const CourseDefinition& def);
    void OnJudge(JudgeResult result);
    void OnStageCleared();

    float GetLife() const { return m_life; }
    bool  IsFailed() const { return m_failed; }

private:
    CourseDefinition m_def;
    float            m_life   = 100.0f;
    bool             m_failed = false;
};

class CourseSession
{
public:
    static CourseSession& GetInstance();

    bool Start(const CourseDefinition& def, const std::vector<ChartInfo>& availableCharts);
    void Abort();

    bool IsActive() const { return m_active; }
    const CourseDefinition& GetCourse() const { return m_course; }
    int GetCurrentStageIndex() const { return m_stageIndex; }
    int GetTotalStages() const { return static_cast<int>(m_resolvedCharts.size()); }

    CourseGauge&       Gauge() { return m_gauge; }
    const CourseGauge& Gauge() const { return m_gauge; }

    bool GetCurrentStageChart(ChartInfo& outChart, int& outDiffIndex) const;
    void RecordStageResult(const GameResult& stageResult);
    bool HasNextStage() const;
    bool AdvanceStage();

    CourseRecord BuildFinalRecord(long long timestamp = 0) const;
    const std::vector<GameResult>& GetStageResults() const { return m_stageResults; }

private:
    bool                    m_active     = false;
    CourseDefinition        m_course;
    CourseGauge             m_gauge;
    std::vector<ChartInfo>  m_resolvedCharts;
    std::vector<int>        m_resolvedDiffIndices;
    std::vector<GameResult> m_stageResults;
    int                     m_stageIndex = 0;
};

const std::vector<CourseDefinition>& GetOfficialCourses();
const CourseDefinition* FindOfficialCourse(std::string_view courseId);

// ── 每日挑战 (UTC+8 确定性轮换，客户端离线与服务端同算法一致) ─────────────────

struct DailyChallengeSpec
{
    std::string dateUtc8;        // "YYYY-MM-DD"
    std::string chartId;
    std::string chartTitle;
    std::string difficultyName;
    float       difficultyLevel = 1.0f;
    std::string goalText;
    int         targetScore     = 950000;
};

std::string GetCurrentDateUtc8(long long unixSeconds = 0);
DailyChallengeSpec ComputeOfflineDailyChallenge(std::string_view dateUtc8);

// ── 命名多曲单收藏夹 (Collections) ────────────────────────────────────────────

struct SongCollection
{
    std::string              id;
    std::string              name;
    std::vector<std::string> chartIds;
    long long                updatedAt = 0;
};

class CollectionManager
{
public:
    static std::vector<SongCollection> LoadFromConfig();
    static void SaveToConfig(const std::vector<SongCollection>& collections);

    static std::vector<SongCollection> MergeCollections(
        const std::vector<SongCollection>& localCols,
        const std::vector<SongCollection>& remoteCols);

    static SongCollection CreateCollection(std::vector<SongCollection>& cols,
                                           std::string_view name,
                                           long long nowSec = 0);
    static bool RenameCollection(std::vector<SongCollection>& cols,
                                 std::string_view collectionId,
                                 std::string_view newName,
                                 long long nowSec = 0);
    static bool DeleteCollection(std::vector<SongCollection>& cols,
                                 std::string_view collectionId);
    static bool ToggleChartInCollection(std::vector<SongCollection>& cols,
                                        std::string_view collectionId,
                                        std::string_view chartId,
                                        long long nowSec = 0);
    static bool ContainsChart(const SongCollection& col, std::string_view chartId);
};

} // namespace sakura::game
