#include "core/updater.h"

#include "core/config.h"
#include "net/api_client.h"

#include <cctype>
#include <nlohmann/json.hpp>

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
        std::string tag = j.value("tag_name", "");
        if (tag.empty()) tag = j.value("latest_version", "");
        const auto latestSem = SemVer::Parse(tag);
        if (!latestSem)
        {
            res.error = "Invalid remote release tag.";
            return res;
        }

        res.ok           = true;
        res.latestVer    = tag;
        res.hasUpdate    = (*curSem < *latestSem);
        res.releaseUrl   = j.value("html_url", "https://github.com/StevenLee147/Sakura/releases/latest");
        res.downloadUrl  = j.value("download_url", "");
        res.releaseNotes = j.value("body", "");

        if (res.downloadUrl.empty() && j.contains("assets") && j["assets"].is_array())
        {
            std::string fallbackZip;
            for (const auto& asset : j["assets"])
            {
                const std::string name = asset.value("name", "");
                const std::string url  = asset.value("browser_download_url", "");
                if (name.ends_with("-setup.exe"))
                {
                    res.downloadUrl = url;
                    break;
                }
                if (name.ends_with(".zip") && fallbackZip.empty())
                    fallbackZip = url;
            }
            if (res.downloadUrl.empty())
                res.downloadUrl = fallbackZip;
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

UpdateCheckResult Updater::GetLastResult() const
{
    std::lock_guard lock(m_mutex);
    return m_lastResult;
}

} // namespace sakura::core
