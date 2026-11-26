// tests/test_v200_updater_and_career.cpp — v2.0.0 Atomic Self-Updater & Sakura Rating (B30 + R10) unit tests

#include "test_framework.h"

#include "core/updater.h"
#include "game/pp_calculator.h"
#include "net/http_client.h"
#include "utils/archive.h"
#include "utils/file_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

using sakura::core::StagedUpdateResult;
using sakura::core::UpdateCheckResult;
using sakura::core::UpdateInstallMode;
using sakura::core::Updater;
using sakura::game::GameResult;
using sakura::game::Grade;
using sakura::game::PPCalculator;
using sakura::game::RatingSummary;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::IHttpTransport;
using sakura::tests::Matchers::WithinAbs;

namespace
{

struct ScopedV200TempEnv
{
    fs::path dir;

    ScopedV200TempEnv()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v200_" + std::to_string(stamp));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }

    ~ScopedV200TempEnv()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

class MockUpdateHttpTransport final : public IHttpTransport
{
public:
    std::string sumsUrl;
    std::string sumsBody;
    std::string zipUrl;
    std::string zipBody;

    HttpResponse Send(const HttpRequest& request) override
    {
        HttpResponse res;
        if (request.url == sumsUrl)
        {
            res.statusCode = 200;
            res.body       = sumsBody;
            if (request.onProgress)
                request.onProgress(sumsBody.size(), sumsBody.size());
            return res;
        }
        if (request.url == zipUrl)
        {
            res.statusCode = 200;
            res.body       = zipBody;
            if (request.onProgress)
            {
                const std::size_t half = zipBody.size() / 2;
                request.onProgress(half, zipBody.size());
                request.onProgress(zipBody.size(), zipBody.size());
            }
            return res;
        }
        res.statusCode = 404;
        res.error      = "Not Found";
        return res;
    }
};

GameResult MakeCareerPlay(std::string chartId,
                          std::string difficulty,
                          float level,
                          float accuracy,
                          int score,
                          bool isFullCombo = false,
                          bool isAllPerfect = false,
                          bool assisted = false,
                          long long playedAt = 1700000000LL)
{
    GameResult r;
    r.chartId         = std::move(chartId);
    r.chartTitle      = r.chartId;
    r.difficulty      = std::move(difficulty);
    r.difficultyLevel = level;
    r.accuracy        = accuracy;
    r.score           = score;
    r.grade           = accuracy >= 99.0f ? Grade::SS : (accuracy >= 95.0f ? Grade::S : Grade::A);
    r.isFullCombo     = isFullCombo;
    r.isAllPerfect    = isAllPerfect;
    r.assisted        = assisted;
    r.playbackRate    = 1.0f;
    r.playedAt        = playedAt;
    return r;
}

} // namespace

TEST_CASE("Updater::ParseReleaseJson extracts portable, installer, and SHA256SUMS URLs from GitHub and API payloads", "[updater][v200]")
{
    const std::string expectedZip   = "https://github.com/StevenLee147/Sakura/releases/download/v2.0.0/Sakura-v2.0.0-win64.zip";
    const std::string expectedSetup = "https://github.com/StevenLee147/Sakura/releases/download/v2.0.0/Sakura-v2.0.0-win64-setup.exe";
    const std::string expectedSums  = "https://github.com/StevenLee147/Sakura/releases/download/v2.0.0/Sakura-v2.0.0-SHA256SUMS.txt";

    const nlohmann::json ghRelease = {
        { "tag_name", "v2.0.0" },
        { "html_url", "https://github.com/StevenLee147/Sakura/releases/tag/v2.0.0" },
        { "body", "Sakura v2.0.0 Full-Loop Auto-Updater & Career Rating" },
        { "assets", nlohmann::json::array({
            { { "name", "Sakura-v2.0.0-win64.zip" }, { "browser_download_url", expectedZip } },
            { { "name", "Sakura-v2.0.0-win64-setup.exe" }, { "browser_download_url", expectedSetup } },
            { { "name", "Sakura-v2.0.0-SHA256SUMS.txt" }, { "browser_download_url", expectedSums } }
        }) }
    };

    const UpdateCheckResult fromGh = Updater::ParseReleaseJson(ghRelease.dump(), "v1.7.0");
    REQUIRE(fromGh.ok);
    REQUIRE(fromGh.hasUpdate);
    REQUIRE(fromGh.latestVer == "v2.0.0");
    REQUIRE(fromGh.portableZipUrl == expectedZip);
    REQUIRE(fromGh.installerExeUrl == expectedSetup);
    REQUIRE(fromGh.sha256SumsUrl == expectedSums);

    const nlohmann::json apiVersion = {
        { "tag_name", "v2.0.0" },
        { "latest_version", "v2.0.0" },
        { "html_url", "https://github.com/StevenLee147/Sakura/releases/tag/v2.0.0" },
        { "download_url", expectedSetup },
        { "portable_zip_url", expectedZip },
        { "installer_exe_url", expectedSetup },
        { "sha256_sums_url", expectedSums },
        { "body", "Sakura v2.0.0 Server Version Endpoint" }
    };

    const UpdateCheckResult fromApi = Updater::ParseReleaseJson(apiVersion.dump(), "v1.7.0");
    REQUIRE(fromApi.ok);
    REQUIRE(fromApi.hasUpdate);
    REQUIRE(fromApi.portableZipUrl.ends_with("-win64.zip"));
    REQUIRE(fromApi.installerExeUrl.ends_with("-win64-setup.exe"));
    REQUIRE(fromApi.sha256SumsUrl.ends_with("-SHA256SUMS.txt"));
    REQUIRE(fromApi.portableZipUrl == expectedZip);
    REQUIRE(fromApi.installerExeUrl == expectedSetup);
    REQUIRE(fromApi.sha256SumsUrl == expectedSums);
}

TEST_CASE("Updater::ParseSha256Sums, DetectInstallMode, and BuildHotSwapPowerShellScript validate safety contracts", "[updater][v200]")
{
    const std::string zipDigest   = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string setupDigest = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

    const std::string sumsContent =
        "   " + zipDigest + "  Sakura-v2.0.0-win64.zip\r\n"
        "\r\n"
        + setupDigest + " *Sakura-v2.0.0-win64-setup.exe   \r\n";

    const auto table = Updater::ParseSha256Sums(sumsContent);
    REQUIRE(table.size() == 2);
    REQUIRE(table.at("sakura-v2.0.0-win64.zip") == zipDigest);
    REQUIRE(table.at("sakura-v2.0.0-win64-setup.exe") == setupDigest);
    REQUIRE(Updater::FindExpectedSha256(sumsContent, "Sakura-v2.0.0-win64.zip") == zipDigest);
    REQUIRE(Updater::FindExpectedSha256(sumsContent, "Sakura-v2.0.0-win64-setup.exe") == setupDigest);
    REQUIRE(Updater::FindExpectedSha256(sumsContent, "unknown.bin").empty());

    ScopedV200TempEnv env;
    const fs::path portableDir  = env.dir / "portable_install";
    const fs::path installerDir = env.dir / "inno_install";
    fs::create_directories(portableDir);
    fs::create_directories(installerDir);
    REQUIRE(sakura::utils::AtomicWrite(portableDir / "Sakura.exe", "MZ"));
    REQUIRE(sakura::utils::AtomicWrite(installerDir / "Sakura.exe", "MZ"));
    REQUIRE(sakura::utils::AtomicWrite(installerDir / "unins000.exe", "MZ-UNINSTALL"));

    REQUIRE(Updater::DetectInstallMode(portableDir) == UpdateInstallMode::Portable);
    REQUIRE(Updater::DetectInstallMode(installerDir) == UpdateInstallMode::Installer);

    const std::string portablePs1 = Updater::BuildHotSwapPowerShellScript(
        4242U, portableDir, env.dir / "staged_payload", UpdateInstallMode::Portable, true);
    REQUIRE(portablePs1.find("4242") != std::string::npos);
    REQUIRE(portablePs1.find("Get-Process") != std::string::npos || portablePs1.find("Wait-Process") != std::string::npos);
    REQUIRE(portablePs1.find(".update_backup") != std::string::npos);
    REQUIRE(portablePs1.find("userdata") != std::string::npos);
    REQUIRE(portablePs1.find("portable.txt") != std::string::npos);
    REQUIRE(portablePs1.find("Sakura.exe") != std::string::npos);

    const std::string installerPs1 = Updater::BuildHotSwapPowerShellScript(
        4242U, installerDir, env.dir / "Sakura-v2.0.0-win64-setup.exe", UpdateInstallMode::Installer, true);
    REQUIRE(installerPs1.find("/SILENT") != std::string::npos);
    REQUIRE(installerPs1.find("/CLOSEAPPLICATIONS") != std::string::npos);
    REQUIRE(installerPs1.find("/RESTARTAPPLICATIONS") != std::string::npos);
}

TEST_CASE("Updater::DownloadAndStageUpdate stages verified portable zip and rejects tampered SHA256SUMS", "[updater][v200]")
{
    ScopedV200TempEnv env;

    // Build a real mini portable .zip containing Sakura-v2.0.0-windows-x64/Sakura.exe & VERSION.json
    const fs::path zipSrcRoot = env.dir / "zip_src";
    const fs::path innerRoot  = zipSrcRoot / "Sakura-v2.0.0-windows-x64";
    fs::create_directories(innerRoot);
    REQUIRE(sakura::utils::AtomicWrite(innerRoot / "Sakura.exe", "MZ-SAKURA-PORTABLE-V200-EXE"));
    REQUIRE(sakura::utils::AtomicWrite(innerRoot / "VERSION.json", R"({"version":"2.0.0","channel":"stable"})"));

    const fs::path zipArchivePath = env.dir / "Sakura-v2.0.0-win64.zip";
    REQUIRE(sakura::utils::CreateZipArchive(zipSrcRoot, zipArchivePath));

    std::ifstream zipIn(zipArchivePath, std::ios::binary);
    REQUIRE(static_cast<bool>(zipIn));
    const std::string zipBinary((std::istreambuf_iterator<char>(zipIn)), std::istreambuf_iterator<char>());
    REQUIRE(!zipBinary.empty());

    const std::string realZipSha256 = sakura::utils::ComputeSha256Hex(zipBinary);
    REQUIRE(realZipSha256.size() == 64);

    UpdateCheckResult check;
    check.checked         = true;
    check.ok              = true;
    check.hasUpdate       = true;
    check.currentVer      = "v1.7.0";
    check.latestVer       = "v2.0.0";
    check.portableZipUrl  = "https://updates.sakura.test/v2.0.0/Sakura-v2.0.0-win64.zip";
    check.installerExeUrl = "https://updates.sakura.test/v2.0.0/Sakura-v2.0.0-win64-setup.exe";
    check.sha256SumsUrl   = "https://updates.sakura.test/v2.0.0/Sakura-v2.0.0-SHA256SUMS.txt";
    check.downloadUrl     = check.portableZipUrl;

    auto transport     = std::make_shared<MockUpdateHttpTransport>();
    transport->sumsUrl = check.sha256SumsUrl;
    transport->zipUrl  = check.portableZipUrl;
    transport->zipBody = zipBinary;
    transport->sumsBody = realZipSha256 + " *Sakura-v2.0.0-win64.zip\r\n";

    const fs::path appDir     = env.dir / "live_app";
    const fs::path stagingDir = env.dir / "staging_ok";
    fs::create_directories(appDir);

    float maxProgress = 0.0f;
    float lastProgress = 0.0f;
    const StagedUpdateResult staged = Updater::GetInstance().DownloadAndStageUpdate(
        check,
        UpdateInstallMode::Portable,
        appDir,
        stagingDir,
        transport,
        [&](float p)
        {
            maxProgress  = std::max(maxProgress, p);
            lastProgress = p;
        },
        9876U);

    REQUIRE(staged.ok);
    REQUIRE(staged.sha256Verified);
    REQUIRE(staged.sha256Hex == realZipSha256);
    REQUIRE(!staged.payloadPath.empty());
    REQUIRE(fs::is_regular_file(fs::path(staged.payloadPath) / "Sakura.exe"));
    REQUIRE(fs::is_regular_file(fs::path(staged.payloadPath) / "VERSION.json"));
    REQUIRE(!staged.scriptPath.empty());
    REQUIRE(fs::path(staged.scriptPath).filename() == "apply_update.ps1");
    REQUIRE(fs::is_regular_file(fs::path(staged.scriptPath)));
    REQUIRE_THAT(maxProgress, WithinAbs(1.0, 1e-4));
    REQUIRE_THAT(lastProgress, WithinAbs(1.0, 1e-4));

    // Tampered SHA256SUMS.txt must be rejected and cleaned up
    transport->sumsBody = std::string(64, 'a') + " *Sakura-v2.0.0-win64.zip\r\n";
    const fs::path badStagingDir = env.dir / "staging_tampered";
    const StagedUpdateResult badStaged = Updater::GetInstance().DownloadAndStageUpdate(
        check,
        UpdateInstallMode::Portable,
        appDir,
        badStagingDir,
        transport,
        {},
        9876U);

    REQUIRE(!badStaged.ok);
    REQUIRE(!badStaged.sha256Verified);
    REQUIRE(!badStaged.error.empty());
    REQUIRE(!fs::exists(badStagingDir / "Sakura-v2.0.0-win64.zip"));
    if (!badStaged.packagePath.empty())
        REQUIRE(!fs::exists(fs::path(badStaged.packagePath)));
    if (!badStaged.payloadPath.empty())
        REQUIRE(!fs::exists(fs::path(badStaged.payloadPath) / "Sakura.exe"));
}

TEST_CASE("PPCalculator Sakura Rating computes chart rating, 5 tiers, B30+R10 weights, and anti-degradation floor", "[career][rating][v200]")
{
    // 1. CalculateChartRating: assisted returns 0.0, 100% AP on Lv.14.5 > 16.5, monotonic across accuracy
    const GameResult assistedPlay = MakeCareerPlay("sakura_storm", "Master", 14.5f, 100.0f, 1000000, true, true, true);
    REQUIRE_THAT(PPCalculator::CalculateChartRating(assistedPlay, 14.5f), WithinAbs(0.0, 1e-9));

    const GameResult lv145Ap = MakeCareerPlay("sakura_storm", "Master", 14.5f, 100.0f, 1000000, true, true, false);
    const double ap145Rating = PPCalculator::CalculateChartRating(lv145Ap, 14.5f);
    REQUIRE(ap145Rating > 16.5);
    REQUIRE(!PPCalculator::FormatRating(ap145Rating).empty());

    const double r920 = PPCalculator::CalculateChartRating(MakeCareerPlay("mono", "Expert", 12.0f, 92.0f,  920000), 12.0f);
    const double r960 = PPCalculator::CalculateChartRating(MakeCareerPlay("mono", "Expert", 12.0f, 96.0f,  960000), 12.0f);
    const double r985 = PPCalculator::CalculateChartRating(MakeCareerPlay("mono", "Expert", 12.0f, 98.5f,  985000), 12.0f);
    const double r998 = PPCalculator::CalculateChartRating(MakeCareerPlay("mono", "Expert", 12.0f, 99.8f,  998000), 12.0f);
    const double r100 = PPCalculator::CalculateChartRating(MakeCareerPlay("mono", "Expert", 12.0f, 100.0f, 1000000, true, true), 12.0f);
    REQUIRE(r920 < r960);
    REQUIRE(r960 < r985);
    REQUIRE(r985 < r998);
    REQUIRE(r998 < r100);

    // 2. GetRatingTierName: verifies all 5 tiers in exact ascending progression
    std::vector<std::string> observedTiers;
    for (int step = 0; step <= 180; ++step)
    {
        const double probe = static_cast<double>(step) * 0.1;
        const std::string tier(PPCalculator::GetRatingTierName(probe));
        if (observedTiers.empty() || observedTiers.back() != tier)
            observedTiers.push_back(tier);
    }
    REQUIRE(observedTiers.size() == 5);
    REQUIRE(observedTiers[0] == "青樱 · Bud");
    REQUIRE(observedTiers[1] == "银月 · Crescent");
    REQUIRE(observedTiers[2] == "鎏金 · Gilded");
    REQUIRE(observedTiers[3] == "绯虹 · Crimson");
    REQUIRE(observedTiers[4] == "极星 · Astral");

    // 3. CalculateSakuraRating: deduplicates (chartId, difficulty) keeping highest rating
    {
        const GameResult dupLow  = MakeCareerPlay("dup_song", "Master", 13.0f, 94.0f, 940000, false, false, false, 100);
        const GameResult dupBest = MakeCareerPlay("dup_song", "Master", 13.0f, 100.0f, 1000000, true, true, false, 200);
        const GameResult dupMid  = MakeCareerPlay("dup_song", "Master", 13.0f, 98.0f, 980000, true, false, false, 300);
        const GameResult altDiff = MakeCareerPlay("dup_song", "Expert", 11.0f, 99.5f, 995000, true, false, false, 400);

        const RatingSummary dedupSummary = PPCalculator::CalculateSakuraRating({ dupLow, dupBest, dupMid, altDiff }, { dupBest, altDiff });
        REQUIRE(dedupSummary.best30.size() == 2);
        const double expectedMasterBest = PPCalculator::CalculateChartRating(dupBest, 13.0f);
        REQUIRE_THAT(dedupSummary.best30[0].rating, WithinAbs(expectedMasterBest, 1e-6));
    }

    // 4. Caps best30 at 30 & recent10 at 10, weights 0.75 * b30Avg + 0.25 * r10Avg, and enforces r10Avg >= 0.85 * b30Avg
    std::vector<GameResult> allBests;
    for (int i = 0; i < 36; ++i)
    {
        const float lvl = 10.5f + static_cast<float>(i) * 0.1f; // 10.5 .. 14.0
        allBests.push_back(MakeCareerPlay(
            "b30_chart_" + std::to_string(i), "Master", lvl, 99.5f, 995000, true, false, false, 1700000000LL + i));
    }
    // Duplicate lower play on top chart must not displace it
    allBests.push_back(MakeCareerPlay("b30_chart_35", "Master", 14.0f, 91.0f, 910000, false, false, false, 1700000999LL));

    std::vector<GameResult> strongRecents;
    for (int i = 0; i < 14; ++i)
    {
        const float lvl = (i < 10) ? 13.8f : 11.0f;
        strongRecents.push_back(MakeCareerPlay(
            "recent_chart_" + std::to_string(i), "Master", lvl, 99.6f, 996000, true, false, false, 1700010000LL - i));
    }

    const RatingSummary normalSummary = PPCalculator::CalculateSakuraRating(allBests, strongRecents);
    REQUIRE(normalSummary.best30.size() == 30);
    REQUIRE(normalSummary.recent10.size() == 10);
    REQUIRE(normalSummary.r10Avg > 0.85 * normalSummary.b30Avg);
    REQUIRE_THAT(normalSummary.sakuraRating,
                 WithinAbs(0.75 * normalSummary.b30Avg + 0.25 * normalSummary.r10Avg, 0.01));
    REQUIRE(normalSummary.tierName == std::string(PPCalculator::GetRatingTierName(normalSummary.sakuraRating)));

    // Low recent scores trigger anti-degradation protection: r10Avg >= 0.85 * b30Avg
    std::vector<GameResult> slumpRecents;
    for (int i = 0; i < 12; ++i)
    {
        slumpRecents.push_back(MakeCareerPlay(
            "easy_warmup_" + std::to_string(i), "Easy", 2.0f, 82.0f, 760000, false, false, false, 1700020000LL - i));
    }

    const RatingSummary slumpSummary = PPCalculator::CalculateSakuraRating(allBests, slumpRecents);
    REQUIRE(slumpSummary.best30.size() == 30);
    REQUIRE(slumpSummary.recent10.size() == 10);
    REQUIRE_THAT(slumpSummary.b30Avg, WithinAbs(normalSummary.b30Avg, 1e-6));
    REQUIRE(slumpSummary.r10Avg >= 0.85 * slumpSummary.b30Avg - 1e-6);
    REQUIRE_THAT(slumpSummary.r10Avg, WithinAbs(0.85 * slumpSummary.b30Avg, 0.01));
    REQUIRE_THAT(slumpSummary.sakuraRating,
                 WithinAbs(0.75 * slumpSummary.b30Avg + 0.25 * slumpSummary.r10Avg, 0.01));
}
