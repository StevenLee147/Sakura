// database.cpp — SQLite3 数据层实现

#include "database.h"
#include "utils/logger.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace sakura::data
{

// ═════════════════════════════════════════════════════════════════════════════
// 匿名命名空间 — SQL 语句常量
// ═════════════════════════════════════════════════════════════════════════════

namespace
{

constexpr const char* SQL_CREATE_SCORES = R"sql(
CREATE TABLE IF NOT EXISTS scores (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    chart_id         TEXT    NOT NULL,
    chart_title      TEXT    NOT NULL DEFAULT '',
    difficulty       TEXT    NOT NULL DEFAULT '',
    difficulty_level REAL    NOT NULL DEFAULT 0.0,
    score            INTEGER NOT NULL DEFAULT 0,
    accuracy         REAL    NOT NULL DEFAULT 0.0,
    max_combo        INTEGER NOT NULL DEFAULT 0,
    grade            TEXT    NOT NULL DEFAULT 'D',
    perfect_count    INTEGER NOT NULL DEFAULT 0,
    great_count      INTEGER NOT NULL DEFAULT 0,
    good_count       INTEGER NOT NULL DEFAULT 0,
    bad_count        INTEGER NOT NULL DEFAULT 0,
    miss_count       INTEGER NOT NULL DEFAULT 0,
    is_full_combo    INTEGER NOT NULL DEFAULT 0,
    is_all_perfect   INTEGER NOT NULL DEFAULT 0,
    played_at        INTEGER NOT NULL DEFAULT 0,
    hit_errors_json  TEXT    NOT NULL DEFAULT '[]'
);
)sql";

constexpr const char* SQL_CREATE_STATISTICS = R"sql(
CREATE TABLE IF NOT EXISTS statistics (
    key   TEXT    PRIMARY KEY NOT NULL,
    value REAL    NOT NULL DEFAULT 0.0
);
)sql";

constexpr const char* SQL_CREATE_ACHIEVEMENTS = R"sql(
CREATE TABLE IF NOT EXISTS achievements (
    id          TEXT    PRIMARY KEY NOT NULL,
    unlocked_at INTEGER NOT NULL DEFAULT 0
);
)sql";

// 列索引常量（与 SELECT 顺序对应）
enum ScoreCol
{
    COL_CHART_ID         = 0,
    COL_CHART_TITLE      = 1,
    COL_DIFFICULTY       = 2,
    COL_DIFFICULTY_LEVEL = 3,
    COL_SCORE            = 4,
    COL_ACCURACY         = 5,
    COL_MAX_COMBO        = 6,
    COL_GRADE            = 7,
    COL_PERFECT          = 8,
    COL_GREAT            = 9,
    COL_GOOD             = 10,
    COL_BAD              = 11,
    COL_MISS             = 12,
    COL_IS_FC            = 13,
    COL_IS_AP            = 14,
    COL_PLAYED_AT        = 15,
    COL_HIT_ERRORS       = 16
};

// 将 Grade 枚举转换为字符串
const char* GradeToStr(sakura::game::Grade g)
{
    switch (g)
    {
        case sakura::game::Grade::SS: return "SS";
        case sakura::game::Grade::S:  return "S";
        case sakura::game::Grade::A:  return "A";
        case sakura::game::Grade::B:  return "B";
        case sakura::game::Grade::C:  return "C";
        default:                      return "D";
    }
}

// 将字符串转换为 Grade 枚举
sakura::game::Grade StrToGrade(const char* s)
{
    if (!s) return sakura::game::Grade::D;
    if (std::strcmp(s, "SS") == 0) return sakura::game::Grade::SS;
    if (std::strcmp(s, "S")  == 0) return sakura::game::Grade::S;
    if (std::strcmp(s, "A")  == 0) return sakura::game::Grade::A;
    if (std::strcmp(s, "B")  == 0) return sakura::game::Grade::B;
    if (std::strcmp(s, "C")  == 0) return sakura::game::Grade::C;
    return sakura::game::Grade::D;
}

// 将 hit_errors vector 序列化为 JSON 字符串
std::string HitErrorsToJson(const std::vector<int>& errors)
{
    nlohmann::json j = errors;
    return j.dump();
}

