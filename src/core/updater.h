#pragma once

#include "net/http_client.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sakura::core
{

struct SemVer
{
    int         major = 0;
    int         minor = 0;
    int         patch = 0;
    std::string prerelease;

    static std::optional<SemVer> Parse(std::string_view text);
    static int Compare(const SemVer& a, const SemVer& b);
    bool operator<(const SemVer& other) const { return Compare(*this, other) < 0; }
    bool operator==(const SemVer& other) const { return Compare(*this, other) == 0; }
};

enum class UpdateInstallMode
{
    Portable,
    Installer
};

struct UpdateCheckResult
{
    bool        checked         = false;
    bool        ok              = false;
    bool        hasUpdate       = false;
    std::string currentVer;
    std::string latestVer;
    std::string releaseUrl      = "https://github.com/StevenLee147/Sakura/releases/latest";
    std::string downloadUrl;
    std::string portableZipUrl;
    std::string installerExeUrl;
    std::string sha256SumsUrl;
    std::string releaseNotes;
    std::string error;
};

struct StagedUpdateResult
{
    bool              ok          = false;
    UpdateInstallMode mode        = UpdateInstallMode::Portable;
    std::string       version;
    std::string       packagePath;
    std::string       payloadPath; // Unpacked root dir for Portable, or setup.exe path for Installer
    std::string       scriptPath;  // Generated PowerShell atomic hot-swap & auto-restart script
    std::string       sha256Hex;
    bool              sha256Verified = false;
    std::string       error;
};

class Updater
{
public:
    static Updater& GetInstance();

    static UpdateCheckResult ParseReleaseJson(const std::string& jsonText, std::string_view currentVersion);
    static std::unordered_map<std::string, std::string> ParseSha256Sums(std::string_view text);
    static std::string FindExpectedSha256(std::string_view sumsText, std::string_view assetFileName);
    static UpdateInstallMode DetectInstallMode(const std::filesystem::path& appDir);
    static std::string BuildHotSwapPowerShellScript(std::uint32_t waitPid,
                                                    const std::filesystem::path& appDir,
                                                    const std::filesystem::path& stagedPayloadPath,
                                                    UpdateInstallMode mode,
                                                    bool relaunch = true);

    UpdateCheckResult CheckNow(std::string_view currentVersion,
                               std::shared_ptr<sakura::net::IHttpTransport> customTransport = nullptr,
                               std::string_view overrideEndpoint = {});

    void CheckAsync(std::string_view currentVersion,
                    std::function<void(const UpdateCheckResult&)> onComplete = {});

    StagedUpdateResult DownloadAndStageUpdate(const UpdateCheckResult& check,
                                              UpdateInstallMode mode,
                                              const std::filesystem::path& appDir,
                                              const std::filesystem::path& stagingDir,
                                              std::shared_ptr<sakura::net::IHttpTransport> customTransport = nullptr,
                                              std::function<void(float)> onProgress = {},
                                              std::uint32_t currentPid = 0);

    void DownloadAndStageUpdateAsync(const UpdateCheckResult& check,
                                     UpdateInstallMode mode,
                                     const std::filesystem::path& appDir,
                                     const std::filesystem::path& stagingDir,
                                     std::function<void(float)> onProgress,
                                     std::function<void(StagedUpdateResult)> onComplete);

    static bool LaunchHotSwapScript(const std::filesystem::path& scriptPath);

    UpdateCheckResult GetLastResult() const;

private:
    mutable std::mutex m_mutex;
    UpdateCheckResult  m_lastResult;
};

} // namespace sakura::core
