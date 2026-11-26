#include "net/online_cache.h"

#include "core/paths.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace sakura::net
{

namespace
{

long long NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

long long ResolveEpochSeconds(long long nowEpochSec)
{
    return (nowEpochSec > 0) ? nowEpochSec : NowUnixSeconds();
}

std::string JsonStringOr(const nlohmann::json& obj, const char* key, const char* altKey = nullptr)
{
    if (obj.is_object())
    {
        if (obj.contains(key) && obj[key].is_string())
        {
            return obj[key].get<std::string>();
        }
        if (altKey != nullptr && obj.contains(altKey) && obj[altKey].is_string())
        {
            return obj[altKey].get<std::string>();
        }
    }
    return {};
}

long long JsonInt64Or(const nlohmann::json& obj,
                      const char* key,
                      long long fallback,
                      const char* altKey = nullptr)
{
    if (obj.is_object())
    {
        if (obj.contains(key) && obj[key].is_number_integer())
        {
            return obj[key].get<long long>();
        }
        if (altKey != nullptr && obj.contains(altKey) && obj[altKey].is_number_integer())
        {
            return obj[altKey].get<long long>();
        }
    }
    return fallback;
}

bool JsonBoolOr(const nlohmann::json& obj,
                const char* key,
                bool fallback,
                const char* altKey = nullptr)
{
    if (obj.is_object())
    {
        if (obj.contains(key) && obj[key].is_boolean())
        {
            return obj[key].get<bool>();
        }
        if (altKey != nullptr && obj.contains(altKey) && obj[altKey].is_boolean())
        {
            return obj[altKey].get<bool>();
        }
    }
    return fallback;
}

} // namespace

OnlineCache& OnlineCache::GetInstance()
{
    static OnlineCache instance;
    return instance;
}

OnlineCache::OnlineCache(std::filesystem::path customCachePath)
    : m_customCachePath(std::move(customCachePath))
{
}

void OnlineCache::SetCustomCachePath(std::filesystem::path customCachePath)
{
    std::lock_guard lock(m_mutex);
    m_customCachePath = std::move(customCachePath);
    m_lruOrder.clear();
    m_entries.clear();
    m_loaded = false;
}

std::filesystem::path OnlineCache::ResolveCachePath() const
{
    std::lock_guard lock(m_mutex);
    return ResolveCachePathUnlocked();
}

std::filesystem::path OnlineCache::ResolveCachePathUnlocked() const
{
    if (!m_customCachePath.empty())
    {
        return m_customCachePath;
    }
    return std::filesystem::path(sakura::core::Paths::User("cache/online_cache.json"));
}

void OnlineCache::EnsureLoadedUnlocked() const
{
    if (!m_loaded)
    {
        LoadUnlocked();
    }
}

bool OnlineCache::Load()
{
    std::lock_guard lock(m_mutex);
    return LoadUnlocked();
}

bool OnlineCache::LoadUnlocked() const
{
    m_lruOrder.clear();
    m_entries.clear();
    m_loaded = true;

    const std::filesystem::path cachePath = ResolveCachePathUnlocked();
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec) || ec)
    {
        return true;
    }

    try
    {
        std::ifstream ifs(cachePath, std::ios::binary);
        if (!ifs.is_open())
        {
            LOG_WARN("OnlineCache: failed to open cache file: {}", cachePath.generic_string());
            return false;
        }

        const nlohmann::json root = nlohmann::json::parse(ifs, nullptr, true, true);
        const nlohmann::json* entriesArr = nullptr;
        if (root.is_object() && root.contains("entries") && root["entries"].is_array())
        {
            entriesArr = &root["entries"];
        }
        else if (root.is_array())
        {
            entriesArr = &root;
        }
        else
        {
            LOG_WARN("OnlineCache: invalid root JSON in {}", cachePath.generic_string());
            return false;
        }

        for (const auto& item : *entriesArr)
        {
            if (!item.is_object())
            {
                continue;
            }
            const std::string key = JsonStringOr(item, "key");
            if (key.empty() || m_entries.find(key) != m_entries.end())
            {
                continue;
            }

            const long long updatedAt = JsonInt64Or(item, "updated_at", 0LL, "updatedAt");
            const long long savedAt   = JsonInt64Or(item, "saved_at", updatedAt, "savedAt");
            const bool forcedStale    = JsonBoolOr(item, "forced_stale", (updatedAt == 1LL && savedAt > 1LL), "forcedStale");

            m_lruOrder.push_back(key);
            auto lruIt = std::prev(m_lruOrder.end());

            CacheEntry entry;
            entry.key         = key;
            entry.etag        = JsonStringOr(item, "etag");
            entry.payloadJson = JsonStringOr(item, "payload_json", "payloadJson");
            entry.updatedAt   = updatedAt;
            entry.savedAt     = savedAt;
            entry.forcedStale = forcedStale;
            entry.lruIt       = lruIt;
            m_entries.emplace(key, std::move(entry));

            if (m_entries.size() >= MAX_ENTRIES)
            {
                break;
            }
        }
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_WARN("OnlineCache: failed to parse {}: {}", cachePath.generic_string(), e.what());
        m_lruOrder.clear();
        m_entries.clear();
        return false;
    }
}

