#include "net/api_client.h"

#include "core/config.h"
#include "core/paths.h"
#include "data/database.h"
#include "game/achievement_manager.h"
#include "game/chart_loader.h"
#include "game/course.h"
#include "utils/archive.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace sakura::net
{

namespace
{
constexpr const char* kDefaultOnlineServerUrl      = "https://steven-lee.tech:233";
constexpr long long   kProactiveRefreshLeadSeconds = 86400LL;
constexpr int         kOnlineCacheSoftTtlSec       = 20;
constexpr int         kOnlineCacheHardTtlSec       = 86400;
constexpr std::size_t kWorkerPoolThreadCount       = 2;

thread_local bool g_inTransferPoolTask = false;

long long NowUnixSeconds()
{
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void SetBearerHeader(HttpRequest& req, const std::string& token)
{
    std::erase_if(req.headers, [](const std::pair<std::string, std::string>& h) {
        return h.first == "Authorization" || h.first == "authorization";
    });
    if (!token.empty())
    {
        req.headers.emplace_back("Authorization", "Bearer " + token);
    }
}

std::string AsciiLower(std::string s)
{
    for (char& ch : s)
    {
        if (static_cast<unsigned char>(ch) < 128)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool IsPasswordMismatch401(const HttpResponse& res)
{
    if (res.statusCode != 401 || res.body.empty())
        return false;
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        const std::string code = AsciiLower(j.value("code", ""));
        if (code == "invalid_password" || code == "wrong_password" || code == "invalid_credentials")
            return true;
        const std::string err = AsciiLower(j.value("error", ""));
        if (err.find("password") != std::string::npos || err.find("密码") != std::string::npos)
            return true;
    }
    catch (...)
    {
    }
    return false;
}

OnlineUser SessionProfileToOnlineUser(const sakura::core::AuthSessionData& sess)
{
    OnlineUser u;
    u.id = !sess.activeUserId.empty() ? sess.activeUserId : sess.profile.userId;
    u.username     = sess.profile.username;
    u.isGuest      = sess.profile.isGuest;
    u.totalPP      = sess.profile.totalPP;
    u.sakuraRating = sess.profile.sakuraRating;
    u.danBadge     = sess.profile.danBadge;

    if (u.username.empty() && !u.id.empty())
    {
        for (const auto& entry : sess.savedAccounts)
        {
            if (entry.userId == u.id)
            {
                u.username     = entry.username;
                u.isGuest      = entry.isGuest;
                u.totalPP      = entry.totalPP;
                u.sakuraRating = entry.sakuraRating;
                u.danBadge     = entry.danBadge;
                break;
            }
        }
    }
    return u;
}

std::string TrimTrailingSlash(std::string url)
{
    while (!url.empty() && (url.front() == ' ' || url.front() == '\t'))
        url.erase(url.begin());
    while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\t'))
        url.pop_back();
    if (url.empty())
        return kDefaultOnlineServerUrl;
    if (url.find("://") == std::string::npos)
    {
        const bool isLocal = (url.rfind("127.0.0.1", 0) == 0 || url.rfind("localhost", 0) == 0);
        url = (isLocal ? "http://" : "https://") + url;
    }
    return url;
}

const char* GradeToStr(sakura::game::Grade g)
{
    using sakura::game::Grade;
    switch (g)
    {
        case Grade::SS: return "SS";
        case Grade::S:  return "S";
        case Grade::A:  return "A";
        case Grade::B:  return "B";
        case Grade::C:  return "C";
        default:        return "D";
    }
}

std::string ReadWholeFileUtf8(const std::string& path)
{
    if (path.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return {};
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::string JsonStringOr(const nlohmann::json& obj, const char* key, const std::string& fallback = {})
{
    if (obj.contains(key) && obj[key].is_null())
        return fallback;
    return obj.value(key, fallback);
}

double JsonSakuraRatingOr(const nlohmann::json& obj)
{
    if (obj.contains("sakura_rating") && obj["sakura_rating"].is_number())
        return obj["sakura_rating"].get<double>();
    if (obj.contains("rating") && obj["rating"].is_number())
        return obj["rating"].get<double>();
    return 0.0;
}

LeaderboardRow ParseLeaderboardRowJson(const nlohmann::json& item)
{
    LeaderboardRow row;
    row.rank         = item.value("rank", 0);
    row.scoreId      = JsonStringOr(item, "score_id", "");
    row.userId       = JsonStringOr(item, "user_id", "");
    row.username     = JsonStringOr(item, "username", "Unknown");
    row.score        = item.value("score", 0);
    row.accuracy     = item.value("accuracy", 0.0f);
    row.maxCombo     = item.value("max_combo", 0);
    row.grade        = JsonStringOr(item, "grade", "D");
    row.pp           = (item.contains("pp") && item["pp"].is_number()) ? item["pp"].get<double>() : 0.0;
    row.isFullCombo  = (item.contains("is_full_combo") && item["is_full_combo"].is_number_integer())
        ? (item["is_full_combo"].get<int>() != 0)
        : item.value("is_full_combo", false);
    row.isAllPerfect = (item.contains("is_all_perfect") && item["is_all_perfect"].is_number_integer())
        ? (item["is_all_perfect"].get<int>() != 0)
        : item.value("is_all_perfect", false);
    row.hasReplay    = (item.contains("has_replay") && item["has_replay"].is_number_integer())
        ? (item["has_replay"].get<int>() != 0)
        : item.value("has_replay", false);
    row.playedAt     = item.value("played_at", 0LL);
    row.sakuraRating = JsonSakuraRatingOr(item);
    row.danBadge     = (item.contains("dan_badge") && item["dan_badge"].is_null())
        ? std::string{}
        : item.value("dan_badge", "");
    return row;
}

std::string ExtractETagHeader(const HttpResponse& res)
{
    std::string etag = res.GetHeader("ETag");
    if (!etag.empty())
        return etag;
    for (const auto& kv : res.headers)
    {
        if (AsciiLower(kv.first) == "etag")
            return kv.second;
    }
    return {};
}

std::string ExtractHttpErrorReason(const HttpResponse& res)
{
    if (!res.body.empty())
    {
        try
        {
            const auto j = nlohmann::json::parse(res.body);
            if (j.is_object() && j.contains("error") && j["error"].is_string())
                return j["error"].get<std::string>();
        }
        catch (...)
        {
        }
    }
    if (!res.error.empty())
        return res.error;
    return "HTTP " + std::to_string(res.statusCode);
}

std::string ComputeGameResultSubmissionHash(const sakura::game::GameResult& result)
{
    std::ostringstream oss;
    oss << result.chartId << '|'
        << (result.chartHash.empty() ? result.chartId : result.chartHash) << '|'
        << result.difficulty << '|'
        << result.score << '|'
        << result.accuracy << '|'
        << result.maxCombo << '|'
        << result.perfectCount << '|'
        << result.greatCount << '|'
        << result.goodCount << '|'
        << result.badCount << '|'
        << result.missCount << '|'
        << result.synergyCount << '|'
        << result.playbackRate << '|'
        << result.playedAt << '|'
        << result.replayFile;
    return sakura::utils::ComputeSha256Hex(oss.str()).substr(0, 32);
}

LeaderboardResult ParseLeaderboardBodyJson(const std::string& body)
{
    LeaderboardResult out;
    try
    {
        const auto j = nlohmann::json::parse(body);
        if (!j.value("ok", false))
        {
            out.error = j.value("error", "获取排行榜失败");
            return out;
        }
        out.ok = true;
        if (j.contains("my_rank") && j["my_rank"].is_number_integer())
            out.myRank = j["my_rank"].get<int>();
        if (j.contains("entries") && j["entries"].is_array())
        {
            for (const auto& item : j["entries"])
            {
                out.entries.push_back(ParseLeaderboardRowJson(item));
            }
        }
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("排行榜解析失败: ") + e.what();
    }
    return out;
}
}

APIClient& APIClient::GetInstance()
{
    static APIClient instance(SingletonTag{});
    return instance;
}

APIClient::APIClient(SingletonTag)
    : m_transport(CreateDefaultHttpTransport())
    , m_onlineCache(&OnlineCache::GetInstance())
    , m_offlineQueue(&OfflineQueue::GetInstance())
{
}

APIClient::APIClient(std::shared_ptr<IHttpTransport> transport,
                     std::string queueFilePath,
                     std::string authStorePath)
    : m_transport(transport ? std::move(transport) : CreateDefaultHttpTransport())
    , m_customQueuePath(std::move(queueFilePath))
{
    namespace fs = std::filesystem;
    if (!authStorePath.empty())
    {
        m_ownedAuthStore = std::make_shared<sakura::core::AuthStore>(authStorePath);
    }
    else if (!m_customQueuePath.empty())
    {
        const fs::path qPath(m_customQueuePath);
        const fs::path derived = qPath.parent_path() / (qPath.stem().string() + "_auth.json");
        m_ownedAuthStore = std::make_shared<sakura::core::AuthStore>(derived.string());
    }
    else
    {
        static std::atomic<std::uint64_t> s_isolatedInstanceSeq{ 0 };
        const std::uint64_t seq = ++s_isolatedInstanceSeq;
        const auto stamp = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::error_code ec;
        fs::path tmpDir = fs::temp_directory_path(ec);
        if (ec || tmpDir.empty())
            tmpDir = fs::path(".");
        const std::string prefix = "sakura_apiclient_isolated_"
            + std::to_string(stamp) + "_" + std::to_string(seq);
        m_isolatedAuthStorePath    = (tmpDir / (prefix + "_auth.json")).string();
        m_isolatedCachePath        = (tmpDir / (prefix + "_cache.json")).string();
        m_isolatedQueuePath        = (tmpDir / (prefix + "_queue.json")).string();
        m_cleanupIsolatedAuthStore = true;
        m_ownedAuthStore           = std::make_shared<sakura::core::AuthStore>(m_isolatedAuthStorePath);
    }

    if (!m_customQueuePath.empty())
    {
        const fs::path qPath(m_customQueuePath);
        const fs::path derivedCache = qPath.parent_path() / (qPath.stem().string() + "_online_cache.json");
        m_ownedOnlineCache  = std::make_shared<OnlineCache>(derivedCache.string());
        m_ownedOfflineQueue = std::make_shared<OfflineQueue>(m_customQueuePath, m_customQueuePath);
    }
    else if (!authStorePath.empty())
    {
        const fs::path aPath(authStorePath);
        const fs::path derivedCache = aPath.parent_path() / (aPath.stem().string() + "_online_cache.json");
        const fs::path derivedQueue = aPath.parent_path() / (aPath.stem().string() + "_pending_actions.json");
        m_ownedOnlineCache  = std::make_shared<OnlineCache>(derivedCache.string());
        m_ownedOfflineQueue = std::make_shared<OfflineQueue>(derivedQueue.string(), derivedQueue.string());
    }
    else
    {
        m_ownedOnlineCache  = std::make_shared<OnlineCache>(m_isolatedCachePath);
        m_ownedOfflineQueue = std::make_shared<OfflineQueue>(m_isolatedQueuePath, m_isolatedQueuePath);
    }

    m_onlineCache  = m_ownedOnlineCache.get();
    m_offlineQueue = m_ownedOfflineQueue.get();
    m_onlineCache->Load();
    m_offlineQueue->Load();
}

APIClient::~APIClient()
{
    Shutdown();
    m_ownedOnlineCache.reset();
    m_ownedOfflineQueue.reset();
    m_ownedAuthStore.reset();
    if (m_cleanupIsolatedAuthStore)
    {
        std::error_code ec;
        if (!m_isolatedAuthStorePath.empty())
            std::filesystem::remove(m_isolatedAuthStorePath, ec);
        if (!m_isolatedCachePath.empty())
            std::filesystem::remove(m_isolatedCachePath, ec);
        if (!m_isolatedQueuePath.empty())
            std::filesystem::remove(m_isolatedQueuePath, ec);
    }
}

sakura::core::AuthStore& APIClient::MutableAuthStore() const
{
    return m_ownedAuthStore ? *m_ownedAuthStore : sakura::core::AuthStore::GetInstance();
}

OnlineCache& APIClient::MutableOnlineCache() const
{
    return m_onlineCache ? *m_onlineCache : OnlineCache::GetInstance();
}

OfflineQueue& APIClient::MutableOfflineQueue() const
{
    return m_offlineQueue ? *m_offlineQueue : OfflineQueue::GetInstance();
}

sakura::core::AuthStore& APIClient::GetAuthStore()
{
    return MutableAuthStore();
}

const sakura::core::AuthStore& APIClient::GetAuthStore() const
{
    return MutableAuthStore();
}

OnlineCache& APIClient::GetOnlineCache()
{
    return MutableOnlineCache();
}

const OnlineCache& APIClient::GetOnlineCache() const
{
    return MutableOnlineCache();
}

OfflineQueue& APIClient::GetOfflineQueue()
{
    return MutableOfflineQueue();
}

const OfflineQueue& APIClient::GetOfflineQueue() const
{
    return MutableOfflineQueue();
}

void APIClient::MarkAllUserScopedCachesStale()
{
    auto& cache = MutableOnlineCache();
    cache.MarkStale("leaderboard:");
    cache.MarkStale("daily:");
    cache.MarkStale("market:");
}

void APIClient::SyncServerUrlToConfig(const std::string& serverUrl) const
{
    auto& cfg = sakura::core::Config::GetInstance();
    cfg.Set("online.server_url", serverUrl);
    for (const char* legacyKey : {
             "online.auth_token",
             "online.refresh_token",
             "online.user_id",
             "online.username",
             "online.is_guest",
             "online.total_pp",
             "online.sakura_rating",
             "online.dan_badge" })
    {
        if (cfg.Has(legacyKey))
            cfg.Remove(legacyKey);
    }
    if (m_ownedAuthStore == nullptr || !cfg.GetFilePath().empty())
    {
        cfg.Save();
    }
}

void APIClient::MarkOfflinePreserveSession()
{
    m_status.store(OnlineStatus::Offline);
    std::lock_guard lock(m_stateMutex);
    if (!m_authToken.empty() && !m_user.id.empty() && m_authState != AuthState::SessionExpired)
    {
        m_authState = AuthState::AuthenticatedOffline;
    }
}

void APIClient::MarkOnlineFromAuthenticatedSuccess()
{
    m_status.store(OnlineStatus::Online);
    std::lock_guard lock(m_stateMutex);
    if (!m_authToken.empty() && !m_user.id.empty() && m_authState != AuthState::SessionExpired)
    {
        m_authState = AuthState::AuthenticatedOnline;
    }
}

void APIClient::LoadFromConfig()
{
    MutableOnlineCache().Load();
    MutableOfflineQueue().Load();

    auto& cfg   = sakura::core::Config::GetInstance();
    auto& store = GetAuthStore();
    store.Load(&cfg);

    const auto sess = store.GetSession();
    const std::string cfgUrl   = cfg.Get<std::string>("online.server_url", "");
    const std::string storeUrl = !sess.serverUrl.empty() ? sess.serverUrl : store.GetServerUrl();

    std::string rawUrl;
    if (!storeUrl.empty() && (cfgUrl.empty() || cfgUrl == kDefaultOnlineServerUrl))
        rawUrl = storeUrl;
    else if (!cfgUrl.empty())
        rawUrl = cfgUrl;
    else
        rawUrl = kDefaultOnlineServerUrl;

    const std::string resolvedUrl = TrimTrailingSlash(rawUrl);
    store.SetServerUrl(resolvedUrl);
    SyncServerUrlToConfig(resolvedUrl);

    const OnlineUser loadedUser = SessionProfileToOnlineUser(sess);
    std::lock_guard lock(m_stateMutex);
    m_serverUrl    = resolvedUrl;
    m_authToken    = sess.accessToken;
    m_refreshToken = sess.refreshToken;
    m_expiresAt    = sess.expiresAt;
    m_user         = loadedUser;

    if (!m_authToken.empty() && !m_user.id.empty())
    {
        m_authState = AuthState::AuthenticatedOffline;
    }
    else if (m_authToken.empty() && !m_user.id.empty())
    {
        m_authState = AuthState::SessionExpired;
    }
    else
    {
        m_authToken.clear();
        m_refreshToken.clear();
        m_expiresAt = 0;
        m_user      = {};
        m_authState = AuthState::Unauthenticated;
    }
}

void APIClient::SaveToConfig() const
{
    std::string serverUrl;
    std::string authToken;
    std::string refreshToken;
    long long   expiresAt = 0;
    OnlineUser  user;
    AuthState   authState = AuthState::Unauthenticated;
    {
        std::lock_guard lock(m_stateMutex);
        serverUrl    = m_serverUrl;
        authToken    = m_authToken;
        refreshToken = m_refreshToken;
        expiresAt    = m_expiresAt;
        user         = m_user;
        authState    = m_authState;
    }

    SyncServerUrlToConfig(serverUrl);

    auto& store = MutableAuthStore();
    store.SetServerUrl(serverUrl);
    if (!user.id.empty() && !authToken.empty() && authState != AuthState::SessionExpired)
    {
        sakura::core::AuthSessionData sess;
        sess.serverUrl            = serverUrl;
        sess.activeUserId         = user.id;
        sess.accessToken          = authToken;
        sess.refreshToken         = refreshToken;
        sess.expiresAt            = expiresAt;
        sess.profile.userId       = user.id;
        sess.profile.username     = user.username;
        sess.profile.isGuest      = user.isGuest;
        sess.profile.totalPP      = user.totalPP;
        sess.profile.sakuraRating = user.sakuraRating;
        sess.profile.danBadge     = user.danBadge;
        sess.profile.serverUrl    = serverUrl;
        sess.profile.expiresAt    = expiresAt;
        store.UpsertActiveSession(sess);
    }
    else if (authState == AuthState::SessionExpired && !user.id.empty())
    {
        store.ClearActiveTokensKeepIdentity();
    }
    else
    {
        store.Save();
    }
}

void APIClient::Shutdown()
{
    CancelChannel(m_leaderboardChannel);
    CancelChannel(m_marketListChannel);
    ShutdownPool(m_interactivePool);
    ShutdownPool(m_transferPool);
    m_activeTransferCount.store(0, std::memory_order_release);
    m_activeTransferProgress.store(0.0f, std::memory_order_release);
    m_bgFlushScheduled.store(false, std::memory_order_release);
    {
        std::lock_guard lock(m_cbMutex);
        m_callbacks.clear();
    }
}

void APIClient::SetTransport(std::shared_ptr<IHttpTransport> transport)
{
    std::lock_guard lock(m_stateMutex);
    m_transport = transport ? std::move(transport) : CreateDefaultHttpTransport();
}

std::shared_ptr<IHttpTransport> APIClient::GetTransport() const
{
    std::lock_guard lock(m_stateMutex);
    return m_transport;
}

void APIClient::SetServerUrl(const std::string& url)
{
    bool changed = false;
    {
        std::lock_guard lock(m_stateMutex);
        const std::string normalized = TrimTrailingSlash(url);
        changed     = (m_serverUrl != normalized);
        m_serverUrl = normalized;
    }
    if (changed)
    {
        MarkAllUserScopedCachesStale();
    }
    SaveToConfig();
}

std::string APIClient::GetServerUrl() const
{
    std::lock_guard lock(m_stateMutex);
    return m_serverUrl;
}

bool APIClient::IsAuthenticated() const
{
    std::lock_guard lock(m_stateMutex);
    return !m_authToken.empty() && !m_user.id.empty() && m_authState != AuthState::SessionExpired;
}

AuthState APIClient::GetAuthState() const
{
    std::lock_guard lock(m_stateMutex);
    return m_authState;
}

OnlineUser APIClient::GetCurrentUser() const
{
    std::lock_guard lock(m_stateMutex);
    return m_user;
}

std::string APIClient::GetAuthToken() const
{
    std::lock_guard lock(m_stateMutex);
    return m_authToken;
}

std::string APIClient::GetRefreshToken() const
{
    std::lock_guard lock(m_stateMutex);
    return m_refreshToken;
}

long long APIClient::GetTokenExpiresAt() const
{
    std::lock_guard lock(m_stateMutex);
    return m_expiresAt;
}

void APIClient::Logout()
{
    std::string oldAuthToken;
    std::string oldRefreshToken;
    std::string serverUrl;
    {
        std::lock_guard lock(m_stateMutex);
        oldAuthToken    = m_authToken;
        oldRefreshToken = m_refreshToken;
        serverUrl       = m_serverUrl;
    }

    if (!oldRefreshToken.empty() || !oldAuthToken.empty())
    {
        nlohmann::json body = nlohmann::json::object();
        if (!oldRefreshToken.empty())
            body["refresh_token"] = oldRefreshToken;
        HttpRequest req = MakeRequest("POST", "/api/v1/auth/logout", body.dump(), !oldAuthToken.empty());
        req.timeoutMs   = 3000;
        try
        {
            (void)GetTransport()->Send(req);
        }
        catch (...)
        {
        }
    }

    auto& store = GetAuthStore();
    store.ClearActiveSession(false);
    store.Save();

    {
        std::lock_guard lock(m_stateMutex);
        m_authToken.clear();
        m_refreshToken.clear();
        m_expiresAt = 0;
        m_user      = {};
        m_authState = AuthState::Unauthenticated;
    }
    MarkAllUserScopedCachesStale();
    SyncServerUrlToConfig(serverUrl);
}

std::size_t APIClient::GetPendingScoreCount() const
{
    return static_cast<std::size_t>(MutableOfflineQueue().GetCount());
}

int APIClient::GetActiveTransferCount() const
{
    return std::max(0, m_activeTransferCount.load(std::memory_order_acquire));
}

float APIClient::GetActiveTransferProgress() const
{
    return std::clamp(m_activeTransferProgress.load(std::memory_order_acquire), 0.0f, 1.0f);
}

uint64_t APIClient::NextRequestSeq()
{
    return m_requestSeq.fetch_add(1, std::memory_order_acq_rel) + 1;
}

std::optional<LeaderboardResult> APIClient::GetCachedLeaderboard(const std::string& chartId,
                                                                 const std::string& difficulty,
                                                                 std::string_view scope) const
{
    if (chartId.empty() || difficulty.empty())
        return std::nullopt;
    const std::string prefix    = (scope == "rivals") ? "leaderboard_rivals:" : "leaderboard:";
    const std::string cacheKey  = prefix + chartId + ":" + difficulty;
    const CacheLookupResult hit = MutableOnlineCache().Lookup(
        cacheKey, kOnlineCacheSoftTtlSec, kOnlineCacheHardTtlSec);
    if (!hit.found || hit.payloadJson.empty())
        return std::nullopt;
    LeaderboardResult parsed = ParseLeaderboardBodyJson(hit.payloadJson);
    if (!parsed.ok)
        return std::nullopt;
    return parsed;
}

std::string APIClient::FormatScoreDeltaText(std::string_view rivalName, int myBestScore, int rivalScore)
{
    const long long delta = static_cast<long long>(myBestScore) - static_cast<long long>(rivalScore);
    const long long absVal = (delta < 0) ? -delta : delta;
    const std::string raw = std::to_string(absVal);
    std::string formatted;
    int digits = 0;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it)
    {
        if (digits > 0 && digits % 3 == 0)
            formatted.push_back(',');
        formatted.push_back(*it);
        ++digits;
    }
    std::reverse(formatted.begin(), formatted.end());
    const std::string sign = (delta >= 0) ? "+" : "-";
    const std::string who  = rivalName.empty() ? std::string("Rival") : std::string(rivalName);
    return "VS " + who + " " + sign + formatted;
}

HttpRequest APIClient::MakeRequest(const std::string& method, const std::string& path,
                                   const std::string& jsonBody, bool withAuth) const
{
    HttpRequest req;
    req.method = method;
    std::string token;
    {
        std::lock_guard lock(m_stateMutex);
        req.url = m_serverUrl + path;
        token   = m_authToken;
    }
    req.headers.emplace_back("Accept", "application/json");
    if (!jsonBody.empty())
    {
        req.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
        req.body = jsonBody;
    }
    if (withAuth && !token.empty())
    {
        req.headers.emplace_back("Authorization", "Bearer " + token);
    }
    return req;
}

HttpResponse APIClient::SendWithAuthRetry(HttpRequest req)
{
    if (req.cancelToken && req.cancelToken->IsCancelled())
    {
        HttpResponse cancelledRes;
        cancelledRes.cancelled = true;
        return cancelledRes;
    }

    const long long nowSec = NowUnixSeconds();
    long long initialExpiresAt = 0;
    std::string initialRefresh;
    {
        std::lock_guard stateLock(m_stateMutex);
        initialExpiresAt = m_expiresAt;
        initialRefresh   = m_refreshToken;
    }

    if (initialExpiresAt > 0 && !initialRefresh.empty() && nowSec >= initialExpiresAt - kProactiveRefreshLeadSeconds)
    {
        std::lock_guard refreshLock(m_refreshMutex);
        long long curExpiresAt = 0;
        std::string curRefresh;
        {
            std::lock_guard stateLock(m_stateMutex);
            curExpiresAt = m_expiresAt;
            curRefresh   = m_refreshToken;
        }
        if (curExpiresAt > 0 && !curRefresh.empty() && nowSec >= curExpiresAt - kProactiveRefreshLeadSeconds)
        {
            HttpResponse proactiveRaw;
            const AuthResult proactiveRes = RefreshSessionLocked(&proactiveRaw);
            if (!proactiveRes.ok)
            {
                if (GetAuthState() == AuthState::SessionExpired ||
                    proactiveRaw.IsNetworkError() || proactiveRaw.statusCode >= 500)
                {
                    return proactiveRaw;
                }
            }
            else if (req.url.ends_with("/api/v1/auth/change-password") && !proactiveRes.refreshToken.empty())
            {
                try
                {
                    auto bodyJson = nlohmann::json::parse(req.body);
                    if (bodyJson.is_object() && bodyJson.contains("refresh_token"))
                    {
                        bodyJson["refresh_token"] = proactiveRes.refreshToken;
                        req.body                  = bodyJson.dump();
                    }
                }
                catch (...)
                {
                }
            }
        }
    }

    const std::string usedToken = GetAuthToken();
    SetBearerHeader(req, usedToken);
    HttpResponse res = GetTransport()->Send(req);

    if (res.cancelled || (req.cancelToken && req.cancelToken->IsCancelled()))
    {
        res.cancelled = true;
        return res;
    }

    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        return res;
    }

    if (res.statusCode != 401)
    {
        if (res.IsSuccess() || res.IsNotModified())
            MarkOnlineFromAuthenticatedSuccess();
        else
            m_status.store(OnlineStatus::Online);
        return res;
    }

    if (req.url.ends_with("/api/v1/auth/change-password") && IsPasswordMismatch401(res))
    {
        m_status.store(OnlineStatus::Online);
        return res;
    }

    bool shouldReplay = false;
    std::string retryToken;
    std::string retryRefreshToken;
    {
        std::lock_guard refreshLock(m_refreshMutex);

        std::string currentToken;
        std::string currentRefresh;
        std::string currentUserId;
        AuthState   currentState = AuthState::Unauthenticated;
        {
            std::lock_guard stateLock(m_stateMutex);
            currentToken   = m_authToken;
            currentRefresh = m_refreshToken;
            currentUserId  = m_user.id;
            currentState   = m_authState;
        }

        if (!currentToken.empty() && currentToken != usedToken)
        {
            shouldReplay      = true;
            retryToken        = currentToken;
            retryRefreshToken = currentRefresh;
        }
        else if (currentState == AuthState::SessionExpired)
        {
            m_status.store(OnlineStatus::Online);
            return res;
        }
        else if (currentRefresh.empty())
        {
            if (!currentUserId.empty())
            {
                auto& store = GetAuthStore();
                store.ClearActiveTokensKeepIdentity();
                store.Save();
                std::lock_guard stateLock(m_stateMutex);
                m_authToken.clear();
                m_refreshToken.clear();
                m_expiresAt = 0;
                m_authState = AuthState::SessionExpired;
            }
            m_status.store(OnlineStatus::Online);
            return res;
        }
        else
        {
            HttpResponse refreshRaw;
            const AuthResult refRes = RefreshSessionLocked(&refreshRaw);
            if (refRes.ok && !refRes.token.empty())
            {
                shouldReplay      = true;
                retryToken        = refRes.token;
                retryRefreshToken = refRes.refreshToken;
            }
            else if (refreshRaw.IsNetworkError() || refreshRaw.statusCode >= 500)
            {
                return refreshRaw;
            }
            else
            {
                return res;
            }
        }
    }

    if (shouldReplay && !retryToken.empty())
    {
        if (req.url.ends_with("/api/v1/auth/change-password") && !retryRefreshToken.empty())
        {
            try
            {
                auto bodyJson = nlohmann::json::parse(req.body);
                if (bodyJson.is_object() && bodyJson.contains("refresh_token"))
                {
                    bodyJson["refresh_token"] = retryRefreshToken;
                    req.body                  = bodyJson.dump();
                }
            }
            catch (...)
            {
            }
        }

        SetBearerHeader(req, retryToken);
        HttpResponse retryRes = GetTransport()->Send(req);
        if (retryRes.cancelled || (req.cancelToken && req.cancelToken->IsCancelled()))
        {
            retryRes.cancelled = true;
            return retryRes;
        }
        if (retryRes.IsNetworkError() || retryRes.statusCode >= 500)
        {
            MarkOfflinePreserveSession();
        }
        else if (retryRes.IsSuccess() || retryRes.IsNotModified())
        {
            MarkOnlineFromAuthenticatedSuccess();
        }
        else
        {
            m_status.store(OnlineStatus::Online);
        }
        return retryRes;
    }

    return res;
}

AuthResult APIClient::RefreshSessionLocked(HttpResponse* outRawHttpResponse)
{
    std::string refreshToken;
    std::string userId;
    {
        std::lock_guard stateLock(m_stateMutex);
        refreshToken = m_refreshToken;
        userId       = m_user.id;
    }

    if (refreshToken.empty())
    {
        AuthResult out;
        out.errorCode = "missing_refresh_token";
        out.error     = "缺少刷新令牌，无法续期会话";
        if (!userId.empty() && GetAuthToken().empty())
        {
            std::lock_guard stateLock(m_stateMutex);
            m_authState = AuthState::SessionExpired;
            out.user    = m_user;
        }
        if (outRawHttpResponse)
        {
            outRawHttpResponse->statusCode = 401;
            outRawHttpResponse->error      = out.error;
        }
        return out;
    }

    const nlohmann::json body = {
        { "refresh_token", refreshToken },
        { "device_name", "Windows Desktop" }
    };
    const HttpResponse res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/refresh", body.dump(), false));
    if (outRawHttpResponse)
        *outRawHttpResponse = res;

    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        AuthResult out;
        out.errorCode = res.IsNetworkError() ? "network_error" : "server_error";
        out.error     = res.error.empty() ? "网络不可达，暂时维持离线登录态" : res.error;
        out.user      = GetCurrentUser();
        return out;
    }

    if (res.statusCode == 401 || res.statusCode == 403)
    {
        AuthResult out;
        out.errorCode = "session_expired";
        out.error     = "登录会话已过期，请重新输入密码验证";
        try
        {
            if (!res.body.empty())
            {
                const auto j = nlohmann::json::parse(res.body);
                if (j.contains("code") && j["code"].is_string())
                    out.errorCode = j["code"].get<std::string>();
                if (j.contains("error") && j["error"].is_string())
                    out.error = j["error"].get<std::string>();
            }
        }
        catch (...)
        {
        }

        auto& store = GetAuthStore();
        store.ClearActiveTokensKeepIdentity();
        store.Save();
        {
            std::lock_guard stateLock(m_stateMutex);
            m_authToken.clear();
            m_refreshToken.clear();
            m_expiresAt = 0;
            m_authState = m_user.id.empty() ? AuthState::Unauthenticated : AuthState::SessionExpired;
            out.user    = m_user;
        }
        m_status.store(OnlineStatus::Online);
        return out;
    }

    return HandleAuthResponse(res, true);
}

AuthResult APIClient::RefreshSession()
{
    std::lock_guard refreshLock(m_refreshMutex);
    return RefreshSessionLocked(nullptr);
}

bool APIClient::CheckHealth()
{
    auto transport = GetTransport();
    const auto res = transport->Send(MakeRequest("GET", "/api/v1/health", {}, false));
    if (res.IsSuccess())
    {
        m_status.store(OnlineStatus::Online);
        return true;
    }
    MarkOfflinePreserveSession();
    return false;
}

AuthResult APIClient::HandleAuthResponse(const HttpResponse& res, bool preserveMissingTokens)
{
    AuthResult out;
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.errorCode = res.IsNetworkError() ? "network_error" : "server_error";
        if (!res.body.empty())
        {
            try
            {
                const auto j = nlohmann::json::parse(res.body);
                if (j.contains("code") && j["code"].is_string())
                    out.errorCode = j["code"].get<std::string>();
                if (j.contains("error") && j["error"].is_string())
                    out.error = j["error"].get<std::string>();
            }
            catch (...)
            {
            }
        }
        if (out.error.empty())
        {
            out.error = res.IsNetworkError()
                ? (res.error.empty() ? "无法连接至在线服务器" : res.error)
                : ("服务器暂时不可用 (HTTP " + std::to_string(res.statusCode) + ")");
        }
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            if (j.contains("code") && j["code"].is_string())
                out.errorCode = j["code"].get<std::string>();
            else if (j.contains("error_code") && j["error_code"].is_string())
                out.errorCode = j["error_code"].get<std::string>();
            out.error = j.value("error", "认证失败 (HTTP " + std::to_string(res.statusCode) + ")");
            out.user  = GetCurrentUser();
            return out;
        }

        std::string prevToken;
        std::string prevRefresh;
        long long   prevExpiresAt = 0;
        OnlineUser  prevUser;
        {
            std::lock_guard lock(m_stateMutex);
            prevToken     = m_authToken;
            prevRefresh   = m_refreshToken;
            prevExpiresAt = m_expiresAt;
            prevUser      = m_user;
        }

        out.ok = true;
        if (j.contains("token") && j["token"].is_string())
            out.token = j["token"].get<std::string>();
        else if (j.contains("access_token") && j["access_token"].is_string())
            out.token = j["access_token"].get<std::string>();
        else if (preserveMissingTokens)
            out.token = prevToken;

        if (j.contains("refresh_token") && j["refresh_token"].is_string())
            out.refreshToken = j["refresh_token"].get<std::string>();
        else if (j.contains("refreshToken") && j["refreshToken"].is_string())
            out.refreshToken = j["refreshToken"].get<std::string>();
        else if (preserveMissingTokens)
            out.refreshToken = prevRefresh;

        if (j.contains("expires_at") && j["expires_at"].is_number())
            out.expiresAt = j["expires_at"].get<long long>();
        else if (j.contains("expiresAt") && j["expiresAt"].is_number())
            out.expiresAt = j["expiresAt"].get<long long>();
        else if (preserveMissingTokens)
            out.expiresAt = prevExpiresAt;

        if (j.contains("user") && j["user"].is_object())
        {
            const auto& u         = j["user"];
            out.user.id           = JsonStringOr(u, "id", preserveMissingTokens ? prevUser.id : "");
            out.user.username     = JsonStringOr(u, "username", preserveMissingTokens ? prevUser.username : "");
            out.user.isGuest      = u.contains("is_guest") && u["is_guest"].is_boolean()
                ? u["is_guest"].get<bool>()
                : (preserveMissingTokens ? prevUser.isGuest : false);
            out.user.totalPP      = (u.contains("total_pp") && u["total_pp"].is_number())
                ? u["total_pp"].get<double>()
                : (preserveMissingTokens ? prevUser.totalPP : 0.0);
            out.user.sakuraRating = (u.contains("sakura_rating") || u.contains("rating"))
                ? JsonSakuraRatingOr(u)
                : (preserveMissingTokens ? prevUser.sakuraRating : 0.0);
            out.user.danBadge     = u.contains("dan_badge")
                ? (u["dan_badge"].is_null() ? std::string{} : JsonStringOr(u, "dan_badge", ""))
                : (preserveMissingTokens ? prevUser.danBadge : std::string{});
        }
        else if (preserveMissingTokens && !prevUser.id.empty())
        {
            out.user = prevUser;
        }
        else
        {
            out.ok    = false;
            out.error = "响应缺少用户信息";
            return out;
        }

        {
            std::lock_guard lock(m_stateMutex);
            m_authToken    = out.token;
            m_refreshToken = out.refreshToken;
            m_expiresAt    = out.expiresAt;
            m_user         = out.user;
            m_authState    = (!m_authToken.empty() && !m_user.id.empty())
                ? AuthState::AuthenticatedOnline
                : AuthState::Unauthenticated;
        }
        if (prevUser.id != out.user.id)
        {
            MarkAllUserScopedCachesStale();
        }
        SaveToConfig();
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("响应解析失败: ") + e.what();
        return out;
    }
}

