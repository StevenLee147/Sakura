#include "core/updater.h"

#include "core/config.h"
#include "net/api_client.h"
#include "utils/archive.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sakura::core
{

namespace
{
int PrereleaseRank(std::string_view pre)
{
    if (pre.empty()) return 100;
    if (pre.starts_with("rc")) return 30;
    if (pre.starts_with("beta")) return 20;
    if (pre.starts_with("alpha")) return 10;
    if (pre.starts_with("dev")) return 5;
    return 15;
}

std::string ToLowerAscii(std::string_view text)
{
    std::string out(text);
    for (char& ch : out)
    {
        const unsigned char uc = static_cast<unsigned char>(ch);
        if (uc < 128)
            ch = static_cast<char>(std::tolower(uc));
    }
    return out;
}

bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix)
{
    if (suffix.size() > text.size())
        return false;
    const std::string_view tail = text.substr(text.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(tail[i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        const int la = (a < 128) ? std::tolower(a) : static_cast<int>(a);
        const int lb = (b < 128) ? std::tolower(b) : static_cast<int>(b);
        if (la != lb)
            return false;
    }
    return true;
}

std::string JsonStringOr(const nlohmann::json& j, std::string_view key, std::string_view fallback = {})
{
    const auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    return std::string(fallback);
}

std::string_view TrimAsciiWhitespace(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::string_view ExtractUrlOrPathBasename(std::string_view raw, bool stripQueryAndFragment)
{
    raw = TrimAsciiWhitespace(raw);
    if (stripQueryAndFragment)
    {
        const std::size_t hashPos = raw.find('#');
        if (hashPos != std::string_view::npos)
            raw = raw.substr(0, hashPos);
        const std::size_t qPos = raw.find('?');
        if (qPos != std::string_view::npos)
            raw = raw.substr(0, qPos);
    }
    const std::size_t slashPos = raw.find_last_of("/\\");
    if (slashPos != std::string_view::npos)
        raw = raw.substr(slashPos + 1);
    return TrimAsciiWhitespace(raw);
}

std::string PathToUtf8(const std::filesystem::path& p)
{
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string EscapePsSingleQuoted(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text)
    {
        if (ch == '\'')
            out.append("''");
        else
            out.push_back(ch);
    }
    return out;
}

std::string DetermineAssetFileName(std::string_view targetUrl, UpdateInstallMode mode)
{
    const std::string_view base = ExtractUrlOrPathBasename(targetUrl, true);
    if (!base.empty() && base != "." && base != ".." && base.find(':') == std::string_view::npos)
        return std::string(base);
    return (mode == UpdateInstallMode::Installer) ? "Sakura-setup.exe" : "Sakura-portable.zip";
}
}

std::optional<SemVer> SemVer::Parse(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    if (text.empty())
        return std::nullopt;

    const std::size_t plusPos = text.find('+');
    if (plusPos != std::string_view::npos)
        text = text.substr(0, plusPos);

    std::string_view core = text;
    std::string pre;
    const std::size_t dashPos = text.find('-');
    if (dashPos != std::string_view::npos)
    {
        core = text.substr(0, dashPos);
        pre  = std::string(text.substr(dashPos + 1));
    }

    const std::size_t dot1 = core.find('.');
    if (dot1 == std::string_view::npos)
        return std::nullopt;
    const std::size_t dot2 = core.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos)
        return std::nullopt;

    auto parsePart = [](std::string_view s, int& out) -> bool {
        if (s.empty()) return false;
        int val = 0;
        for (char c : s)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
            val = val * 10 + (c - '0');
        }
        out = val;
        return true;
    };

    SemVer v;
    if (!parsePart(core.substr(0, dot1), v.major)) return std::nullopt;
    if (!parsePart(core.substr(dot1 + 1, dot2 - dot1 - 1), v.minor)) return std::nullopt;
    if (!parsePart(core.substr(dot2 + 1), v.patch)) return std::nullopt;
    v.prerelease = std::move(pre);
    return v;
}

int SemVer::Compare(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.prerelease == b.prerelease) return 0;
    const int ra = PrereleaseRank(a.prerelease);
    const int rb = PrereleaseRank(b.prerelease);
    if (ra != rb) return ra < rb ? -1 : 1;
    return a.prerelease < b.prerelease ? -1 : 1;
}

Updater& Updater::GetInstance()
{
    static Updater instance;
    return instance;
}

