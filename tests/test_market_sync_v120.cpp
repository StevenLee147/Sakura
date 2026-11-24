#include "test_framework.h"

#include "core/config.h"
#include "data/database.h"
#include "game/achievement_manager.h"
#include "game/chart_loader.h"
#include "net/api_client.h"
#include "net/http_client.h"
#include "utils/archive.h"
#include "utils/file_io.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using sakura::core::Config;
using sakura::data::Database;
using sakura::game::ChartLoader;
using sakura::game::GameResult;
using sakura::game::Grade;
using sakura::net::APIClient;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::MarketInstallState;
using sakura::net::MarketQuery;
using sakura::net::MockHttpTransport;
using sakura::tests::Matchers::WithinAbs;
using sakura::utils::ComputeSha256Hex;
using sakura::utils::CreateZipArchive;

namespace
{
void WriteMinimalWav(const std::filesystem::path& path)
{
    constexpr int samples = 4410;
    std::ofstream wav(path, std::ios::binary);
    auto u16 = [&](uint16_t v) { wav.put(static_cast<char>(v & 0xFF)); wav.put(static_cast<char>((v >> 8) & 0xFF)); };
    auto u32 = [&](uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>((v >> 16) & 0xFFFF)); };
    wav.write("RIFF", 4); u32(36 + samples * 2);
    wav.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(44100); u32(88200); u16(2); u16(16);
    wav.write("data", 4); u32(samples * 2);
    for (int i = 0; i < samples; ++i) u16(0);
}
}