AuthResult APIClient::LoginGuest(const std::string& nickname)
{
    nlohmann::json body = { { "device_name", "Windows Desktop" } };
    if (!nickname.empty()) body["nickname"] = nickname;
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/guest", body.dump(), false));
    return HandleAuthResponse(res, false);
}

AuthResult APIClient::Register(const std::string& username, const std::string& password)
{
    const nlohmann::json body = {
        { "username", username },
        { "password", password },
        { "device_name", "Windows Desktop" }
    };
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/register", body.dump(), false));
    return HandleAuthResponse(res, false);
}

AuthResult APIClient::Login(const std::string& username, const std::string& password)
{
    const nlohmann::json body = {
        { "username", username },
        { "password", password },
        { "device_name", "Windows Desktop" }
    };
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/login", body.dump(), false));
    return HandleAuthResponse(res, false);
}

AuthResult APIClient::RefreshProfile()
{
    if (!IsAuthenticated())
    {
        if (!GetRefreshToken().empty() && !GetCurrentUser().id.empty())
        {
            const auto refreshed = RefreshSession();
            if (!refreshed.ok)
                return refreshed;
        }
        else
        {
            AuthResult r;
            r.errorCode = (GetAuthState() == AuthState::SessionExpired) ? "session_expired" : "unauthenticated";
            r.error     = (GetAuthState() == AuthState::SessionExpired) ? "登录会话已过期" : "尚未登录";
            r.user      = GetCurrentUser();
            return r;
        }
    }
    const auto res = SendWithAuthRetry(MakeRequest("GET", "/api/v1/auth/me", {}, true));
    return HandleAuthResponse(res, true);
}

