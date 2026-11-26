#pragma once

// offline_queue.h — OfflineQueue v2 统一离线动作队列与指数退避持久化
// 支持常规成绩、今日挑战与樱华道场段位记录，并无缝迁移旧版 pending_scores.json。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace sakura::net
{

enum class PendingActionType
{
    StandardScore,
    DailyScore,
    DanCourseRecord
};

struct PendingActionItem
{
    std::string       id;
    PendingActionType type         = PendingActionType::StandardScore;
    std::string       payloadJson;
    std::string       dateUtc8;
    int               attemptCount = 0;
    long long         nextRetryAt  = 0;
    long long         createdAt    = 0;
    std::string       lastError;
};

class OfflineQueue
{
public:
    static OfflineQueue& GetInstance();

    explicit OfflineQueue(std::filesystem::path customQueuePath = {},
                          std::filesystem::path legacyScoresPath = {});

    OfflineQueue(const OfflineQueue&)            = delete;
    OfflineQueue& operator=(const OfflineQueue&) = delete;

    void SetPaths(std::filesystem::path customQueuePath,
                  std::filesystem::path legacyScoresPath = {});
    std::filesystem::path ResolveQueuePath() const;

    bool Load();
    bool Save() const;

    std::string Enqueue(PendingActionType type,
                        const std::string& payloadJson,
                        const std::string& dateUtc8 = {},
                        const std::string& dedupeId = {});

    std::vector<PendingActionItem> GetReadyItems(long long nowEpochSec = 0) const;
    std::vector<PendingActionItem> GetAllItems() const;
    std::size_t GetCount() const;

    void MarkSucceeded(const std::string& id);
    void MarkRetryableFailure(const std::string& id,
                              const std::string& error,
                              long long nowEpochSec = 0);
    void MarkPermanentFailure(const std::string& id, const std::string& error);

    static int ComputeBackoffSeconds(int attemptCount);

private:
    std::filesystem::path ResolveQueuePathUnlocked() const;
    std::filesystem::path ResolveLegacyPathUnlocked() const;
    void EnsureLoadedUnlocked() const;
    bool LoadUnlocked() const;
    bool SaveUnlocked() const;
    std::string GenerateActionIdUnlocked(long long nowSec) const;

    mutable std::mutex                     m_mutex;
    std::filesystem::path                  m_customQueuePath;
    std::filesystem::path                  m_legacyScoresPath;
    mutable bool                           m_loaded    = false;
    mutable std::uint64_t                  m_idCounter = 0;
    mutable std::vector<PendingActionItem> m_items;
};

} // namespace sakura::net