TEST_CASE("SHA-256 implementation matches FIPS 180-4 test vectors", "[market][v120]")
{
    REQUIRE(ComputeSha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(ComputeSha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("Chart Market client publishes, searches, verifies SHA-256, rejects tampering, and installs .sakura", "[market][v120]")
{
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "sakura_market_v120_test";
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    const fs::path srcChartDir = tmpRoot / "author_chart";
    const fs::path pkgPath     = tmpRoot / "moonlit_bloom.sakura";
    const fs::path userCharts  = tmpRoot / "user_charts";
    fs::create_directories(srcChartDir, ec);

    const nlohmann::json info = {
        { "version", 3 },
        { "id", "moonlit_bloom" },
        { "title", "Moonlit Bloom" },
        { "artist", "Sakura Sound" },
        { "charter", "CharterAoi" },
        { "music_file", "music.wav" },
        { "bpm", 168.0 },
        { "tags", { "featured", "stream" } },
        { "difficulties", nlohmann::json::array({
            { { "name", "Master" }, { "level", 11.5 }, { "chart_file", "master.json" } }
        }) }
    };
    const nlohmann::json chartData = {
        { "version", 3 },
        { "timing_points", nlohmann::json::array({ { { "time", 0 }, { "bpm", 168.0 } } }) },
        { "keyboard_notes", nlohmann::json::array({
            { { "time", 500 }, { "lane", 0 }, { "type", "tap" } },
            { { "time", 900 }, { "lane", 4 }, { "type", "tap" }, { "link_id", 1 } }
        }) },
        { "mouse_notes", nlohmann::json::array({
            { { "time", 900 }, { "x", 0.5 }, { "y", 0.5 }, { "type", "circle" }, { "link_id", 1 }, { "require_lane", 4 } }
        }) }
    };
    REQUIRE(sakura::utils::AtomicWrite(srcChartDir / "info.json", info.dump(2)));
    REQUIRE(sakura::utils::AtomicWrite(srcChartDir / "master.json", chartData.dump(2)));
    WriteMinimalWav(srcChartDir / "music.wav");
    REQUIRE(CreateZipArchive(srcChartDir, pkgPath));

    std::ifstream pkgIfs(pkgPath, std::ios::binary);
    const std::string pkgBlob((std::istreambuf_iterator<char>(pkgIfs)), std::istreambuf_iterator<char>());
    const std::string realSha = ComputeSha256Hex(pkgBlob);

    bool corruptDownload = false;
    std::string uploadedBody;

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true }, { "token", "tok-v120" },
                { "user", { { "id", "u-aoi" }, { "username", "CharterAoi" }, { "is_guest", false }, { "total_pp", 300.0 } } }
            }).dump();
            return res;
        }
        if (req.method == "POST" && req.url.find("/api/v1/charts") != std::string::npos && req.url.find("/rate") == std::string::npos)
        {
            uploadedBody = req.body;
            res.statusCode = 201;
            res.body = nlohmann::json({
                { "ok", true },
                { "chart", {
                    { "id", "moonlit_bloom" }, { "chart_slug", "moonlit_bloom" },
                    { "uploader_id", "u-aoi" }, { "uploader_name", "CharterAoi" },
                    { "title", "Moonlit Bloom" }, { "artist", "Sakura Sound" }, { "charter", "CharterAoi" },
                    { "bpm", 168.0 }, { "min_level", 11.5 }, { "max_level", 11.5 },
                    { "package_sha256", realSha }, { "package_size", pkgBlob.size() },
                    { "downloads", 0 }, { "rating_avg", 0.0 }, { "rating_count", 0 }
                } }
            }).dump();
            return res;
        }
        if (req.method == "GET" && req.url.find("/api/v1/charts?") != std::string::npos)
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true }, { "total", 1 },
                { "charts", nlohmann::json::array({
                    {
                        { "id", "moonlit_bloom" }, { "chart_slug", "moonlit_bloom" },
                        { "uploader_id", "u-aoi" }, { "uploader_name", "CharterAoi" },
                        { "title", "Moonlit Bloom" }, { "artist", "Sakura Sound" }, { "charter", "CharterAoi" },
                        { "bpm", 168.0 }, { "min_level", 11.5 }, { "max_level", 11.5 },
                        { "package_sha256", realSha }, { "package_size", pkgBlob.size() },
                        { "downloads", 3 }, { "rating_avg", 5.0 }, { "rating_count", 1 },
                        { "difficulties", nlohmann::json::array({ { { "name", "Master" }, { "level", 11.5 }, { "chart_file", "master.json" }, { "note_count", 3 } } }) }
                    }
                }) }
            }).dump();
            return res;
        }
        if (req.method == "POST" && req.url.ends_with("/rate"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "rating_avg", 5.0 }, { "rating_count", 1 }, { "my_rating", 5 } }).dump();
            return res;
        }
        if (req.method == "GET" && req.url.ends_with("/download"))
        {
            res.statusCode = 200;
            res.headers.emplace_back("X-Sakura-Sha256", realSha);
            res.body = corruptDownload ? (pkgBlob + "tampered") : pkgBlob;
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    Config::GetInstance().Load((tmpRoot / "settings.json").string());
    APIClient client(mock, (tmpRoot / "queue.json").string());
    client.SetServerUrl("http://127.0.0.1:233");
    REQUIRE(client.Login("CharterAoi", "pw123456").ok);

    const auto pub = client.PublishChartPackage(pkgPath.string(), "Chart v3 synergy test");
    REQUIRE(pub.ok);
    REQUIRE(uploadedBody == pkgBlob);
    REQUIRE(pub.chart.id == "moonlit_bloom");

    MarketQuery mq;
    mq.q = "Moonlit";
    const auto list = client.FetchMarketCharts(mq);
    REQUIRE(list.ok);
    REQUIRE(list.charts.size() == 1);
    REQUIRE(APIClient::GetInstalledMarketState(list.charts[0], userCharts.string()) == MarketInstallState::NotInstalled);

    const auto rate = client.RateMarketChart("moonlit_bloom", 5, "Great chart!");
    REQUIRE(rate.ok);
    REQUIRE(rate.myRating == 5);

    // Tampered payload must be blocked by SHA-256 check
    corruptDownload = true;
    const auto blocked = client.DownloadAndInstallChart(list.charts[0], userCharts.string());
    REQUIRE(!blocked.ok);

    // Valid payload installs cleanly and reports Installed state
    corruptDownload = false;
    float maxProg = 0.0f;
    const auto installed = client.DownloadAndInstallChart(list.charts[0], userCharts.string(), [&](float p) { maxProg = std::max(maxProg, p); });
    REQUIRE(installed.ok);
    REQUIRE_THAT(maxProg, WithinAbs(1.0, 0.01));
    REQUIRE(installed.chartId == "moonlit_bloom");
    REQUIRE(APIClient::GetInstalledMarketState(list.charts[0], userCharts.string()) == MarketInstallState::Installed);

    ChartLoader loader;
    const auto scanned = loader.ScanCharts(userCharts.string());
    REQUIRE(scanned.size() == 1);
    REQUIRE(scanned[0].id == "moonlit_bloom");

    fs::remove_all(tmpRoot, ec);
}