AuthResult APIClient::UpgradeGuest(const std::string& username, const std::string& password)
{
    if (!IsAuthenticated())
    {
        AuthResult r;
        r.errorCode = "unauthenticated";
        r.error     = "请先登录游客账号后再升级为正式账号";
        return r;
    }
    const nlohmann::json body = {
        { "username", username },
        { "password", password }
    };
    const auto res = SendWithAuthRetry(MakeRequest("POST", "/api/v1/auth/upgrade-guest", body.dump(), true));
    return HandleAuthResponse(res, true);
}

AuthResult APIClient::ChangePassword(const std::string& oldPassword, const std::string& newPassword)
{
    if (!IsAuthenticated())
    {
        AuthResult r;
        r.errorCode = "unauthenticated";
        r.error     = "请先登录正式账号后再修改密码";
        return r;
    }
    nlohmann::json body = {
        { "old_password", oldPassword },
        { "new_password", newPassword }
    };
    const std::string rt = GetRefreshToken();
    if (!rt.empty())
        body["refresh_token"] = rt;
    const auto res = SendWithAuthRetry(MakeRequest("POST", "/api/v1/auth/change-password", body.dump(), true));
    return HandleAuthResponse(res, true);
}

std::vector<sakura::core::SavedAccountEntry> APIClient::GetSavedAccounts() const
{
    return GetAuthStore().GetSavedAccounts();
}

AuthResult APIClient::SwitchSavedAccount(const std::string& userId)
{
    AuthResult out;
    if (userId.empty())
    {
        out.errorCode = "invalid_user_id";
        out.error     = "无效的账号标识";
        return out;
    }

    auto& store = GetAuthStore();
    sakura::core::AuthSessionData switchedSess;
    if (!store.SwitchToSavedAccount(userId, &switchedSess))
    {
        if (switchedSess.activeUserId == userId)
        {
            const OnlineUser expiredUser = SessionProfileToOnlineUser(switchedSess);
            std::string resolvedUrl;
            {
                std::lock_guard lock(m_stateMutex);
                if (!switchedSess.serverUrl.empty())
                    m_serverUrl = TrimTrailingSlash(switchedSess.serverUrl);
                resolvedUrl    = m_serverUrl;
                m_authToken.clear();
                m_refreshToken.clear();
                m_expiresAt = 0;
                m_user      = expiredUser;
                m_authState = AuthState::SessionExpired;
            }
            store.Save();
            SyncServerUrlToConfig(resolvedUrl);
            out.ok        = false;
            out.errorCode = "session_expired";
            out.error     = "该账号登录凭据已失效，请重新输入密码验证";
            out.user      = expiredUser;
            return out;
        }
        out.errorCode = "account_not_found";
        out.error     = "未找到已保存的账号记录";
        return out;
    }

    const auto& sess            = switchedSess;
    const OnlineUser targetUser = SessionProfileToOnlineUser(sess);
    std::string resolvedUrl;
    {
        std::lock_guard lock(m_stateMutex);
        if (!sess.serverUrl.empty())
            m_serverUrl = TrimTrailingSlash(sess.serverUrl);
        resolvedUrl    = m_serverUrl;
        m_authToken    = sess.accessToken;
        m_refreshToken = sess.refreshToken;
        m_expiresAt    = sess.expiresAt;
        m_user         = targetUser;

        if (m_authToken.empty() && m_refreshToken.empty())
            m_authState = AuthState::SessionExpired;
        else
            m_authState = AuthState::AuthenticatedOffline;
    }
    MarkAllUserScopedCachesStale();
    SyncServerUrlToConfig(resolvedUrl);

    if (sess.accessToken.empty() && sess.refreshToken.empty())
    {
        out.ok        = false;
        out.errorCode = "session_expired";
        out.error     = "该账号登录凭据已过期，请重新输入密码验证";
        out.user      = targetUser;
        return out;
    }

    const long long nowSec = NowUnixSeconds();
    const bool needsRefresh = sess.accessToken.empty() ||
        (sess.expiresAt > 0 && !sess.refreshToken.empty() && nowSec >= sess.expiresAt - kProactiveRefreshLeadSeconds);
    if (needsRefresh && !sess.refreshToken.empty())
    {
        const AuthResult refRes = RefreshSession();
        if (refRes.ok)
            return refRes;
        if (GetAuthState() == AuthState::SessionExpired || sess.accessToken.empty())
            return refRes;

        out.ok           = true;
        out.user         = GetCurrentUser();
        out.token        = GetAuthToken();
        out.refreshToken = GetRefreshToken();
        out.expiresAt    = GetTokenExpiresAt();
        return out;
    }

    const HttpResponse meRes = SendWithAuthRetry(MakeRequest("GET", "/api/v1/auth/me", {}, true));
    if (meRes.IsSuccess())
    {
        return HandleAuthResponse(meRes, true);
    }
    if (GetAuthState() == AuthState::SessionExpired)
    {
        out.ok        = false;
        out.errorCode = "session_expired";
        out.error     = "该账号登录凭据已过期，请重新输入密码验证";
        out.user      = GetCurrentUser();
        return out;
    }
    if (!meRes.IsNetworkError() && meRes.statusCode < 500 && meRes.statusCode != 404)
    {
        MarkOnlineFromAuthenticatedSuccess();
    }
    else if (meRes.statusCode == 404)
    {
        MarkOnlineFromAuthenticatedSuccess();
    }

    out.ok           = true;
    out.user         = GetCurrentUser();
    out.token        = GetAuthToken();
    out.refreshToken = GetRefreshToken();
    out.expiresAt    = GetTokenExpiresAt();
    return out;
}

bool APIClient::RemoveSavedAccount(const std::string& userId)
{
    if (userId.empty())
        return false;

    const std::string prevActiveId = GetCurrentUser().id;
    auto& store                    = GetAuthStore();
    const bool removed             = store.RemoveSavedAccount(userId);
    store.Save();

    const auto sess      = store.GetSession();
    const OnlineUser cur = SessionProfileToOnlineUser(sess);
    if (cur.id.empty() || prevActiveId == userId)
    {
        std::lock_guard lock(m_stateMutex);
        if (cur.id.empty())
        {
            m_authToken.clear();
            m_refreshToken.clear();
            m_expiresAt = 0;
            m_user      = {};
            m_authState = AuthState::Unauthenticated;
        }
        else
        {
            m_authToken    = sess.accessToken;
            m_refreshToken = sess.refreshToken;
            m_expiresAt    = sess.expiresAt;
            m_user         = cur;
            m_authState    = !m_authToken.empty() ? AuthState::AuthenticatedOffline : AuthState::SessionExpired;
        }
    }
    return removed;
}

ScoreSubmitResult APIClient::SubmitScore(const sakura::game::GameResult& result)
{
    ScoreSubmitResult out;
    if (result.assisted)
    {
        out.error = "辅助演奏不计入全球排行榜";
        return out;
    }

    const int totalJudged = result.totalJudgments > 0
        ? result.totalJudgments
        : (result.perfectCount + result.greatCount + result.goodCount + result.badCount + result.missCount);
    const std::string subHash = ComputeGameResultSubmissionHash(result);

    nlohmann::json payload = {
        { "client_submission_id", subHash },
        { "chart_id", result.chartId },
        { "chart_title", result.chartTitle },
        { "chart_hash", result.chartHash.empty() ? result.chartId : result.chartHash },
        { "difficulty", result.difficulty },
        { "difficulty_level", result.difficultyLevel },
        { "score", result.score },
        { "accuracy", result.accuracy },
        { "max_combo", result.maxCombo },
        { "grade", GradeToStr(result.grade) },
        { "perfect_count", result.perfectCount },
        { "great_count", result.greatCount },
        { "good_count", result.goodCount },
        { "bad_count", result.badCount },
        { "miss_count", result.missCount },
        { "total_judgments", totalJudged },
        { "synergy_count", result.synergyCount },
        { "is_full_combo", result.isFullCombo },
        { "is_all_perfect", result.isAllPerfect },
        { "assisted", result.assisted },
        { "playback_rate", result.playbackRate }
    };

    if (!result.replayFile.empty())
        payload["replay_file"] = result.replayFile;
    const std::string replayRaw = ReadWholeFileUtf8(result.replayFile);
    if (!replayRaw.empty())
        payload["replay_data"] = replayRaw;

    const std::string bodyStr  = payload.dump();
    const std::string dedupeId = "score:" + subHash;
    if (!IsAuthenticated())
    {
        MutableOfflineQueue().Enqueue(PendingActionType::StandardScore, bodyStr, std::string{}, dedupeId);
        out.queuedOffline = true;
        out.error         = "未登录，已缓存至待同步队列";
        return out;
    }

    const auto res = SendWithAuthRetry(MakeRequest("POST", "/api/v1/scores", bodyStr, true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        MutableOfflineQueue().Enqueue(PendingActionType::StandardScore, bodyStr, std::string{}, dedupeId);
        out.queuedOffline = true;
        out.error         = "网络离线，成绩已加入待同步队列";
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "成绩提交被拒绝");
            return out;
        }
        out.ok      = true;
        out.scoreId = j.value("score_id", "");
        out.rank    = j.value("rank", 0);
        out.pp      = j.value("pp", 0.0);
        out.totalPP = j.value("total_pp", 0.0);
        out.newBest = j.value("new_best", false);
        {
            std::lock_guard lock(m_stateMutex);
            m_user.totalPP = out.totalPP;
            if (j.contains("sakura_rating") && j["sakura_rating"].is_number())
                m_user.sakuraRating = j["sakura_rating"].get<double>();
        }
        if (!result.chartId.empty())
        {
            MutableOnlineCache().MarkStale("leaderboard:" + result.chartId + ":");
            MutableOnlineCache().MarkStale("leaderboard_rivals:" + result.chartId + ":");
        }
        SaveToConfig();
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("成绩响应解析失败: ") + e.what();
        return out;
    }
}

bool APIClient::EnqueueCourseRecordOffline(const sakura::game::CourseRecord& record)
{
    if (record.courseId.empty())
        return false;

    const nlohmann::json payload = {
        { "course_id", record.courseId },
        { "badge_text", record.badgeText },
        { "dan_rank", record.danRank },
        { "total_score", record.totalScore },
        { "avg_accuracy", record.avgAccuracy },
        { "remaining_life", record.remainingLife },
        { "cleared", record.cleared },
        { "cleared_at", record.clearedAt }
    };
    const std::string dedupeId = "course:" + record.courseId + ":" + std::to_string(record.totalScore)
        + ":" + (record.cleared ? "1" : "0");
    const std::string actionId = MutableOfflineQueue().Enqueue(
        PendingActionType::DanCourseRecord, payload.dump(), std::string{}, dedupeId);
    return !actionId.empty();
}

