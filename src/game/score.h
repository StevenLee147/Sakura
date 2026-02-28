#pragma once

// score.h — 计分系统

#include "note.h"
#include "chart.h"
#include <vector>
#include <string>
#include <ctime>

namespace sakura::game
{

// ScoreCalculator — 管理一局游戏的得分与统计
class ScoreCalculator
{
public:
    ScoreCalculator()  = default;
    ~ScoreCalculator() = default;

    // ── 初始化 ────────────────────────────────────────────────────────────────

    // 开始一局游戏前调用，设置总音符数（用于计算每音符基础分）
    void Initialize(int totalNoteCount);

    // ── 判定事件 ──────────────────────────────────────────────────────────────

    // 每次产生判定时调用
    // result: 判定结果
    // hitError: 偏差（毫秒，可选，用于偏差图）
    void OnJudge(JudgeResult result, int hitError = 0);

    // ── 查询 ──────────────────────────────────────────────────────────────────

    int   GetScore()       const { return m_score; }
    float GetAccuracy()    const;    // 0.0~100.0%
    int   GetCombo()       const { return m_combo; }
    int   GetMaxCombo()    const { return m_maxCombo; }

    int   GetPerfectCount() const { return m_perfectCount; }
    int   GetGreatCount()   const { return m_greatCount; }
    int   GetGoodCount()    const { return m_goodCount; }
    int   GetBadCount()     const { return m_badCount; }
    int   GetMissCount()    const { return m_missCount; }

    int   GetJudgedCount()  const
    {
        return m_perfectCount + m_greatCount + m_goodCount
             + m_badCount     + m_missCount;
    }

    Grade GetGrade()        const;
    bool  IsFullCombo()     const { return m_badCount == 0 && m_missCount == 0; }
    bool  IsAllPerfect()    const { return m_greatCount == 0 && m_goodCount == 0
                                        && m_badCount == 0  && m_missCount == 0; }

    // 打包完整游戏结果
    GameResult GetResult(const std::string& chartId,
                         const std::string& chartTitle,
                         const std::string& difficultyName,
                         float              difficultyLevel) const;

    const std::vector<int>& GetHitErrors() const { return m_hitErrors; }

private:
    int   m_totalNoteCount  = 0;
    float m_baseScorePerNote = 0.0f;

    int   m_score          = 0;
    float m_accuracySum    = 0.0f;    // 加权准确率之和（未除以总数）

    int   m_combo          = 0;
    int   m_maxCombo       = 0;

    int   m_perfectCount   = 0;
    int   m_greatCount     = 0;
    int   m_goodCount      = 0;
    int   m_badCount       = 0;
    int   m_missCount      = 0;

    std::vector<int> m_hitErrors;  // 每次判定的偏差记录

    // 各判定的准确率权重（用于均值计算）
    static constexpr float WEIGHT_PERFECT = 1.00f;
    static constexpr float WEIGHT_GREAT   = 0.70f;
    static constexpr float WEIGHT_GOOD    = 0.40f;
    static constexpr float WEIGHT_BAD     = 0.10f;
    static constexpr float WEIGHT_MISS    = 0.00f;

    // 各判定的得分比例
    static constexpr float SCORE_PERFECT  = 1.00f;
    static constexpr float SCORE_GREAT    = 0.70f;
    static constexpr float SCORE_GOOD     = 0.40f;
    static constexpr float SCORE_BAD      = 0.10f;
    static constexpr float SCORE_MISS     = 0.00f;

    // 连击加成上限（10%）
    static constexpr float COMBO_BONUS_CAP = 0.10f;
    // 每连击增加 0.1%
    static constexpr float COMBO_BONUS_PER = 0.001f;
};

} // namespace sakura::game
