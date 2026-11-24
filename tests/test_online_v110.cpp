#include "test_framework.h"

#include "core/updater.h"
#include "net/api_client.h"
#include "net/http_client.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using sakura::core::SemVer;
using sakura::core::Updater;
using sakura::net::APIClient;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::MockHttpTransport;
using sakura::net::ParseHttpUrl;
using sakura::net::UrlEncode;
using sakura::tests::Matchers::WithinAbs;

TEST_CASE("HTTP URL parser and SemVer comparator handle prereleases and ports", "[online][v110]")
{
    const auto localUrl = ParseHttpUrl("http://127.0.0.1:8787/api/v1/health?mode=1");
    REQUIRE(localUrl.has_value());
    REQUIRE(!localUrl->secure);
    REQUIRE(localUrl->host == "127.0.0.1");
    REQUIRE(localUrl->port == 8787);
    REQUIRE(localUrl->pathAndQuery == "/api/v1/health?mode=1");

    const auto ghUrl = ParseHttpUrl("https://api.github.com/repos/StevenLee147/Sakura/releases/latest");
    REQUIRE(ghUrl.has_value());
    REQUIRE(ghUrl->secure);
    REQUIRE(ghUrl->host == "api.github.com");
    REQUIRE(ghUrl->port == 443);
    REQUIRE(ghUrl->pathAndQuery == "/repos/StevenLee147/Sakura/releases/latest");

    REQUIRE(!ParseHttpUrl("ftp://example.com").has_value());
    REQUIRE(UrlEncode("Expert+") == "Expert%2B");

    const auto vAlpha  = SemVer::Parse("v0.8.0-alpha");
    const auto vBeta   = SemVer::Parse("0.9.1-beta");
    const auto vStable = SemVer::Parse("v1.0.0");
    const auto v100rc  = SemVer::Parse("v1.0.0-rc.1");
    const auto vNext   = SemVer::Parse("v1.1.0");

    REQUIRE(vAlpha.has_value());
    REQUIRE(vBeta.has_value());
    REQUIRE(vStable.has_value());
    REQUIRE(v100rc.has_value());
    REQUIRE(vNext.has_value());

    REQUIRE(*vAlpha < *vBeta);
    REQUIRE(*v100rc < *vStable);
    REQUIRE(*vStable < *vNext);
    REQUIRE(!(*vNext < *vStable));
}

TEST_CASE("Updater parses GitHub Releases and custom version payloads", "[online][v110]")
{
    const nlohmann::json ghPayload = {
        { "tag_name", "v1.2.0" },
        { "html_url", "https://github.com/StevenLee147/Sakura/releases/tag/v1.2.0" },
        { "body", "Chart Market & Cloud Sync" },
        { "assets", nlohmann::json::array({
            { { "name", "Sakura-v1.2.0-win64.zip" }, { "browser_download_url", "https://example.com/sakura.zip" } },
            { { "name", "Sakura-v1.2.0-win64-setup.exe" }, { "browser_download_url", "https://example.com/sakura-setup.exe" } }
        }) }
    };

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest&) {
        HttpResponse r;
        r.statusCode = 200;
        r.body       = ghPayload.dump();
        return r;
    });

    const auto resNewer = Updater::GetInstance().CheckNow("1.1.0", mock, "https://api.github.com/repos/StevenLee147/Sakura/releases/latest");
    REQUIRE(resNewer.ok);
    REQUIRE(resNewer.hasUpdate);
    REQUIRE(resNewer.latestVer == "v1.2.0");
    REQUIRE(resNewer.downloadUrl == "https://example.com/sakura-setup.exe");

    const auto resSame = Updater::GetInstance().CheckNow("v1.2.0", mock, "https://api.github.com/repos/StevenLee147/Sakura/releases/latest");
    REQUIRE(resSame.ok);
    REQUIRE(!resSame.hasUpdate);
}