int APIClient::FlushPendingActions()
{
    if (!IsAuthenticated())
        return 0;

    std::lock_guard flushLock(m_flushMutex);
    if (!IsAuthenticated())
        return 0;

    auto& queue = MutableOfflineQueue();
    const auto readyItems = queue.GetReadyItems();
    if (readyItems.empty())
        return 0;

    int flushed        = 0;
    bool userMetaDirty = false;

    for (const auto& item : readyItems)
    {
        HttpRequest req;
        std::string scoreChartId;

        if (item.type == PendingActionType::StandardScore)
        {
            std::string bodyStr = item.payloadJson;
            try
            {
                auto bodyJson = nlohmann::json::parse(item.payloadJson, nullptr, false);
                if (bodyJson.is_object())
                {
                    scoreChartId = JsonStringOr(bodyJson, "chart_id", "");
                    if (!bodyJson.contains("replay_data") && bodyJson.contains("replay_file") &&
                        bodyJson["replay_file"].is_string())
                    {
                        const std::string replayRaw = ReadWholeFileUtf8(bodyJson["replay_file"].get<std::string>());
                        if (!replayRaw.empty())
                        {
                            bodyJson["replay_data"] = replayRaw;
                            bodyStr                 = bodyJson.dump();
                        }
                    }
                }
            }
            catch (...)
            {
            }
            req = MakeRequest("POST", "/api/v1/scores", bodyStr, true);
        }
        else if (item.type == PendingActionType::DailyScore)
        {
            std::string bodyStr = item.payloadJson;
            try
            {
                auto bodyJson = nlohmann::json::parse(item.payloadJson, nullptr, false);
                if (bodyJson.is_object())
                {
                    if (!item.dateUtc8.empty() && !bodyJson.contains("date"))
                        bodyJson["date"] = item.dateUtc8;
                    if (!bodyJson.contains("replay_data") && bodyJson.contains("replay_file") &&
                        bodyJson["replay_file"].is_string())
                    {
                        const std::string replayRaw = ReadWholeFileUtf8(bodyJson["replay_file"].get<std::string>());
                        if (!replayRaw.empty())
                            bodyJson["replay_data"] = replayRaw;
                    }
                    bodyStr = bodyJson.dump();
                }
            }
            catch (...)
            {
            }
            req = MakeRequest("POST", "/api/v1/daily/scores", bodyStr, true);
        }
        else
        {
            auto parsedCourse = nlohmann::json::parse(item.payloadJson, nullptr, false);
            std::string bodyStr;
            if (parsedCourse.is_object() && parsedCourse.contains("course_records"))
            {
                bodyStr = parsedCourse.dump();
            }
            else if (parsedCourse.is_object())
            {
                const nlohmann::json syncBody = {
                    { "scores", nlohmann::json::array() },
                    { "achievements", nlohmann::json::array() },
                    { "course_records", nlohmann::json::array({ parsedCourse }) },
                    { "settings", nlohmann::json::object() },
                    { "settings_updated_at", 0 }
                };
                bodyStr = syncBody.dump();
            }
            else
            {
                bodyStr = item.payloadJson;
            }
            req = MakeRequest("POST", "/api/v1/sync/push", bodyStr, true);
        }

        const HttpResponse res = SendWithAuthRetry(std::move(req));
        if (res.IsSuccess())
        {
            MarkOnlineFromAuthenticatedSuccess();
            if (item.type == PendingActionType::StandardScore)
            {
                if (!scoreChartId.empty())
                {
                    MutableOnlineCache().MarkStale("leaderboard:" + scoreChartId + ":");
                    MutableOnlineCache().MarkStale("leaderboard_rivals:" + scoreChartId + ":");
                }
            }
            else if (item.type == PendingActionType::DailyScore)
            {
                MutableOnlineCache().MarkStale("daily:");
            }

            try
            {
                if (!res.body.empty())
                {
                    const auto j = nlohmann::json::parse(res.body, nullptr, false);
                    if (j.is_object())
                    {
                        const bool hasPP     = j.contains("total_pp") && j["total_pp"].is_number();
                        const bool hasRating = j.contains("sakura_rating") && j["sakura_rating"].is_number();
                        if (hasPP || hasRating)
                        {
                            std::lock_guard stateLock(m_stateMutex);
                            if (hasPP)
                                m_user.totalPP = j["total_pp"].get<double>();
                            if (hasRating)
                                m_user.sakuraRating = j["sakura_rating"].get<double>();
                            userMetaDirty = true;
                        }
                    }
                }
            }
            catch (...)
            {
            }

            queue.MarkSucceeded(item.id);
            ++flushed;
        }
        else if (res.IsNetworkError() || res.statusCode >= 500 || res.statusCode == 408 || res.statusCode == 429)
        {
            if (res.IsNetworkError() || res.statusCode >= 500)
                MarkOfflinePreserveSession();
            queue.MarkRetryableFailure(item.id, ExtractHttpErrorReason(res));
        }
        else if (res.statusCode == 401 || res.statusCode == 403)
        {
            break;
        }
        else if (res.statusCode == 400 || res.statusCode == 422 || (res.statusCode >= 400 && res.statusCode < 500))
        {
            queue.MarkPermanentFailure(item.id, ExtractHttpErrorReason(res));
        }
    }

    if (userMetaDirty)
        SaveToConfig();

    return flushed;
}

int APIClient::FlushPendingScores()
{
    return FlushPendingActions();
}

LeaderboardResult APIClient::FetchLeaderboardInternal(const std::string& chartId,
                                                      const std::string& difficulty,
                                                      int limit,
                                                      std::string_view scope,
                                                      const std::shared_ptr<CancelToken>& cancelToken)
{
    LeaderboardResult out;
    const std::string cleanScope = (scope == "rivals") ? "rivals" : "global";
    if (cleanScope == "rivals" && !IsAuthenticated())
    {
        out.error = "请先登录在线账号后再查看劲敌圈排行榜";
        return out;
    }
    const int safeLimit        = std::max(1, limit);
    const std::string prefix   = (cleanScope == "rivals") ? "leaderboard_rivals:" : "leaderboard:";
    const std::string cacheKey = prefix + chartId + ":" + difficulty;
    auto& cache                = MutableOnlineCache();
    const CacheLookupResult cached = cache.Lookup(cacheKey, kOnlineCacheSoftTtlSec, kOnlineCacheHardTtlSec);

    auto applyLimit = [safeLimit](LeaderboardResult& lb)
    {
        if (lb.entries.size() > static_cast<std::size_t>(safeLimit))
            lb.entries.resize(static_cast<std::size_t>(safeLimit));
    };

    if (cached.found && !cached.isStale && !cached.payloadJson.empty())
    {
        LeaderboardResult parsed = ParseLeaderboardBodyJson(cached.payloadJson);
        if (parsed.ok)
        {
            applyLimit(parsed);
            return parsed;
        }
    }

    const std::string path = "/api/v1/leaderboards/" + UrlEncode(chartId)
        + "?difficulty=" + UrlEncode(difficulty)
        + "&scope=" + cleanScope + "&limit=" + std::to_string(safeLimit);

    HttpRequest req = MakeRequest("GET", path, {}, IsAuthenticated());
    req.cancelToken = cancelToken;
    if (cached.found && cached.isStale && !cached.etag.empty())
    {
        req.headers.emplace_back("If-None-Match", cached.etag);
    }

    const HttpResponse res = IsAuthenticated()
        ? SendWithAuthRetry(std::move(req))
        : GetTransport()->Send(req);

    if (res.cancelled || (cancelToken && cancelToken->IsCancelled()))
    {
        out.error = "请求已取消";
        return out;
    }

    if (res.IsNotModified() && cached.found && !cached.payloadJson.empty())
    {
        if (!IsAuthenticated())
            m_status.store(OnlineStatus::Online);
        cache.TouchFresh(cacheKey);
        LeaderboardResult parsed = ParseLeaderboardBodyJson(cached.payloadJson);
        if (parsed.ok)
        {
            applyLimit(parsed);
            return parsed;
        }
    }

    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        if (cached.found && !cached.payloadJson.empty())
        {
            LeaderboardResult fallback = ParseLeaderboardBodyJson(cached.payloadJson);
            if (fallback.ok)
            {
                applyLimit(fallback);
                return fallback;
            }
        }
        out.error = res.error.empty() ? "离线：无法获取全球排行榜" : res.error;
        return out;
    }

    m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess())
    {
        try
        {
            const auto j = nlohmann::json::parse(res.body);
            out.error    = j.value("error", "获取排行榜失败");
        }
        catch (...)
        {
            out.error = "获取排行榜失败";
        }
        return out;
    }

    LeaderboardResult parsed = ParseLeaderboardBodyJson(res.body);
    if (parsed.ok)
    {
        cache.Put(cacheKey, res.body, ExtractETagHeader(res));
        applyLimit(parsed);
    }
    return parsed;
}

LeaderboardResult APIClient::FetchLeaderboard(const std::string& chartId,
                                              const std::string& difficulty,
                                              int limit,
                                              std::string_view scope)
{
    return FetchLeaderboardInternal(chartId, difficulty, limit, scope, nullptr);
}

bool APIClient::DownloadReplay(const std::string& scoreId, const std::string& destPath)
{
    if (scoreId.empty() || destPath.empty())
        return false;

    const bool trackHere = !g_inTransferPoolTask;
    if (trackHere)
        m_activeTransferCount.fetch_add(1, std::memory_order_acq_rel);
    struct SyncTransferScope
    {
        APIClient* self = nullptr;
        ~SyncTransferScope()
        {
            if (self)
                self->OnTransferFinished();
        }
    } guard{ trackHere ? this : nullptr };

    m_activeTransferProgress.store(0.0f, std::memory_order_release);

    HttpRequest req = MakeRequest("GET", "/api/v1/replays/" + UrlEncode(scoreId), {}, true);
    req.onProgress  = [this](std::size_t dl, std::size_t total)
    {
        const float frac = total > 0
            ? std::clamp(static_cast<float>(dl) / static_cast<float>(total), 0.0f, 1.0f)
            : 0.5f;
        m_activeTransferProgress.store(frac, std::memory_order_release);
    };

    const auto res = SendWithAuthRetry(std::move(req));
    if (!res.IsSuccess() || res.body.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(destPath).parent_path(), ec);
    const bool wrote = sakura::utils::AtomicWrite(destPath, res.body);
    if (wrote)
        m_activeTransferProgress.store(1.0f, std::memory_order_release);
    return wrote;
}

void APIClient::EnsurePoolStarted(TaskPool& pool, std::size_t threadCount)
{
    std::lock_guard lock(pool.mutex);
    if (pool.running)
        return;
    pool.stopping = false;
    pool.running  = true;
    pool.threads.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        pool.threads.emplace_back([&pool]()
        {
            for (;;)
            {
                std::function<void()> task;
                {
                    std::unique_lock ulock(pool.mutex);
                    pool.cv.wait(ulock, [&pool]() { return pool.stopping || !pool.tasks.empty(); });
                    if (pool.stopping && pool.tasks.empty())
                        break;
                    task = std::move(pool.tasks.front());
                    pool.tasks.pop_front();
                }
                if (task)
                {
                    try
                    {
                        task();
                    }
                    catch (...)
                    {
                    }
                }
            }
        });
    }
}

void APIClient::ShutdownPool(TaskPool& pool)
{
    {
        std::lock_guard lock(pool.mutex);
        if (!pool.running)
            return;
        pool.stopping = true;
    }
    pool.cv.notify_all();
    for (auto& worker : pool.threads)
    {
        if (worker.joinable())
            worker.join();
    }
    {
        std::lock_guard lock(pool.mutex);
        pool.threads.clear();
        pool.tasks.clear();
        pool.running  = false;
        pool.stopping = false;
    }
}

std::pair<uint64_t, std::shared_ptr<CancelToken>> APIClient::BeginChannelRequest(ChannelState& channel)
{
    std::lock_guard lock(channel.mutex);
    if (channel.activeToken)
        channel.activeToken->Cancel();
    const uint64_t seq      = NextRequestSeq();
    auto           newToken = std::make_shared<CancelToken>();
    channel.latestSeq       = seq;
    channel.activeToken     = newToken;
    return { seq, std::move(newToken) };
}

bool APIClient::IsChannelRequestCurrent(const ChannelState& channel,
                                        uint64_t seq,
                                        const std::shared_ptr<CancelToken>& token)
{
    if (token && token->IsCancelled())
        return false;
    std::lock_guard lock(channel.mutex);
    return channel.latestSeq == seq;
}

void APIClient::CancelChannel(ChannelState& channel)
{
    std::lock_guard lock(channel.mutex);
    if (channel.activeToken)
        channel.activeToken->Cancel();
    channel.activeToken.reset();
}

void APIClient::OnTransferFinished() noexcept
{
    int prev = m_activeTransferCount.fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 1)
    {
        int expected = m_activeTransferCount.load(std::memory_order_acquire);
        while (expected < 0 && !m_activeTransferCount.compare_exchange_weak(
                                   expected, 0, std::memory_order_acq_rel))
        {
        }
    }
}

void APIClient::ScheduleBackgroundOfflineFlush()
{
    if (!IsAuthenticated() || MutableOfflineQueue().GetCount() == 0)
        return;
    bool expected = false;
    if (!m_bgFlushScheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    PostTransferTask([this]()
    {
        struct ResetFlag
        {
            std::atomic<bool>& flag;
            ~ResetFlag() { flag.store(false, std::memory_order_release); }
        } reset{ m_bgFlushScheduled };
        (void)FlushPendingActions();
    });
}

void APIClient::PostTask(std::function<void()> workerTask)
{
    if (!workerTask)
        return;
    EnsurePoolStarted(m_interactivePool, kWorkerPoolThreadCount);
    {
        std::lock_guard lock(m_interactivePool.mutex);
        if (m_interactivePool.stopping)
            return;
        m_interactivePool.tasks.push_back(std::move(workerTask));
    }
    m_interactivePool.cv.notify_one();
}

void APIClient::PostTransferTask(std::function<void()> transferTask)
{
    if (!transferTask)
        return;
    EnsurePoolStarted(m_transferPool, kWorkerPoolThreadCount);
    {
        std::lock_guard lock(m_transferPool.mutex);
        if (m_transferPool.stopping)
            return;
        m_activeTransferCount.fetch_add(1, std::memory_order_acq_rel);
        std::shared_ptr<int> token(new int(1), [this](int* p)
        {
            delete p;
            OnTransferFinished();
        });
        m_transferPool.tasks.push_back([task = std::move(transferTask), token = std::move(token)]() mutable
        {
            const bool prevInPool = g_inTransferPoolTask;
            g_inTransferPoolTask  = true;
            try
            {
                task();
            }
            catch (...)
            {
            }
            g_inTransferPoolTask = prevInPool;
            token.reset();
        });
    }
    m_transferPool.cv.notify_one();
}

void APIClient::PostMainCallback(std::function<void()> mainCallback)
{
    std::lock_guard lock(m_cbMutex);
    m_callbacks.push_back(std::move(mainCallback));
}

void APIClient::PumpCallbacks()
{
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard lock(m_cbMutex);
        batch.swap(m_callbacks);
    }
    for (auto& fn : batch)
    {
        if (fn)
            fn();
    }
}

