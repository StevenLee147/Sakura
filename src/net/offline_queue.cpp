#include "net/offline_queue.h"

#include "core/paths.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <fstream>
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

const char* ActionTypeToString(PendingActionType type)
{
    switch (type)
    {
    case PendingActionType::StandardScore:
        return "standard_score";
    case PendingActionType::DailyScore:
        return "daily_score";
    case PendingActionType::DanCourseRecord:
        return "dan_course_record";
    }
    return "standard_score";
}

PendingActionType ParseActionType(const nlohmann::json& val)
{
    if (val.is_string())
    {
        const std::string s = val.get<std::string>();
        if (s == "daily_score" || s == "DailyScore")
        {
            return PendingActionType::DailyScore;
        }
        if (s == "dan_course_record" || s == "DanCourseRecord")
        {
            return PendingActionType::DanCourseRecord;
        }
        return PendingActionType::StandardScore;
    }
    if (val.is_number_integer())
    {
        const int v = val.get<int>();
        if (v == 1)
        {
            return PendingActionType::DailyScore;
        }
        if (v == 2)
        {
            return PendingActionType::DanCourseRecord;
        }
    }
    return PendingActionType::StandardScore;
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

int JsonIntOr(const nlohmann::json& obj,
              const char* key,
              int fallback,
              const char* altKey = nullptr)
{
    if (obj.is_object())
    {
        if (obj.contains(key) && obj[key].is_number_integer())
        {
            return obj[key].get<int>();
        }
        if (altKey != nullptr && obj.contains(altKey) && obj[altKey].is_number_integer())
        {
            return obj[altKey].get<int>();
        }
    }
    return fallback;
}

bool IsWrapperActionObject(const nlohmann::json& obj)
{
    return obj.is_object()
        && obj.contains("type")
        && (obj.contains("payload_json") || obj.contains("payloadJson"));
}

bool PathsReferToSameFile(const std::filesystem::path& a, const std::filesystem::path& b)
{
    if (a.empty() || b.empty())
    {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(a, ec) && !ec && std::filesystem::exists(b, ec) && !ec)
    {
        const bool eq = std::filesystem::equivalent(a, b, ec);
        if (!ec)
        {
            return eq;
        }
    }

    std::error_code ecA;
    std::error_code ecB;
    const auto canonA = std::filesystem::weakly_canonical(a, ecA);
    const auto canonB = std::filesystem::weakly_canonical(b, ecB);
    if (!ecA && !ecB)
    {
        return canonA == canonB;
    }
    return a.lexically_normal() == b.lexically_normal();
}

} // namespace

OfflineQueue& OfflineQueue::GetInstance()
{
    static OfflineQueue instance;
    return instance;
}

OfflineQueue::OfflineQueue(std::filesystem::path customQueuePath,
                           std::filesystem::path legacyScoresPath)
    : m_customQueuePath(std::move(customQueuePath))
    , m_legacyScoresPath(std::move(legacyScoresPath))
{
}

void OfflineQueue::SetPaths(std::filesystem::path customQueuePath,
                            std::filesystem::path legacyScoresPath)
{
    std::lock_guard lock(m_mutex);
    m_customQueuePath  = std::move(customQueuePath);
    m_legacyScoresPath = std::move(legacyScoresPath);
    m_items.clear();
    m_loaded = false;
}

std::filesystem::path OfflineQueue::ResolveQueuePath() const
{
    std::lock_guard lock(m_mutex);
    return ResolveQueuePathUnlocked();
}

std::filesystem::path OfflineQueue::ResolveQueuePathUnlocked() const
{
    if (!m_customQueuePath.empty())
    {
        return m_customQueuePath;
    }
    return std::filesystem::path(sakura::core::Paths::User("cache/pending_actions.json"));
}

std::filesystem::path OfflineQueue::ResolveLegacyPathUnlocked() const
{
    if (!m_legacyScoresPath.empty())
    {
        return m_legacyScoresPath;
    }
    if (m_customQueuePath.empty())
    {
        return std::filesystem::path(sakura::core::Paths::User("data/pending_scores.json"));
    }
    return {};
}

