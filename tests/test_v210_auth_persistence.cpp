// tests/test_v210_auth_persistence.cpp — v2.1.0 Isolated AuthStore, Dual-Token Rotation & Account Lifecycle unit tests

#include "test_framework.h"

#include "core/auth_store.h"
#include "core/config.h"
#include "core/paths.h"
#include "net/api_client.h"
#include "net/http_client.h"
#include "utils/file_io.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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

using sakura::core::AuthSessionData;
using sakura::core::AuthStore;
using sakura::core::Config;
using sakura::core::Paths;
using sakura::core::SavedAccountEntry;
using sakura::net::APIClient;
using sakura::net::AuthResult;
using sakura::net::AuthState;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::LeaderboardResult;
using sakura::net::MockHttpTransport;
using sakura::tests::Matchers::WithinAbs;

namespace
{

long long NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct ScopedV210TempEnv
{
    fs::path dir;
    fs::path previousUserRoot;

    ScopedV210TempEnv()
    {
        static std::atomic<unsigned long long> seq{ 0ULL };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto idx   = ++seq;
        dir = fs::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v210_" + std::to_string(stamp) + "_" + std::to_string(idx));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "config", ec);
        fs::create_directories(dir / "data", ec);

        previousUserRoot = fs::path(Paths::User());
        Paths::SetUserRoot(dir);
        Config::GetInstance().Load((dir / "config" / "settings.json").string());
    }

    ~ScopedV210TempEnv()
    {
        Paths::SetUserRoot(previousUserRoot);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string ReadWholeFile(const fs::path& filePath)
{
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs)
        return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::string ExtractBearerToken(const HttpRequest& req)
{
    for (const auto& [k, v] : req.headers)
    {
        if (k == "Authorization" && v.starts_with("Bearer "))
            return v.substr(7);
    }
    return {};
}

void CorruptProtectedStrings(nlohmann::json& node, std::string_view replacement)
{
    if (node.is_string())
    {
        const std::string s = node.get<std::string>();
        if (s.starts_with("dpapi:") || s.starts_with("obf:"))
            node = std::string(replacement);
        return;
    }
    if (node.is_object())
    {
        for (auto& [key, val] : node.items())
        {
            (void)key;
            CorruptProtectedStrings(val, replacement);
        }
        return;
    }
    if (node.is_array())
    {
        for (auto& elem : node)
            CorruptProtectedStrings(elem, replacement);
    }
}

} // namespace

TEST_CASE("AuthStore ProtectSecret/UnprotectSecret round-trip, zero plaintext on disk, and corrupted ciphertext safety", "[auth][v210]")
{
    ScopedV210TempEnv env;

    const std::string rawAccess  = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.v210_secret_access_payload.sig_9911";
    const std::string rawRefresh = "rt_v210_csprng_secret_refresh_token_0123456789abcdef";

    const std::string protectedAccess  = AuthStore::ProtectSecret(rawAccess);
    const std::string protectedRefresh = AuthStore::ProtectSecret(rawRefresh);

#ifdef _WIN32
    REQUIRE(protectedAccess.starts_with("dpapi:"));
    REQUIRE(protectedRefresh.starts_with("dpapi:"));
#else
    REQUIRE(protectedAccess.starts_with("obf:"));
    REQUIRE(protectedRefresh.starts_with("obf:"));
#endif
    REQUIRE(protectedAccess.find(rawAccess) == std::string::npos);
    REQUIRE(protectedRefresh.find(rawRefresh) == std::string::npos);

    const std::optional<std::string> decAccess  = AuthStore::UnprotectSecret(protectedAccess);
    const std::optional<std::string> decRefresh = AuthStore::UnprotectSecret(protectedRefresh);
    REQUIRE(decAccess.has_value());
    REQUIRE(decRefresh.has_value());
    REQUIRE(*decAccess == rawAccess);
    REQUIRE(*decRefresh == rawRefresh);

    // Corrupted or malformed prefixes must degrade safely to std::nullopt without throwing
    std::optional<std::string> badDpapi;
    std::optional<std::string> badObf;
    std::optional<std::string> badBase64Dpapi;
    std::optional<std::string> badBase64Obf;
    std::optional<std::string> badUnknown;
    REQUIRE_NOTHROW(badDpapi = AuthStore::UnprotectSecret("dpapi:bad"));
    REQUIRE_NOTHROW(badObf = AuthStore::UnprotectSecret("obf:bad"));
    REQUIRE_NOTHROW(badBase64Dpapi = AuthStore::UnprotectSecret("dpapi:@@@not_valid_base64@@@"));
    REQUIRE_NOTHROW(badBase64Obf = AuthStore::UnprotectSecret("obf:@@@not_valid_base64@@@"));
    REQUIRE_NOTHROW(badUnknown = AuthStore::UnprotectSecret("plaintext_or_unknown:YWJj"));
    REQUIRE(!badDpapi.has_value());
    REQUIRE(!badObf.has_value());
    REQUIRE(!badBase64Dpapi.has_value());
    REQUIRE(!badBase64Obf.has_value());
    REQUIRE(!badUnknown.has_value());

    // Persist session to disk and verify zero plaintext tokens appear in auth_session.json
    const fs::path authPath = env.dir / "config" / "auth_session.json";
    AuthStore store(authPath);

    AuthSessionData session;
    session.serverUrl              = "https://vault.sakura.test";
    session.activeUserId           = "user_cipher_01";
    session.accessToken            = rawAccess;
    session.refreshToken           = rawRefresh;
    session.expiresAt              = NowUnixSeconds() + 7LL * 86400LL;
    session.profile.userId         = "user_cipher_01";
    session.profile.username       = "CipherBlossom";
    session.profile.isGuest        = false;
    session.profile.totalPP        = 2890.4;
    session.profile.sakuraRating   = 15.92;
    session.profile.danBadge       = "皆传";
    session.profile.serverUrl      = session.serverUrl;
    session.profile.expiresAt      = session.expiresAt;
    session.profile.lastUsedAt     = NowUnixSeconds();

    store.UpsertActiveSession(session);
    REQUIRE(store.Save());
    REQUIRE(fs::is_regular_file(authPath));

    const std::string diskRaw = ReadWholeFile(authPath);
    REQUIRE(!diskRaw.empty());
    REQUIRE(diskRaw.find(rawAccess) == std::string::npos);
    REQUIRE(diskRaw.find(rawRefresh) == std::string::npos);
    REQUIRE(diskRaw.find("dpapi:") != std::string::npos || diskRaw.find("obf:") != std::string::npos);

    AuthStore reloaded(authPath);
    REQUIRE(reloaded.Load());
    REQUIRE(reloaded.HasActiveSession());
    const AuthSessionData restored = reloaded.GetSession();
    REQUIRE(restored.activeUserId == "user_cipher_01");
    REQUIRE(restored.accessToken == rawAccess);
    REQUIRE(restored.refreshToken == rawRefresh);
    REQUIRE(restored.profile.username == "CipherBlossom");
    REQUIRE_THAT(restored.profile.sakuraRating, WithinAbs(15.92, 1e-4));
    REQUIRE(restored.profile.danBadge == "皆传");

    // Tampered ciphertext on disk must load gracefully without throwing and clear undecryptable tokens
    nlohmann::json tamperedJson = nlohmann::json::parse(diskRaw);
    CorruptProtectedStrings(tamperedJson, "dpapi:bad");
    const fs::path tamperedPath = env.dir / "config" / "auth_session_corrupt.json";
    REQUIRE(sakura::utils::AtomicWrite(tamperedPath, tamperedJson.dump(2)));

    AuthStore tamperedStore(tamperedPath);
    REQUIRE_NOTHROW(tamperedStore.Load());
    REQUIRE(!tamperedStore.HasActiveSession());
    REQUIRE(tamperedStore.GetSession().accessToken.empty());
    REQUIRE(tamperedStore.GetSession().refreshToken.empty());
}

TEST_CASE("APIClient physical isolation preserves session across Config::ResetToDefaults and Config::Restore", "[auth][v210]")
{
    ScopedV210TempEnv env;

    auto& cfg = Config::GetInstance();
    const nlohmann::json preLoginSnapshot = cfg.GetRoot();

    const std::string expectedToken   = "jwt_v210_isolated_access_token";
    const std::string expectedRefresh = "rt_v210_isolated_refresh_token";
    const long long   expectedExpiry  = NowUnixSeconds() + 7LL * 86400LL;

    auto mock = std::make_shared<MockHttpTransport>([=](const HttpRequest& req) {
        HttpResponse res;
        const nlohmann::json userObj = {
            { "id", "u_isolated_100" },
            { "username", "IsolatedSakura" },
            { "is_guest", false },
            { "total_pp", 3125.5 },
            { "sakura_rating", 16.08 },
            { "dan_badge", "十段" }
        };
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "token", expectedToken },
                { "refresh_token", expectedRefresh },
                { "expires_at", expectedExpiry },
                { "user", userObj }
            }).dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/me"))
        {
            if (ExtractBearerToken(req) == expectedToken)
            {
                res.statusCode = 200;
                res.body = nlohmann::json({ { "ok", true }, { "user", userObj } }).dump();
            }
            else
            {
                res.statusCode = 401;
                res.body = R"({"ok":false,"error":"Unauthorized"})";
            }
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = env.dir / "data" / "pending_scores.json";
    const fs::path authPath  = env.dir / "config" / "auth_session.json";

    {
        APIClient clientA(mock, queuePath.string(), authPath.string());
        clientA.SetServerUrl("https://iso.sakura.test");

        const AuthResult loginRes = clientA.Login("IsolatedSakura", "SakuraPass#210");
        REQUIRE(loginRes.ok);
        REQUIRE(clientA.IsAuthenticated());
        REQUIRE(clientA.GetAuthState() == AuthState::AuthenticatedOnline);
        REQUIRE(clientA.GetAuthToken() == expectedToken);
        REQUIRE(clientA.GetRefreshToken() == expectedRefresh);
        REQUIRE(clientA.GetTokenExpiresAt() == expectedExpiry);
        REQUIRE(!cfg.Has("online.auth_token"));

        // Destructive settings operations must not touch auth_session.json
        cfg.Set("gameplay.note_speed", 8.5f);
        cfg.ResetToDefaults();
        REQUIRE(cfg.SaveForce());
        cfg.Restore(preLoginSnapshot);
        REQUIRE(cfg.SaveForce());

        REQUIRE(clientA.IsAuthenticated());
        REQUIRE(clientA.GetAuthStore().HasActiveSession());
        clientA.Shutdown();
    }

    {
        APIClient clientB(mock, queuePath.string(), authPath.string());
        clientB.LoadFromConfig();

        REQUIRE(clientB.IsAuthenticated());
        const auto user = clientB.GetCurrentUser();
        REQUIRE(user.id == "u_isolated_100");
        REQUIRE(user.username == "IsolatedSakura");
        REQUIRE(!user.isGuest);
        REQUIRE_THAT(user.totalPP, WithinAbs(3125.5, 1e-4));
        REQUIRE_THAT(user.sakuraRating, WithinAbs(16.08, 1e-4));
        REQUIRE(user.danBadge == "十段");
        REQUIRE(clientB.GetAuthToken() == expectedToken);
        REQUIRE(clientB.GetRefreshToken() == expectedRefresh);
        REQUIRE(clientB.GetTokenExpiresAt() == expectedExpiry);
        clientB.Shutdown();
    }
}

TEST_CASE("APIClient::LoadFromConfig migrates legacy settings.json auth_token into encrypted auth_session.json and scrubs Config", "[auth][v210]")
{
    ScopedV210TempEnv env;

    const fs::path settingsPath = env.dir / "config" / "settings.json";
    const fs::path authPath     = env.dir / "config" / "auth_session.json";
    const fs::path queuePath    = env.dir / "data" / "pending_scores.json";
    REQUIRE(!fs::exists(authPath));

    const std::string legacyPlainToken = "legacy_v200_plaintext_jwt_secret_token_xyz";
    auto& cfg = Config::GetInstance();
    cfg.Load(settingsPath.string());
    cfg.Set("online.server_url", std::string("https://legacy.sakura.test"));
    cfg.Set("online.auth_token", legacyPlainToken);
    cfg.Set("online.user_id", std::string("u_legacy_77"));
    cfg.Set("online.username", std::string("LegacyMigrator"));
    cfg.Set("online.is_guest", false);
    cfg.Set("online.total_pp", 1940.25);
    cfg.Set("online.sakura_rating", 14.20);
    cfg.Set("online.dan_badge", std::string("九段"));
    REQUIRE(cfg.SaveForce());

    auto mock = std::make_shared<MockHttpTransport>([=](const HttpRequest& req) {
        HttpResponse res;
        if (req.url.ends_with("/api/v1/auth/me") && ExtractBearerToken(req) == legacyPlainToken)
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "user", {
                    { "id", "u_legacy_77" },
                    { "username", "LegacyMigrator" },
                    { "is_guest", false },
                    { "total_pp", 1940.25 },
                    { "sakura_rating", 14.20 },
                    { "dan_badge", "九段" }
                } }
            }).dump();
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    APIClient client(mock, queuePath.string(), authPath.string());
    client.LoadFromConfig();

    REQUIRE(client.IsAuthenticated());
    REQUIRE(client.GetAuthToken() == legacyPlainToken);
    const auto migratedUser = client.GetCurrentUser();
    REQUIRE(migratedUser.id == "u_legacy_77");
    REQUIRE(migratedUser.username == "LegacyMigrator");
    REQUIRE(!migratedUser.isGuest);
    REQUIRE_THAT(migratedUser.totalPP, WithinAbs(1940.25, 1e-4));
    REQUIRE_THAT(migratedUser.sakuraRating, WithinAbs(14.20, 1e-4));
    REQUIRE(migratedUser.danBadge == "九段");

    // Verify encrypted auth_session.json was written without plaintext token
    REQUIRE(fs::is_regular_file(authPath));
    const std::string authDiskText = ReadWholeFile(authPath);
    REQUIRE(!authDiskText.empty());
    REQUIRE(authDiskText.find(legacyPlainToken) == std::string::npos);
    REQUIRE(authDiskText.find("dpapi:") != std::string::npos || authDiskText.find("obf:") != std::string::npos);
    REQUIRE(client.GetSavedAccounts().size() == 1);
    REQUIRE(client.GetSavedAccounts()[0].userId == "u_legacy_77");

    // Verify plaintext online.auth_token was removed from Config in memory and on disk
    REQUIRE(!cfg.Has("online.auth_token"));
    REQUIRE(cfg.Get<std::string>("online.auth_token", "").empty());
    REQUIRE(cfg.Get<std::string>("online.server_url", "") == "https://legacy.sakura.test");

    const std::string settingsDiskText = ReadWholeFile(settingsPath);
    REQUIRE(settingsDiskText.find(legacyPlainToken) == std::string::npos);
    REQUIRE(settingsDiskText.find("auth_token") == std::string::npos);

    client.Shutdown();
}

TEST_CASE("AuthStore and APIClient enforce 5-slot MRU rotation across 6 accounts with SwitchSavedAccount and RemoveSavedAccount", "[auth][v210]")
{
    ScopedV210TempEnv env;

    const long long futureExpiry = NowUnixSeconds() + 7LL * 86400LL;

    auto makeUserJson = [](int idx) {
        return nlohmann::json({
            { "id", "uid_mru_" + std::to_string(idx) },
            { "username", "MruPlayer_" + std::to_string(idx) },
            { "is_guest", (idx % 2 == 0) },
            { "total_pp", 500.0 * static_cast<double>(idx) },
            { "sakura_rating", 10.0 + static_cast<double>(idx) },
            { "dan_badge", "Dan" + std::to_string(idx) }
        });
    };

    auto mock = std::make_shared<MockHttpTransport>([=](const HttpRequest& req) {
        HttpResponse res;
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            const auto body = nlohmann::json::parse(req.body, nullptr, false);
            const std::string uname = body.value("username", "");
            for (int idx = 1; idx <= 6; ++idx)
            {
                if (uname == "MruPlayer_" + std::to_string(idx))
                {
                    res.statusCode = 200;
                    res.body = nlohmann::json({
                        { "ok", true },
                        { "token", "jwt_mru_slot_" + std::to_string(idx) },
                        { "refresh_token", "rt_mru_slot_" + std::to_string(idx) },
                        { "expires_at", futureExpiry },
                        { "user", makeUserJson(idx) }
                    }).dump();
                    return res;
                }
            }
            res.statusCode = 401;
            res.body = R"({"ok":false,"error":"Unknown user"})";
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/refresh"))
        {
            const auto body = nlohmann::json::parse(req.body, nullptr, false);
            const std::string rt = body.value("refresh_token", "");
            for (int idx = 1; idx <= 6; ++idx)
            {
                if (rt == "rt_mru_slot_" + std::to_string(idx))
                {
                    res.statusCode = 200;
                    res.body = nlohmann::json({
                        { "ok", true },
                        { "token", "jwt_mru_slot_" + std::to_string(idx) },
                        { "refresh_token", "rt_mru_slot_" + std::to_string(idx) },
                        { "expires_at", futureExpiry },
                        { "user", makeUserJson(idx) }
                    }).dump();
                    return res;
                }
            }
            res.statusCode = 401;
            res.body = R"({"ok":false,"code":"session_expired","error":"Invalid refresh token"})";
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/me"))
        {
            const std::string bearer = ExtractBearerToken(req);
            for (int idx = 1; idx <= 6; ++idx)
            {
                if (bearer == "jwt_mru_slot_" + std::to_string(idx))
                {
                    res.statusCode = 200;
                    res.body = nlohmann::json({ { "ok", true }, { "user", makeUserJson(idx) } }).dump();
                    return res;
                }
            }
            res.statusCode = 401;
            res.body = R"({"ok":false,"error":"Invalid token"})";
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = env.dir / "data" / "pending_mru.json";
    const fs::path authPath  = env.dir / "config" / "auth_mru.json";

    APIClient client(mock, queuePath.string(), authPath.string());
    client.SetServerUrl("https://mru.sakura.test");

    for (int idx = 1; idx <= 6; ++idx)
    {
        const AuthResult r = client.Login(
            "MruPlayer_" + std::to_string(idx),
            "PlainSecretPassword_" + std::to_string(idx) + "!");
        REQUIRE(r.ok);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto slots = client.GetSavedAccounts();
    REQUIRE(slots.size() == AuthStore::MAX_SAVED_ACCOUNTS);
    REQUIRE(slots.size() == 5);
    REQUIRE(slots.front().userId == "uid_mru_6");
    REQUIRE(std::none_of(slots.begin(), slots.end(),
                         [](const SavedAccountEntry& e) { return e.userId == "uid_mru_1"; }));

    // Ensure zero passwords or plaintext tokens are persisted on disk
    const std::string mruDisk = ReadWholeFile(authPath);
    REQUIRE(mruDisk.find("PlainSecretPassword_") == std::string::npos);
    REQUIRE(mruDisk.find("password") == std::string::npos);
    REQUIRE(mruDisk.find("jwt_mru_slot_") == std::string::npos);
    REQUIRE(mruDisk.find("rt_mru_slot_") == std::string::npos);

    // Switch to saved account #3 without re-entering password
    const AuthResult swOk = client.SwitchSavedAccount("uid_mru_3");
    REQUIRE(swOk.ok);
    REQUIRE(client.IsAuthenticated());
    REQUIRE(client.GetCurrentUser().id == "uid_mru_3");
    REQUIRE(client.GetCurrentUser().username == "MruPlayer_3");
    REQUIRE_THAT(client.GetCurrentUser().sakuraRating, WithinAbs(13.0, 1e-4));
    REQUIRE(client.GetAuthToken() == "jwt_mru_slot_3");

    // Switching to evicted slot #1 must fail cleanly
    const AuthResult swEvicted = client.SwitchSavedAccount("uid_mru_1");
    REQUIRE(!swEvicted.ok);

    // RemoveSavedAccount removes slot #4 and returns false for unknown user
    REQUIRE(client.RemoveSavedAccount("uid_mru_4"));
    const auto afterRemoval = client.GetSavedAccounts();
    REQUIRE(afterRemoval.size() == 4);
    REQUIRE(std::none_of(afterRemoval.begin(), afterRemoval.end(),
                         [](const SavedAccountEntry& e) { return e.userId == "uid_mru_4"; }));
    REQUIRE(!client.RemoveSavedAccount("uid_mru_nonexistent"));

    client.Shutdown();
}

TEST_CASE("APIClient handles AuthenticatedOffline on network failure, single-flight 401 refresh across 4 threads, and SessionExpired", "[auth][v210]")
{
    ScopedV210TempEnv env;

    std::atomic<bool> networkDown{ false };
    std::atomic<bool> revokeRefreshToken{ false };
    std::atomic<int>  refreshCallCount{ 0 };
    std::atomic<int>  expiredBearerHits{ 0 };
    std::mutex              barrierMutex;
    std::condition_variable barrierCv;

    const std::string initialAccess  = "jwt_sf_initial_v210";
    const std::string initialRefresh = "rt_sf_initial_v210";
    const std::string rotatedAccess  = "jwt_sf_rotated_v210";
    const std::string rotatedRefresh = "rt_sf_rotated_v210";
    const long long   validExpiry    = NowUnixSeconds() + 7LL * 86400LL;

    const nlohmann::json survivorUser = {
        { "id", "u_sf_survivor" },
        { "username", "SingleFlightHero" },
        { "is_guest", false },
        { "total_pp", 3400.0 },
        { "sakura_rating", 16.55 },
        { "dan_badge", "皆传" }
    };

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (networkDown.load())
        {
            res.statusCode = 0;
            res.error      = "Simulated WinHTTP network unreachable";
            return res;
        }

        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "token", initialAccess },
                { "refresh_token", initialRefresh },
                { "expires_at", validExpiry },
                { "user", survivorUser }
            }).dump();
            return res;
        }

        if (req.url.ends_with("/api/v1/auth/refresh"))
        {
            const int callNum = ++refreshCallCount;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            if (revokeRefreshToken.load())
            {
                res.statusCode = 401;
                res.body = R"({"ok":false,"code":"session_expired","error":"Refresh token revoked"})";
                return res;
            }

            const auto body = nlohmann::json::parse(req.body, nullptr, false);
            const std::string sentRt = body.value("refresh_token", "");
            if (callNum == 1 && sentRt == initialRefresh)
            {
                res.statusCode = 200;
                res.body = nlohmann::json({
                    { "ok", true },
                    { "token", rotatedAccess },
                    { "refresh_token", rotatedRefresh },
                    { "expires_at", validExpiry },
                    { "user", survivorUser }
                }).dump();
                return res;
            }

            // Any duplicate refresh with the already-rotated token fails with 401
            res.statusCode = 401;
            res.body = R"({"ok":false,"code":"session_expired","error":"Refresh token replay"})";
            return res;
        }

        if (req.url.find("/api/v1/leaderboards/") != std::string::npos)
        {
            const std::string bearer = ExtractBearerToken(req);
            if (revokeRefreshToken.load())
            {
                res.statusCode = 401;
                res.body = R"({"ok":false,"error":"Access token expired"})";
                return res;
            }
            if (bearer == initialAccess)
            {
                const int arrived = ++expiredBearerHits;
                if (arrived >= 4)
                {
                    barrierCv.notify_all();
                }
                else
                {
                    std::unique_lock<std::mutex> lk(barrierMutex);
                    barrierCv.wait_for(lk, std::chrono::milliseconds(120),
                                       [&]() { return expiredBearerHits.load() >= 4; });
                }
                res.statusCode = 401;
                res.body = R"({"ok":false,"error":"Access token expired"})";
                return res;
            }
            if (bearer == rotatedAccess)
            {
                res.statusCode = 200;
                res.body = nlohmann::json({
                    { "ok", true },
                    { "my_rank", 1 },
                    { "entries", nlohmann::json::array({
                        { { "rank", 1 }, { "score_id", "sc_1" }, { "user_id", "u_sf_survivor" },
                          { "username", "SingleFlightHero" }, { "score", 998500 }, { "accuracy", 99.85 },
                          { "max_combo", 500 }, { "grade", "SS" }, { "pp", 320.0 },
                          { "sakura_rating", 16.55 }, { "dan_badge", "皆传" },
                          { "is_full_combo", true }, { "is_all_perfect", false },
                          { "has_replay", false }, { "played_at", 1750000000 } }
                    }) }
                }).dump();
                return res;
            }
            res.statusCode = 401;
            res.body = R"({"ok":false,"error":"Missing bearer"})";
            return res;
        }

        if (req.url.ends_with("/api/v1/auth/me"))
        {
            const std::string bearer = ExtractBearerToken(req);
            if (!revokeRefreshToken.load() && bearer == rotatedAccess)
            {
                res.statusCode = 200;
                res.body = nlohmann::json({ { "ok", true }, { "user", survivorUser } }).dump();
                return res;
            }
            res.statusCode = 401;
            res.body = R"({"ok":false,"error":"Token expired"})";
            return res;
        }

        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = env.dir / "data" / "pending_sf.json";
    const fs::path authPath  = env.dir / "config" / "auth_sf.json";

    APIClient client(mock, queuePath.string(), authPath.string());
    client.SetServerUrl("https://sf.sakura.test");

    const AuthResult loginRes = client.Login("SingleFlightHero", "HeroPass#210");
    REQUIRE(loginRes.ok);
    REQUIRE(client.GetAuthState() == AuthState::AuthenticatedOnline);

    // 1. Network failure (statusCode == 0) transitions to AuthenticatedOffline while keeping identity & tokens
    networkDown.store(true);
    const AuthResult offlineProf = client.RefreshProfile();
    REQUIRE(!offlineProf.ok);
    REQUIRE(client.GetAuthState() == AuthState::AuthenticatedOffline);
    REQUIRE(client.IsAuthenticated());
    REQUIRE(client.GetCurrentUser().id == "u_sf_survivor");
    REQUIRE(client.GetCurrentUser().username == "SingleFlightHero");
    REQUIRE_THAT(client.GetCurrentUser().sakuraRating, WithinAbs(16.55, 1e-4));
    REQUIRE(client.GetCurrentUser().danBadge == "皆传");
    REQUIRE(client.GetAuthToken() == initialAccess);
    REQUIRE(client.GetRefreshToken() == initialRefresh);

    // 2. Restore network: 4 concurrent std::async threads hit 401 -> single-flight refresh runs ONLY ONCE and all 4 succeed
    networkDown.store(false);
    std::vector<std::future<LeaderboardResult>> futures;
    futures.reserve(4);
    for (int t = 0; t < 4; ++t)
    {
        futures.push_back(std::async(std::launch::async, [&client, t]() {
            return client.FetchLeaderboard("sakura_breath", "Master", 10 + t);
        }));
    }

    for (auto& fut : futures)
    {
        const LeaderboardResult lb = fut.get();
        REQUIRE(lb.ok);
        REQUIRE(lb.myRank.has_value());
        REQUIRE(*lb.myRank == 1);
        REQUIRE(lb.entries.size() == 1);
        REQUIRE(lb.entries[0].username == "SingleFlightHero");
    }

    REQUIRE(refreshCallCount.load() == 1);
    REQUIRE(client.GetAuthState() == AuthState::AuthenticatedOnline);
    REQUIRE(client.GetAuthToken() == rotatedAccess);
    REQUIRE(client.GetRefreshToken() == rotatedRefresh);

    // 3. Refresh endpoint returning HTTP 401 transitions to AuthState::SessionExpired while keeping username
    revokeRefreshToken.store(true);
    refreshCallCount.store(0);

    const AuthResult expiredProf = client.RefreshProfile();
    REQUIRE(!expiredProf.ok);
    REQUIRE(refreshCallCount.load() == 1);
    REQUIRE(client.GetAuthState() == AuthState::SessionExpired);
    REQUIRE(!client.IsAuthenticated());
    REQUIRE(client.GetAuthToken().empty());
    REQUIRE(client.GetRefreshToken().empty());
    REQUIRE(client.GetCurrentUser().username == "SingleFlightHero");
    REQUIRE(client.GetAuthStore().GetSession().accessToken.empty());
    REQUIRE(client.GetAuthStore().GetSession().refreshToken.empty());
    REQUIRE(client.GetAuthStore().GetSession().profile.username == "SingleFlightHero");

    client.Shutdown();
}