// 将 JSON 字符串反序列化为 hit_errors vector
std::vector<int> JsonToHitErrors(const std::string& json)
{
    try
    {
        auto j = nlohmann::json::parse(json);
        return j.get<std::vector<int>>();
    }
    catch (...)
    {
        return {};
    }
}

// 获取当前 Unix 时间戳（秒）
long long NowTimestamp()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace (anonymous)

// ═════════════════════════════════════════════════════════════════════════════
// GetInstance
// ═════════════════════════════════════════════════════════════════════════════

Database& Database::GetInstance()
{
    static Database s_instance;
    return s_instance;
}

// ═════════════════════════════════════════════════════════════════════════════
// Initialize / Shutdown
// ═════════════════════════════════════════════════════════════════════════════

bool Database::Initialize(const std::string& dbPath)
{
    if (m_db)
    {
        LOG_WARN("[Database] 已经初始化，跳过重复调用");
        return true;
    }

    m_path = dbPath;

    // 确保父目录存在
    try
    {
        std::filesystem::path p(dbPath);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[Database] 创建目录失败: {}", e.what());
    }

    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK)
    {
        LOG_ERROR("[Database] 无法打开数据库 {}: {}", dbPath, sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // 启用 WAL 模式，提升并发写入性能
    ExecSQL("PRAGMA journal_mode=WAL;");
    // 外键约束
    ExecSQL("PRAGMA foreign_keys=ON;");

    if (!CreateTables())
    {
        LOG_ERROR("[Database] 建表失败");
        Shutdown();
        return false;
    }

    LOG_INFO("[Database] 数据库已打开: {}", dbPath);
    return true;
}

void Database::Shutdown()
{
    if (m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
        LOG_INFO("[Database] 数据库已关闭");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// CreateTables / ExecSQL
// ═════════════════════════════════════════════════════════════════════════════

bool Database::CreateTables()
{
    return ExecSQL(SQL_CREATE_SCORES)
        && ExecSQL(SQL_CREATE_STATISTICS)
        && ExecSQL(SQL_CREATE_ACHIEVEMENTS);
}

bool Database::ExecSQL(const char* sql) const
{
    if (!m_db) return false;
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        LOG_ERROR("[Database] SQL 执行失败: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// RowToGameResult — 从 sqlite3_stmt 读取一行结果
// 调用者保证列顺序与 ScoreCol 枚举对应
// ═════════════════════════════════════════════════════════════════════════════

sakura::game::GameResult Database::RowToGameResult(sqlite3_stmt* stmt) const
{
    sakura::game::GameResult r;

    auto getStr = [stmt](int col) -> std::string
    {
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return s ? s : "";
    };

    r.chartId        = getStr(COL_CHART_ID);
    r.chartTitle     = getStr(COL_CHART_TITLE);
    r.difficulty     = getStr(COL_DIFFICULTY);
    r.difficultyLevel = static_cast<float>(sqlite3_column_double(stmt, COL_DIFFICULTY_LEVEL));
    r.score          = sqlite3_column_int(stmt, COL_SCORE);
    r.accuracy       = static_cast<float>(sqlite3_column_double(stmt, COL_ACCURACY));
    r.maxCombo       = sqlite3_column_int(stmt, COL_MAX_COMBO);
    r.grade          = StrToGrade(reinterpret_cast<const char*>(sqlite3_column_text(stmt, COL_GRADE)));
    r.perfectCount   = sqlite3_column_int(stmt, COL_PERFECT);
    r.greatCount     = sqlite3_column_int(stmt, COL_GREAT);
    r.goodCount      = sqlite3_column_int(stmt, COL_GOOD);
    r.badCount       = sqlite3_column_int(stmt, COL_BAD);
    r.missCount      = sqlite3_column_int(stmt, COL_MISS);
    r.isFullCombo    = sqlite3_column_int(stmt, COL_IS_FC) != 0;
    r.isAllPerfect   = sqlite3_column_int(stmt, COL_IS_AP) != 0;
    r.playedAt       = sqlite3_column_int64(stmt, COL_PLAYED_AT);
    r.hitErrors      = JsonToHitErrors(getStr(COL_HIT_ERRORS));

    return r;
}

// ═════════════════════════════════════════════════════════════════════════════
// SaveScore
// ═════════════════════════════════════════════════════════════════════════════

bool Database::SaveScore(const sakura::game::GameResult& result)
{
    if (!m_db)
    {
        LOG_WARN("[Database] SaveScore: 数据库未打开");
        return false;
    }

    const char* sql = R"sql(
        INSERT INTO scores (
            chart_id, chart_title, difficulty, difficulty_level,
            score, accuracy, max_combo, grade,
            perfect_count, great_count, good_count, bad_count, miss_count,
            is_full_combo, is_all_perfect, played_at, hit_errors_json
        ) VALUES (?,?,?,?, ?,?,?,?, ?,?,?,?,?, ?,?,?,?);
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] SaveScore prepare 失败: {}", sqlite3_errmsg(m_db));
        return false;
    }

    long long playedAt = result.playedAt > 0 ? result.playedAt : NowTimestamp();
    std::string hitJson = HitErrorsToJson(result.hitErrors);

    sqlite3_bind_text (stmt,  1, result.chartId.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt,  2, result.chartTitle.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt,  3, result.difficulty.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, result.difficultyLevel);
    sqlite3_bind_int  (stmt,  5, result.score);
    sqlite3_bind_double(stmt, 6, result.accuracy);
    sqlite3_bind_int  (stmt,  7, result.maxCombo);
    sqlite3_bind_text (stmt,  8, GradeToStr(result.grade),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt,  9, result.perfectCount);
    sqlite3_bind_int  (stmt, 10, result.greatCount);
    sqlite3_bind_int  (stmt, 11, result.goodCount);
    sqlite3_bind_int  (stmt, 12, result.badCount);
    sqlite3_bind_int  (stmt, 13, result.missCount);
    sqlite3_bind_int  (stmt, 14, result.isFullCombo  ? 1 : 0);
    sqlite3_bind_int  (stmt, 15, result.isAllPerfect ? 1 : 0);
    sqlite3_bind_int64(stmt, 16, playedAt);
    sqlite3_bind_text (stmt, 17, hitJson.c_str(),             -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok)
        LOG_ERROR("[Database] SaveScore step 失败: {}", sqlite3_errmsg(m_db));
    else
        LOG_INFO("[Database] 已保存成绩: chart={} diff={} score={}",
                 result.chartId, result.difficulty, result.score);

    sqlite3_finalize(stmt);

    // 同步更新统计
    IncrementStatistic("total_play_count",   1.0);

    return ok;
}

// ═════════════════════════════════════════════════════════════════════════════
// GetBestScore
// ═════════════════════════════════════════════════════════════════════════════

std::optional<sakura::game::GameResult> Database::GetBestScore(
    const std::string& chartId,
    const std::string& difficulty) const
{
    if (!m_db) return std::nullopt;

    const char* sql = R"sql(
        SELECT chart_id, chart_title, difficulty, difficulty_level,
               score, accuracy, max_combo, grade,
               perfect_count, great_count, good_count, bad_count, miss_count,
               is_full_combo, is_all_perfect, played_at, hit_errors_json
        FROM scores
        WHERE chart_id = ? AND difficulty = ?
        ORDER BY score DESC
        LIMIT 1;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] GetBestScore prepare 失败: {}", sqlite3_errmsg(m_db));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, chartId.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, difficulty.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<sakura::game::GameResult> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = RowToGameResult(stmt);

    sqlite3_finalize(stmt);
    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
// GetTopScores
// ═════════════════════════════════════════════════════════════════════════════

std::vector<sakura::game::GameResult> Database::GetTopScores(
    const std::string& chartId,
    const std::string& difficulty,
    int limit) const
{
    if (!m_db) return {};

    const char* sql = R"sql(
        SELECT chart_id, chart_title, difficulty, difficulty_level,
               score, accuracy, max_combo, grade,
               perfect_count, great_count, good_count, bad_count, miss_count,
               is_full_combo, is_all_perfect, played_at, hit_errors_json
        FROM scores
        WHERE chart_id = ? AND difficulty = ?
        ORDER BY score DESC
        LIMIT ?;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] GetTopScores prepare 失败: {}", sqlite3_errmsg(m_db));
        return {};
    }

    sqlite3_bind_text(stmt, 1, chartId.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, difficulty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 3, limit);

    std::vector<sakura::game::GameResult> results;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(RowToGameResult(stmt));

    sqlite3_finalize(stmt);
    return results;
}

// ═════════════════════════════════════════════════════════════════════════════
// GetAllBestScores — 每个 (chart_id, difficulty) 只保留最高分
// ═════════════════════════════════════════════════════════════════════════════

std::vector<sakura::game::GameResult> Database::GetAllBestScores() const
{
    if (!m_db) return {};

    const char* sql = R"sql(
        SELECT chart_id, chart_title, difficulty, difficulty_level,
               score, accuracy, max_combo, grade,
               perfect_count, great_count, good_count, bad_count, miss_count,
               is_full_combo, is_all_perfect, played_at, hit_errors_json
        FROM scores AS s1
        WHERE score = (
            SELECT MAX(score) FROM scores AS s2
            WHERE s2.chart_id = s1.chart_id AND s2.difficulty = s1.difficulty
        )
        GROUP BY chart_id, difficulty
        ORDER BY difficulty_level DESC;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] GetAllBestScores prepare 失败: {}", sqlite3_errmsg(m_db));
        return {};
    }

    std::vector<sakura::game::GameResult> results;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(RowToGameResult(stmt));

    sqlite3_finalize(stmt);
    return results;
}