void OfflineQueue::EnsureLoadedUnlocked() const
{
    if (!m_loaded)
    {
        LoadUnlocked();
    }
}

std::string OfflineQueue::GenerateActionIdUnlocked(long long nowSec) const
{
    for (;;)
    {
        ++m_idCounter;
        std::string candidate = "act_" + std::to_string(nowSec) + "_" + std::to_string(m_idCounter);
        const bool exists = std::any_of(m_items.begin(), m_items.end(),
            [&](const PendingActionItem& existing) { return existing.id == candidate; });
        if (!exists)
        {
            return candidate;
        }
    }
}

bool OfflineQueue::Load()
{
    std::lock_guard lock(m_mutex);
    return LoadUnlocked();
}

bool OfflineQueue::LoadUnlocked() const
{
    m_items.clear();
    m_loaded = true;

    const std::filesystem::path queuePath  = ResolveQueuePathUnlocked();
    const std::filesystem::path legacyPath = ResolveLegacyPathUnlocked();
    const bool distinctLegacy              = !legacyPath.empty() && !PathsReferToSameFile(queuePath, legacyPath);
    const long long nowSec                 = NowUnixSeconds();

    bool allParsedOk         = true;
    bool shouldSaveNewFormat = false;
    bool migratedSeparate    = false;

    auto appendItemDedup = [&](PendingActionItem item)
    {
        if (item.id.empty())
        {
            item.id = GenerateActionIdUnlocked(nowSec);
        }
        auto dup = std::find_if(m_items.begin(), m_items.end(),
            [&](const PendingActionItem& existing) { return existing.id == item.id; });
        if (dup != m_items.end())
        {
            return;
        }
        m_items.push_back(std::move(item));
    };

    auto parseWrapperObject = [&](const nlohmann::json& obj) -> PendingActionItem
    {
        PendingActionItem item;
        item.id           = JsonStringOr(obj, "id");
        item.type         = ParseActionType(obj["type"]);
        if (obj.contains("payload_json") && obj["payload_json"].is_string())
        {
            item.payloadJson = obj["payload_json"].get<std::string>();
        }
        else if (obj.contains("payloadJson") && obj["payloadJson"].is_string())
        {
            item.payloadJson = obj["payloadJson"].get<std::string>();
        }
        else if (obj.contains("payload_json"))
        {
            item.payloadJson = obj["payload_json"].dump();
        }
        else if (obj.contains("payloadJson"))
        {
            item.payloadJson = obj["payloadJson"].dump();
        }
        item.dateUtc8     = JsonStringOr(obj, "date_utc8", "dateUtc8");
        item.attemptCount = std::max(0, JsonIntOr(obj, "attempt_count", 0, "attemptCount"));
        item.nextRetryAt  = JsonInt64Or(obj, "next_retry_at", 0LL, "nextRetryAt");
        item.createdAt    = JsonInt64Or(obj, "created_at", nowSec, "createdAt");
        item.lastError    = JsonStringOr(obj, "last_error", "lastError");
        return item;
    };

    auto migrateLegacyElement = [&](const nlohmann::json& elem) -> PendingActionItem
    {
        PendingActionItem item;
        item.type = PendingActionType::StandardScore;
        if (elem.is_object())
        {
            item.id        = JsonStringOr(elem, "client_submission_id");
            item.payloadJson = elem.dump();
            item.createdAt = JsonInt64Or(elem, "played_at", nowSec);
        }
        else if (elem.is_string())
        {
            item.payloadJson = elem.get<std::string>();
            item.createdAt   = nowSec;
        }
        else
        {
            item.payloadJson = elem.dump();
            item.createdAt   = nowSec;
        }
        item.attemptCount = 0;
        item.nextRetryAt  = 0;
        return item;
    };

    std::error_code ec;
    if (std::filesystem::is_regular_file(queuePath, ec) && !ec)
    {
        try
        {
            std::ifstream ifs(queuePath, std::ios::binary);
            if (!ifs.is_open())
            {
                LOG_WARN("OfflineQueue: failed to open queue file: {}", queuePath.generic_string());
                allParsedOk = false;
            }
            else
            {
                const nlohmann::json root = nlohmann::json::parse(ifs, nullptr, true, true);
                if (root.is_object() && root.contains("items") && root["items"].is_array())
                {
                    for (const auto& elem : root["items"])
                    {
                        if (IsWrapperActionObject(elem))
                        {
                            appendItemDedup(parseWrapperObject(elem));
                        }
                        else if (elem.is_object() || elem.is_string())
                        {
                            appendItemDedup(migrateLegacyElement(elem));
                            shouldSaveNewFormat = true;
                        }
                    }
                }
                else if (root.is_array())
                {
                    shouldSaveNewFormat = true;
                    for (const auto& elem : root)
                    {
                        if (IsWrapperActionObject(elem))
                        {
                            appendItemDedup(parseWrapperObject(elem));
                        }
                        else if (!elem.is_null())
                        {
                            appendItemDedup(migrateLegacyElement(elem));
                        }
                    }
                }
                else
                {
                    LOG_WARN("OfflineQueue: unrecognized JSON structure in {}", queuePath.generic_string());
                    allParsedOk = false;
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN("OfflineQueue: failed to parse {}: {}", queuePath.generic_string(), e.what());
            allParsedOk = false;
        }
    }

    ec.clear();
    if (distinctLegacy && std::filesystem::is_regular_file(legacyPath, ec) && !ec)
    {
        try
        {
            std::ifstream ifs(legacyPath, std::ios::binary);
            if (!ifs.is_open())
            {
                LOG_WARN("OfflineQueue: failed to open legacy scores file: {}", legacyPath.generic_string());
                allParsedOk = false;
            }
            else
            {
                const nlohmann::json legacyRoot = nlohmann::json::parse(ifs, nullptr, true, true);
                if (legacyRoot.is_array())
                {
                    for (const auto& elem : legacyRoot)
                    {
                        if (IsWrapperActionObject(elem))
                        {
                            appendItemDedup(parseWrapperObject(elem));
                        }
                        else if (!elem.is_null())
                        {
                            appendItemDedup(migrateLegacyElement(elem));
                        }
                    }
                    shouldSaveNewFormat = true;
                    migratedSeparate    = true;
                }
                else if (legacyRoot.is_object() && legacyRoot.contains("items") && legacyRoot["items"].is_array())
                {
                    for (const auto& elem : legacyRoot["items"])
                    {
                        if (IsWrapperActionObject(elem))
                        {
                            appendItemDedup(parseWrapperObject(elem));
                        }
                        else if (!elem.is_null())
                        {
                            appendItemDedup(migrateLegacyElement(elem));
                        }
                    }
                    shouldSaveNewFormat = true;
                    migratedSeparate    = true;
                }
                else
                {
                    LOG_WARN("OfflineQueue: legacy file {} is not a JSON array", legacyPath.generic_string());
                    allParsedOk = false;
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN("OfflineQueue: failed to parse legacy scores {}: {}", legacyPath.generic_string(), e.what());
            allParsedOk = false;
        }
    }

    if (shouldSaveNewFormat)
    {
        if (SaveUnlocked())
        {
            if (migratedSeparate && distinctLegacy)
            {
                std::error_code rmEc;
                std::filesystem::remove(legacyPath, rmEc);
            }
        }
        else
        {
            allParsedOk = false;
        }
    }

    return allParsedOk;
}

bool OfflineQueue::Save() const
{
    std::lock_guard lock(m_mutex);
    return SaveUnlocked();
}

bool OfflineQueue::SaveUnlocked() const
{
    const std::filesystem::path queuePath = ResolveQueuePathUnlocked();

    nlohmann::json itemsArr = nlohmann::json::array();
    for (const auto& item : m_items)
    {
        itemsArr.push_back(nlohmann::json{
            { "id",            item.id },
            { "type",          ActionTypeToString(item.type) },
            { "payload_json",  item.payloadJson },
            { "date_utc8",     item.dateUtc8 },
            { "attempt_count", item.attemptCount },
            { "next_retry_at", item.nextRetryAt },
            { "created_at",    item.createdAt },
            { "last_error",    item.lastError }
        });
    }

    const nlohmann::json root = {
        { "version", 2 },
        { "items",   std::move(itemsArr) }
    };

    if (!sakura::utils::AtomicWrite(queuePath, root.dump(2)))
    {
        LOG_WARN("OfflineQueue: failed to write queue file: {}", queuePath.generic_string());
        return false;
    }
    return true;
}

std::string OfflineQueue::Enqueue(PendingActionType type,
                                  const std::string& payloadJson,
                                  const std::string& dateUtc8,
                                  const std::string& dedupeId)
{
    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    const long long now = NowUnixSeconds();

    if (!dedupeId.empty())
    {
        auto it = std::find_if(m_items.begin(), m_items.end(),
            [&](const PendingActionItem& existing) { return existing.id == dedupeId; });
        if (it != m_items.end())
        {
            it->type         = type;
            it->payloadJson  = payloadJson;
            it->dateUtc8     = dateUtc8;
            it->attemptCount = 0;
            it->nextRetryAt  = 0;
            if (it->createdAt <= 0)
            {
                it->createdAt = now;
            }
            it->lastError.clear();
            SaveUnlocked();
            return it->id;
        }
    }

    PendingActionItem item;
    item.id           = !dedupeId.empty() ? dedupeId : GenerateActionIdUnlocked(now);
    item.type         = type;
    item.payloadJson  = payloadJson;
    item.dateUtc8     = dateUtc8;
    item.attemptCount = 0;
    item.nextRetryAt  = 0;
    item.createdAt    = now;
    m_items.push_back(item);
    SaveUnlocked();
    return item.id;
}

std::vector<PendingActionItem> OfflineQueue::GetReadyItems(long long nowEpochSec) const
{
    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    const long long now = ResolveEpochSeconds(nowEpochSec);
    std::vector<PendingActionItem> ready;
    ready.reserve(m_items.size());
    for (const auto& item : m_items)
    {
        if (item.nextRetryAt <= 0 || item.nextRetryAt <= now)
        {
            ready.push_back(item);
        }
    }
    return ready;
}

std::vector<PendingActionItem> OfflineQueue::GetAllItems() const
{
    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();
    return m_items;
}

std::size_t OfflineQueue::GetCount() const
{
    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();
    return m_items.size();
}

void OfflineQueue::MarkSucceeded(const std::string& id)
{
    if (id.empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    const std::size_t before = m_items.size();
    m_items.erase(
        std::remove_if(m_items.begin(), m_items.end(),
            [&](const PendingActionItem& item) { return item.id == id; }),
        m_items.end());

    if (m_items.size() != before)
    {
        SaveUnlocked();
    }
}

void OfflineQueue::MarkRetryableFailure(const std::string& id,
                                        const std::string& error,
                                        long long nowEpochSec)
{
    if (id.empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    auto it = std::find_if(m_items.begin(), m_items.end(),
        [&](const PendingActionItem& item) { return item.id == id; });
    if (it == m_items.end())
    {
        return;
    }

    const long long now   = ResolveEpochSeconds(nowEpochSec);
    const int backoffSec  = ComputeBackoffSeconds(it->attemptCount);
    it->attemptCount     += 1;
    it->nextRetryAt       = now + static_cast<long long>(backoffSec);
    it->lastError         = error;
    SaveUnlocked();
}

void OfflineQueue::MarkPermanentFailure(const std::string& id, const std::string& error)
{
    if (id.empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    EnsureLoadedUnlocked();

    auto it = std::find_if(m_items.begin(), m_items.end(),
        [&](const PendingActionItem& item) { return item.id == id; });
    if (it == m_items.end())
    {
        return;
    }

    LOG_WARN("OfflineQueue: dropping permanently failed action '{}' (type={}): {}",
             id, ActionTypeToString(it->type), error);
    m_items.erase(it);
    SaveUnlocked();
}

int OfflineQueue::ComputeBackoffSeconds(int attemptCount)
{
    return std::min(300, 5 * (1 << std::min(std::max(0, attemptCount), 6)));
}

} // namespace sakura::net