void APIClient::LoginGuestAsync(const std::string& nickname, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, nickname, cb = std::move(cb)]() mutable
    {
        auto res = LoginGuest(nickname);
        if (res.ok)
            ScheduleBackgroundOfflineFlush();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RegisterAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, username, password, cb = std::move(cb)]() mutable
    {
        auto res = Register(username, password);
        if (res.ok)
            ScheduleBackgroundOfflineFlush();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::LoginAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, username, password, cb = std::move(cb)]() mutable
    {
        auto res = Login(username, password);
        if (res.ok)
            ScheduleBackgroundOfflineFlush();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RefreshProfileAsync(std::function<void(AuthResult)> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        auto res = RefreshProfile();
        if (res.ok)
            ScheduleBackgroundOfflineFlush();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::UpgradeGuestAsync(const std::string& username, const std::string& password,
                                  std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, username, password, cb = std::move(cb)]() mutable
    {
        auto res = UpgradeGuest(username, password);
        if (res.ok)
            ScheduleBackgroundOfflineFlush();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::ChangePasswordAsync(const std::string& oldPassword, const std::string& newPassword,
                                    std::function<void(AuthResult)> cb)
{
    PostTask([this, oldPassword, newPassword, cb = std::move(cb)]() mutable
    {
        auto res = ChangePassword(oldPassword, newPassword);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SwitchSavedAccountAsync(const std::string& userId, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, userId, cb = std::move(cb)]() mutable
    {
        auto res = SwitchSavedAccount(userId);
        if (res.ok)
            ScheduleBackgroundOfflineFlush();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::LogoutAsync(std::function<void()> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        Logout();
        if (cb)
            PostMainCallback([cb = std::move(cb)]() mutable { cb(); });
    });
}

void APIClient::SubmitScoreAsync(const sakura::game::GameResult& result, std::function<void(ScoreSubmitResult)> cb)
{
    PostTask([this, result, cb = std::move(cb)]() mutable
    {
        auto res = SubmitScore(result);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FlushPendingActionsAsync(std::function<void(int)> cb)
{
    PostTransferTask([this, cb = std::move(cb)]() mutable
    {
        const int flushed = FlushPendingActions();
        if (cb)
            PostMainCallback([cb = std::move(cb), flushed]() mutable { cb(flushed); });
    });
}

void APIClient::FetchLeaderboardAsync(const std::string& chartId, const std::string& difficulty, int limit,
                                      std::function<void(LeaderboardResult)> cb,
                                      std::string_view scope)
{
    const std::string scopeStr(scope);
    auto [seq, token] = BeginChannelRequest(m_leaderboardChannel);
    PostTask([this, chartId, difficulty, limit, scopeStr, seq, token = std::move(token), cb = std::move(cb)]() mutable
    {
        if (!IsChannelRequestCurrent(m_leaderboardChannel, seq, token))
            return;
        auto res = FetchLeaderboardInternal(chartId, difficulty, limit, scopeStr, token);
        if (!IsChannelRequestCurrent(m_leaderboardChannel, seq, token))
            return;
        if (cb)
        {
            PostMainCallback([this, seq, token, cb = std::move(cb), res = std::move(res)]() mutable
            {
                if (!IsChannelRequestCurrent(m_leaderboardChannel, seq, token))
                    return;
                cb(std::move(res));
            });
        }
    });
}

void APIClient::DownloadReplayAsync(const std::string& scoreId, const std::string& destPath,
                                    std::function<void(bool, std::string)> cb)
{
    PostTransferTask([this, scoreId, destPath, cb = std::move(cb)]() mutable
    {
        const bool ok = DownloadReplay(scoreId, destPath);
        if (cb)
            PostMainCallback([cb = std::move(cb), ok, destPath]() mutable { cb(ok, destPath); });
    });
}

namespace
{
std::string ToLowerCopy(std::string s)
{
    for (char& ch : s)
    {
        if (static_cast<unsigned char>(ch) < 128)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

sakura::game::Grade ParseGradeStr(const std::string& s)
{
    using sakura::game::Grade;
    if (s == "SS") return Grade::SS;
    if (s == "S")  return Grade::S;
    if (s == "A")  return Grade::A;
    if (s == "B")  return Grade::B;
    if (s == "C")  return Grade::C;
    return Grade::D;
}

MarketChartItem ParseMarketChartJson(const nlohmann::json& j)
{
    MarketChartItem item;
    item.id            = j.value("id", "");
    item.chartSlug     = j.value("chart_slug", item.id);
    item.uploaderId    = j.value("uploader_id", "");
    item.uploaderName  = j.value("uploader_name", "Unknown");
    item.title         = j.value("title", "Untitled");
    item.artist        = j.value("artist", "Unknown");
    item.charter       = j.value("charter", "Unknown");
    item.description   = j.value("description", "");
    item.bpm           = j.value("bpm", 120.0f);
    item.minLevel      = j.value("min_level", 1.0f);
    item.maxLevel      = j.value("max_level", 1.0f);
    item.packageSha256 = j.value("package_sha256", "");
    item.contentHash   = j.value("content_hash", "");
    item.packageSize   = static_cast<std::size_t>(j.value("package_size", 0ULL));
    item.downloads     = j.value("downloads", 0);
    item.ratingAvg     = j.value("rating_avg", 0.0);
    item.ratingCount   = j.value("rating_count", 0);
    if (j.contains("my_rating") && j["my_rating"].is_number_integer())
        item.myRating = j["my_rating"].get<int>();
    item.createdAt     = j.value("created_at", 0LL);
    item.updatedAt     = j.value("updated_at", 0LL);

    if (j.contains("tags") && j["tags"].is_array())
    {
        for (const auto& t : j["tags"])
            if (t.is_string()) item.tags.push_back(t.get<std::string>());
    }
    if (j.contains("difficulties") && j["difficulties"].is_array())
    {
        for (const auto& d : j["difficulties"])
        {
            MarketDifficultyInfo di;
            di.name      = d.value("name", "Normal");
            di.level     = d.value("level", 1.0f);
            di.chartFile = d.value("chart_file", "");
            di.noteCount = d.value("note_count", 0);
            item.difficulties.push_back(std::move(di));
        }
    }
    return item;
}

std::string SanitizeFolderSlug(std::string raw)
{
    if (raw.empty()) return "market_chart";
    for (char& c : raw)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 128 && !(std::isalnum(uc) || c == '-' || c == '_'))
            c = '_';
    }
    return raw;
}
}

MarketInstallState APIClient::GetInstalledMarketState(const MarketChartItem& item,
                                                      const std::string& chartsRootDir,
                                                      std::string* outInstalledChartId)
{
    namespace fs = std::filesystem;
    const fs::path root = chartsRootDir.empty() ? fs::path(sakura::core::Paths::User("charts")) : fs::path(chartsRootDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return MarketInstallState::NotInstalled;

    for (const auto& entry : fs::directory_iterator(root, ec))
    {
        if (ec || !entry.is_directory(ec)) continue;
        const fs::path metaFile = entry.path() / ".market_meta.json";
        if (!fs::is_regular_file(metaFile, ec)) continue;
        try
        {
            std::ifstream ifs(metaFile);
            const auto meta = nlohmann::json::parse(ifs);
            if (meta.value("market_id", "") == item.id)
            {
                if (outInstalledChartId)
                    *outInstalledChartId = meta.value("local_chart_id", item.chartSlug);
                const std::string localSha = ToLowerCopy(meta.value("package_sha256", ""));
                if (!item.packageSha256.empty() && localSha != ToLowerCopy(item.packageSha256))
                    return MarketInstallState::UpdateAvailable;
                return MarketInstallState::Installed;
            }
        }
        catch (...) {}
    }
    return MarketInstallState::NotInstalled;
}

MarketPublishResult APIClient::PublishChartPackage(const std::string& sakuraFilePath, const std::string& description)
{
    MarketPublishResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号后再发布谱面";
        return out;
    }
    const std::string pkgBytes = ReadWholeFileUtf8(sakuraFilePath);
    if (pkgBytes.empty())
    {
        out.error = "无法读取 .sakura 谱面包文件";
        return out;
    }

    const bool trackHere = !g_inTransferPoolTask;
    if (trackHere)
        m_activeTransferCount.fetch_add(1, std::memory_order_acq_rel);
    struct SyncTransferScope
    {
        APIClient* self = nullptr;
        ~SyncTransferScope()
        {
            if (self)
                self->OnTransferFinished();
        }
    } guard{ trackHere ? this : nullptr };

    m_activeTransferProgress.store(0.0f, std::memory_order_release);

    std::string path = "/api/v1/charts";
    if (!description.empty())
        path += "?description=" + UrlEncode(description);

    HttpRequest req = MakeRequest("POST", path, {}, true);
    req.headers.emplace_back("Content-Type", "application/octet-stream");
    req.body      = pkgBytes;
    req.timeoutMs = 30000;

    const auto res = SendWithAuthRetry(std::move(req));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "网络连接失败，无法上传谱面" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "谱面上传失败");
            return out;
        }
        out.ok    = true;
        out.chart = ParseMarketChartJson(j.at("chart"));
        m_activeTransferProgress.store(1.0f, std::memory_order_release);
        MutableOnlineCache().MarkStale("market:");
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("发布响应解析失败: ") + e.what();
        return out;
    }
}

MarketListResult APIClient::FetchMarketChartsInternal(const MarketQuery& query,
                                                      const std::shared_ptr<CancelToken>& cancelToken)
{
    MarketListResult out;
    std::ostringstream qs;
    qs << "/api/v1/charts?sort=" << UrlEncode(query.sort.empty() ? "newest" : query.sort)
       << "&minLevel=" << query.minLevel
       << "&maxLevel=" << query.maxLevel
       << "&page=" << std::max(1, query.page)
       << "&pageSize=" << std::max(1, query.pageSize);
    if (!query.q.empty())   qs << "&q=" << UrlEncode(query.q);
    if (!query.tag.empty()) qs << "&tag=" << UrlEncode(query.tag);

    const std::string queryPath = qs.str();
    const std::string cacheKey  = "market:" + sakura::utils::ComputeSha256Hex(queryPath).substr(0, 32);
    auto& cache                 = MutableOnlineCache();
    const CacheLookupResult cached = cache.Lookup(cacheKey, kOnlineCacheSoftTtlSec, kOnlineCacheHardTtlSec);

    auto parseBody = [](const std::string& bodyText) -> MarketListResult
    {
        MarketListResult parsed;
        try
        {
            const auto j = nlohmann::json::parse(bodyText);
            if (!j.value("ok", false))
            {
                parsed.error = j.value("error", "获取谱面市场列表失败");
                return parsed;
            }
            parsed.ok    = true;
            parsed.total = j.value("total", 0);
            if (j.contains("charts") && j["charts"].is_array())
            {
                for (const auto& item : j["charts"])
                    parsed.charts.push_back(ParseMarketChartJson(item));
            }
        }
        catch (const std::exception& e)
        {
            parsed.ok    = false;
            parsed.error = std::string("市场响应解析失败: ") + e.what();
        }
        return parsed;
    };

    if (cached.found && !cached.isStale && !cached.payloadJson.empty())
    {
        MarketListResult parsed = parseBody(cached.payloadJson);
        if (parsed.ok)
            return parsed;
    }

    HttpRequest req = MakeRequest("GET", queryPath, {}, IsAuthenticated());
    req.cancelToken = cancelToken;
    if (cached.found && cached.isStale && !cached.etag.empty())
    {
        req.headers.emplace_back("If-None-Match", cached.etag);
    }

    const HttpResponse res = IsAuthenticated()
        ? SendWithAuthRetry(std::move(req))
        : GetTransport()->Send(req);

    if (res.cancelled || (cancelToken && cancelToken->IsCancelled()))
    {
        out.error = "请求已取消";
        return out;
    }

    if (res.IsNotModified() && cached.found && !cached.payloadJson.empty())
    {
        if (!IsAuthenticated())
            m_status.store(OnlineStatus::Online);
        cache.TouchFresh(cacheKey);
        MarketListResult parsed = parseBody(cached.payloadJson);
        if (parsed.ok)
            return parsed;
    }

    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        if (cached.found && !cached.payloadJson.empty())
        {
            MarketListResult fallback = parseBody(cached.payloadJson);
            if (fallback.ok)
                return fallback;
        }
        out.error = res.error.empty() ? "离线：无法连接在线谱面市场" : res.error;
        return out;
    }

    m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess())
    {
        try
        {
            const auto j = nlohmann::json::parse(res.body);
            out.error    = j.value("error", "获取谱面市场列表失败");
        }
        catch (...)
        {
            out.error = "获取谱面市场列表失败";
        }
        return out;
    }

    MarketListResult parsed = parseBody(res.body);
    if (parsed.ok)
    {
        cache.Put(cacheKey, res.body, ExtractETagHeader(res));
    }
    return parsed;
}

MarketListResult APIClient::FetchMarketCharts(const MarketQuery& query)
{
    return FetchMarketChartsInternal(query, nullptr);
}

MarketDownloadResult APIClient::DownloadAndInstallChart(const MarketChartItem& item,
                                                        const std::string& chartsRootDir,
                                                        std::function<void(float)> onProgress)
{
    MarketDownloadResult out;
    if (item.id.empty())
    {
        out.error = "无效的市场谱面 ID";
        return out;
    }

    const bool trackHere = !g_inTransferPoolTask;
    if (trackHere)
        m_activeTransferCount.fetch_add(1, std::memory_order_acq_rel);
    struct SyncTransferScope
    {
        APIClient* self = nullptr;
        ~SyncTransferScope()
        {
            if (self)
                self->OnTransferFinished();
        }
    } guard{ trackHere ? this : nullptr };

    m_activeTransferProgress.store(0.0f, std::memory_order_release);

    HttpRequest req = MakeRequest("GET", "/api/v1/charts/" + UrlEncode(item.id) + "/download", {}, false);
    req.timeoutMs = 30000;
    const std::size_t fallbackTotal = item.packageSize;
    req.onProgress = [this, onProgress, fallbackTotal](std::size_t dl, std::size_t total)
    {
        const std::size_t denom = total > 0 ? total : fallbackTotal;
        const float frac = denom > 0
            ? std::clamp(static_cast<float>(dl) / static_cast<float>(denom), 0.0f, 1.0f)
            : 0.5f;
        m_activeTransferProgress.store(frac, std::memory_order_release);
        if (onProgress)
            onProgress(frac);
    };

    const auto res = GetTransport()->Send(req);
    if (res.cancelled)
    {
        out.error = "下载已取消";
        return out;
    }
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "下载失败：网络不可达" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess() || res.body.empty())
    {
        out.error = "下载谱面包失败 (HTTP " + std::to_string(res.statusCode) + ")";
        return out;
    }

    std::string expectedSha = res.GetHeader("X-Sakura-Sha256");
    if (expectedSha.empty())
        expectedSha = item.packageSha256;
    const std::string actualSha = sakura::utils::ComputeSha256Hex(res.body);
    if (!expectedSha.empty() && ToLowerCopy(actualSha) != ToLowerCopy(expectedSha))
    {
        out.error = "SHA-256 完整性校验失败，已拦截安装";
        return out;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path rootDir = chartsRootDir.empty()
        ? fs::path(sakura::core::Paths::User("charts"))
        : fs::path(chartsRootDir);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path stageRoot = rootDir.parent_path() / "imports" / ("mkt-" + std::to_string(stamp));
    const fs::path pkgFile   = stageRoot / "download.sakura";
    const fs::path unpackDir = stageRoot / "unpacked";
    fs::create_directories(unpackDir, ec);

    auto cleanupStage = [&]() { fs::remove_all(stageRoot, ec); };

    if (!sakura::utils::AtomicWrite(pkgFile, res.body) ||
        !sakura::utils::ExtractZipArchive(pkgFile, unpackDir))
    {
        cleanupStage();
        out.error = ".sakura 谱面包解压失败";
        return out;
    }

    fs::path chartFolder = unpackDir;
    if (!fs::is_regular_file(chartFolder / "info.json", ec))
    {
        for (const auto& sub : fs::directory_iterator(unpackDir, ec))
        {
            if (sub.is_directory(ec) && fs::is_regular_file(sub.path() / "info.json", ec))
            {
                chartFolder = sub.path();
                break;
            }
        }
    }

    sakura::game::ChartLoader loader;
    const auto info = loader.LoadChartInfo((chartFolder / "info.json").string());
    if (!info || !fs::is_regular_file(chartFolder / info->musicFile, ec))
    {
        cleanupStage();
        out.error = "谱面包缺少有效的 info.json 或音频文件";
        return out;
    }
    for (const auto& diff : info->difficulties)
    {
        const auto cdata = loader.LoadChartData(info->folderPath + "/" + diff.chartFile);
        if (!cdata || !loader.ValidateChartData(*cdata))
        {
            cleanupStage();
            out.error = "难度谱面校验不通过：" + diff.name;
            return out;
        }
    }

    const nlohmann::json meta = {
        { "market_id", item.id },
        { "local_chart_id", info->id },
        { "package_sha256", actualSha },
        { "updated_at", item.updatedAt }
    };
    sakura::utils::AtomicWrite(chartFolder / ".market_meta.json", meta.dump(2));

    const fs::path destDir = rootDir / ("market_" + SanitizeFolderSlug(item.id));
    fs::create_directories(rootDir, ec);
    if (fs::exists(destDir, ec))
        fs::remove_all(destDir, ec);
    fs::rename(chartFolder, destDir, ec);
    if (ec)
    {
        ec.clear();
        fs::copy(chartFolder, destDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            cleanupStage();
            out.error = "无法写入本地曲库目录";
            return out;
        }
    }

    cleanupStage();
    m_activeTransferProgress.store(1.0f, std::memory_order_release);
    out.ok              = true;
    out.installedFolder = destDir.generic_string();
    out.chartId         = info->id;
    return out;
}

MarketDownloadResult APIClient::DownloadMarketChartById(const std::string& marketChartId,
                                                        const std::string& chartsRootDir,
                                                        std::function<void(float)> onProgress)
{
    MarketDownloadResult out;
    if (marketChartId.empty())
    {
        out.error = "无效的市场谱面 ID";
        return out;
    }

    const bool trackHere = !g_inTransferPoolTask;
    if (trackHere)
        m_activeTransferCount.fetch_add(1, std::memory_order_acq_rel);
    const bool savedInPool = g_inTransferPoolTask;
    g_inTransferPoolTask   = true;
    struct SyncTransferScope
    {
        APIClient* self        = nullptr;
        bool       restoreFlag = false;
        ~SyncTransferScope()
        {
            g_inTransferPoolTask = restoreFlag;
            if (self)
                self->OnTransferFinished();
        }
    } guard{ trackHere ? this : nullptr, savedInPool };

    const std::string metaPath = "/api/v1/charts/" + UrlEncode(marketChartId);
    const HttpResponse metaRes = IsAuthenticated()
        ? SendWithAuthRetry(MakeRequest("GET", metaPath, {}, true))
        : GetTransport()->Send(MakeRequest("GET", metaPath, {}, false));
    if (metaRes.cancelled)
    {
        out.error = "请求已取消";
        return out;
    }
    if (metaRes.IsNetworkError() || metaRes.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = metaRes.error.empty() ? "离线：无法获取谱面元信息" : metaRes.error;
        return out;
    }

    m_status.store(OnlineStatus::Online);
    MarketChartItem item;
    try
    {
        const auto j = nlohmann::json::parse(metaRes.body);
        if (!metaRes.IsSuccess() || !j.value("ok", false))
        {
            out.error = JsonStringOr(j, "error", "获取市场谱面元信息失败");
            return out;
        }
        const nlohmann::json& chartJson = (j.contains("chart") && j["chart"].is_object()) ? j["chart"] : j;
        item                            = ParseMarketChartJson(chartJson);
        if (item.id.empty())
            item.id = marketChartId;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("谱面元信息解析失败: ") + e.what();
        return out;
    }

    return DownloadAndInstallChart(item, chartsRootDir, std::move(onProgress));
}

MarketRateResult APIClient::RateMarketChart(const std::string& marketChartId, int rating, const std::string& comment)
{
    MarketRateResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号";
        return out;
    }
    const nlohmann::json body = { { "rating", rating }, { "comment", comment } };
    const auto res = SendWithAuthRetry(
        MakeRequest("POST", "/api/v1/charts/" + UrlEncode(marketChartId) + "/rate", body.dump(), true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法提交评价" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "评分提交失败");
            return out;
        }
        out.ok          = true;
        out.ratingAvg   = j.value("rating_avg", 0.0);
        out.ratingCount = j.value("rating_count", 0);
        out.myRating    = j.value("my_rating", rating);
        MutableOnlineCache().MarkStale("market:");
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("评分解析失败: ") + e.what();
        return out;
    }
}

CloudSyncResult APIClient::SyncCloudSave()
{
    CloudSyncResult out;
    if (!IsAuthenticated())
    {
        out.error = "尚未登录在线账号";
        return out;
    }

    const bool trackHere = !g_inTransferPoolTask;
    if (trackHere)
        m_activeTransferCount.fetch_add(1, std::memory_order_acq_rel);
    const bool savedInPool = g_inTransferPoolTask;
    g_inTransferPoolTask   = true;
    struct SyncTransferScope
    {
        APIClient* self        = nullptr;
        bool       restoreFlag = false;
        ~SyncTransferScope()
        {
            g_inTransferPoolTask = restoreFlag;
            if (self)
                self->OnTransferFinished();
        }
    } guard{ trackHere ? this : nullptr, savedInPool };

    m_activeTransferProgress.store(0.0f, std::memory_order_release);
    FlushPendingScores();

    auto& db  = sakura::data::Database::GetInstance();
    auto& cfg = sakura::core::Config::GetInstance();

    nlohmann::json scoresArr = nlohmann::json::array();
    const auto localScores = db.GetAllBestScores();
    for (const auto& s : localScores)
    {
        scoresArr.push_back({
            { "chart_id", s.chartId },
            { "chart_title", s.chartTitle },
            { "chart_hash", s.chartHash.empty() ? s.chartId : s.chartHash },
            { "difficulty", s.difficulty },
            { "difficulty_level", s.difficultyLevel },
            { "score", s.score },
            { "accuracy", s.accuracy },
            { "max_combo", s.maxCombo },
            { "grade", GradeToStr(s.grade) },
            { "perfect_count", s.perfectCount },
            { "great_count", s.greatCount },
            { "good_count", s.goodCount },
            { "bad_count", s.badCount },
            { "miss_count", s.missCount },
            { "is_full_combo", s.isFullCombo },
            { "is_all_perfect", s.isAllPerfect },
            { "played_at", s.playedAt }
        });
    }

    nlohmann::json achArr = nlohmann::json::array();
    const auto localAchs = db.GetAchievements();
    for (const auto& a : localAchs)
    {
        achArr.push_back({ { "id", a.id }, { "unlocked_at", a.unlockedAt } });
    }

    nlohmann::json coursesArr = nlohmann::json::array();
    const auto localCourses = db.GetAllCourseRecords();
    for (const auto& c : localCourses)
    {
        coursesArr.push_back({
            { "course_id", c.courseId },
            { "badge_text", c.badgeText },
            { "dan_rank", c.danRank },
            { "total_score", c.totalScore },
            { "avg_accuracy", c.avgAccuracy },
            { "remaining_life", c.remainingLife },
            { "cleared", c.cleared },
            { "cleared_at", c.clearedAt }
        });
    }

    const long long nowSec = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const long long origLocalSettingsTs = cfg.Get<long long>("online.settings_updated_at", 0LL);
    long long localSettingsTs           = origLocalSettingsTs;

    const auto localCols = sakura::game::CollectionManager::LoadFromConfig();
    if (!localCols.empty() && localSettingsTs == 0)
        localSettingsTs = nowSec;

    nlohmann::json colsArr = nlohmann::json::array();
    for (const auto& col : localCols)
    {
        colsArr.push_back({
            { "id", col.id },
            { "name", col.name },
            { "chart_ids", col.chartIds },
            { "updated_at", col.updatedAt }
        });
    }

    nlohmann::json settingsObj = {
        { "note_speed", cfg.Get<float>("gameplay.note_speed", 5.0f) },
        { "mouse_approach_ms", cfg.Get<int>("gameplay.mouse_approach_ms", 1000) },
        { "global_offset_ms", cfg.Get<int>("audio.global_offset_ms", 0) },
        { "pitch_lock", cfg.Get<bool>("audio.pitch_lock", true) },
        { "key_lane_0", cfg.Get<int>("input.key_lane_0", 4) },
        { "key_lane_1", cfg.Get<int>("input.key_lane_1", 22) },
        { "key_lane_2", cfg.Get<int>("input.key_lane_2", 7) },
        { "key_lane_3", cfg.Get<int>("input.key_lane_3", 9) },
        { "key_thumb",  cfg.Get<int>("input.key_thumb", 44) },
        { "collections", colsArr }
    };

    const nlohmann::json pushBody = {
        { "scores", scoresArr },
        { "achievements", achArr },
        { "course_records", coursesArr },
        { "settings", localSettingsTs > 0 ? settingsObj : nlohmann::json::object() },
        { "settings_updated_at", localSettingsTs }
    };

    const auto res = SendWithAuthRetry(MakeRequest("POST", "/api/v1/sync/push", pushBody.dump(), true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：云端同步暂不可用" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "云端同步失败");
            return out;
        }

        out.ok           = true;
        out.scoresPushed = j.value("scores_merged", 0);
        out.syncedAt     = j.value("synced_at", nowSec);

        if (j.contains("scores") && j["scores"].is_array())
        {
            for (const auto& rs : j["scores"])
            {
                sakura::game::GameResult gr;
                gr.chartId         = rs.value("chart_id", "");
                gr.chartTitle      = rs.value("chart_title", gr.chartId);
                gr.chartHash       = rs.value("chart_hash", "");
                gr.difficulty      = rs.value("difficulty", "Normal");
                gr.difficultyLevel = rs.value("difficulty_level", 1.0f);
                gr.score           = rs.value("score", 0);
                gr.accuracy        = rs.value("accuracy", 0.0f);
                gr.maxCombo        = rs.value("max_combo", 0);
                gr.grade           = ParseGradeStr(rs.value("grade", "D"));
                gr.perfectCount    = rs.value("perfect_count", 0);
                gr.greatCount      = rs.value("great_count", 0);
                gr.goodCount       = rs.value("good_count", 0);
                gr.badCount        = rs.value("bad_count", 0);
                gr.missCount       = rs.value("miss_count", 0);
                gr.isFullCombo     = rs.value("is_full_combo", false);
                gr.isAllPerfect    = rs.value("is_all_perfect", false);
                gr.playedAt        = rs.value("played_at", 0LL);
                if (db.MergeCloudScore(gr))
                    ++out.scoresPulled;
            }
        }

        if (j.contains("achievements") && j["achievements"].is_array())
        {
            for (const auto& ra : j["achievements"])
            {
                const std::string aid = ra.value("id", "");
                const long long ats   = ra.value("unlocked_at", 0LL);
                if (db.MergeCloudAchievement(aid, ats))
                    ++out.achievementsPulled;
            }
            sakura::game::AchievementManager::GetInstance().ReloadUnlocked();
        }

        if (j.contains("course_records") && j["course_records"].is_array())
        {
            for (const auto& rc : j["course_records"])
            {
                if (!rc.is_object()) continue;
                sakura::game::CourseRecord cr;
                cr.courseId      = JsonStringOr(rc, "course_id", "");
                cr.badgeText     = JsonStringOr(rc, "badge_text", "");
                cr.danRank       = rc.value("dan_rank", 0);
                cr.totalScore    = rc.value("total_score", 0);
                cr.avgAccuracy   = rc.value("avg_accuracy", 0.0f);
                cr.remainingLife = rc.value("remaining_life", 0.0f);
                cr.cleared       = (rc.contains("cleared") && rc["cleared"].is_number_integer())
                    ? (rc["cleared"].get<int>() != 0)
                    : rc.value("cleared", false);
                cr.clearedAt     = rc.value("cleared_at", 0LL);
                if (!cr.courseId.empty() && db.MergeCloudCourseRecord(cr))
                    ++out.coursesPulled;
            }
        }

        if (const auto highest = db.GetHighestClearedCourse(); highest.has_value() && highest->cleared)
        {
            std::string badge = highest->badgeText;
            if (badge.empty())
            {
                if (const auto* def = sakura::game::FindOfficialCourse(highest->courseId))
                    badge = def->badgeText;
            }
            if (!badge.empty())
            {
                double curTotalPP = 0.0;
                double curRating  = 0.0;
                {
                    std::lock_guard lock(m_stateMutex);
                    m_user.danBadge = badge;
                    curTotalPP      = m_user.totalPP;
                    curRating       = m_user.sakuraRating;
                }
                GetAuthStore().UpdateProfileMetadata(curTotalPP, curRating, badge);
                GetAuthStore().Save();
            }
        }

        const long long remoteSetTs = j.value("settings_updated_at", 0LL);
        if (remoteSetTs > localSettingsTs && j.contains("settings") && j["settings"].is_object())
        {
            const auto& rs = j["settings"];
            if (rs.contains("note_speed") && rs["note_speed"].is_number())
                cfg.Set("gameplay.note_speed", std::clamp(rs["note_speed"].get<float>(), 0.5f, 15.0f));
            if (rs.contains("mouse_approach_ms") && rs["mouse_approach_ms"].is_number_integer())
                cfg.Set("gameplay.mouse_approach_ms", std::clamp(rs["mouse_approach_ms"].get<int>(), 400, 2000));
            if (rs.contains("global_offset_ms") && rs["global_offset_ms"].is_number_integer())
                cfg.Set("audio.global_offset_ms", std::clamp(rs["global_offset_ms"].get<int>(), -500, 500));
            if (rs.contains("pitch_lock") && rs["pitch_lock"].is_boolean())
                cfg.Set("audio.pitch_lock", rs["pitch_lock"].get<bool>());
            for (const char* k : { "key_lane_0", "key_lane_1", "key_lane_2", "key_lane_3", "key_thumb" })
            {
                if (rs.contains(k) && rs[k].is_number_integer())
                {
                    const int sc = rs[k].get<int>();
                    if (sc > 0 && sc < 512)
                        cfg.Set(std::string("input.") + k, sc);
                }
            }
            cfg.Set("online.settings_updated_at", remoteSetTs);
        }
        else if (localSettingsTs == 0)
        {
            // Seed cloud settings if neither local nor cloud had a timestamp yet
            const nlohmann::json seedBody = {
                { "scores", nlohmann::json::array() },
                { "achievements", nlohmann::json::array() },
                { "course_records", nlohmann::json::array() },
                { "settings", settingsObj },
                { "settings_updated_at", nowSec }
            };
            SendWithAuthRetry(MakeRequest("POST", "/api/v1/sync/push", seedBody.dump(), true));
            cfg.Set("online.settings_updated_at", nowSec);
        }
        else if (origLocalSettingsTs == 0)
        {
            cfg.Set("online.settings_updated_at", localSettingsTs);
        }

        if (j.contains("settings") && j["settings"].is_object() &&
            j["settings"].contains("collections") && j["settings"]["collections"].is_array())
        {
            std::vector<sakura::game::SongCollection> remoteCols;
            for (const auto& item : j["settings"]["collections"])
            {
                if (!item.is_object()) continue;
                sakura::game::SongCollection col;
                col.id        = JsonStringOr(item, "id", "");
                col.name      = JsonStringOr(item, "name", "");
                col.updatedAt = item.value("updated_at", 0LL);
                if (item.contains("chart_ids") && item["chart_ids"].is_array())
                {
                    for (const auto& cid : item["chart_ids"])
                    {
                        if (cid.is_string())
                            col.chartIds.push_back(cid.get<std::string>());
                    }
                }
                if (!col.id.empty() || !col.name.empty())
                    remoteCols.push_back(std::move(col));
            }
            const auto merged = sakura::game::CollectionManager::MergeCollections(
                sakura::game::CollectionManager::LoadFromConfig(), remoteCols);
            sakura::game::CollectionManager::SaveToConfig(merged);
        }

        cfg.Set("online.last_cloud_sync_ts", out.syncedAt);
        cfg.Save();
        m_activeTransferProgress.store(1.0f, std::memory_order_release);
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("云同步解析失败: ") + e.what();
        return out;
    }
}

void APIClient::PublishChartPackageAsync(const std::string& sakuraFilePath, const std::string& description,
                                         std::function<void(MarketPublishResult)> cb)
{
    PostTransferTask([this, sakuraFilePath, description, cb = std::move(cb)]() mutable
    {
        auto res = PublishChartPackage(sakuraFilePath, description);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchMarketChartsAsync(const MarketQuery& query, std::function<void(MarketListResult)> cb)
{
    auto [seq, token] = BeginChannelRequest(m_marketListChannel);
    PostTask([this, query, seq, token = std::move(token), cb = std::move(cb)]() mutable
    {
        if (!IsChannelRequestCurrent(m_marketListChannel, seq, token))
            return;
        auto res = FetchMarketChartsInternal(query, token);
        if (!IsChannelRequestCurrent(m_marketListChannel, seq, token))
            return;
        if (cb)
        {
            PostMainCallback([this, seq, token, cb = std::move(cb), res = std::move(res)]() mutable
            {
                if (!IsChannelRequestCurrent(m_marketListChannel, seq, token))
                    return;
                cb(std::move(res));
            });
        }
    });
}

void APIClient::DownloadAndInstallChartAsync(const MarketChartItem& item, const std::string& chartsRootDir,
                                             std::function<void(float)> onProgress,
                                             std::function<void(MarketDownloadResult)> cb)
{
    PostTransferTask([this, item, chartsRootDir, onProgress = std::move(onProgress), cb = std::move(cb)]() mutable
    {
        auto res = DownloadAndInstallChart(item, chartsRootDir, [this, onProgress](float p)
        {
            if (onProgress)
                PostMainCallback([onProgress, p]() { onProgress(p); });
        });
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::DownloadMarketChartByIdAsync(const std::string& marketChartId, const std::string& chartsRootDir,
                                             std::function<void(float)> onProgress,
                                             std::function<void(MarketDownloadResult)> cb)
{
    PostTransferTask([this, marketChartId, chartsRootDir, onProgress = std::move(onProgress), cb = std::move(cb)]() mutable
    {
        auto res = DownloadMarketChartById(marketChartId, chartsRootDir, [this, onProgress](float p)
        {
            if (onProgress)
                PostMainCallback([onProgress, p]() { onProgress(p); });
        });
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RateMarketChartAsync(const std::string& marketChartId, int rating, const std::string& comment,
                                     std::function<void(MarketRateResult)> cb)
{
    PostTask([this, marketChartId, rating, comment, cb = std::move(cb)]() mutable
    {
        auto res = RateMarketChart(marketChartId, rating, comment);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SyncCloudSaveAsync(std::function<void(CloudSyncResult)> cb)
{
    PostTransferTask([this, cb = std::move(cb)]() mutable
    {
        auto res = SyncCloudSave();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

DailyInfoResult APIClient::FetchDailyChallenge(const std::string& dateUtc8, int limit)
{
    DailyInfoResult out;
    const int safeLimit          = std::max(1, limit);
    const std::string targetDate = dateUtc8.empty() ? sakura::game::GetCurrentDateUtc8() : dateUtc8;
    const std::string cacheKey   = "daily:" + targetDate;
    auto& cache                  = MutableOnlineCache();
    const CacheLookupResult cached = cache.Lookup(cacheKey, kOnlineCacheSoftTtlSec, kOnlineCacheHardTtlSec);

    auto parseDailyJson = [&targetDate, safeLimit](const std::string& bodyText) -> DailyInfoResult
    {
        DailyInfoResult parsed;
        try
        {
            const auto j = nlohmann::json::parse(bodyText);
            if (!j.value("ok", false))
            {
                parsed.error = JsonStringOr(j, "error", "获取每日挑战失败");
                return parsed;
            }

            const nlohmann::json& d = (j.contains("daily") && j["daily"].is_object())
                ? j["daily"]
                : ((j.contains("challenge") && j["challenge"].is_object())
                    ? j["challenge"]
                    : ((j.contains("spec") && j["spec"].is_object()) ? j["spec"] : j));

            parsed.ok                   = true;
            parsed.offlineFallback      = false;
            parsed.spec.dateUtc8        = JsonStringOr(d, "date", targetDate);
            parsed.spec.chartId         = JsonStringOr(d, "chart_id", "");
            parsed.spec.chartTitle      = JsonStringOr(d, "chart_title", "");
            parsed.spec.difficultyName  = (d.contains("difficulty") && d["difficulty"].is_string())
                ? d["difficulty"].get<std::string>()
                : JsonStringOr(d, "difficulty_name", "Hard");
            parsed.spec.difficultyLevel = d.value("difficulty_level", 7.0f);
            parsed.spec.goalText        = JsonStringOr(d, "goal_text", "");
            parsed.spec.targetScore     = d.value("target_score", 950000);

            const nlohmann::json& lbJson = (j.contains("leaderboard") && j["leaderboard"].is_object())
                ? j["leaderboard"]
                : ((j.contains("entries") || j.contains("my_rank")) ? j : d);

            parsed.leaderboard.ok = true;
            if (lbJson.contains("my_rank") && lbJson["my_rank"].is_number_integer())
                parsed.leaderboard.myRank = lbJson["my_rank"].get<int>();
            else if (j.contains("my_rank") && j["my_rank"].is_number_integer())
                parsed.leaderboard.myRank = j["my_rank"].get<int>();

            if (lbJson.contains("entries") && lbJson["entries"].is_array())
            {
                for (const auto& item : lbJson["entries"])
                {
                    if (static_cast<int>(parsed.leaderboard.entries.size()) >= safeLimit)
                        break;
                    parsed.leaderboard.entries.push_back(ParseLeaderboardRowJson(item));
                }
            }
        }
        catch (const std::exception& e)
        {
            parsed.ok    = false;
            parsed.error = std::string("每日挑战响应解析失败: ") + e.what();
        }
        return parsed;
    };

    if (cached.found && !cached.isStale && !cached.payloadJson.empty())
    {
        DailyInfoResult parsed = parseDailyJson(cached.payloadJson);
        if (parsed.ok)
            return parsed;
    }

    const std::string path = "/api/v1/daily?date=" + UrlEncode(targetDate)
        + "&limit=" + std::to_string(safeLimit);

    HttpRequest req = MakeRequest("GET", path, {}, IsAuthenticated());
    if (cached.found && cached.isStale && !cached.etag.empty())
    {
        req.headers.emplace_back("If-None-Match", cached.etag);
    }

    const HttpResponse res = IsAuthenticated()
        ? SendWithAuthRetry(std::move(req))
        : GetTransport()->Send(req);

    if (res.IsNotModified() && cached.found && !cached.payloadJson.empty())
    {
        if (!IsAuthenticated())
            m_status.store(OnlineStatus::Online);
        cache.TouchFresh(cacheKey);
        DailyInfoResult parsed = parseDailyJson(cached.payloadJson);
        if (parsed.ok)
            return parsed;
    }

    if (res.IsNetworkError() || res.statusCode >= 500 || res.statusCode == 404)
    {
        if (res.IsNetworkError() || res.statusCode >= 500)
            MarkOfflinePreserveSession();
        if (cached.found && !cached.payloadJson.empty())
        {
            DailyInfoResult fallback = parseDailyJson(cached.payloadJson);
            if (fallback.ok)
            {
                fallback.offlineFallback = true;
                return fallback;
            }
        }
        out.ok              = true;
        out.offlineFallback = true;
        out.spec            = sakura::game::ComputeOfflineDailyChallenge(targetDate);
        return out;
    }

    m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess())
    {
        try
        {
            const auto j = nlohmann::json::parse(res.body);
            out.error    = JsonStringOr(j, "error", "获取每日挑战失败");
        }
        catch (...)
        {
            out.error = "获取每日挑战失败";
        }
        return out;
    }

    DailyInfoResult parsed = parseDailyJson(res.body);
    if (parsed.ok)
    {
        cache.Put(cacheKey, res.body, ExtractETagHeader(res));
    }
    return parsed;
}

DailyScoreSubmitResult APIClient::SubmitDailyScore(const std::string& dateUtc8,
                                                   const sakura::game::GameResult& result)
{
    DailyScoreSubmitResult out;
    if (result.assisted)
    {
        out.error = "辅助演奏不计入每日挑战排行榜";
        return out;
    }

    const std::string targetDate = dateUtc8.empty() ? sakura::game::GetCurrentDateUtc8() : dateUtc8;
    const int totalJudged = result.totalJudgments > 0
        ? result.totalJudgments
        : (result.perfectCount + result.greatCount + result.goodCount + result.badCount + result.missCount);
    const std::string subHash  = ComputeGameResultSubmissionHash(result);
    const std::string dedupeId = "daily:" + targetDate + ":" + subHash;

    nlohmann::json payload = {
        { "client_submission_id", dedupeId },
        { "date", targetDate },
        { "chart_id", result.chartId },
        { "chart_title", result.chartTitle },
        { "chart_hash", result.chartHash.empty() ? result.chartId : result.chartHash },
        { "difficulty", result.difficulty },
        { "difficulty_level", result.difficultyLevel },
        { "score", result.score },
        { "accuracy", result.accuracy },
        { "max_combo", result.maxCombo },
        { "grade", GradeToStr(result.grade) },
        { "perfect_count", result.perfectCount },
        { "great_count", result.greatCount },
        { "good_count", result.goodCount },
        { "bad_count", result.badCount },
        { "miss_count", result.missCount },
        { "total_judgments", totalJudged },
        { "synergy_count", result.synergyCount },
        { "is_full_combo", result.isFullCombo },
        { "is_all_perfect", result.isAllPerfect },
        { "assisted", result.assisted },
        { "playback_rate", result.playbackRate }
    };

    if (!result.replayFile.empty())
        payload["replay_file"] = result.replayFile;
    const std::string replayRaw = ReadWholeFileUtf8(result.replayFile);
    if (!replayRaw.empty())
        payload["replay_data"] = replayRaw;

    const std::string bodyStr = payload.dump();
    if (!IsAuthenticated())
    {
        MutableOfflineQueue().Enqueue(PendingActionType::DailyScore, bodyStr, targetDate, dedupeId);
        out.queuedOffline = true;
        out.error         = "未登录，每日挑战成绩已缓存至待同步队列";
        return out;
    }

    const auto res = SendWithAuthRetry(MakeRequest("POST", "/api/v1/daily/scores", bodyStr, true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        MutableOfflineQueue().Enqueue(PendingActionType::DailyScore, bodyStr, targetDate, dedupeId);
        out.queuedOffline = true;
        out.error         = "网络离线，每日挑战成绩已加入待同步队列";
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = JsonStringOr(j, "error", "每日挑战成绩提交失败");
            return out;
        }
        out.ok      = true;
        out.rank    = (j.contains("rank") && j["rank"].is_number_integer()) ? j["rank"].get<int>() : 0;
        out.newBest = (j.contains("new_best") && j["new_best"].is_boolean())
            ? j["new_best"].get<bool>()
            : j.value("new_best", false);
        MutableOnlineCache().MarkStale("daily:");

        const bool hasTotalPP      = j.contains("total_pp") && j["total_pp"].is_number();
        const bool hasSakuraRating = j.contains("sakura_rating") && j["sakura_rating"].is_number();
        if (hasTotalPP || hasSakuraRating)
        {
            {
                std::lock_guard lock(m_stateMutex);
                if (hasTotalPP)
                    m_user.totalPP = j["total_pp"].get<double>();
                if (hasSakuraRating)
                    m_user.sakuraRating = j["sakura_rating"].get<double>();
            }
            SaveToConfig();
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("每日挑战成绩响应解析失败: ") + e.what();
        return out;
    }
}

void APIClient::FetchDailyChallengeAsync(const std::string& dateUtc8, int limit,
                                         std::function<void(DailyInfoResult)> cb)
{
    PostTask([this, dateUtc8, limit, cb = std::move(cb)]() mutable
    {
        auto res = FetchDailyChallenge(dateUtc8, limit);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SubmitDailyScoreAsync(const std::string& dateUtc8, const sakura::game::GameResult& result,
                                      std::function<void(DailyScoreSubmitResult)> cb)
{
    PostTask([this, dateUtc8, result, cb = std::move(cb)]() mutable
    {
        auto res = SubmitDailyScore(dateUtc8, result);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

namespace
{
std::string TrimRoomCode(std::string code)
{
    while (!code.empty() && (code.front() == ' ' || code.front() == '\t' || code.front() == '\r' || code.front() == '\n'))
        code.erase(code.begin());
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t' || code.back() == '\r' || code.back() == '\n'))
        code.pop_back();
    return code;
}

bool JsonBoolOr(const nlohmann::json& obj, const char* key, bool fallback = false)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number_integer())
        return v.get<long long>() != 0;
    return fallback;
}

int JsonIntOr(const nlohmann::json& obj, const char* key, int fallback = 0)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_number_integer())
        return static_cast<int>(v.get<long long>());
    if (v.is_number())
        return static_cast<int>(v.get<double>());
    return fallback;
}

long long JsonInt64Or(const nlohmann::json& obj, const char* key, long long fallback = 0LL)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_number_integer())
        return v.get<long long>();
    if (v.is_number())
        return static_cast<long long>(v.get<double>());
    return fallback;
}

float JsonFloatOr(const nlohmann::json& obj, const char* key, float fallback = 0.0f)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_number())
        return static_cast<float>(v.get<double>());
    return fallback;
}

std::string SafeJsonStringOr(const nlohmann::json& obj, const char* key, const std::string& fallback = {})
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    if (obj[key].is_string())
        return obj[key].get<std::string>();
    return fallback;
}

ArenaRoomState ParseArenaRoomJson(const nlohmann::json& r)
{
    ArenaRoomState room;
    if (!r.is_object())
        return room;

    room.code            = SafeJsonStringOr(r, "code", "");
    room.name            = SafeJsonStringOr(r, "name", "");
    room.hostId          = SafeJsonStringOr(r, "host_id", "");
    room.hostName        = SafeJsonStringOr(r, "host_name", "");
    room.status          = SafeJsonStringOr(r, "status", "waiting");
    room.chartId         = SafeJsonStringOr(r, "chart_id", "");
    room.chartTitle      = SafeJsonStringOr(r, "chart_title", "");
    room.difficulty      = SafeJsonStringOr(r, "difficulty", "");
    room.difficultyLevel = JsonFloatOr(r, "difficulty_level", 1.0f);
    room.chartHash       = SafeJsonStringOr(r, "chart_hash", "");
    room.startedAt       = JsonInt64Or(r, "started_at", 0LL);
    room.updatedAt       = JsonInt64Or(r, "updated_at", 0LL);
    room.revision        = JsonIntOr(r, "revision", 0);
    room.marketChartId   = r.contains("market_chart_id")
        ? SafeJsonStringOr(r, "market_chart_id", "")
        : SafeJsonStringOr(r, "marketChartId", "");
    const long long rawPkgSize = r.contains("package_size")
        ? JsonInt64Or(r, "package_size", 0LL)
        : JsonInt64Or(r, "packageSize", 0LL);
    room.packageSize = static_cast<std::size_t>(std::max(0LL, rawPkgSize));

    if (r.contains("players") && r["players"].is_array())
    {
        for (const auto& p : r["players"])
        {
            if (!p.is_object())
                continue;
            ArenaPlayerState ps;
            ps.userId     = SafeJsonStringOr(p, "user_id", "");
            ps.username   = SafeJsonStringOr(p, "username", "");
            ps.danBadge   = SafeJsonStringOr(p, "dan_badge", "");
            ps.isHost     = JsonBoolOr(p, "is_host", false);
            ps.ready      = JsonBoolOr(p, "ready", false);
            ps.finished   = JsonBoolOr(p, "finished", false);
            ps.score      = JsonIntOr(p, "score", 0);
            ps.accuracy   = JsonFloatOr(p, "accuracy", 0.0f);
            ps.combo      = JsonIntOr(p, "combo", 0);
            ps.maxCombo   = JsonIntOr(p, "max_combo", 0);
            ps.grade      = SafeJsonStringOr(p, "grade", "D");
            ps.placement  = JsonIntOr(p, "placement", 0);
            ps.connected  = JsonBoolOr(p, "connected", true);
            ps.lastSeenAt = p.contains("last_seen_at")
                ? JsonInt64Or(p, "last_seen_at", 0LL)
                : JsonInt64Or(p, "lastSeenAt", 0LL);
            room.players.push_back(std::move(ps));
        }
    }

    if (r.contains("messages") && r["messages"].is_array())
    {
        for (const auto& m : r["messages"])
        {
            if (!m.is_object())
                continue;
            ArenaChatMessage msg;
            msg.id       = JsonIntOr(m, "id", 0);
            msg.userId   = m.contains("user_id")
                ? SafeJsonStringOr(m, "user_id", "")
                : SafeJsonStringOr(m, "userId", "");
            msg.username = SafeJsonStringOr(m, "username", "");
            msg.phraseId = m.contains("phrase_id")
                ? JsonIntOr(m, "phrase_id", 0)
                : JsonIntOr(m, "phraseId", 0);
            msg.text   = SafeJsonStringOr(m, "text", "");
            msg.sentAt = m.contains("sent_at")
                ? JsonInt64Or(m, "sent_at", 0LL)
                : JsonInt64Or(m, "sentAt", 0LL);
            room.messages.push_back(std::move(msg));
        }
    }
    return room;
}

ArenaRoomResult ParseArenaRoomResponse(const HttpResponse& res,
                                       const std::function<void()>& onTransientOffline,
                                       const std::function<void()>& onOnlineReachable,
                                       const char* fallbackError,
                                       int sinceRevision = 0,
                                       const std::string& fallbackCode = {})
{
    ArenaRoomResult out;
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        onTransientOffline();
        out.error = res.error.empty() ? "离线：无法连接至对战服务器" : res.error;
        return out;
    }

    onOnlineReachable();
    if (res.IsNotModified())
    {
        out.ok            = true;
        out.unchanged     = true;
        out.room.code     = fallbackCode;
        out.room.revision = sinceRevision;
        return out;
    }

    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", fallbackError);
            return out;
        }
        if (JsonBoolOr(j, "unchanged", false))
        {
            out.ok        = true;
            out.unchanged = true;
            if (j.contains("room") && j["room"].is_object())
                out.room = ParseArenaRoomJson(j["room"]);
            if (out.room.code.empty())
                out.room.code = SafeJsonStringOr(j, "code", fallbackCode);
            if (out.room.revision == 0)
                out.room.revision = JsonIntOr(j, "revision", sinceRevision);
            return out;
        }
        const nlohmann::json& roomJson = (j.contains("room") && j["room"].is_object()) ? j["room"] : j;
        out.ok   = true;
        out.room = ParseArenaRoomJson(roomJson);
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("对战房间响应解析失败: ") + e.what();
        return out;
    }
}
} // namespace

ArenaRoomResult APIClient::CreateArenaRoom(const std::string& roomName,
                                           const std::string& chartId,
                                           const std::string& chartTitle,
                                           const std::string& difficulty,
                                           float difficultyLevel,
                                           const std::string& chartHash,
                                           const std::string& marketChartId)
{
    nlohmann::json body = {
        { "name", roomName },
        { "chart_id", chartId },
        { "chart_title", chartTitle },
        { "difficulty", difficulty },
        { "difficulty_level", difficultyLevel },
        { "chart_hash", chartHash }
    };
    if (!marketChartId.empty())
        body["market_chart_id"] = marketChartId;
    const auto res = SendWithAuthRetry(MakeRequest("POST", "/api/v1/rooms", body.dump(), true));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if (res.IsSuccess()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "创建对战房间失败");
}

ArenaRoomListResult APIClient::ListArenaRooms()
{
    ArenaRoomListResult out;
    const auto res = IsAuthenticated()
        ? SendWithAuthRetry(MakeRequest("GET", "/api/v1/rooms", {}, true))
        : GetTransport()->Send(MakeRequest("GET", "/api/v1/rooms", {}, false));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法获取对战房间列表" : res.error;
        return out;
    }

    if (res.IsSuccess() && IsAuthenticated())
        MarkOnlineFromAuthenticatedSuccess();
    else
        m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", "获取对战房间列表失败");
            return out;
        }
        out.ok = true;
        if (j.contains("rooms") && j["rooms"].is_array())
        {
            for (const auto& item : j["rooms"])
            {
                if (item.is_object())
                    out.rooms.push_back(ParseArenaRoomJson(item));
            }
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("对战房间列表解析失败: ") + e.what();
        return out;
    }
}

ArenaRoomResult APIClient::JoinArenaRoom(const std::string& code)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/join";
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, "{}", true));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if (res.IsSuccess()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "加入对战房间失败");
}

bool APIClient::LeaveArenaRoom(const std::string& code)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
        return false;

    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/leave";
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, "{}", true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        return false;
    }

    if (res.IsSuccess())
        MarkOnlineFromAuthenticatedSuccess();
    else
        m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess())
        return false;
    if (res.body.empty())
        return true;

    try
    {
        const auto j = nlohmann::json::parse(res.body);
        return JsonBoolOr(j, "ok", true);
    }
    catch (...)
    {
        return false;
    }
}

ArenaRoomResult APIClient::SelectArenaChart(const std::string& code,
                                            const std::string& chartId,
                                            const std::string& chartTitle,
                                            const std::string& difficulty,
                                            float difficultyLevel,
                                            const std::string& chartHash,
                                            const std::string& marketChartId)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    nlohmann::json body = {
        { "chart_id", chartId },
        { "chart_title", chartTitle },
        { "difficulty", difficulty },
        { "difficulty_level", difficultyLevel },
        { "chart_hash", chartHash }
    };
    if (!marketChartId.empty())
        body["market_chart_id"] = marketChartId;
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/select";
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if (res.IsSuccess()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "切换房间曲目失败");
}

ArenaRoomResult APIClient::SetArenaReady(const std::string& code, bool ready, bool startMatch)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const nlohmann::json body = {
        { "ready", ready },
        { "start", startMatch }
    };
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/ready";
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if (res.IsSuccess()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "更新准备状态失败");
}

ArenaRoomResult APIClient::ReportArenaProgress(const std::string& code,
                                               int score,
                                               float accuracy,
                                               int combo,
                                               int maxCombo,
                                               const std::string& grade,
                                               bool finished)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const nlohmann::json body = {
        { "score", score },
        { "accuracy", accuracy },
        { "combo", combo },
        { "max_combo", maxCombo },
        { "grade", grade.empty() ? std::string("D") : grade },
        { "finished", finished }
    };
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/progress";
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if (res.IsSuccess()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "同步对战进度失败");
}

