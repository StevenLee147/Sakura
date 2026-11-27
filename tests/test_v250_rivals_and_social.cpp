// tests/test_v250_rivals_and_social.cpp — v2.5.0 APIClient Player Profiles, Rivals, Rival-Scoped Leaderboards & Score Delta unit tests

#include "test_framework.h"

#include "core/config.h"
#include "net/api_client.h"
#include "net/http_client.h"

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;

using sakura::net::APIClient;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::MockHttpTransport;

TEST_CASE("APIClient v2.5 serializes and parses profiles, rivals, rival-scoped leaderboards, ladder, and delta pills", "[net][v250]")
{
    REQUIRE(APIClient::FormatScoreDeltaText("BobMoonlit", 994200, 980000) == "VS BobMoonlit +14,200");
    REQUIRE(APIClient::FormatScoreDeltaText("BobMoonlit", 973150, 980000) == "VS BobMoonlit -6,850");
    REQUIRE(APIClient::FormatScoreDeltaText("BobMoonlit", 980000, 980000) == "VS BobMoonlit +0");

    std::error_code ec;
    const fs::path tempDir = fs::current_path() / "build" / "qa" / "test_v250_social";
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    std::string capturedLbUrl;
    std::string capturedPatchBody;
    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        res.statusCode = 200;
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.body = R"({"ok":true,"token":"jwt_v250","refresh_token":"rt_v250","expires_at":1999999999,"user":{"id":"u_me","username":"Alice","is_guest":false,"total_pp":1200.5,"sakura_rating":13.42,"dan_badge":"皆传"}})";
            return res;
        }
        if (req.method == "PATCH" && req.url.ends_with("/api/v1/users/me/profile"))
        {
            capturedPatchBody = req.body;
            res.body = R"({"ok":true,"profile":{"user_id":"u_me","username":"Alice","is_guest":false,"bio":"夜樱之下","featured_title":"月夜剑心","dan_badge":"皆传","sakura_rating":13.42,"total_pp":1200.5,"play_count":42,"follower_count":3,"rival_count":1,"is_rival":false,"is_mutual":false,"created_at":1700000000,"best_scores":[{"score_id":"sc_1","chart_id":"sakura_sprint","chart_title":"樱语疾行","difficulty":"Expert","difficulty_level":13.0,"score":995000,"accuracy":99.5,"max_combo":600,"grade":"S","pp":310.2,"chart_rating":14.56,"is_full_combo":true,"is_all_perfect":false,"has_replay":true,"played_at":1700000100}]}})";
            return res;
        }
        if (req.method == "GET" && req.url.find("/api/v1/users/u_bob/profile") != std::string::npos)
        {
            res.body = R"({"ok":true,"profile":{"user_id":"u_bob","username":"Bob","is_guest":false,"bio":"Rival bio","featured_title":"绯霞旅人","dan_badge":"十段","sakura_rating":14.10,"total_pp":1500.0,"play_count":88,"follower_count":5,"rival_count":4,"is_rival":true,"is_mutual":true,"created_at":1700000000,"best_scores":[]}})";
            return res;
        }
        if (req.method == "POST" && req.url.ends_with("/api/v1/rivals/u_bob"))
        {
            res.body = R"({"ok":true,"is_rival":true,"is_mutual":true,"rival_count":1,"follower_count":6})";
            return res;
        }
        if (req.method == "GET" && req.url.ends_with("/api/v1/rivals"))
        {
            res.body = R"({"ok":true,"total":1,"rivals":[{"user_id":"u_bob","username":"Bob","bio":"Rival bio","featured_title":"绯霞旅人","dan_badge":"十段","sakura_rating":14.10,"total_pp":1500.0,"play_count":88,"is_mutual":true,"added_at":1700000200}]})";
            return res;
        }
        if (req.method == "GET" && req.url.find("/api/v1/leaderboards/sakura_sprint") != std::string::npos)
        {
            capturedLbUrl = req.url;
            res.body = R"({"ok":true,"chart_id":"sakura_sprint","difficulty":"Expert","scope":"rivals","my_rank":2,"entries":[{"rank":1,"score_id":"sc_b","user_id":"u_bob","username":"Bob","dan_badge":"十段","score":998000,"accuracy":99.8,"max_combo":600,"grade":"SS","pp":330.0,"sakura_rating":14.10,"is_full_combo":true,"is_all_perfect":false,"has_replay":true,"played_at":1700000300}]})";
            return res;
        }
        if (req.method == "GET" && req.url.find("/api/v1/rankings/rating") != std::string::npos)
        {
            res.body = R"({"ok":true,"my_rank":2,"my_rating":13.42,"entries":[{"rank":1,"user_id":"u_bob","username":"Bob","featured_title":"绯霞旅人","dan_badge":"十段","sakura_rating":14.10,"total_pp":1500.0,"play_count":88,"is_rival":true},{"rank":2,"user_id":"u_me","username":"Alice","featured_title":"月夜剑心","dan_badge":"皆传","sakura_rating":13.42,"total_pp":1200.5,"play_count":42,"is_rival":false}]})";
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    APIClient client(mock, (tempDir / "q.json").string(), (tempDir / "auth.json").string());
    client.SetServerUrl("https://social.sakura.test");
    REQUIRE(client.Login("Alice", "pass123").ok);

    const auto upd = client.UpdateMyProfile("夜樱之下", "月夜剑心");
    REQUIRE(upd.ok);
    REQUIRE(upd.profile.bio == "夜樱之下");
    REQUIRE(upd.profile.featuredTitle == "月夜剑心");
    REQUIRE(upd.profile.bestScores.size() == 1);
    REQUIRE(upd.profile.bestScores[0].chartId == "sakura_sprint");
    REQUIRE(capturedPatchBody.find("featured_title") != std::string::npos);

    const auto addRes = client.AddRival("u_bob");
    REQUIRE(addRes.ok);
    REQUIRE(addRes.isRival);
    REQUIRE(addRes.isMutual);

    const auto bobProf = client.FetchUserProfile("u_bob");
    REQUIRE(bobProf.ok);
    REQUIRE(bobProf.profile.isMutual);
    REQUIRE(bobProf.profile.featuredTitle == "绯霞旅人");

    const auto rivals = client.FetchRivals();
    REQUIRE(rivals.ok);
    REQUIRE(rivals.rivals.size() == 1);
    REQUIRE(rivals.rivals[0].username == "Bob");

    const auto rivalLb = client.FetchLeaderboard("sakura_sprint", "Expert", 10, "rivals");
    REQUIRE(rivalLb.ok);
    REQUIRE(capturedLbUrl.find("scope=rivals") != std::string::npos);
    REQUIRE(rivalLb.myRank.has_value());
    REQUIRE(*rivalLb.myRank == 2);
    REQUIRE(rivalLb.entries[0].username == "Bob");

    const auto ladder = client.FetchRatingLadder(50);
    REQUIRE(ladder.ok);
    REQUIRE(ladder.myRank.has_value());
    REQUIRE(*ladder.myRank == 2);
    REQUIRE(ladder.entries.size() == 2);
    REQUIRE(ladder.entries[0].isRival);

    client.Shutdown();
    fs::remove_all(tempDir, ec);
}