UpdateCheckResult Updater::ParseReleaseJson(const std::string& jsonText, std::string_view currentVersion)
{
    UpdateCheckResult res;
    res.checked    = true;
    res.currentVer = std::string(currentVersion);
    const auto curSem = SemVer::Parse(currentVersion);
    if (!curSem)
    {
        res.error = "Invalid current version string.";
        return res;
    }

    try
    {
        const auto j = nlohmann::json::parse(jsonText);
        if (!j.is_object())
        {
            res.error = "Release metadata JSON root must be an object.";
            return res;
        }
        std::string tag = JsonStringOr(j, "tag_name");
        if (tag.empty()) tag = JsonStringOr(j, "latest_version");
        const auto latestSem = SemVer::Parse(tag);
        if (!latestSem)
        {
            res.error = "Invalid remote release tag.";
            return res;
        }

        res.ok        = true;
        res.latestVer = tag;
        res.hasUpdate = (*curSem < *latestSem);
        res.releaseUrl = JsonStringOr(j, "html_url", "https://github.com/StevenLee147/Sakura/releases/latest");
        if (res.releaseUrl.empty())
            res.releaseUrl = "https://github.com/StevenLee147/Sakura/releases/latest";
        const std::string rawDownloadUrl = JsonStringOr(j, "download_url");
        res.downloadUrl     = rawDownloadUrl;
        res.portableZipUrl  = JsonStringOr(j, "portable_zip_url");
        res.installerExeUrl = JsonStringOr(j, "installer_exe_url");
        res.sha256SumsUrl   = JsonStringOr(j, "sha256_sums_url");
        res.releaseNotes    = JsonStringOr(j, "body");
        if (res.releaseNotes.empty())
            res.releaseNotes = JsonStringOr(j, "release_notes");

        const bool hasAssets = j.contains("assets") && j["assets"].is_array();
        if (hasAssets)
        {
            for (const auto& asset : j["assets"])
            {
                if (!asset.is_object())
                    continue;
                const std::string name = JsonStringOr(asset, "name");
                const std::string url  = JsonStringOr(asset, "browser_download_url");
                if (url.empty())
                    continue;
                const std::string_view matchName = !name.empty()
                    ? std::string_view(name)
                    : ExtractUrlOrPathBasename(url, true);

                if (EndsWithIgnoreCase(matchName, "-setup.exe"))
                {
                    if (res.installerExeUrl.empty())
                        res.installerExeUrl = url;
                }
                else if (EndsWithIgnoreCase(matchName, ".zip"))
                {
                    if (res.portableZipUrl.empty())
                        res.portableZipUrl = url;
                }
                else if (EndsWithIgnoreCase(matchName, "SHA256SUMS.txt"))
                {
                    if (res.sha256SumsUrl.empty())
                        res.sha256SumsUrl = url;
                }
            }

            if (res.downloadUrl.empty())
            {
                if (!res.installerExeUrl.empty())
                    res.downloadUrl = res.installerExeUrl;
                else if (!res.portableZipUrl.empty())
                    res.downloadUrl = res.portableZipUrl;
            }
        }

        if ((!hasAssets || j["assets"].empty()) && !rawDownloadUrl.empty())
        {
            if (EndsWithIgnoreCase(rawDownloadUrl, ".zip"))
            {
                if (res.portableZipUrl.empty())
                    res.portableZipUrl = rawDownloadUrl;
                constexpr std::string_view kZipSuffix = "-win64.zip";
                const std::size_t pos = rawDownloadUrl.rfind(kZipSuffix);
                if (pos != std::string::npos)
                {
                    if (res.installerExeUrl.empty())
                    {
                        std::string exeUrl = rawDownloadUrl;
                        exeUrl.replace(pos, kZipSuffix.size(), "-win64-setup.exe");
                        res.installerExeUrl = std::move(exeUrl);
                    }
                    if (res.sha256SumsUrl.empty())
                    {
                        std::string sumsUrl = rawDownloadUrl;
                        sumsUrl.replace(pos, kZipSuffix.size(), "-SHA256SUMS.txt");
                        res.sha256SumsUrl = std::move(sumsUrl);
                    }
                }
            }
            else
            {
                if (res.installerExeUrl.empty())
                    res.installerExeUrl = rawDownloadUrl;
                constexpr std::string_view kSetupSuffix = "-win64-setup.exe";
                const std::size_t pos = rawDownloadUrl.rfind(kSetupSuffix);
                if (pos != std::string::npos)
                {
                    if (res.portableZipUrl.empty())
                    {
                        std::string zipUrl = rawDownloadUrl;
                        zipUrl.replace(pos, kSetupSuffix.size(), "-win64.zip");
                        res.portableZipUrl = std::move(zipUrl);
                    }
                    if (res.sha256SumsUrl.empty())
                    {
                        std::string sumsUrl = rawDownloadUrl;
                        sumsUrl.replace(pos, kSetupSuffix.size(), "-SHA256SUMS.txt");
                        res.sha256SumsUrl = std::move(sumsUrl);
                    }
                }
            }
        }

        if (res.downloadUrl.empty())
            res.downloadUrl = res.releaseUrl;
        return res;
    }
    catch (const std::exception& e)
    {
        res.error = std::string("Release metadata parse error: ") + e.what();
        return res;
    }
}