ArenaRoomResult APIClient::FetchArenaRoomState(const std::string& code, int sinceRevision)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/state";
    if (sinceRevision > 0)
        path += "?since_revision=" + std::to_string(sinceRevision);

    const auto res = IsAuthenticated()
        ? SendWithAuthRetry(MakeRequest("GET", path, {}, true))
        : GetTransport()->Send(MakeRequest("GET", path, {}, false));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if ((res.IsSuccess() || res.IsNotModified()) && IsAuthenticated()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "获取对战房间状态失败",
        sinceRevision,
        trimmed);
}

ArenaRoomResult APIClient::SendArenaChat(const std::string& code, int phraseId)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const nlohmann::json body = {
        { "phrase_id", phraseId }
    };
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/chat";
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(
        res,
        [this]() { MarkOfflinePreserveSession(); },
        [this, &res]() { if (res.IsSuccess()) MarkOnlineFromAuthenticatedSuccess(); else m_status.store(OnlineStatus::Online); },
        "发送快捷短语失败");
}

void APIClient::CreateArenaRoomAsync(const std::string& roomName, const std::string& chartId,
                                     const std::string& chartTitle, const std::string& difficulty,
                                     float difficultyLevel, const std::string& chartHash,
                                     std::function<void(ArenaRoomResult)> cb)
{
    CreateArenaRoomAsync(roomName, chartId, chartTitle, difficulty, difficultyLevel, chartHash, std::string{}, std::move(cb));
}