TEST_CASE("Cloud Sync merges max scores, earliest achievements, and core settings idempotently", "[sync][v120]")
{
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "sakura_sync_v120_test";
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    fs::create_directories(tmpRoot, ec);

    auto& db  = Database::GetInstance();
    auto& cfg = Config::GetInstance();
    db.Shutdown();
    REQUIRE(db.Initialize((tmpRoot / "fresh_device.db").string()));
    cfg.Load((tmpRoot / "fresh_settings.json").string());
    cfg.Set("gameplay.note_speed", 5.0f);
    cfg.Set("online.settings_updated_at", 100LL);

    // Seed a lower local score that should be upgraded by cloud pull
    GameResult localLow;
    localLow.chartId         = "sakura_breath";
    localLow.chartTitle      = "Sakura Breath";
    localLow.chartHash       = "hash-sb";
    localLow.difficulty      = "Expert";
    localLow.difficultyLevel = 10.0f;
    localLow.score           = 910000;
    localLow.accuracy        = 94.5f;
    localLow.maxCombo        = 150;
    localLow.grade           = Grade::A;
    localLow.perfectCount    = 180;
    localLow.greatCount      = 20;
    localLow.playedAt        = 1700000100;
    REQUIRE(db.SaveScore(localLow));

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        if (req.url.ends_with("/api/v1/auth/login"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true }, { "token", "sync-tok" },
                { "user", { { "id", "u-sync" }, { "username", "SyncUser" }, { "is_guest", false }, { "total_pp", 200.0 } } }
            }).dump();
            return res;
        }
        if (req.url.ends_with("/api/v1/sync/push"))
        {
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "synced_at", 1700000999 },
                { "scores_merged", 1 },
                { "achievements_merged", 1 },
                { "scores", nlohmann::json::array({
                    {
                        { "chart_id", "sakura_breath" }, { "chart_title", "Sakura Breath" }, { "chart_hash", "hash-sb" },
                        { "difficulty", "Expert" }, { "difficulty_level", 10.0 }, { "score", 992500 }, { "accuracy", 99.4 },
                        { "max_combo", 200 }, { "grade", "SS" }, { "perfect_count", 198 }, { "great_count", 2 },
                        { "good_count", 0 }, { "bad_count", 0 }, { "miss_count", 0 },
                        { "is_full_combo", true }, { "is_all_perfect", false }, { "played_at", 1700000500 }
                    }
                }) },
                { "achievements", nlohmann::json::array({
                    { { "id", "first_fc" }, { "unlocked_at", 1700000300 } }
                }) },
                { "settings", { { "note_speed", 7.4 }, { "global_offset_ms", -12 } } },
                { "settings_updated_at", 1700000800 }
            }).dump();
            return res;
        }
        res.statusCode = 404;
        return res;
    });

    APIClient client(mock, (tmpRoot / "queue.json").string());
    client.SetServerUrl("http://127.0.0.1:233");
    REQUIRE(client.Login("SyncUser", "pw123456").ok);

    const auto sync1 = client.SyncCloudSave();
    REQUIRE(sync1.ok);
    REQUIRE(sync1.scoresPulled == 1);
    REQUIRE(sync1.achievementsPulled == 1);

    const auto bestAfter = db.GetBestScore("sakura_breath", "Expert", "hash-sb");
    REQUIRE(bestAfter.has_value());
    REQUIRE(bestAfter->score == 992500);
    REQUIRE(db.IsAchievementUnlocked("first_fc"));
    REQUIRE_THAT(cfg.Get<float>("gameplay.note_speed", 5.0f), WithinAbs(7.4, 0.01));
    REQUIRE(cfg.Get<int>("audio.global_offset_ms", 0) == -12);

    // Second sync is idempotent (0 newly pulled items)
    const auto sync2 = client.SyncCloudSave();
    REQUIRE(sync2.ok);
    REQUIRE(sync2.scoresPulled == 0);
    REQUIRE(sync2.achievementsPulled == 0);

    db.Shutdown();
    fs::remove_all(tmpRoot, ec);
}