bool OnlineCache::Save() const
{
    std::lock_guard lock(m_mutex);
    return SaveUnlocked();
}

bool OnlineCache::SaveUnlocked() const
{
    const std::filesystem::path cachePath = ResolveCachePathUnlocked();

    nlohmann::json entriesArr = nlohmann::json::array();
    for (const auto& key : m_lruOrder)
    {
        const auto it = m_entries.find(key);
        if (it == m_entries.end())
        {
            continue;
        }
        const CacheEntry& entry = it->second;
        entriesArr.push_back(nlohmann::json{
            { "key",          entry.key },
            { "etag",         entry.etag },
            { "payload_json", entry.payloadJson },
            { "updated_at",   entry.updatedAt },
            { "saved_at",     entry.savedAt },
            { "forced_stale", entry.forcedStale }
        });
    }

    const nlohmann::json root = {
        { "version", 1 },
        { "entries", std::move(entriesArr) }
    };

    if (!sakura::utils::AtomicWrite(cachePath, root.dump(2)))
    {
        LOG_WARN("OnlineCache: failed to write cache file: {}", cachePath.generic_string());
        return false;
    }
    return true;
}

void OnlineCache::PromoteUnlocked(CacheEntry& entry) const
{
    if (entry.lruIt != m_lruOrder.begin())
    {
        m_lruOrder.splice(m_lruOrder.begin(), m_lruOrder, entry.lruIt);
    }
}

void OnlineCache::TrimToCapacityUnlocked() const
{
    while (m_entries.size() > MAX_ENTRIES && !m_lruOrder.empty())
    {
        const std::string oldestKey = m_lruOrder.back();
        m_lruOrder.pop_back();
        m_entries.erase(oldestKey);
    }
}

CacheLookupResult OnlineCache::Lookup(const std::string& key,
                                      int softTtlSec,
                                      int hardTtlSec,
                                      long long nowEpochSec) const
{
    if (key.empty())
    {
        return {};
    }

    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    const auto it = m_entries.find(key);
    if (it == m_entries.end())
    {
        return {};
    }

    CacheEntry& entry   = it->second;
    const long long now = ResolveEpochSeconds(nowEpochSec);

    const long long hardOrigin = (entry.forcedStale && entry.savedAt > 0)
        ? entry.savedAt
        : entry.updatedAt;
    const long long hardAge = now - hardOrigin;
    if (hardAge > static_cast<long long>(hardTtlSec))
    {
        return {};
    }

    PromoteUnlocked(entry);

    const long long age = now - entry.updatedAt;
    CacheLookupResult out;
    out.found       = true;
    out.isStale     = entry.forcedStale || (age > static_cast<long long>(softTtlSec));
    out.etag        = entry.etag;
    out.payloadJson = entry.payloadJson;
    out.updatedAt   = entry.updatedAt;
    return out;
}

void OnlineCache::Put(const std::string& key,
                      const std::string& payloadJson,
                      const std::string& etag,
                      long long nowEpochSec)
{
    if (key.empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    const long long now = ResolveEpochSeconds(nowEpochSec);
    const auto it       = m_entries.find(key);
    if (it != m_entries.end())
    {
        CacheEntry& entry = it->second;
        entry.etag        = etag;
        entry.payloadJson = payloadJson;
        entry.updatedAt   = now;
        entry.savedAt     = now;
        entry.forcedStale = false;
        PromoteUnlocked(entry);
    }
    else
    {
        m_lruOrder.push_front(key);
        CacheEntry entry;
        entry.key         = key;
        entry.etag        = etag;
        entry.payloadJson = payloadJson;
        entry.updatedAt   = now;
        entry.savedAt     = now;
        entry.forcedStale = false;
        entry.lruIt       = m_lruOrder.begin();
        m_entries.emplace(key, std::move(entry));
        TrimToCapacityUnlocked();
    }

    SaveUnlocked();
}

void OnlineCache::TouchFresh(const std::string& key, long long nowEpochSec)
{
    if (key.empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    const auto it = m_entries.find(key);
    if (it == m_entries.end())
    {
        return;
    }

    const long long now   = ResolveEpochSeconds(nowEpochSec);
    CacheEntry& entry     = it->second;
    entry.updatedAt       = now;
    entry.savedAt         = now;
    entry.forcedStale     = false;
    PromoteUnlocked(entry);
    SaveUnlocked();
}

void OnlineCache::MarkStale(const std::string& keyPrefix)
{
    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    bool modified = false;
    for (auto& [key, entry] : m_entries)
    {
        if (key.starts_with(keyPrefix))
        {
            if (entry.savedAt <= 0 && entry.updatedAt > 1)
            {
                entry.savedAt = entry.updatedAt;
            }
            entry.updatedAt   = 1;
            entry.forcedStale = true;
            modified          = true;
        }
    }

    if (modified)
    {
        SaveUnlocked();
    }
}

void OnlineCache::Clear()
{
    std::lock_guard lock(m_mutex);
    m_lruOrder.clear();
    m_entries.clear();
    m_loaded = true;
    SaveUnlocked();
}

} // namespace sakura::net