std::unordered_map<std::string, std::string> Updater::ParseSha256Sums(std::string_view text)
{
    std::unordered_map<std::string, std::string> result;
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    std::size_t pos = 0;
    while (pos < text.size())
    {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos)
            ? text.substr(pos)
            : text.substr(pos, nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() : (nl + 1);

        line = TrimAsciiWhitespace(line);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;
        if (line.size() <= 64)
            continue;

        bool validHex = true;
        for (std::size_t i = 0; i < 64; ++i)
        {
            if (!std::isxdigit(static_cast<unsigned char>(line[i])))
            {
                validHex = false;
                break;
            }
        }
        if (!validHex)
            continue;

        const char sep = line[64];
        if (sep != ' ' && sep != '\t')
            continue;

        std::string_view rest = line.substr(64);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
            rest.remove_prefix(1);
        if (!rest.empty() && rest.front() == '*')
        {
            rest.remove_prefix(1);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
                rest.remove_prefix(1);
        }

        const std::string_view basename = ExtractUrlOrPathBasename(rest, false);
        if (basename.empty())
            continue;

        result[ToLowerAscii(basename)] = ToLowerAscii(line.substr(0, 64));
    }
    return result;
}

std::string Updater::FindExpectedSha256(std::string_view sumsText, std::string_view assetFileName)
{
    const std::string_view basename = ExtractUrlOrPathBasename(assetFileName, true);
    if (basename.empty())
        return {};

    const auto sums = ParseSha256Sums(sumsText);
    const auto it   = sums.find(ToLowerAscii(basename));
    return (it != sums.end()) ? it->second : std::string{};
}

UpdateInstallMode Updater::DetectInstallMode(const std::filesystem::path& appDir)
{
    std::error_code ec;
    if (!appDir.empty() && std::filesystem::is_regular_file(appDir / "unins000.exe", ec))
        return UpdateInstallMode::Installer;
    return UpdateInstallMode::Portable;
}

