#pragma once

// online_cache.h — Stale-While-Revalidate + HTTP ETag 双层持久化缓存
// 内存 LRU 哈希表 + Paths::User("cache/online_cache.json") 原子落盘。

#include <cstddef>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sakura::net
{

struct CacheLookupResult
{
    bool        found       = false;
    bool        isStale     = false;
    std::string etag;
    std::string payloadJson;
    long long   updatedAt   = 0;
};

class OnlineCache
{
public:
    static constexpr std::size_t MAX_ENTRIES = 256;

    static OnlineCache& GetInstance();

    explicit OnlineCache(std::filesystem::path customCachePath = {});

    OnlineCache(const OnlineCache&)            = delete;
    OnlineCache& operator=(const OnlineCache&) = delete;

    void SetCustomCachePath(std::filesystem::path customCachePath);
    std::filesystem::path ResolveCachePath() const;

    bool Load();
    bool Save() const;

    CacheLookupResult Lookup(const std::string& key,
                             int softTtlSec = 30,
                             int hardTtlSec = 86400,
                             long long nowEpochSec = 0) const;

    void Put(const std::string& key,
             const std::string& payloadJson,
             const std::string& etag = {},
             long long nowEpochSec = 0);

    void TouchFresh(const std::string& key, long long nowEpochSec = 0);
    void MarkStale(const std::string& keyPrefix);
    void Clear();

private:
    struct CacheEntry
    {
        std::string                      key;
        std::string                      etag;
        std::string                      payloadJson;
        long long                        updatedAt   = 0;
        long long                        savedAt     = 0;
        bool                             forcedStale = false;
        std::list<std::string>::iterator lruIt;
    };

    std::filesystem::path ResolveCachePathUnlocked() const;
    void EnsureLoadedUnlocked() const;
    bool LoadUnlocked() const;
    bool SaveUnlocked() const;
    void PromoteUnlocked(CacheEntry& entry) const;
    void TrimToCapacityUnlocked() const;

    mutable std::mutex                                  m_mutex;
    std::filesystem::path                               m_customCachePath;
    mutable bool                                        m_loaded = false;
    mutable std::list<std::string>                      m_lruOrder;
    mutable std::unordered_map<std::string, CacheEntry> m_entries;
};

} // namespace sakura::net
