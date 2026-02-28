#pragma once

// database.h — 本地 SQLite3 数据层
// 提供成绩、统计、成就的持久化存储

#include "game/chart.h"

#include <optional>
#include <string>
#include <vector>

// 前向声明 sqlite3 句柄，避免污染全局命名空间
struct sqlite3;
struct sqlite3_stmt;

namespace sakura::data
{

// AchievementRecord — 成就记录
struct AchievementRecord
{
    std::string id;
    long long   unlockedAt = 0;  // Unix 时间戳（秒）
};

// ── Database ──────────────────────────────────────────────────────────────────
// 单例。通过 Initialize() 开启数据库，Shutdown() 关闭。
// 所有公有方法在未调用 Initialize() 时安全返回默认值/false。
class Database
{
public:
    static Database& GetInstance();

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    // 打开（或创建）数据库，自动建表
    // dbPath：相对于可执行文件目录的路径（默认 "data/sakura.db"）
    bool Initialize(const std::string& dbPath = "data/sakura.db");
    void Shutdown();

    bool IsOpen() const { return m_db != nullptr; }

    // ── 成绩 ─────────────────────────────────────────────────────────────────

    // 将一局游戏结果写入 scores 表
    bool SaveScore(const sakura::game::GameResult& result);

    // 返回某谱面某难度的最高分记录（无记录则返回空 optional）
    std::optional<sakura::game::GameResult> GetBestScore(
        const std::string& chartId,
        const std::string& difficulty) const;

    // 返回某谱面某难度排行榜（按 score 降序，最多 limit 条）
    std::vector<sakura::game::GameResult> GetTopScores(
        const std::string& chartId,
        const std::string& difficulty,
        int limit = 10) const;

    // 返回所有曲目各难度最高分（用于全局 PP 计算）
    std::vector<sakura::game::GameResult> GetAllBestScores() const;

    // ── 统计 ─────────────────────────────────────────────────────────────────

    // 将统计项 key 的值增加 amount（若不存在则从 0 开始）
    bool   IncrementStatistic(const std::string& key, double amount = 1.0);

    // 读取统计项，不存在时返回 0.0
    double GetStatistic(const std::string& key) const;

    // 便捷接口
    long long GetTotalPlayCount()       const;
    double    GetTotalPlayTimeSeconds() const;

    // ── 成就 ─────────────────────────────────────────────────────────────────

    // 记录成就解锁（已存在则忽略，不覆盖时间戳）
    bool SaveAchievement(const std::string& id);

    // 返回所有已解锁成就列表（按解锁时间升序）
    std::vector<AchievementRecord> GetAchievements() const;

    // 检查某成就是否已解锁
    bool IsAchievementUnlocked(const std::string& id) const;

    // 禁止拷贝与移动
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&)                 = delete;
    Database& operator=(Database&&)      = delete;

private:
    Database()  = default;
    ~Database() { Shutdown(); }

    bool CreateTables();
    bool ExecSQL(const char* sql) const;

    // 将当前 sqlite3_stmt 行的各列读入 GameResult
    sakura::game::GameResult RowToGameResult(sqlite3_stmt* stmt) const;

    sqlite3*    m_db   = nullptr;
    std::string m_path;
};

} // namespace sakura::data