void APIClient::CreateArenaRoomAsync(const std::string& roomName, const std::string& chartId,
                                     const std::string& chartTitle, const std::string& difficulty,
                                     float difficultyLevel, const std::string& chartHash,
                                     const std::string& marketChartId,
                                     std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, roomName, chartId, chartTitle, difficulty, difficultyLevel, chartHash, marketChartId, cb = std::move(cb)]() mutable
    {
        auto res = CreateArenaRoom(roomName, chartId, chartTitle, difficulty, difficultyLevel, chartHash, marketChartId);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::ListArenaRoomsAsync(std::function<void(ArenaRoomListResult)> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        auto res = ListArenaRooms();
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::JoinArenaRoomAsync(const std::string& code, std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, cb = std::move(cb)]() mutable
    {
        auto res = JoinArenaRoom(code);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::LeaveArenaRoomAsync(const std::string& code, std::function<void(bool)> cb)
{
    PostTask([this, code, cb = std::move(cb)]() mutable
    {
        const bool ok = LeaveArenaRoom(code);
        if (cb)
            PostMainCallback([cb = std::move(cb), ok]() mutable { cb(ok); });
    });
}

void APIClient::SelectArenaChartAsync(const std::string& code, const std::string& chartId,
                                      const std::string& chartTitle, const std::string& difficulty,
                                      float difficultyLevel, const std::string& chartHash,
                                      std::function<void(ArenaRoomResult)> cb)
{
    SelectArenaChartAsync(code, chartId, chartTitle, difficulty, difficultyLevel, chartHash, std::string{}, std::move(cb));
}

void APIClient::SelectArenaChartAsync(const std::string& code, const std::string& chartId,
                                      const std::string& chartTitle, const std::string& difficulty,
                                      float difficultyLevel, const std::string& chartHash,
                                      const std::string& marketChartId,
                                      std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, chartId, chartTitle, difficulty, difficultyLevel, chartHash, marketChartId, cb = std::move(cb)]() mutable
    {
        auto res = SelectArenaChart(code, chartId, chartTitle, difficulty, difficultyLevel, chartHash, marketChartId);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SetArenaReadyAsync(const std::string& code, bool ready, bool startMatch,
                                   std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, ready, startMatch, cb = std::move(cb)]() mutable
    {
        auto res = SetArenaReady(code, ready, startMatch);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::ReportArenaProgressAsync(const std::string& code, int score, float accuracy,
                                         int combo, int maxCombo, const std::string& grade,
                                         bool finished, std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, score, accuracy, combo, maxCombo, grade, finished, cb = std::move(cb)]() mutable
    {
        auto res = ReportArenaProgress(code, score, accuracy, combo, maxCombo, grade, finished);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchArenaRoomStateAsync(const std::string& code,
                                         std::function<void(ArenaRoomResult)> cb,
                                         int sinceRevision)
{
    PostTask([this, code, sinceRevision, cb = std::move(cb)]() mutable
    {
        auto res = FetchArenaRoomState(code, sinceRevision);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SendArenaChatAsync(const std::string& code, int phraseId,
                                   std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, phraseId, cb = std::move(cb)]() mutable
    {
        auto res = SendArenaChat(code, phraseId);
        if (cb)
            PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

namespace
{
UserProfileData ParseUserProfileJson(const nlohmann::json& p)
{
    UserProfileData out;
    if (!p.is_object())
        return out;
    out.userId        = SafeJsonStringOr(p, "user_id", "");
    out.username      = SafeJsonStringOr(p, "username", "");
    out.isGuest       = JsonBoolOr(p, "is_guest", false);
    out.bio           = SafeJsonStringOr(p, "bio", "");
    out.featuredTitle = SafeJsonStringOr(p, "featured_title", "");
    out.danBadge      = SafeJsonStringOr(p, "dan_badge", "");
    out.sakuraRating  = JsonSakuraRatingOr(p);
    out.totalPP       = p.contains("total_pp") && p["total_pp"].is_number() ? p["total_pp"].get<double>() : 0.0;
    out.playCount     = JsonIntOr(p, "play_count", 0);
    out.followerCount = JsonIntOr(p, "follower_count", 0);
    out.rivalCount    = JsonIntOr(p, "rival_count", 0);
    out.isRival       = JsonBoolOr(p, "is_rival", false);
    out.isMutual      = JsonBoolOr(p, "is_mutual", false);
    out.createdAt     = JsonInt64Or(p, "created_at", 0LL);

    if (p.contains("best_scores") && p["best_scores"].is_array())
    {
        for (const auto& s : p["best_scores"])
        {
            if (!s.is_object()) continue;
            UserProfileScoreItem item;
            item.scoreId         = SafeJsonStringOr(s, "score_id", "");
            item.chartId         = SafeJsonStringOr(s, "chart_id", "");
            item.chartTitle      = SafeJsonStringOr(s, "chart_title", "");
            item.difficulty      = SafeJsonStringOr(s, "difficulty", "Normal");
            item.difficultyLevel = JsonFloatOr(s, "difficulty_level", 1.0f);
            item.score           = JsonIntOr(s, "score", 0);
            item.accuracy        = JsonFloatOr(s, "accuracy", 0.0f);
            item.maxCombo        = JsonIntOr(s, "max_combo", 0);
            item.grade           = SafeJsonStringOr(s, "grade", "D");
            item.pp              = s.contains("pp") && s["pp"].is_number() ? s["pp"].get<double>() : 0.0;
            item.chartRating     = s.contains("chart_rating") && s["chart_rating"].is_number() ? s["chart_rating"].get<double>() : 0.0;
            item.isFullCombo     = JsonBoolOr(s, "is_full_combo", false);
            item.isAllPerfect    = JsonBoolOr(s, "is_all_perfect", false);
            item.hasReplay       = JsonBoolOr(s, "has_replay", false);
            item.playedAt        = JsonInt64Or(s, "played_at", 0LL);
            out.bestScores.push_back(std::move(item));
        }
    }
    return out;
}
} // namespace

UserProfileResult APIClient::FetchUserProfile(const std::string& userId)
{
    UserProfileResult out;
    if (userId.empty())
    {
        out.error = "用户 ID 不能为空";
        return out;
    }
    const std::string path = "/api/v1/users/" + UrlEncode(userId) + "/profile";
    const auto res = IsAuthenticated()
        ? SendWithAuthRetry(MakeRequest("GET", path, {}, true))
        : GetTransport()->Send(MakeRequest("GET", path, {}, false));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法获取玩家名片" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false) || !j.contains("profile"))
        {
            out.error = SafeJsonStringOr(j, "error", "获取玩家名片失败");
            return out;
        }
        out.ok      = true;
        out.profile = ParseUserProfileJson(j["profile"]);
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("玩家名片解析失败: ") + e.what();
        return out;
    }
}

UserProfileResult APIClient::UpdateMyProfile(const std::string& bio, const std::string& featuredTitle)
{
    UserProfileResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号";
        return out;
    }
    const nlohmann::json body = {
        { "bio", bio },
        { "featured_title", featuredTitle }
    };
    const auto res = SendWithAuthRetry(MakeRequest("PATCH", "/api/v1/users/me/profile", body.dump(), true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法更新个人资料" : res.error;
        return out;
    }
    MarkOnlineFromAuthenticatedSuccess();
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false) || !j.contains("profile"))
        {
            out.error = SafeJsonStringOr(j, "error", "更新个人资料失败");
            return out;
        }
        out.ok      = true;
        out.profile = ParseUserProfileJson(j["profile"]);
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("响应解析失败: ") + e.what();
        return out;
    }
}

RivalActionResult APIClient::AddRival(const std::string& targetUserId)
{
    RivalActionResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号";
        return out;
    }
    const std::string path = "/api/v1/rivals/" + UrlEncode(targetUserId);
    const auto res = SendWithAuthRetry(MakeRequest("POST", path, "{}", true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法添加劲敌" : res.error;
        return out;
    }
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", "添加劲敌失败");
            return out;
        }
        out.ok            = true;
        out.isRival       = JsonBoolOr(j, "is_rival", true);
        out.isMutual      = JsonBoolOr(j, "is_mutual", false);
        out.rivalCount    = JsonIntOr(j, "rival_count", 0);
        out.followerCount = JsonIntOr(j, "follower_count", 0);
        MutableOnlineCache().MarkStale("leaderboard_rivals:");
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("响应解析失败: ") + e.what();
        return out;
    }
}

RivalActionResult APIClient::RemoveRival(const std::string& targetUserId)
{
    RivalActionResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号";
        return out;
    }
    const std::string path = "/api/v1/rivals/" + UrlEncode(targetUserId);
    const auto res = SendWithAuthRetry(MakeRequest("DELETE", path, {}, true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法移除劲敌" : res.error;
        return out;
    }
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", "移除劲敌失败");
            return out;
        }
        out.ok            = true;
        out.isRival       = JsonBoolOr(j, "is_rival", false);
        out.rivalCount    = JsonIntOr(j, "rival_count", 0);
        out.followerCount = JsonIntOr(j, "follower_count", 0);
        MutableOnlineCache().MarkStale("leaderboard_rivals:");
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("响应解析失败: ") + e.what();
        return out;
    }
}

RivalListResult APIClient::FetchRivals()
{
    RivalListResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号";
        return out;
    }
    const auto res = SendWithAuthRetry(MakeRequest("GET", "/api/v1/rivals", {}, true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法获取劲敌列表" : res.error;
        return out;
    }
    MarkOnlineFromAuthenticatedSuccess();
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", "获取劲敌列表失败");
            return out;
        }
        out.ok = true;
        if (j.contains("rivals") && j["rivals"].is_array())
        {
            for (const auto& r : j["rivals"])
            {
                if (!r.is_object()) continue;
                RivalSummaryItem item;
                item.userId        = SafeJsonStringOr(r, "user_id", "");
                item.username      = SafeJsonStringOr(r, "username", "");
                item.bio           = SafeJsonStringOr(r, "bio", "");
                item.featuredTitle = SafeJsonStringOr(r, "featured_title", "");
                item.danBadge      = SafeJsonStringOr(r, "dan_badge", "");
                item.sakuraRating  = JsonSakuraRatingOr(r);
                item.totalPP       = r.contains("total_pp") && r["total_pp"].is_number() ? r["total_pp"].get<double>() : 0.0;
                item.playCount     = JsonIntOr(r, "play_count", 0);
                item.isMutual      = JsonBoolOr(r, "is_mutual", false);
                item.addedAt       = JsonInt64Or(r, "added_at", 0LL);
                out.rivals.push_back(std::move(item));
            }
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("劲敌列表解析失败: ") + e.what();
        return out;
    }
}

RatingLadderResult APIClient::FetchRatingLadder(int limit)
{
    RatingLadderResult out;
    const int safeLimit = std::clamp(limit, 1, 100);
    const std::string path = "/api/v1/rankings/rating?limit=" + std::to_string(safeLimit);
    const auto res = IsAuthenticated()
        ? SendWithAuthRetry(MakeRequest("GET", path, {}, true))
        : GetTransport()->Send(MakeRequest("GET", path, {}, false));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        MarkOfflinePreserveSession();
        out.error = res.error.empty() ? "离线：无法获取全球天梯" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", "获取全球天梯失败");
            return out;
        }
        out.ok = true;
        if (j.contains("my_rank") && j["my_rank"].is_number_integer())
            out.myRank = j["my_rank"].get<int>();
        out.myRating = j.contains("my_rating") && j["my_rating"].is_number() ? j["my_rating"].get<double>() : 0.0;
        if (j.contains("entries") && j["entries"].is_array())
        {
            for (const auto& e : j["entries"])
            {
                if (!e.is_object()) continue;
                RatingLadderRow row;
                row.rank          = JsonIntOr(e, "rank", 0);
                row.userId        = SafeJsonStringOr(e, "user_id", "");
                row.username      = SafeJsonStringOr(e, "username", "");
                row.featuredTitle = SafeJsonStringOr(e, "featured_title", "");
                row.danBadge      = SafeJsonStringOr(e, "dan_badge", "");
                row.sakuraRating  = JsonSakuraRatingOr(e);
                row.totalPP       = e.contains("total_pp") && e["total_pp"].is_number() ? e["total_pp"].get<double>() : 0.0;
                row.playCount     = JsonIntOr(e, "play_count", 0);
                row.isRival       = JsonBoolOr(e, "is_rival", false);
                out.entries.push_back(std::move(row));
            }
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("全球天梯解析失败: ") + e.what();
        return out;
    }
}