std::string Updater::BuildHotSwapPowerShellScript(std::uint32_t waitPid,
                                                  const std::filesystem::path& appDir,
                                                  const std::filesystem::path& stagedPayloadPath,
                                                  UpdateInstallMode mode,
                                                  bool relaunch)
{
    const std::string escapedAppDir  = EscapePsSingleQuoted(PathToUtf8(appDir));
    const std::string escapedPayload = EscapePsSingleQuoted(PathToUtf8(stagedPayloadPath));

    std::ostringstream ps;
    ps << "$ErrorActionPreference = 'Stop'\n";

    if (waitPid > 0)
    {
        ps << "for ($i = 0; $i -lt 60; $i++) {\n"
           << "    if (-not (Get-Process -Id " << waitPid << " -ErrorAction SilentlyContinue)) {\n"
           << "        break\n"
           << "    }\n"
           << "    Start-Sleep -Milliseconds 500\n"
           << "}\n";
    }

    if (mode == UpdateInstallMode::Portable)
    {
        ps << "$appDir = '" << escapedAppDir << "'\n"
           << "$payloadDir = '" << escapedPayload << "'\n"
           << "$backupDir = Join-Path $appDir '.update_backup'\n"
           << "if (Test-Path -LiteralPath $backupDir) {\n"
           << "    Remove-Item -LiteralPath $backupDir -Recurse -Force\n"
           << "}\n"
           << "New-Item -ItemType Directory -Path $backupDir -Force | Out-Null\n"
           << "$exePath = Join-Path $appDir 'Sakura.exe'\n"
           << "if (Test-Path -LiteralPath $exePath) {\n"
           << "    Copy-Item -LiteralPath $exePath -Destination $backupDir -Force\n"
           << "}\n"
           << "$resPath = Join-Path $appDir 'resources'\n"
           << "if (Test-Path -LiteralPath $resPath) {\n"
           << "    Copy-Item -LiteralPath $resPath -Destination $backupDir -Recurse -Force\n"
           << "}\n"
           << "try {\n"
           << "    foreach ($item in Get-ChildItem -LiteralPath $payloadDir -Force) {\n"
           << "        if ($item.Name -ieq 'userdata' -or $item.Name -ieq 'portable.txt') {\n"
           << "            continue\n"
           << "        }\n"
           << "        Copy-Item -LiteralPath $item.FullName -Destination $appDir -Recurse -Force\n"
           << "    }\n"
           << "} catch {\n"
           << "    $backupExe = Join-Path $backupDir 'Sakura.exe'\n"
           << "    if (Test-Path -LiteralPath $backupExe) {\n"
           << "        Copy-Item -LiteralPath $backupExe -Destination $appDir -Force\n"
           << "    }\n"
           << "    $backupRes = Join-Path $backupDir 'resources'\n"
           << "    if (Test-Path -LiteralPath $backupRes) {\n"
           << "        if (Test-Path -LiteralPath $resPath) {\n"
           << "            Remove-Item -LiteralPath $resPath -Recurse -Force\n"
           << "        }\n"
           << "        Copy-Item -LiteralPath $backupRes -Destination $appDir -Recurse -Force\n"
           << "    }\n"
           << "    throw\n"
           << "}\n"
           << "if (Test-Path -LiteralPath $backupDir) {\n"
           << "    Remove-Item -LiteralPath $backupDir -Recurse -Force\n"
           << "}\n";

        if (relaunch)
        {
            ps << "Start-Process -FilePath (Join-Path $appDir 'Sakura.exe') -WorkingDirectory $appDir\n";
        }
    }
    else
    {
        ps << "$appDir = '" << escapedAppDir << "'\n"
           << "$payloadPath = '" << escapedPayload << "'\n"
           << "Start-Process -FilePath $payloadPath -ArgumentList '/SILENT','/CLOSEAPPLICATIONS','/RESTARTAPPLICATIONS' -Wait\n";

        if (relaunch)
        {
            ps << "if (-not (Get-Process -Name 'Sakura' -ErrorAction SilentlyContinue)) {\n"
               << "    if (Test-Path -LiteralPath (Join-Path $appDir 'Sakura.exe')) {\n"
               << "        Start-Process -FilePath (Join-Path $appDir 'Sakura.exe') -WorkingDirectory $appDir\n"
               << "    }\n"
               << "}\n";
        }
    }

    return ps.str();
}

UpdateCheckResult Updater::CheckNow(std::string_view currentVersion,
                                    std::shared_ptr<sakura::net::IHttpTransport> customTransport,
                                    std::string_view overrideEndpoint)
{
    std::string endpoint(overrideEndpoint);
    if (endpoint.empty())
    {
        endpoint = Config::GetInstance().Get<std::string>(
            "online.update_url",
            "https://api.github.com/repos/StevenLee147/Sakura/releases/latest");
    }

    auto transport = customTransport ? std::move(customTransport) : sakura::net::APIClient::GetInstance().GetTransport();
    sakura::net::HttpRequest req;
    req.method    = "GET";
    req.url       = endpoint;
    req.timeoutMs = 8000;
    req.headers.emplace_back("Accept", "application/vnd.github+json");
    req.headers.emplace_back("User-Agent", "Sakura-RhythmGame");

    const auto httpRes = transport->Send(req);
    UpdateCheckResult res;
    res.checked    = true;
    res.currentVer = std::string(currentVersion);
    if (!httpRes.IsSuccess())
    {
        res.error = httpRes.error.empty()
            ? ("HTTP " + std::to_string(httpRes.statusCode))
            : httpRes.error;
    }
    else
    {
        res = ParseReleaseJson(httpRes.body, currentVersion);
    }

    {
        std::lock_guard lock(m_mutex);
        m_lastResult = res;
    }
    return res;
}

void Updater::CheckAsync(std::string_view currentVersion,
                         std::function<void(const UpdateCheckResult&)> onComplete)
{
    const std::string curVer(currentVersion);
    sakura::net::APIClient::GetInstance().PostTask([this, curVer, cb = std::move(onComplete)]() mutable
    {
        auto res = CheckNow(curVer);
        if (cb)
        {
            sakura::net::APIClient::GetInstance().PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable
            {
                cb(res);
            });
        }
    });
}