// ═════════════════════════════════════════════════════════════════════════════
// IncrementStatistic / GetStatistic
// ═════════════════════════════════════════════════════════════════════════════

bool Database::IncrementStatistic(const std::string& key, double amount)
{
    if (!m_db) return false;

    const char* sql = R"sql(
        INSERT INTO statistics (key, value)
        VALUES (?, ?)
        ON CONFLICT(key) DO UPDATE SET value = value + excluded.value;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] IncrementStatistic prepare 失败: {}", sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text  (stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, amount);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

double Database::GetStatistic(const std::string& key) const
{
    if (!m_db) return 0.0;

    const char* sql = "SELECT value FROM statistics WHERE key = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0.0;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    double val = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        val = sqlite3_column_double(stmt, 0);

    sqlite3_finalize(stmt);
    return val;
}

long long Database::GetTotalPlayCount() const
{
    return static_cast<long long>(GetStatistic("total_play_count"));
}

double Database::GetTotalPlayTimeSeconds() const
{
    return GetStatistic("total_play_time_seconds");
}

// ═════════════════════════════════════════════════════════════════════════════
// SaveAchievement / GetAchievements / IsAchievementUnlocked
// ═════════════════════════════════════════════════════════════════════════════

bool Database::SaveAchievement(const std::string& id)
{
    if (!m_db) return false;

    // 已解锁则忽略（INSERT OR IGNORE）
    const char* sql =
        "INSERT OR IGNORE INTO achievements (id, unlocked_at) VALUES (?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] SaveAchievement prepare 失败: {}", sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text (stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, NowTimestamp());

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (ok)
        LOG_INFO("[Database] 解锁成就: {}", id);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<AchievementRecord> Database::GetAchievements() const
{
    if (!m_db) return {};

    const char* sql =
        "SELECT id, unlocked_at FROM achievements ORDER BY unlocked_at ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return {};

    std::vector<AchievementRecord> records;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        AchievementRecord rec;
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.id          = s ? s : "";
        rec.unlockedAt  = sqlite3_column_int64(stmt, 1);
        records.push_back(std::move(rec));
    }

    sqlite3_finalize(stmt);
    return records;
}

bool Database::IsAchievementUnlocked(const std::string& id) const
{
    if (!m_db) return false;

    const char* sql =
        "SELECT COUNT(*) FROM achievements WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        found = sqlite3_column_int(stmt, 0) > 0;

    sqlite3_finalize(stmt);
    return found;
}

} // namespace sakura::data
