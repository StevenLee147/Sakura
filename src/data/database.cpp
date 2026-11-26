// database.cpp — SQLite3 数据层实现

#include "database.h"
#include "utils/logger.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
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

std::string ReadEnvironmentVariable(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value)
        return {};

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

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
    hit_errors_json  TEXT    NOT NULL DEFAULT '[]',
    chart_hash       TEXT    NOT NULL DEFAULT ''
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

constexpr const char* SQL_CREATE_COURSE_RECORDS = R"sql(
CREATE TABLE IF NOT EXISTS course_records (
    course_id      TEXT    PRIMARY KEY NOT NULL,
    badge_text     TEXT    NOT NULL DEFAULT '',
    dan_rank       INTEGER NOT NULL DEFAULT 0,
    total_score    INTEGER NOT NULL DEFAULT 0,
    avg_accuracy   REAL    NOT NULL DEFAULT 0.0,
    remaining_life REAL    NOT NULL DEFAULT 0.0,
    cleared        INTEGER NOT NULL DEFAULT 0,
    cleared_at     INTEGER NOT NULL DEFAULT 0
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

std::string ResolveDatabasePath(std::string_view dbPath)
{
    if (!dbPath.empty())
        return std::string(dbPath);

    std::string envPath = ReadEnvironmentVariable("SAKURA_DB_PATH");
    if (!envPath.empty())
        return envPath;

    return "data/sakura.db";
}

sakura::game::CourseRecord RowToCourseRecord(sqlite3_stmt* stmt)
{
    auto getStr = [stmt](int col) -> std::string
    {
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return s ? s : "";
    };

    sakura::game::CourseRecord rec;
    rec.courseId      = getStr(0);
    rec.badgeText     = getStr(1);
    rec.danRank       = sqlite3_column_int(stmt, 2);
    rec.totalScore    = sqlite3_column_int(stmt, 3);
    rec.avgAccuracy   = static_cast<float>(sqlite3_column_double(stmt, 4));
    rec.remainingLife = static_cast<float>(sqlite3_column_double(stmt, 5));
    rec.cleared       = sqlite3_column_int(stmt, 6) != 0;
    rec.clearedAt     = static_cast<long long>(sqlite3_column_int64(stmt, 7));
    return rec;
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

bool Database::Initialize(std::string_view dbPath)
{
    if (m_db)
    {
        LOG_WARN("[Database] 已经初始化，跳过重复调用");
        return true;
    }

    m_path = ResolveDatabasePath(dbPath);

    // 确保父目录存在
    try
    {
        std::filesystem::path p(m_path);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[Database] 创建目录失败: {}", e.what());
    }

    int rc = sqlite3_open(m_path.c_str(), &m_db);
    if (rc != SQLITE_OK)
    {
        LOG_ERROR("[Database] 无法打开数据库 {}: {}", m_path, sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    if (m_path != ":memory:")
    {
        sqlite3_stmt* checkStmt = nullptr;
        bool healthy = false;
        if (sqlite3_prepare_v2(m_db, "PRAGMA quick_check", -1, &checkStmt, nullptr) == SQLITE_OK && checkStmt)
        {
            if (sqlite3_step(checkStmt) == SQLITE_ROW)
            {
                const char* status = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 0));
                healthy = (status && std::strcmp(status, "ok") == 0);
            }
        }
        if (checkStmt) sqlite3_finalize(checkStmt);

        if (!healthy)
        {
            LOG_WARN("[Database] 检测到损坏数据库 {}，自动备份并重建干净库", m_path);
            sqlite3_close(m_db);
            m_db = nullptr;
            std::error_code ec;
            const std::filesystem::path dbFile(m_path);
            if (std::filesystem::exists(dbFile, ec))
            {
                std::filesystem::copy_file(dbFile, m_path + ".corrupt.bak",
                    std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::remove(dbFile, ec);
            }
            std::filesystem::remove(m_path + "-wal", ec);
            std::filesystem::remove(m_path + "-shm", ec);
            if (sqlite3_open(m_path.c_str(), &m_db) != SQLITE_OK)
            {
                if (m_db) sqlite3_close(m_db);
                m_db = nullptr;
                return false;
            }
        }
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

    LOG_INFO("[Database] 数据库已打开: {}", m_path);
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
    sqlite3_stmt* statement=nullptr;
    int version=0;bool existed=false;
    if(sqlite3_prepare_v2(m_db,"PRAGMA user_version",-1,&statement,nullptr)==SQLITE_OK && sqlite3_step(statement)==SQLITE_ROW)version=sqlite3_column_int(statement,0);
    sqlite3_finalize(statement);statement=nullptr;
    if(sqlite3_prepare_v2(m_db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='scores'",-1,&statement,nullptr)==SQLITE_OK)existed=sqlite3_step(statement)==SQLITE_ROW;
    sqlite3_finalize(statement);
    if(version>3){LOG_ERROR("[Database] 存档来自更新版本，保持原样");return false;}
    if(!ExecSQL("BEGIN IMMEDIATE"))return false;
    bool ok=true;
    if(version<2 && existed)ok=ExecSQL("ALTER TABLE scores RENAME TO legacy_scores_v1");
    if(version==2 && existed)ok=ExecSQL("ALTER TABLE scores ADD COLUMN chart_hash TEXT NOT NULL DEFAULT ''");
    ok=ok && ExecSQL(SQL_CREATE_SCORES) && ExecSQL(SQL_CREATE_STATISTICS) && ExecSQL(SQL_CREATE_ACHIEVEMENTS)
        && ExecSQL(SQL_CREATE_COURSE_RECORDS)
        && ExecSQL("CREATE INDEX IF NOT EXISTS idx_scores_revision ON scores(chart_id,difficulty,chart_hash,score DESC)")
        && ExecSQL("PRAGMA user_version=3");
    if(ok)ok=ExecSQL("COMMIT");
    if(!ok)ExecSQL("ROLLBACK");
    return ok;
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
    r.chartHash      = getStr(17);

    return r;
}

// ═════════════════════════════════════════════════════════════════════════════
// SaveScore
// ═════════════════════════════════════════════════════════════════════════════

bool Database::BackupTo(const std::string& path) const
{
    if (!m_db) return false;
    sqlite3* destination = nullptr;
    if (sqlite3_open(path.c_str(), &destination) != SQLITE_OK)
    { if (destination) sqlite3_close(destination); return false; }
    auto* backup = sqlite3_backup_init(destination, "main", m_db, "main");
    const bool copied = backup && sqlite3_backup_step(backup, -1) == SQLITE_DONE;
    const bool finished = backup && sqlite3_backup_finish(backup) == SQLITE_OK;
    sqlite3_close(destination);
    return copied && finished;
}

bool Database::RestoreFrom(const std::string& path)
{
    if (!m_db) return false;
    sqlite3* source = nullptr;
    if (sqlite3_open_v2(path.c_str(), &source, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    { if (source) sqlite3_close(source); return false; }
    sqlite3_stmt* check = nullptr;
    bool valid = sqlite3_prepare_v2(source, "PRAGMA quick_check", -1, &check, nullptr) == SQLITE_OK;
    valid = valid && sqlite3_step(check) == SQLITE_ROW &&
        std::string(reinterpret_cast<const char*>(sqlite3_column_text(check, 0))) == "ok";
    if (check) sqlite3_finalize(check);
    check = nullptr;
    for(const char* query:{"SELECT chart_id,chart_title,difficulty,difficulty_level,score,accuracy,max_combo,grade,perfect_count,great_count,good_count,bad_count,miss_count,is_full_combo,is_all_perfect,played_at,hit_errors_json FROM scores LIMIT 0",
        "SELECT key,value FROM statistics LIMIT 0","SELECT id,unlocked_at FROM achievements LIMIT 0"}){
        if(sqlite3_prepare_v2(source,query,-1,&check,nullptr)!=SQLITE_OK)valid=false;
        if(check)sqlite3_finalize(check);check=nullptr;
    }
    if (!valid) { sqlite3_close(source); return false; }
    Database staged;
    if(!staged.Initialize(":memory:")){sqlite3_close(source);return false;}
    auto copy=[](sqlite3* destination,sqlite3* origin){
        auto* backup=sqlite3_backup_init(destination,"main",origin,"main");
        if(!backup)return false;
        const bool copied=sqlite3_backup_step(backup,-1)==SQLITE_DONE;
        const bool finished=sqlite3_backup_finish(backup)==SQLITE_OK;
        return copied&&finished;
    };
    const bool stagedOk=copy(staged.m_db,source);
    sqlite3_close(source);
    return stagedOk && staged.CreateTables() && copy(m_db,staged.m_db);
}

bool Database::SaveScore(const sakura::game::GameResult& result)
{
    if (result.assisted) return false;
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
            is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
        ) VALUES (?,?,?,?, ?,?,?,?, ?,?,?,?,?, ?,?,?,?,?);
    )sql";

    if(!ExecSQL("BEGIN IMMEDIATE"))return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        ExecSQL("ROLLBACK");
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

    sqlite3_bind_text(stmt, 18, result.chartHash.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok)
        LOG_ERROR("[Database] SaveScore step 失败: {}", sqlite3_errmsg(m_db));
    else
        LOG_INFO("[Database] 已保存成绩: chart={} diff={} score={}",
                 result.chartId, result.difficulty, result.score);

    sqlite3_finalize(stmt);

    // A failed insert must not increment play counts.
    if (ok)
    {
        ok=IncrementStatistic("total_play_count", 1.0)
            && IncrementStatistic("total_play_time_seconds", result.playTimeSeconds)
            && (result.synergyCount <= 0 || IncrementStatistic("total_synergy_hits", static_cast<double>(result.synergyCount)))
            && (result.thumbHitCount <= 0 || IncrementStatistic("total_thumb_hits", static_cast<double>(result.thumbHitCount)))
            && (result.feverCount <= 0 || IncrementStatistic("total_fever_triggers", static_cast<double>(result.feverCount)));
    }

    if(ok)ok=ExecSQL("COMMIT");
    if(!ok)ExecSQL("ROLLBACK");
    return ok;
}

// ═════════════════════════════════════════════════════════════════════════════
// GetBestScore
// ═════════════════════════════════════════════════════════════════════════════

std::optional<sakura::game::GameResult> Database::GetBestScore(
    const std::string& chartId,
    const std::string& difficulty, const std::string& chartHash) const
{
    if (!m_db) return std::nullopt;

    const char* sql = R"sql(
        SELECT chart_id, chart_title, difficulty, difficulty_level,
               score, accuracy, max_combo, grade,
               perfect_count, great_count, good_count, bad_count, miss_count,
               is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
        FROM scores
        WHERE chart_id = ? AND difficulty = ? AND (? = '' OR chart_hash = ?)
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

    sqlite3_bind_text(stmt, 3, chartHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, chartHash.c_str(), -1, SQLITE_TRANSIENT);
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
               is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
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
               is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
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

int Database::GetHighestScore() const
{
    if (!m_db) return 0;

    const char* sql = "SELECT COALESCE(MAX(score), 0) FROM scores;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

float Database::GetHighestAccuracy() const
{
    if (!m_db) return 0.0f;

    const char* sql = "SELECT COALESCE(MAX(accuracy), 0.0) FROM scores;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0.0f;

    float value = 0.0f;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = static_cast<float>(sqlite3_column_double(stmt, 0));

    sqlite3_finalize(stmt);
    return value;
}

int Database::GetHighestCombo() const
{
    if (!m_db) return 0;

    const char* sql = "SELECT COALESCE(MAX(max_combo), 0) FROM scores;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

bool Database::HasAnyFullCombo() const
{
    if (!m_db) return false;

    const char* sql = "SELECT COUNT(*) FROM scores WHERE is_full_combo = 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bool value = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0) > 0;

    sqlite3_finalize(stmt);
    return value;
}

bool Database::HasAnyAllPerfect() const
{
    if (!m_db) return false;

    const char* sql = "SELECT COUNT(*) FROM scores WHERE is_all_perfect = 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bool value = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0) > 0;

    sqlite3_finalize(stmt);
    return value;
}

double Database::GetAverageAccuracy() const
{
    if (!m_db) return 0.0;

    const char* sql = "SELECT COALESCE(AVG(accuracy), 0.0) FROM scores;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0.0;

    double value = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_double(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

int Database::GetTotalNotesJudged() const
{
    if (!m_db) return 0;

    const char* sql = R"sql(
        SELECT COALESCE(SUM(perfect_count + great_count + good_count + bad_count + miss_count), 0)
        FROM scores;
    )sql";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

int Database::GetFullComboCount() const
{
    if (!m_db) return 0;

    const char* sql = "SELECT COUNT(*) FROM scores WHERE is_full_combo = 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

int Database::GetAllPerfectCount() const
{
    if (!m_db) return 0;

    const char* sql = "SELECT COUNT(*) FROM scores WHERE is_all_perfect = 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

std::array<int, 6> Database::GetGradeDistribution() const
{
    std::array<int, 6> counts = { 0, 0, 0, 0, 0, 0 };
    if (!m_db) return counts;

    const char* sql = R"sql(
        SELECT grade, COUNT(*)
        FROM scores
        GROUP BY grade;
    )sql";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return counts;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* grade = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int count = sqlite3_column_int(stmt, 1);

        if (!grade) continue;
        if (std::strcmp(grade, "SS") == 0) counts[0] = count;
        else if (std::strcmp(grade, "S") == 0) counts[1] = count;
        else if (std::strcmp(grade, "A") == 0) counts[2] = count;
        else if (std::strcmp(grade, "B") == 0) counts[3] = count;
        else if (std::strcmp(grade, "C") == 0) counts[4] = count;
        else counts[5] = count;
    }

    sqlite3_finalize(stmt);
    return counts;
}

std::vector<sakura::game::GameResult> Database::GetRecentScores(int limit) const
{
    if (!m_db) return {};

    const char* sql = R"sql(
        SELECT chart_id, chart_title, difficulty, difficulty_level,
               score, accuracy, max_combo, grade,
               perfect_count, great_count, good_count, bad_count, miss_count,
               is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
        FROM scores
        ORDER BY played_at DESC, id DESC
        LIMIT ?;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return {};

    sqlite3_bind_int(stmt, 1, limit);

    std::vector<sakura::game::GameResult> results;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(RowToGameResult(stmt));

    sqlite3_finalize(stmt);
    return results;
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

bool Database::MergeCloudScore(const sakura::game::GameResult& cloudScore)
{
    if (!m_db || cloudScore.assisted || cloudScore.chartId.empty() || cloudScore.difficulty.empty())
        return false;

    const auto localBest = GetBestScore(cloudScore.chartId, cloudScore.difficulty, cloudScore.chartHash);
    if (localBest.has_value())
    {
        if (cloudScore.score < localBest->score)
            return false;
        if (cloudScore.score == localBest->score && cloudScore.accuracy <= localBest->accuracy + 0.001f)
            return false;
    }

    const char* sql = R"sql(
        INSERT INTO scores (
            chart_id, chart_title, difficulty, difficulty_level,
            score, accuracy, max_combo, grade,
            perfect_count, great_count, good_count, bad_count, miss_count,
            is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
        ) VALUES (?,?,?,?, ?,?,?,?, ?,?,?,?,?, ?,?,?,?,?);
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    const long long playedAt = cloudScore.playedAt > 0 ? cloudScore.playedAt : NowTimestamp();
    const std::string hitJson = HitErrorsToJson(cloudScore.hitErrors);
    sqlite3_bind_text  (stmt,  1, cloudScore.chartId.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt,  2, cloudScore.chartTitle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt,  3, cloudScore.difficulty.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt,  4, cloudScore.difficultyLevel);
    sqlite3_bind_int   (stmt,  5, cloudScore.score);
    sqlite3_bind_double(stmt,  6, cloudScore.accuracy);
    sqlite3_bind_int   (stmt,  7, cloudScore.maxCombo);
    sqlite3_bind_text  (stmt,  8, GradeToStr(cloudScore.grade),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt,  9, cloudScore.perfectCount);
    sqlite3_bind_int   (stmt, 10, cloudScore.greatCount);
    sqlite3_bind_int   (stmt, 11, cloudScore.goodCount);
    sqlite3_bind_int   (stmt, 12, cloudScore.badCount);
    sqlite3_bind_int   (stmt, 13, cloudScore.missCount);
    sqlite3_bind_int   (stmt, 14, cloudScore.isFullCombo  ? 1 : 0);
    sqlite3_bind_int   (stmt, 15, cloudScore.isAllPerfect ? 1 : 0);
    sqlite3_bind_int64 (stmt, 16, playedAt);
    sqlite3_bind_text  (stmt, 17, hitJson.c_str(),               -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 18, cloudScore.chartHash.c_str(),  -1, SQLITE_TRANSIENT);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (ok && GetTotalPlayCount() == 0)
        IncrementStatistic("total_play_count", 1.0);
    return ok;
}

bool Database::MergeCloudAchievement(const std::string& id, long long unlockedAt)
{
    if (!m_db || id.empty())
        return false;

    const bool wasUnlocked = IsAchievementUnlocked(id);
    const long long ts = unlockedAt > 0 ? unlockedAt : NowTimestamp();
    const char* sql = R"sql(
        INSERT INTO achievements (id, unlocked_at)
        VALUES (?, ?)
        ON CONFLICT(id) DO UPDATE SET
            unlocked_at = MIN(achievements.unlocked_at, excluded.unlocked_at);
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text (stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, ts);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok && !wasUnlocked;
}

// ═════════════════════════════════════════════════════════════════════════════
// 段位认定记录 (v1.4.0)
// ═════════════════════════════════════════════════════════════════════════════

bool Database::SaveCourseRecord(const sakura::game::CourseRecord& record)
{
    if (!m_db)
    {
        LOG_WARN("[Database] SaveCourseRecord: 数据库未打开");
        return false;
    }
    if (record.courseId.empty())
        return false;

    const char* sql = R"sql(
        INSERT INTO course_records (
            course_id, badge_text, dan_rank, total_score,
            avg_accuracy, remaining_life, cleared, cleared_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(course_id) DO UPDATE SET
            badge_text     = CASE WHEN excluded.badge_text != '' THEN excluded.badge_text ELSE course_records.badge_text END,
            dan_rank       = MAX(course_records.dan_rank, excluded.dan_rank),
            total_score    = MAX(course_records.total_score, excluded.total_score),
            avg_accuracy   = MAX(course_records.avg_accuracy, excluded.avg_accuracy),
            remaining_life = MAX(course_records.remaining_life, excluded.remaining_life),
            cleared        = MAX(course_records.cleared, excluded.cleared),
            cleared_at     = CASE
                WHEN course_records.cleared_at > 0 AND excluded.cleared_at > 0
                    THEN MIN(course_records.cleared_at, excluded.cleared_at)
                WHEN excluded.cleared_at > 0
                    THEN excluded.cleared_at
                ELSE course_records.cleared_at
            END;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] SaveCourseRecord prepare 失败: {}", sqlite3_errmsg(m_db));
        return false;
    }

    const long long clearedAt = (record.cleared && record.clearedAt <= 0)
        ? NowTimestamp()
        : (record.clearedAt > 0 ? record.clearedAt : 0LL);

    sqlite3_bind_text  (stmt, 1, record.courseId.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, record.badgeText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt, 3, record.danRank);
    sqlite3_bind_int   (stmt, 4, record.totalScore);
    sqlite3_bind_double(stmt, 5, static_cast<double>(record.avgAccuracy));
    sqlite3_bind_double(stmt, 6, static_cast<double>(record.remainingLife));
    sqlite3_bind_int   (stmt, 7, record.cleared ? 1 : 0);
    sqlite3_bind_int64 (stmt, 8, clearedAt);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok)
        LOG_ERROR("[Database] SaveCourseRecord step 失败: {}", sqlite3_errmsg(m_db));
    else
        LOG_INFO("[Database] 已保存段位记录: course={} score={} cleared={}",
                 record.courseId, record.totalScore, record.cleared);

    sqlite3_finalize(stmt);
    return ok;
}

bool Database::MergeCloudCourseRecord(const sakura::game::CourseRecord& record)
{
    if (!m_db || record.courseId.empty())
        return false;

    const auto existing = GetCourseRecord(record.courseId);
    if (existing.has_value())
    {
        const bool improvesCleared   = record.cleared && !existing->cleared;
        const bool improvesScore     = record.totalScore > existing->totalScore;
        const bool improvesAccuracy  = record.avgAccuracy > existing->avgAccuracy + 1e-5f;
        const bool improvesClearedAt = record.clearedAt > 0 &&
            (existing->clearedAt <= 0 || record.clearedAt < existing->clearedAt);

        if (!improvesCleared && !improvesScore && !improvesAccuracy && !improvesClearedAt)
            return false;
    }

    return SaveCourseRecord(record);
}

std::optional<sakura::game::CourseRecord> Database::GetCourseRecord(const std::string& courseId) const
{
    if (!m_db || courseId.empty())
        return std::nullopt;

    const char* sql = R"sql(
        SELECT course_id, badge_text, dan_rank, total_score,
               avg_accuracy, remaining_life, cleared, cleared_at
        FROM course_records
        WHERE course_id = ?
        LIMIT 1;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] GetCourseRecord prepare 失败: {}", sqlite3_errmsg(m_db));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, courseId.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<sakura::game::CourseRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = RowToCourseRecord(stmt);

    sqlite3_finalize(stmt);
    return result;
}

std::vector<sakura::game::CourseRecord> Database::GetAllCourseRecords() const
{
    if (!m_db)
        return {};

    const char* sql = R"sql(
        SELECT course_id, badge_text, dan_rank, total_score,
               avg_accuracy, remaining_life, cleared, cleared_at
        FROM course_records
        ORDER BY dan_rank ASC, course_id ASC;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] GetAllCourseRecords prepare 失败: {}", sqlite3_errmsg(m_db));
        return {};
    }

    std::vector<sakura::game::CourseRecord> records;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        records.push_back(RowToCourseRecord(stmt));

    sqlite3_finalize(stmt);
    return records;
}

std::optional<sakura::game::CourseRecord> Database::GetHighestClearedCourse() const
{
    if (!m_db)
        return std::nullopt;

    const char* sql = R"sql(
        SELECT course_id, badge_text, dan_rank, total_score,
               avg_accuracy, remaining_life, cleared, cleared_at
        FROM course_records
        WHERE cleared = 1
        ORDER BY dan_rank DESC, total_score DESC
        LIMIT 1;
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("[Database] GetHighestClearedCourse prepare 失败: {}", sqlite3_errmsg(m_db));
        return std::nullopt;
    }

    std::optional<sakura::game::CourseRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = RowToCourseRecord(stmt);

    sqlite3_finalize(stmt);
    return result;
}

} // namespace sakura::data