StagedUpdateResult Updater::DownloadAndStageUpdate(const UpdateCheckResult& check,
                                                   UpdateInstallMode mode,
                                                   const std::filesystem::path& appDir,
                                                   const std::filesystem::path& stagingDir,
                                                   std::shared_ptr<sakura::net::IHttpTransport> customTransport,
                                                   std::function<void(float)> onProgress,
                                                   std::uint32_t currentPid)
{
    StagedUpdateResult out;
    out.mode    = mode;
    out.version = check.latestVer;

    std::string targetUrl;
    if (mode == UpdateInstallMode::Installer)
        targetUrl = !check.installerExeUrl.empty() ? check.installerExeUrl : check.downloadUrl;
    else
        targetUrl = !check.portableZipUrl.empty() ? check.portableZipUrl : check.downloadUrl;

    if (!check.ok || !check.hasUpdate || targetUrl.empty())
    {
        out.error = !check.error.empty() ? check.error : "当前没有可下载的更新包";
        return out;
    }
    if (stagingDir.empty())
    {
        out.error = "更新暂存目录无效";
        return out;
    }

    std::error_code ec;
    if (std::filesystem::exists(stagingDir, ec))
        std::filesystem::remove_all(stagingDir, ec);
    ec.clear();
    std::filesystem::create_directories(stagingDir, ec);
    if (ec)
    {
        out.error = "无法创建更新暂存目录: " + ec.message();
        return out;
    }

    auto cleanupStaging = [&]()
    {
        std::error_code rmEc;
        std::filesystem::remove_all(stagingDir, rmEc);
    };

    const std::string assetFileName = DetermineAssetFileName(targetUrl, mode);
    auto transport = customTransport ? std::move(customTransport) : sakura::net::APIClient::GetInstance().GetTransport();
    if (!transport)
        transport = sakura::net::CreateDefaultHttpTransport();

    std::string expectedSha;
    if (!check.sha256SumsUrl.empty())
    {
        sakura::net::HttpRequest sumsReq;
        sumsReq.method    = "GET";
        sumsReq.url       = check.sha256SumsUrl;
        sumsReq.timeoutMs = 15000;
        sumsReq.headers.emplace_back("User-Agent", "Sakura-RhythmGame");

        const auto sumsRes = transport->Send(sumsReq);
        if (sumsRes.statusCode == 200 && !sumsRes.body.empty())
        {
            expectedSha = FindExpectedSha256(sumsRes.body, assetFileName);
        }
    }

    sakura::net::HttpRequest pkgReq;
    pkgReq.method    = "GET";
    pkgReq.url       = targetUrl;
    pkgReq.timeoutMs = 120000;
    pkgReq.headers.emplace_back("User-Agent", "Sakura-RhythmGame");
    bool reportedComplete = false;
    if (onProgress)
    {
        pkgReq.onProgress = [&onProgress, &reportedComplete](std::size_t dl, std::size_t total)
        {
            const float frac = total > 0
                ? std::clamp(static_cast<float>(dl) / static_cast<float>(total), 0.0f, 1.0f)
                : (dl > 0 ? 0.5f : 0.0f);
            if (frac >= 1.0f)
                reportedComplete = true;
            onProgress(frac);
        };
    }

    const auto pkgRes = transport->Send(pkgReq);
    if (!pkgRes.IsSuccess() || pkgRes.body.empty())
    {
        cleanupStaging();
        out.error = !pkgRes.error.empty()
            ? pkgRes.error
            : ("下载更新包失败 (HTTP " + std::to_string(pkgRes.statusCode) + ")");
        return out;
    }
    if (onProgress && !reportedComplete)
        onProgress(1.0f);

    const std::string actualSha = sakura::utils::ComputeSha256Hex(pkgRes.body);
    out.sha256Hex = actualSha;
    if (!expectedSha.empty())
    {
        if (actualSha != expectedSha)
        {
            LOG_ERROR("Updater SHA-256 mismatch for {}: expected {}, got {}", assetFileName, expectedSha, actualSha);
            cleanupStaging();
            out.error = "SHA-256 完整性校验不匹配，已中止更新";
            return out;
        }
        out.sha256Verified = true;
    }

    const std::filesystem::path packageFile = stagingDir / assetFileName;
    if (!sakura::utils::AtomicWrite(packageFile, pkgRes.body))
    {
        cleanupStaging();
        out.error = "写入暂存更新包失败";
        return out;
    }

    std::filesystem::path payloadFsPath = packageFile;
    if (mode == UpdateInstallMode::Portable)
    {
        const std::filesystem::path extractedDir = stagingDir / "extracted";
        if (!sakura::utils::ExtractZipArchive(packageFile, extractedDir, 1024ULL * 1024ULL * 1024ULL))
        {
            cleanupStaging();
            out.error = "解压便携版更新包失败";
            return out;
        }

        std::filesystem::path payloadRoot = extractedDir;
        for (int depth = 0; depth < 4 && !std::filesystem::is_regular_file(payloadRoot / "Sakura.exe", ec); ++depth)
        {
            std::vector<std::filesystem::path> childDirs;
            std::size_t totalEntries = 0;
            for (const auto& entry : std::filesystem::directory_iterator(payloadRoot, ec))
            {
                if (ec)
                    break;
                ++totalEntries;
                if (entry.is_directory(ec))
                    childDirs.push_back(entry.path());
            }
            if (totalEntries == 1 && childDirs.size() == 1)
            {
                payloadRoot = childDirs.front();
                continue;
            }
            bool foundInChild = false;
            for (const auto& dir : childDirs)
            {
                if (std::filesystem::is_regular_file(dir / "Sakura.exe", ec))
                {
                    payloadRoot  = dir;
                    foundInChild = true;
                    break;
                }
            }
            if (!foundInChild)
                break;
        }

        if (!std::filesystem::is_regular_file(payloadRoot / "Sakura.exe", ec))
        {
            cleanupStaging();
            out.error = "便携版更新包内未找到有效的 Sakura.exe";
            return out;
        }
        payloadFsPath = payloadRoot;
    }

    std::uint32_t pid = currentPid;
    if (pid == 0)
    {
#ifdef _WIN32
        pid = static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
        pid = 0;
#endif
    }

    const std::filesystem::path scriptFile = stagingDir / "apply_update.ps1";
    const std::string scriptText = BuildHotSwapPowerShellScript(pid, appDir, payloadFsPath, mode, true);
    if (!sakura::utils::AtomicWrite(scriptFile, scriptText))
    {
        cleanupStaging();
        out.error = "生成热更新脚本失败";
        return out;
    }

    out.ok          = true;
    out.packagePath = PathToUtf8(packageFile);
    out.payloadPath = PathToUtf8(payloadFsPath);
    out.scriptPath  = PathToUtf8(scriptFile);
    return out;
}