void APIClient::FetchUserProfileAsync(const std::string& userId, std::function<void(UserProfileResult)> cb)
{
    PostTask([this, userId, cb = std::move(cb)]() mutable
    {
        auto res = FetchUserProfile(userId);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::UpdateMyProfileAsync(const std::string& bio, const std::string& featuredTitle,
                                     std::function<void(UserProfileResult)> cb)
{
    PostTask([this, bio, featuredTitle, cb = std::move(cb)]() mutable
    {
        auto res = UpdateMyProfile(bio, featuredTitle);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::AddRivalAsync(const std::string& targetUserId, std::function<void(RivalActionResult)> cb)
{
    PostTask([this, targetUserId, cb = std::move(cb)]() mutable
    {
        auto res = AddRival(targetUserId);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RemoveRivalAsync(const std::string& targetUserId, std::function<void(RivalActionResult)> cb)
{
    PostTask([this, targetUserId, cb = std::move(cb)]() mutable
    {
        auto res = RemoveRival(targetUserId);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchRivalsAsync(std::function<void(RivalListResult)> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        auto res = FetchRivals();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchRatingLadderAsync(int limit, std::function<void(RatingLadderResult)> cb)
{
    PostTask([this, limit, cb = std::move(cb)]() mutable
    {
        auto res = FetchRatingLadder(limit);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

} // namespace sakura::net
