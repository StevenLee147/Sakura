#pragma once

#include "net/http_client.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

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

struct UpdateCheckResult
{
    bool        checked       = false;
    bool        ok            = false;
    bool        hasUpdate     = false;
    std::string currentVer;
    std::string latestVer;
    std::string releaseUrl    = "https://github.com/StevenLee147/Sakura/releases/latest";
    std::string downloadUrl;
    std::string releaseNotes;
    std::string error;
};

class Updater
{
public:
    static Updater& GetInstance();

    static UpdateCheckResult ParseReleaseJson(const std::string& jsonText, std::string_view currentVersion);

    UpdateCheckResult CheckNow(std::string_view currentVersion,
                               std::shared_ptr<sakura::net::IHttpTransport> customTransport = nullptr,
                               std::string_view overrideEndpoint = {});

    void CheckAsync(std::string_view currentVersion,
                    std::function<void(const UpdateCheckResult&)> onComplete = {});

    UpdateCheckResult GetLastResult() const;

private:
    mutable std::mutex m_mutex;
    UpdateCheckResult  m_lastResult;
};

} // namespace sakura::core
