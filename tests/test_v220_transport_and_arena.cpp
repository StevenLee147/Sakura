// tests/test_v220_transport_and_arena.cpp — v2.2.0 CancelToken, Dual Worker Pools, OnlineCache SWR/ETag, OfflineQueue v2 & Arena Delta unit tests

#include "test_framework.h"

#include "core/auth_store.h"
#include "core/config.h"
#include "core/paths.h"
#include "data/database.h"
#include "game/chart.h"
#include "game/course.h"
#include "net/api_client.h"
#include "net/http_client.h"
#include "net/offline_queue.h"
#include "net/online_cache.h"
#include "utils/file_io.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using sakura::core::Config;
using sakura::core::Paths;
using sakura::game::CourseRecord;
using sakura::game::GameResult;
using sakura::game::Grade;
using sakura::net::APIClient;
using sakura::net::ArenaRoomResult;
using sakura::net::AuthResult;
using sakura::net::CacheLookupResult;
using sakura::net::CancelToken;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::LeaderboardResult;
using sakura::net::MockHttpTransport;
using sakura::net::OfflineQueue;
using sakura::net::OnlineCache;
using sakura::net::PendingActionItem;
using sakura::net::PendingActionType;

namespace
{

long long NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string AsciiLower(std::string_view input)
{
    std::string out(input);
    for (char& ch : out)
    {
        if (static_cast<unsigned char>(ch) < 128)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string GetRequestHeader(const HttpRequest& req, std::string_view headerName)
{
    const std::string needle = AsciiLower(headerName);
    for (const auto& [k, v] : req.headers)
    {
        if (AsciiLower(k) == needle)
            return v;
    }
    return {};
}

struct ScopedV220TempEnv
{
    fs::path       dir;
    fs::path       previousUserRoot;
    std::string    previousConfigPath;
    nlohmann::json configSnapshot;

    ScopedV220TempEnv()
    {
        static std::atomic<unsigned long long> seq{ 0ULL };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto idx   = ++seq;
        dir = fs::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v220_" + std::to_string(stamp) + "_" + std::to_string(idx));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "config", ec);
        fs::create_directories(dir / "data", ec);
        fs::create_directories(dir / "cache", ec);

        auto& cfg          = Config::GetInstance();
        previousConfigPath = cfg.GetFilePath();
        configSnapshot     = cfg.GetRoot();

        previousUserRoot = fs::path(Paths::User());
        Paths::SetUserRoot(dir);
        cfg.Load((dir / "config" / "settings.json").string());
    }

    ~ScopedV220TempEnv()
    {
        sakura::data::Database::GetInstance().Shutdown();
        Paths::SetUserRoot(previousUserRoot);
        auto& cfg = Config::GetInstance();
        if (!previousConfigPath.empty())
            cfg.Load(previousConfigPath);
        cfg.Restore(configSnapshot);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

GameResult MakeSampleGameResult(const std::string& chartId, const std::string& diff, int scoreVal)
{
    GameResult r;
    r.chartId         = chartId;
    r.chartTitle      = "Title " + chartId;
    r.chartHash       = "hash_" + chartId;
    r.difficulty      = diff;
    r.difficultyLevel = 10.5f;
    r.score           = scoreVal;
    r.accuracy        = 99.1f;
    r.maxCombo        = 240;
    r.grade           = Grade::S;
    r.perfectCount    = 236;
    r.greatCount      = 4;
    r.goodCount       = 0;
    r.badCount        = 0;
    r.missCount       = 0;
    r.totalJudgments  = 240;
    r.isFullCombo     = true;
    r.isAllPerfect    = false;
    r.assisted        = false;
    r.playbackRate    = 1.0f;
    r.playedAt        = 1765000000LL;
    return r;
}

nlohmann::json MakeAuthResponseJson(const std::string& userId, const std::string& username, const std::string& token)
{
    return {
        { "ok", true },
        { "token", token },
        { "refresh_token", "rt_" + token },
        { "expires_at", NowUnixSeconds() + 7LL * 86400LL },
        { "user", {
            { "id", userId },
            { "username", username },
            { "is_guest", false },
            { "total_pp", 2500.0 },
            { "sakura_rating", 15.40 },
            { "dan_badge", "十段" }
        } }
    };
}

nlohmann::json MakeLeaderboardBodyJson(const std::string& scoreId, const std::string& username, int scoreVal)
{
    return {
        { "ok", true },
        { "my_rank", 1 },
        { "entries", nlohmann::json::array({
            {
                { "rank", 1 },
                { "score_id", scoreId },
                { "user_id", "u_v220_1" },
                { "username", username },
                { "score", scoreVal },
                { "accuracy", 99.1 },
                { "max_combo", 240 },
                { "grade", "S" },
                { "pp", 235.0 },
                { "sakura_rating", 15.40 },
                { "dan_badge", "十段" },
                { "is_full_combo", true },
                { "is_all_perfect", false },
                { "has_replay", true },
                { "played_at", 1765000000LL }
            }
        }) }
    };
}

} // namespace

TEST_CASE("CancelToken mid-stream cancellation via MockHttpTransport sets cancelled without IsNetworkError", "[net][v220]")
{
    std::atomic<int> chunksAttempted{ 0 };

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        constexpr std::size_t totalBytes = 16384U;
        for (int chunk = 1; chunk <= 4; ++chunk)
        {
            ++chunksAttempted;
            if (req.onProgress)
                req.onProgress(static_cast<std::size_t>(chunk) * 4096U, totalBytes);
        }
        HttpResponse completed;
        completed.statusCode = 200;
        completed.body       = std::string(totalBytes, 'X');
        return completed;
    });

    auto streamToken = std::make_shared<CancelToken>();
    REQUIRE(!streamToken->IsCancelled());

    std::size_t lastReportedBytes = 0;
    HttpRequest streamReq;
    streamReq.method      = "GET";
    streamReq.url         = "https://cdn.sakura.test/packages/sakura_v220.sakura";
    streamReq.cancelToken = streamToken;
    streamReq.onProgress  = [&](std::size_t downloaded, std::size_t total) {
        REQUIRE(total == 16384U);
        lastReportedBytes = downloaded;
        if (downloaded >= 8192U)
            streamToken->Cancel();
    };

    const HttpResponse midStreamRes = mock->Send(streamReq);
    REQUIRE(streamToken->IsCancelled());
    REQUIRE(midStreamRes.cancelled);
    REQUIRE(!midStreamRes.IsNetworkError());
    REQUIRE(!midStreamRes.IsSuccess());
    REQUIRE(!midStreamRes.IsNotModified());
    REQUIRE(chunksAttempted.load() == 2);
    REQUIRE(lastReportedBytes == 8192U);

    // Pre-cancelled token aborts before invoking the transport handler
    mock->ClearRecordedRequests();
    auto preCancelledToken = std::make_shared<CancelToken>();
    preCancelledToken->Cancel();
    HttpRequest preReq;
    preReq.url         = "https://cdn.sakura.test/packages/ignored.sakura";
    preReq.cancelToken = preCancelledToken;

    const HttpResponse preRes = mock->Send(preReq);
    REQUIRE(preRes.cancelled);
    REQUIRE(!preRes.IsNetworkError());
    REQUIRE(mock->GetRecordedRequests().empty());

    // Verify HttpResponse::IsNotModified() contract for HTTP 304
    HttpResponse notModifiedRes;
    notModifiedRes.statusCode = 304;
    REQUIRE(notModifiedRes.IsNotModified());
    REQUIRE(!notModifiedRes.IsSuccess());
    REQUIRE(!notModifiedRes.IsNetworkError());
}

TEST_CASE("Dual worker pool isolates InteractivePool from blocked TransferPool and cancels stale FetchLeaderboardAsync tokens", "[net][v220]")
{
    ScopedV220TempEnv env;

    std::mutex                                    tokenMutex;
    std::vector<std::shared_ptr<CancelToken>>     rapidTokens;
    std::once_flag                                rapid1StartedOnce;
    std::promise<void>                            rapid1InFlightPromise;
    std::shared_future<void>                      rapid1InFlightFuture = rapid1InFlightPromise.get_future().share();

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body       = MakeAuthResponseJson("u_pool_1", "PoolTester", "jwt_pool_v220").dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/me"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "user", {
                    { "id", "u_pool_1" },
                    { "username", "PoolTester" },
                    { "is_guest", false },
                    { "total_pp", 2500.0 },
                    { "sakura_rating", 15.40 },
                    { "dan_badge", "十段" }
                } }
            }).dump();
            return res;
        }
        if (req.url.find("/api/v1/leaderboards/isolated_chart") != std::string::npos)
        {
            res.statusCode = 200;
            res.headers.emplace_back("ETag", "\"etag-iso-1\"");
            res.body = MakeLeaderboardBodyJson("score_iso_1", "PoolTester", 990100).dump();
            return res;
        }
        if (req.url.find("/api/v1/leaderboards/rapid_song_") != std::string::npos)
        {
            if (req.cancelToken)
            {
                std::lock_guard<std::mutex> lk(tokenMutex);
                rapidTokens.push_back(req.cancelToken);
            }

            if (req.url.find("rapid_song_1") != std::string::npos)
            {
                std::call_once(rapid1StartedOnce, [&]() { rapid1InFlightPromise.set_value(); });
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    if (req.cancelToken && req.cancelToken->IsCancelled())
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (req.onProgress)
                    req.onProgress(64U, 256U);
                res.statusCode = 200;
                res.body       = MakeLeaderboardBodyJson("score_rapid_1", "PoolTester", 910000).dump();
                return res;
            }
            if (req.url.find("rapid_song_2") != std::string::npos)
            {
                for (int spin = 0; spin < 15; ++spin)
                {
                    if (req.cancelToken && req.cancelToken->IsCancelled())
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                res.statusCode = 200;
                res.body       = MakeLeaderboardBodyJson("score_rapid_2", "PoolTester", 920000).dump();
                return res;
            }
            res.statusCode = 200;
            res.body       = MakeLeaderboardBodyJson("score_rapid_3", "PoolTester", 999333).dump();
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = env.dir / "cache" / "pool_pending_actions.json";
    const fs::path authPath  = env.dir / "config" / "pool_auth_session.json";
    APIClient client(mock, queuePath.string(), authPath.string());
    client.SetServerUrl("https://pools.sakura.test");

    REQUIRE(client.Login("PoolTester", "SakuraPass#220").ok);
    REQUIRE(client.IsAuthenticated());

    const uint64_t seq1 = client.NextRequestSeq();
    const uint64_t seq2 = client.NextRequestSeq();
    REQUIRE(seq2 > seq1);

    // Saturate all 2 threads of TransferPool with blocked promises
    std::promise<void> transferStarted1;
    std::promise<void> transferStarted2;
    std::promise<void> releaseTransfers;
    std::shared_future<void> releaseGate = releaseTransfers.get_future().share();
    std::promise<void> transferFinished1;
    std::promise<void> transferFinished2;
    std::atomic<int> completedTransfers{ 0 };

    client.PostTransferTask([&]() {
        transferStarted1.set_value();
        releaseGate.wait();
        ++completedTransfers;
        transferFinished1.set_value();
    });
    client.PostTransferTask([&]() {
        transferStarted2.set_value();
        releaseGate.wait();
        ++completedTransfers;
        transferFinished2.set_value();
    });

    REQUIRE(transferStarted1.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(transferStarted2.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(completedTransfers.load() == 0);

    // Submit InteractivePool calls while TransferPool is completely blocked
    bool              lbDone      = false;
    bool              profileDone = false;
    LeaderboardResult lbRes;
    AuthResult        profRes;

    client.FetchLeaderboardAsync("isolated_chart", "Expert", 10, [&](LeaderboardResult r) {
        lbRes  = std::move(r);
        lbDone = true;
    });
    client.RefreshProfileAsync([&](AuthResult r) {
        profRes     = std::move(r);
        profileDone = true;
    });

    const auto interactiveDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while ((!lbDone || !profileDone) && std::chrono::steady_clock::now() < interactiveDeadline)
    {
        client.PumpCallbacks();
        if (!lbDone || !profileDone)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(lbDone);
    REQUIRE(profileDone);
    REQUIRE(lbRes.ok);
    REQUIRE(lbRes.entries.size() == 1);
    REQUIRE(lbRes.entries[0].score == 990100);
    REQUIRE(profRes.ok);
    REQUIRE(profRes.user.username == "PoolTester");
    REQUIRE(completedTransfers.load() == 0);

    // Release blocked transfer tasks and verify clean completion
    releaseTransfers.set_value();
    REQUIRE(transferFinished1.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(transferFinished2.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(completedTransfers.load() == 2);

    // Rapid sequential FetchLeaderboardAsync calls cancel stale CancelTokens and suppress stale callbacks
    std::atomic<int>  staleLbCallbacks{ 0 };
    bool              latestLbDone = false;
    LeaderboardResult latestLbRes;

    client.FetchLeaderboardAsync("rapid_song_1", "Expert", 10, [&](LeaderboardResult) {
        ++staleLbCallbacks;
    });
    REQUIRE(rapid1InFlightFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);

    client.FetchLeaderboardAsync("rapid_song_2", "Expert", 10, [&](LeaderboardResult) {
        ++staleLbCallbacks;
    });
    client.FetchLeaderboardAsync("rapid_song_3", "Expert", 10, [&](LeaderboardResult r) {
        latestLbRes  = std::move(r);
        latestLbDone = true;
    });

    const auto rapidDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (!latestLbDone && std::chrono::steady_clock::now() < rapidDeadline)
    {
        client.PumpCallbacks();
        if (!latestLbDone)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int extra = 0; extra < 6; ++extra)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        client.PumpCallbacks();
    }

    {
        std::lock_guard<std::mutex> lk(tokenMutex);
        REQUIRE(!rapidTokens.empty());
        REQUIRE(rapidTokens.front()->IsCancelled());
    }
    REQUIRE(latestLbDone);
    REQUIRE(latestLbRes.ok);
    REQUIRE(latestLbRes.entries.size() == 1);
    REQUIRE(latestLbRes.entries[0].score == 999333);
    REQUIRE(staleLbCallbacks.load() == 0);

    client.Shutdown();
}

TEST_CASE("OnlineCache SWR and ETag 304 handle soft TTL, TouchFresh revalidation, SubmitScore invalidation, and offline fallback", "[net][v220]")
{
    ScopedV220TempEnv env;

    // Direct OnlineCache unit verification (soft TTL, stale window, TouchFresh, MarkStale, hard TTL, disk persistence)
    {
        const fs::path cacheFile = env.dir / "cache" / "direct_online_cache.json";
        OnlineCache cache(cacheFile);

        const std::string bodyJson = MakeLeaderboardBodyJson("s_unit_1", "CacheHero", 975000).dump();
        cache.Put("leaderboard:unit_song:Master", bodyJson, "\"etag-unit-v1\"", 50000LL);

        const CacheLookupResult hitSoft = cache.Lookup("leaderboard:unit_song:Master", 20, 86400, 50012LL);
        REQUIRE(hitSoft.found);
        REQUIRE(!hitSoft.isStale);
        REQUIRE(hitSoft.etag == "\"etag-unit-v1\"");
        REQUIRE(hitSoft.payloadJson == bodyJson);

        const CacheLookupResult hitStale = cache.Lookup("leaderboard:unit_song:Master", 20, 86400, 50035LL);
        REQUIRE(hitStale.found);
        REQUIRE(hitStale.isStale);
        REQUIRE(hitStale.etag == "\"etag-unit-v1\"");

        cache.TouchFresh("leaderboard:unit_song:Master", 50035LL);
        const CacheLookupResult hitTouched = cache.Lookup("leaderboard:unit_song:Master", 20, 86400, 50045LL);
        REQUIRE(hitTouched.found);
        REQUIRE(!hitTouched.isStale);
        REQUIRE(hitTouched.updatedAt == 50035LL);

        OnlineCache reloaded(cacheFile);
        REQUIRE(reloaded.Load());
        REQUIRE(reloaded.Lookup("leaderboard:unit_song:Master", 20, 86400, 50045LL).found);

        reloaded.MarkStale("leaderboard:unit_song:");
        const CacheLookupResult markedStale = reloaded.Lookup("leaderboard:unit_song:Master", 20, 86400, 50045LL);
        REQUIRE(markedStale.found);
        REQUIRE(markedStale.isStale);

        const CacheLookupResult expiredHard = reloaded.Lookup("leaderboard:unit_song:Master", 20, 86400, 50035LL + 86401LL);
        REQUIRE(!expiredHard.found);

        reloaded.Clear();
        REQUIRE(!reloaded.Lookup("leaderboard:unit_song:Master", 20, 86400, 50045LL).found);
    }

    // End-to-end APIClient + OnlineCache + HTTP ETag / 304 / SubmitScore invalidation / offline hard-TTL fallback
    std::atomic<bool> serverOffline{ false };
    std::atomic<int>  leaderboardHttpHits{ 0 };
    std::atomic<int>  leaderboard304Responses{ 0 };
    std::mutex        stateMutex;
    std::string       serverEtag     = "\"etag-swr-v1\"";
    int               serverTopScore = 982000;
    std::string       observedIfNoneMatch;

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        const bool isLbReq = (req.url.find("/api/v1/leaderboards/swr_song") != std::string::npos);
        if (isLbReq)
        {
            ++leaderboardHttpHits;
        }
        if (serverOffline.load())
        {
            res.statusCode = 0;
            res.error      = "Simulated network outage";
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body       = MakeAuthResponseJson("u_swr_1", "SwrPlayer", "jwt_swr_v220").dump();
            return res;
        }
        if (isLbReq)
        {
            const std::string inm = GetRequestHeader(req, "If-None-Match");
            std::string etagSnapshot;
            int         scoreSnapshot = 0;
            {
                std::lock_guard<std::mutex> lk(stateMutex);
                observedIfNoneMatch = inm;
                etagSnapshot        = serverEtag;
                scoreSnapshot       = serverTopScore;
            }
            if (!inm.empty() && inm == etagSnapshot)
            {
                ++leaderboard304Responses;
                res.statusCode = 304;
                res.headers.emplace_back("ETag", etagSnapshot);
                res.body.clear();
                return res;
            }
            res.statusCode = 200;
            res.headers.emplace_back("ETag", etagSnapshot);
            res.body = MakeLeaderboardBodyJson("score_swr_1", "SwrPlayer", scoreSnapshot).dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/scores"))
        {
            {
                std::lock_guard<std::mutex> lk(stateMutex);
                serverTopScore = 997400;
                serverEtag     = "\"etag-swr-v2\"";
            }
            res.statusCode = 201;
            res.body = nlohmann::json({
                { "ok", true },
                { "score_id", "score_swr_2" },
                { "rank", 1 },
                { "pp", 248.0 },
                { "total_pp", 2520.0 },
                { "new_best", true }
            }).dump();
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = env.dir / "cache" / "swr_pending_actions.json";
    const fs::path authPath  = env.dir / "config" / "swr_auth_session.json";
    APIClient client(mock, queuePath.string(), authPath.string());
    client.SetServerUrl("https://swr.sakura.test");
    REQUIRE(client.Login("SwrPlayer", "SakuraPass#220").ok);

    // 1. Cold fetch populates OnlineCache with ETag
    const LeaderboardResult lbCold = client.FetchLeaderboard("swr_song", "Expert", 10);
    REQUIRE(lbCold.ok);
    REQUIRE(lbCold.entries.size() == 1);
    REQUIRE(lbCold.entries[0].score == 982000);
    REQUIRE(leaderboardHttpHits.load() == 1);
    REQUIRE(client.GetCachedLeaderboard("swr_song", "Expert").has_value());

    // 2. Soft TTL hit incurs zero network calls
    const LeaderboardResult lbSoft = client.FetchLeaderboard("swr_song", "Expert", 10);
    REQUIRE(lbSoft.ok);
    REQUIRE(lbSoft.entries.size() == 1);
    REQUIRE(lbSoft.entries[0].score == 982000);
    REQUIRE(leaderboardHttpHits.load() == 1);

    // 3. Stale window sends If-None-Match and handles 304 Not Modified via TouchFresh without payload download
    const CacheLookupResult beforeAge = client.GetOnlineCache().Lookup("leaderboard:swr_song:Expert");
    REQUIRE(beforeAge.found);
    client.GetOnlineCache().Put("leaderboard:swr_song:Expert", beforeAge.payloadJson, beforeAge.etag, NowUnixSeconds() - 120LL);
    REQUIRE(client.GetOnlineCache().Lookup("leaderboard:swr_song:Expert", 20, 86400).isStale);

    const LeaderboardResult lb304 = client.FetchLeaderboard("swr_song", "Expert", 10);
    REQUIRE(lb304.ok);
    REQUIRE(lb304.entries.size() == 1);
    REQUIRE(lb304.entries[0].score == 982000);
    REQUIRE(leaderboardHttpHits.load() == 2);
    REQUIRE(leaderboard304Responses.load() == 1);
    {
        std::lock_guard<std::mutex> lk(stateMutex);
        REQUIRE(observedIfNoneMatch == "\"etag-swr-v1\"");
    }
    REQUIRE(!client.GetOnlineCache().Lookup("leaderboard:swr_song:Expert", 20, 86400).isStale);

    // Fresh again after TouchFresh -> zero network call
    const LeaderboardResult lbAfterTouch = client.FetchLeaderboard("swr_song", "Expert", 10);
    REQUIRE(lbAfterTouch.ok);
    REQUIRE(leaderboardHttpHits.load() == 2);

    // 4. SubmitScore marks leaderboard:<chartId>: stale so next fetch revalidates and picks up new score
    const GameResult improvedPlay = MakeSampleGameResult("swr_song", "Expert", 997400);
    const auto subRes = client.SubmitScore(improvedPlay);
    REQUIRE(subRes.ok);
    REQUIRE(client.GetOnlineCache().Lookup("leaderboard:swr_song:Expert", 20, 86400).isStale);

    const LeaderboardResult lbUpdated = client.FetchLeaderboard("swr_song", "Expert", 10);
    REQUIRE(lbUpdated.ok);
    REQUIRE(lbUpdated.entries.size() == 1);
    REQUIRE(lbUpdated.entries[0].score == 997400);
    REQUIRE(leaderboardHttpHits.load() == 3);

    // 5. Offline fallback returns cached entries within hard TTL
    client.GetOnlineCache().MarkStale("leaderboard:swr_song:");
    serverOffline.store(true);

    const LeaderboardResult lbOfflineFallback = client.FetchLeaderboard("swr_song", "Expert", 10);
    REQUIRE(leaderboardHttpHits.load() == 4);
    REQUIRE(lbOfflineFallback.ok);
    REQUIRE(lbOfflineFallback.entries.size() == 1);
    REQUIRE(lbOfflineFallback.entries[0].score == 997400);

    client.Shutdown();
}

TEST_CASE("OfflineQueue v2 migrates legacy pending_scores.json, flushes all 3 action types, enforces backoff curve, and evicts 422", "[net][v220]")
{
    ScopedV220TempEnv env;

    // 1. Verify exponential backoff schedule for attempts 0..8: {5, 10, 20, 40, 80, 160, 300, 300, 300}
    constexpr std::array<int, 9> kExpectedBackoffs = { 5, 10, 20, 40, 80, 160, 300, 300, 300 };
    for (int attempt = 0; attempt <= 8; ++attempt)
    {
        REQUIRE(OfflineQueue::ComputeBackoffSeconds(attempt) == kExpectedBackoffs[static_cast<std::size_t>(attempt)]);
    }

    // 2. Auto-migrate legacy pending_scores.json JSON array into pending_actions.json StandardScore items
    const fs::path legacyScoresFile = env.dir / "data" / "pending_scores.json";
    const fs::path migratedQueueFile = env.dir / "cache" / "pending_actions.json";
    const nlohmann::json legacyArray = nlohmann::json::array({
        {
            { "client_submission_id", "legacy_id_alpha" },
            { "chart_id", "sakura_breath" },
            { "chart_title", "Sakura Breath" },
            { "chart_hash", "hash_alpha" },
            { "difficulty", "Expert" },
            { "difficulty_level", 10.5 },
            { "score", 988100 },
            { "accuracy", 98.8 },
            { "max_combo", 260 },
            { "grade", "S" },
            { "perfect_count", 255 },
            { "great_count", 5 },
            { "good_count", 0 },
            { "bad_count", 0 },
            { "miss_count", 0 },
            { "total_judgments", 260 },
            { "is_full_combo", true },
            { "is_all_perfect", false },
            { "assisted", false },
            { "playback_rate", 1.0 },
            { "played_at", 1765000111LL }
        },
        {
            { "chart_id", "sakura_awakening" },
            { "chart_title", "Sakura Awakening" },
            { "chart_hash", "hash_beta" },
            { "difficulty", "Hard" },
            { "difficulty_level", 7.0 },
            { "score", 962000 },
            { "accuracy", 96.2 },
            { "max_combo", 190 },
            { "grade", "S" },
            { "perfect_count", 180 },
            { "great_count", 10 },
            { "good_count", 0 },
            { "bad_count", 0 },
            { "miss_count", 0 },
            { "total_judgments", 190 },
            { "is_full_combo", true },
            { "is_all_perfect", false },
            { "assisted", false },
            { "playback_rate", 1.0 }
        }
    });
    REQUIRE(sakura::utils::AtomicWrite(legacyScoresFile, legacyArray.dump(2)));
    REQUIRE(fs::is_regular_file(legacyScoresFile));

    {
        OfflineQueue migQueue(migratedQueueFile, legacyScoresFile);
        REQUIRE(migQueue.Load());
        REQUIRE(migQueue.GetCount() == 2U);
        REQUIRE(fs::is_regular_file(migratedQueueFile));
        REQUIRE(!fs::exists(legacyScoresFile));

        const std::vector<PendingActionItem> items = migQueue.GetAllItems();
        REQUIRE(items.size() == 2U);
        REQUIRE(items[0].type == PendingActionType::StandardScore);
        REQUIRE(items[0].id == "legacy_id_alpha");
        REQUIRE(items[1].type == PendingActionType::StandardScore);

        // Verify retryable backoff window gating and permanent failure removal on OfflineQueue directly
        migQueue.MarkRetryableFailure("legacy_id_alpha", "HTTP 503", 20000LL);
        const auto readyAtPlus4 = migQueue.GetReadyItems(20004LL);
        REQUIRE(readyAtPlus4.size() == 1U);
        const auto readyAtPlus5 = migQueue.GetReadyItems(20005LL);
        REQUIRE(readyAtPlus5.size() == 2U);

        migQueue.MarkPermanentFailure(items[1].id, "HTTP 422 invalid");
        REQUIRE(migQueue.GetCount() == 1U);
        migQueue.MarkSucceeded("legacy_id_alpha");
        REQUIRE(migQueue.GetCount() == 0U);
    }

    // 3. End-to-end APIClient enqueue & FlushPendingActions for StandardScore + DailyScore + DanCourseRecord, plus 422 eviction
    std::atomic<bool> networkDown{ false };
    std::atomic<bool> rejectScores422{ false };
    std::atomic<int>  stdScorePosts{ 0 };
    std::atomic<int>  dailyScorePosts{ 0 };
    std::atomic<int>  courseSyncPosts{ 0 };

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (networkDown.load())
        {
            res.statusCode = 0;
            res.error      = "Offline";
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body       = MakeAuthResponseJson("u_q_1", "QueueTester", "jwt_queue_v220").dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/scores"))
        {
            ++stdScorePosts;
            if (rejectScores422.load())
            {
                res.statusCode = 422;
                res.body       = R"({"ok":false,"error":"Score validation failed"})";
                return res;
            }
            res.statusCode = 201;
            res.body = nlohmann::json({
                { "ok", true },
                { "score_id", "score_flushed_1" },
                { "rank", 1 },
                { "pp", 220.0 },
                { "total_pp", 220.0 },
                { "new_best", true }
            }).dump();
            return res;
        }
        if (req.url.find("/api/v1/daily/scores") != std::string::npos)
        {
            ++dailyScorePosts;
            res.statusCode = 201;
            res.body = nlohmann::json({
                { "ok", true },
                { "date", "2026-11-26" },
                { "rank", 1 },
                { "new_best", true },
                { "sakura_rating", 15.50 }
            }).dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/sync/push") || req.url.find("/courses") != std::string::npos)
        {
            ++courseSyncPosts;
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "synced_at", NowUnixSeconds() },
                { "scores_merged", 0 },
                { "achievements_merged", 0 },
                { "courses_merged", 1 },
                { "scores", nlohmann::json::array() },
                { "achievements", nlohmann::json::array() },
                { "course_records", nlohmann::json::array() },
                { "settings", nlohmann::json::object() },
                { "settings_updated_at", 0 }
            }).dump();
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path apiQueueFile = env.dir / "cache" / "api_pending_actions.json";
    const fs::path apiAuthFile  = env.dir / "config" / "api_auth_session.json";
    APIClient client(mock, apiQueueFile.string(), apiAuthFile.string());
    client.SetServerUrl("https://queue.sakura.test");
    REQUIRE(client.Login("QueueTester", "SakuraPass#220").ok);

    // Go offline and enqueue StandardScore + DailyScore + DanCourseRecord
    networkDown.store(true);
    const GameResult stdPlay   = MakeSampleGameResult("sakura_breath", "Expert", 989000);
    const GameResult dailyPlay = MakeSampleGameResult("sakura_awakening", "Hard", 977000);
    const CourseRecord danRec{ "dan_kaiden", "皆传", 10, 3915000, 99.1f, 84.0f, true, 1795600000LL };

    const auto stdOfflineRes = client.SubmitScore(stdPlay);
    REQUIRE(!stdOfflineRes.ok);
    REQUIRE(stdOfflineRes.queuedOffline);

    const auto dailyOfflineRes = client.SubmitDailyScore("2026-11-26", dailyPlay);
    REQUIRE(!dailyOfflineRes.ok);
    REQUIRE(dailyOfflineRes.queuedOffline);

    REQUIRE(client.EnqueueCourseRecordOffline(danRec));
    REQUIRE(client.GetOfflineQueue().GetCount() == 3U);

    // Come back online and flush all 3 queued actions
    networkDown.store(false);
    const int flushedCount = client.FlushPendingActions();
    REQUIRE(flushedCount == 3);
    REQUIRE(client.GetOfflineQueue().GetCount() == 0U);
    REQUIRE(stdScorePosts.load() == 1);
    REQUIRE(dailyScorePosts.load() == 1);
    REQUIRE(courseSyncPosts.load() >= 1);

    // Queue another StandardScore offline, then return HTTP 422 online -> permanently evicted without infinite retry
    networkDown.store(true);
    const GameResult badPlay = MakeSampleGameResult("tampered_song", "Expert", 999999);
    const auto badQueued = client.SubmitScore(badPlay);
    REQUIRE(badQueued.queuedOffline);
    REQUIRE(client.GetOfflineQueue().GetCount() == 1U);

    networkDown.store(false);
    rejectScores422.store(true);
    const int flushedBad = client.FlushPendingActions();
    REQUIRE(flushedBad == 0);
    REQUIRE(stdScorePosts.load() == 2);
    REQUIRE(client.GetOfflineQueue().GetCount() == 0U);

    client.Shutdown();
}

TEST_CASE("APIClient Arena v2.2 parses marketChartId, SendArenaChat messages, and since_revision unchanged delta", "[arena][v220]")
{
    ScopedV220TempEnv env;

    int currentRevision = 1;

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body       = MakeAuthResponseJson("u_arena_1", "ArenaHost", "jwt_arena_v220").dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/rooms") && req.method == "POST")
        {
            currentRevision = 1;
            const nlohmann::json roomObj = {
                { "code", "SK220A" },
                { "name", "Blossom Lobby" },
                { "host_id", "u_arena_1" },
                { "host_name", "ArenaHost" },
                { "status", "waiting" },
                { "chart_id", "mkt_slug_220" },
                { "chart_title", "Blossom Pack" },
                { "difficulty", "Expert" },
                { "difficulty_level", 10.8 },
                { "chart_hash", "hash_220" },
                { "market_chart_id", "mkt_uuid_220" },
                { "package_size", 65536 },
                { "revision", currentRevision },
                { "players", nlohmann::json::array({
                    { { "user_id", "u_arena_1" }, { "username", "ArenaHost" }, { "is_host", true }, { "ready", false }, { "connected", true } }
                }) },
                { "messages", nlohmann::json::array() }
            };
            res.statusCode = 201;
            res.body       = nlohmann::json({ { "ok", true }, { "room", roomObj } }).dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/rooms/SK220A/chat") && req.method == "POST")
        {
            ++currentRevision;
            const nlohmann::json roomObj = {
                { "code", "SK220A" },
                { "name", "Blossom Lobby" },
                { "host_id", "u_arena_1" },
                { "host_name", "ArenaHost" },
                { "status", "waiting" },
                { "chart_id", "mkt_slug_220" },
                { "chart_title", "Blossom Pack" },
                { "difficulty", "Expert" },
                { "difficulty_level", 10.8 },
                { "chart_hash", "hash_220" },
                { "market_chart_id", "mkt_uuid_220" },
                { "package_size", 65536 },
                { "revision", currentRevision },
                { "players", nlohmann::json::array({
                    { { "user_id", "u_arena_1" }, { "username", "ArenaHost" }, { "is_host", true }, { "ready", false }, { "connected", true } }
                }) },
                { "messages", nlohmann::json::array({
                    { { "id", 1 }, { "user_id", "u_arena_1" }, { "username", "ArenaHost" }, { "phrase_id", 0 }, { "text", "请多指教！" }, { "sent_at", 1765000200LL } }
                }) }
            };
            res.statusCode = 200;
            res.body       = nlohmann::json({ { "ok", true }, { "room", roomObj } }).dump();
            return res;
        }
        if (req.url.find("/api/v1/rooms/SK220A/state") != std::string::npos)
        {
            if (req.url.find("since_revision=" + std::to_string(currentRevision)) != std::string::npos)
            {
                res.statusCode = 200;
                res.body       = nlohmann::json({ { "ok", true }, { "unchanged", true }, { "revision", currentRevision } }).dump();
                return res;
            }
            res.statusCode = 200;
            res.body       = nlohmann::json({ { "ok", true }, { "unchanged", false }, { "room", { { "code", "SK220A" }, { "revision", currentRevision } } } }).dump();
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = env.dir / "cache" / "arena_pending.json";
    const fs::path authPath  = env.dir / "config" / "arena_auth.json";
    APIClient client(mock, queuePath.string(), authPath.string());
    client.SetServerUrl("https://arena.sakura.test");
    REQUIRE(client.Login("ArenaHost", "SakuraPass#220").ok);

    const ArenaRoomResult created = client.CreateArenaRoom(
        "Blossom Lobby", "mkt_slug_220", "Blossom Pack", "Expert", 10.8f, "hash_220");
    REQUIRE(created.ok);
    REQUIRE(created.room.code == "SK220A");
    REQUIRE(created.room.marketChartId == "mkt_uuid_220");
    REQUIRE(created.room.packageSize == 65536U);
    REQUIRE(created.room.revision == 1);

    const ArenaRoomResult chatted = client.SendArenaChat("SK220A", 0);
    REQUIRE(chatted.ok);
    REQUIRE(chatted.room.revision == 2);
    REQUIRE(chatted.room.messages.size() == 1U);
    REQUIRE(chatted.room.messages[0].phraseId == 0);
    REQUIRE(!chatted.room.messages[0].text.empty());

    const ArenaRoomResult deltaState = client.FetchArenaRoomState("SK220A", chatted.room.revision);
    REQUIRE(deltaState.ok);
    REQUIRE(deltaState.unchanged);

    client.Shutdown();
}
