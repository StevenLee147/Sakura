// score.cpp — 计分系统实现

#include "score.h"
#include "utils/logger.h"

#include <algorithm>
#include <ctime>

namespace sakura::game
{

// ── Initialize ────────────────────────────────────────────────────────────────

void ScoreCalculator::Initialize(int totalNoteCount)
{
    m_totalNoteCount  = std::max(1, totalNoteCount);
    m_baseScorePerNote = 1000000.0f / static_cast<float>(m_totalNoteCount);

    m_score        = 0;
    m_accuracySum  = 0.0f;
    m_combo        = 0;
    m_maxCombo     = 0;
    m_perfectCount = 0;
    m_greatCount   = 0;
    m_goodCount    = 0;
    m_badCount     = 0;
    m_missCount    = 0;
    m_hitErrors.clear();

    LOG_DEBUG("ScoreCalculator 初始化: 总音符={}, 每音符基础分={:.2f}",
              m_totalNoteCount, m_baseScorePerNote);
}

// ── OnJudge ───────────────────────────────────────────────────────────────────

void ScoreCalculator::OnJudge(JudgeResult result, int hitError)
{
    // 记录偏差（仅记录实际命中的音符，Miss 不记录）
    if (result != JudgeResult::None && result != JudgeResult::Miss)
    {
        m_hitErrors.push_back(hitError);
    }

    // 确定本次判定的得分比例和准确率权重
    float scoreRatio   = 0.0f;
    float accuracyWeight = 0.0f;
    bool  breakCombo   = false;

    switch (result)
    {
    case JudgeResult::Perfect:
        scoreRatio     = SCORE_PERFECT;
        accuracyWeight = WEIGHT_PERFECT;
        ++m_perfectCount;
        break;
    case JudgeResult::Great:
        scoreRatio     = SCORE_GREAT;
        accuracyWeight = WEIGHT_GREAT;
        ++m_greatCount;
        break;
    case JudgeResult::Good:
        scoreRatio     = SCORE_GOOD;
        accuracyWeight = WEIGHT_GOOD;
        ++m_goodCount;
        break;
    case JudgeResult::Bad:
        scoreRatio     = SCORE_BAD;
        accuracyWeight = WEIGHT_BAD;
        ++m_badCount;
        breakCombo = true;
        break;
    case JudgeResult::Miss:
        scoreRatio     = SCORE_MISS;
        accuracyWeight = WEIGHT_MISS;
        ++m_missCount;
        breakCombo = true;
        break;
    default:
        return;  // None: 不计分
    }

    // 更新连击
    if (breakCombo)
    {
        m_combo = 0;
    }
    else
    {
        ++m_combo;
        m_maxCombo = std::max(m_maxCombo, m_combo);
    }

    // 计算连击加成倍率（combo × 0.1%，上限 10%）
    float comboBonus = std::min(
        static_cast<float>(m_combo) * COMBO_BONUS_PER,
        COMBO_BONUS_CAP
    );

    // 实际得分 = 基础分 × 判定比例 × (1 + 连击加成)
    float noteScore = m_baseScorePerNote * scoreRatio * (1.0f + comboBonus);
    m_score += static_cast<int>(noteScore);

    // 累积准确率权重
    m_accuracySum += accuracyWeight;
}

// ── GetAccuracy ───────────────────────────────────────────────────────────────

float ScoreCalculator::GetAccuracy() const
{
    int judged = GetJudgedCount();
    if (judged == 0) return 100.0f;
    return (m_accuracySum / static_cast<float>(judged)) * 100.0f;
}

// ── GetGrade ──────────────────────────────────────────────────────────────────

Grade ScoreCalculator::GetGrade() const
{
    float acc = GetAccuracy();

    // SS: ≥99% + 全 P/Gr（无 Good/Bad/Miss）
    if (acc >= 99.0f && m_goodCount == 0 && IsFullCombo()) return Grade::SS;

    if (acc >= 95.0f) return Grade::S;
    if (acc >= 90.0f) return Grade::A;
    if (acc >= 80.0f) return Grade::B;
    if (acc >= 70.0f) return Grade::C;
    return Grade::D;
}

// ── GetResult ─────────────────────────────────────────────────────────────────

GameResult ScoreCalculator::GetResult(const std::string& chartId,
                                       const std::string& chartTitle,
                                       const std::string& difficultyName,
                                       float              difficultyLevel) const
{
    GameResult result;
    result.chartId         = chartId;
    result.chartTitle      = chartTitle;
    result.difficulty      = difficultyName;
    result.difficultyLevel = difficultyLevel;

    result.score       = m_score;
    result.accuracy    = GetAccuracy();
    result.maxCombo    = m_maxCombo;
    result.grade       = GetGrade();

    result.perfectCount = m_perfectCount;
    result.greatCount   = m_greatCount;
    result.goodCount    = m_goodCount;
    result.badCount     = m_badCount;
    result.missCount    = m_missCount;

    result.isFullCombo  = IsFullCombo();
    result.isAllPerfect = IsAllPerfect();
    result.playedAt     = static_cast<long long>(std::time(nullptr));
    result.hitErrors    = m_hitErrors;

    return result;
}

} // namespace sakura::game