void Updater::DownloadAndStageUpdateAsync(const UpdateCheckResult& check,
                                          UpdateInstallMode mode,
                                          const std::filesystem::path& appDir,
                                          const std::filesystem::path& stagingDir,
                                          std::function<void(float)> onProgress,
                                          std::function<void(StagedUpdateResult)> onComplete)
{
    sakura::net::APIClient::GetInstance().PostTask(
        [this, check, mode, appDir, stagingDir,
         onProgress = std::move(onProgress),
         onComplete = std::move(onComplete)]() mutable
        {
            std::function<void(float)> progressBridge;
            if (onProgress)
            {
                progressBridge = [onProgress](float frac)
                {
                    sakura::net::APIClient::GetInstance().PostMainCallback(
                        [onProgress, frac]() { onProgress(frac); });
                };
            }

            auto res = DownloadAndStageUpdate(
                check, mode, appDir, stagingDir, nullptr, std::move(progressBridge), 0);
            if (onComplete)
            {
                sakura::net::APIClient::GetInstance().PostMainCallback(
                    [cb = std::move(onComplete), res = std::move(res)]() mutable
                    {
                        cb(std::move(res));
                    });
            }
        });
}

bool Updater::LaunchHotSwapScript(const std::filesystem::path& scriptPath)
{
    std::error_code ec;
    if (scriptPath.empty() || !std::filesystem::is_regular_file(scriptPath, ec))
        return false;

#ifdef _WIN32
    const std::wstring cmdLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \""
        + scriptPath.wstring() + L"\"";
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    const std::filesystem::path workDir = scriptPath.parent_path();
    const wchar_t* lpWorkDir = (!workDir.empty() && std::filesystem::is_directory(workDir, ec))
        ? workDir.c_str()
        : nullptr;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = ::CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        lpWorkDir,
        &si,
        &pi);
    if (!ok)
    {
        LOG_ERROR("Updater failed to launch apply_update.ps1 (Win32 error {})", ::GetLastError());
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
#else
    return false;
#endif
}

UpdateCheckResult Updater::GetLastResult() const
{
    std::lock_guard lock(m_mutex);
    return m_lastResult;
}

} // namespace sakura::core