TEST_CASE("APIClient handles auth, offline score queue flush, leaderboard, and replay download", "[online][v110]")
{
    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path() / "sakura_online_v110_test";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    bool serverOnline = false;
    int submittedCount = 0;
    const std::string fakeReplay = R"({"format":"sakura-replay","version":2,"chart_id":"sakura_breath","chart_hash":"999","difficulty":1,"rate":1.0,"start_ms":0,"inputs":[[500,0,0,0,0]]})";

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (!serverOnline)
        {
            res.statusCode = 0;
            res.error      = "Connection refused";
            return res;
        }
        if (req.url.ends_with("/api/v1/auth/register"))
        {
            res.statusCode = 201;
            res.body = nlohmann::json({
                { "ok", true },
                { "token", "jwt-token-123" },
                { "user", { { "id", "u-1" }, { "username", "SakuraTester" }, { "is_guest", false }, { "total_pp", 0.0 } } }
            }).dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/scores"))
        {
            ++submittedCount;
            res.statusCode = 201;
            res.body = nlohmann::json({
                { "ok", true },
                { "score_id", "score-uuid-1" },
                { "rank", 1 },
                { "pp", 215.5 },
                { "total_pp", 215.5 },
                { "new_best", true }
            }).dump();
            return res;
        }
        if (req.url.find("/api/v1/leaderboards/") != std::string::npos)
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "my_rank", 1 },
                { "entries", nlohmann::json::array({
                    { { "rank", 1 }, { "score_id", "score-uuid-1" }, { "user_id", "u-1" },
                      { "username", "SakuraTester" }, { "score", 992000 }, { "accuracy", 99.2 },
                      { "max_combo", 310 }, { "grade", "S" }, { "pp", 215.5 },
                      { "is_full_combo", true }, { "is_all_perfect", false }, { "has_replay", true }, { "played_at", 1700000000 } }
                }) }
            }).dump();
            return res;
        }
        if (req.url.find("/api/v1/replays/score-uuid-1") != std::string::npos)
        {
            res.statusCode = 200;
            res.body       = fakeReplay;
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    const fs::path queuePath = tmpDir / "pending_scores.json";
    APIClient client(mock, queuePath.string());
    client.SetServerUrl("http://127.0.0.1:8787");

    sakura::game::GameResult play;
    play.chartId        = "sakura_breath";
    play.chartTitle     = "Sakura Breath";
    play.chartHash      = "999";
    play.difficulty     = "Expert";
    play.difficultyLevel = 9.0f;
    play.score          = 992000;
    play.accuracy       = 99.2f;
    play.maxCombo       = 310;
    play.grade          = sakura::game::Grade::S;
    play.perfectCount   = 305;
    play.greatCount     = 5;
    play.totalJudgments = 310;
    play.isFullCombo    = true;
    play.assisted       = false;

    // 1. Offline score gets queued locally
    const auto offlineSub = client.SubmitScore(play);
    REQUIRE(!offlineSub.ok);
    REQUIRE(offlineSub.queuedOffline);
    REQUIRE(client.GetPendingScoreCount() == 1);

    // 2. Come online, register, and flush pending queue
    serverOnline = true;
    const auto reg = client.Register("SakuraTester", "password123");
    REQUIRE(reg.ok);
    REQUIRE(client.IsAuthenticated());
    REQUIRE(client.GetCurrentUser().username == "SakuraTester");

    const int flushed = client.FlushPendingScores();
    REQUIRE(flushed == 1);
    REQUIRE(client.GetPendingScoreCount() == 0);
    REQUIRE(submittedCount == 1);

    // 3. Direct online score submit + leaderboard + replay download
    const auto onlineSub = client.SubmitScore(play);
    REQUIRE(onlineSub.ok);
    REQUIRE(onlineSub.rank == 1);
    REQUIRE_THAT(onlineSub.pp, WithinAbs(215.5, 0.01));

    const auto lb = client.FetchLeaderboard("sakura_breath", "Expert", 10);
    REQUIRE(lb.ok);
    REQUIRE(lb.myRank.has_value());
    REQUIRE(*lb.myRank == 1);
    REQUIRE(lb.entries.size() == 1);
    REQUIRE(lb.entries[0].hasReplay);

    const fs::path dlReplayPath = tmpDir / "dl.skr";
    REQUIRE(client.DownloadReplay("score-uuid-1", dlReplayPath.string()));
    std::ifstream ifs(dlReplayPath);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    REQUIRE(content == fakeReplay);

    fs::remove_all(tmpDir, ec);
}
