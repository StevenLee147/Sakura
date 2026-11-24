#include "net/api_client.h"

#include "core/config.h"
#include "core/paths.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace sakura::net
{

namespace
{
constexpr const char* kDefaultOnlineServerUrl = "https://steven-lee.tech:233";

std::string TrimTrailingSlash(std::string url)
{
    while (!url.empty() && (url.front() == ' ' || url.front() == '\t'))
        url.erase(url.begin());
    while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\t'))
        url.pop_back();
    if (url.empty())
        return kDefaultOnlineServerUrl;
    if (url.find("://") == std::string::npos)
    {
        const bool isLocal = (url.rfind("127.0.0.1", 0) == 0 || url.rfind("localhost", 0) == 0);
        url = (isLocal ? "http://" : "https://") + url;
    }
    return url;
}

const char* GradeToStr(sakura::game::Grade g)
{
    using sakura::game::Grade;
    switch (g)
    {
        case Grade::SS: return "SS";
        case Grade::S:  return "S";
        case Grade::A:  return "A";
        case Grade::B:  return "B";
        case Grade::C:  return "C";
        default:        return "D";
    }
}

std::string ReadWholeFileUtf8(const std::string& path)
{
    if (path.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return {};
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}
}

APIClient& APIClient::GetInstance()
{
    static APIClient instance;
    return instance;
}

APIClient::APIClient(std::shared_ptr<IHttpTransport> transport, std::string queueFilePath)
    : m_transport(transport ? std::move(transport) : CreateDefaultHttpTransport())
    , m_customQueuePath(std::move(queueFilePath))
{
}

APIClient::~APIClient()
{
    Shutdown();
}

void APIClient::LoadFromConfig()
{
    auto& cfg = sakura::core::Config::GetInstance();
    std::lock_guard lock(m_stateMutex);
    std::string rawUrl = cfg.Get<std::string>("online.server_url", kDefaultOnlineServerUrl);
    if (rawUrl == "http://127.0.0.1:8787")
        rawUrl = kDefaultOnlineServerUrl;
    m_serverUrl     = TrimTrailingSlash(rawUrl);
    m_authToken     = cfg.Get<std::string>("online.auth_token", "");
    m_user.id       = cfg.Get<std::string>("online.user_id", "");
    m_user.username = cfg.Get<std::string>("online.username", "");
    m_user.isGuest  = cfg.Get<bool>("online.is_guest", true);
    m_user.totalPP  = cfg.Get<double>("online.total_pp", 0.0);
}

void APIClient::SaveToConfig() const
{
    auto& cfg = sakura::core::Config::GetInstance();
    std::lock_guard lock(m_stateMutex);
    cfg.Set("online.server_url", m_serverUrl);
    cfg.Set("online.auth_token", m_authToken);
    cfg.Set("online.user_id", m_user.id);
    cfg.Set("online.username", m_user.username);
    cfg.Set("online.is_guest", m_user.isGuest);
    cfg.Set("online.total_pp", m_user.totalPP);
    cfg.Save();
}

void APIClient::Shutdown()
{
    {
        std::lock_guard lock(m_workerMutex);
        if (!m_workerRunning)
            return;
        m_stopping = true;
    }
    m_workerCv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
    {
        std::lock_guard lock(m_workerMutex);
        m_workerRunning = false;
        m_stopping      = false;
        m_tasks.clear();
    }
    {
        std::lock_guard lock(m_cbMutex);
        m_callbacks.clear();
    }
}

void APIClient::SetTransport(std::shared_ptr<IHttpTransport> transport)
{
    std::lock_guard lock(m_stateMutex);
    m_transport = transport ? std::move(transport) : CreateDefaultHttpTransport();
}

std::shared_ptr<IHttpTransport> APIClient::GetTransport() const
{
    std::lock_guard lock(m_stateMutex);
    return m_transport;
}

void APIClient::SetServerUrl(const std::string& url)
{
    {
        std::lock_guard lock(m_stateMutex);
        m_serverUrl = TrimTrailingSlash(url);
    }
    SaveToConfig();
}

std::string APIClient::GetServerUrl() const
{
    std::lock_guard lock(m_stateMutex);
    return m_serverUrl;
}

bool APIClient::IsAuthenticated() const
{
    std::lock_guard lock(m_stateMutex);
    return !m_authToken.empty() && !m_user.id.empty();
}

OnlineUser APIClient::GetCurrentUser() const
{
    std::lock_guard lock(m_stateMutex);
    return m_user;
}

std::string APIClient::GetAuthToken() const
{
    std::lock_guard lock(m_stateMutex);
    return m_authToken;
}

void APIClient::Logout()
{
    {
        std::lock_guard lock(m_stateMutex);
        m_authToken.clear();
        m_user = {};
    }
    SaveToConfig();
}

std::string APIClient::ResolveQueuePath() const
{
    if (!m_customQueuePath.empty())
        return m_customQueuePath;
    return sakura::core::Paths::User("data/pending_scores.json");
}

std::size_t APIClient::GetPendingScoreCount() const
{
    const std::string qPath = ResolveQueuePath();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(qPath, ec))
        return 0;
    try
    {
        std::ifstream ifs(qPath);
        const auto arr = nlohmann::json::parse(ifs);
        return arr.is_array() ? arr.size() : 0;
    }
    catch (...)
    {
        return 0;
    }
}

HttpRequest APIClient::MakeRequest(const std::string& method, const std::string& path,
                                   const std::string& jsonBody, bool withAuth) const
{
    HttpRequest req;
    req.method = method;
    std::string token;
    {
        std::lock_guard lock(m_stateMutex);
        req.url = m_serverUrl + path;
        token   = m_authToken;
    }
    req.headers.emplace_back("Accept", "application/json");
    if (!jsonBody.empty())
    {
        req.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
        req.body = jsonBody;
    }
    if (withAuth && !token.empty())
    {
        req.headers.emplace_back("Authorization", "Bearer " + token);
    }
    return req;
}

bool APIClient::CheckHealth()
{
    auto transport = GetTransport();
    const auto res = transport->Send(MakeRequest("GET", "/api/v1/health", {}, false));
    if (res.IsSuccess())
    {
        m_status.store(OnlineStatus::Online);
        return true;
    }
    m_status.store(OnlineStatus::Offline);
    return false;
}

AuthResult APIClient::HandleAuthResponse(const HttpResponse& res)
{
    AuthResult out;
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "无法连接至在线服务器" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "认证失败 (HTTP " + std::to_string(res.statusCode) + ")");
            return out;
        }
        out.ok = true;
        if (j.contains("token") && j["token"].is_string())
            out.token = j["token"].get<std::string>();
        else
            out.token = GetAuthToken();

        const auto& u   = j.at("user");
        out.user.id       = u.value("id", "");
        out.user.username = u.value("username", "");
        out.user.isGuest  = u.value("is_guest", false);
        out.user.totalPP  = u.value("total_pp", 0.0);

        {
            std::lock_guard lock(m_stateMutex);
            m_authToken = out.token;
            m_user      = out.user;
        }
        SaveToConfig();
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("响应解析失败: ") + e.what();
        return out;
    }
}

AuthResult APIClient::LoginGuest(const std::string& nickname)
{
    nlohmann::json body = nlohmann::json::object();
    if (!nickname.empty()) body["nickname"] = nickname;
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/guest", body.dump(), false));
    return HandleAuthResponse(res);
}

AuthResult APIClient::Register(const std::string& username, const std::string& password)
{
    const nlohmann::json body = { { "username", username }, { "password", password } };
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/register", body.dump(), false));
    return HandleAuthResponse(res);
}

AuthResult APIClient::Login(const std::string& username, const std::string& password)
{
    const nlohmann::json body = { { "username", username }, { "password", password } };
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/auth/login", body.dump(), false));
    return HandleAuthResponse(res);
}

AuthResult APIClient::RefreshProfile()
{
    if (!IsAuthenticated())
    {
        AuthResult r;
        r.error = "尚未登录";
        return r;
    }
    const auto res = GetTransport()->Send(MakeRequest("GET", "/api/v1/auth/me", {}, true));
    return HandleAuthResponse(res);
}

void APIClient::EnqueueOfflineScorePayload(const std::string& payloadJson)
{
    const std::string qPath = ResolveQueuePath();
    nlohmann::json arr = nlohmann::json::array();
    std::error_code ec;
    if (std::filesystem::is_regular_file(qPath, ec))
    {
        try
        {
            std::ifstream ifs(qPath);
            auto existing = nlohmann::json::parse(ifs);
            if (existing.is_array())
                arr = std::move(existing);
        }
        catch (...) {}
    }
    try
    {
        arr.push_back(nlohmann::json::parse(payloadJson));
        std::filesystem::create_directories(std::filesystem::path(qPath).parent_path(), ec);
        sakura::utils::AtomicWrite(qPath, arr.dump(2));
    }
    catch (...) {}
}

ScoreSubmitResult APIClient::SubmitScore(const sakura::game::GameResult& result)
{
    ScoreSubmitResult out;
    if (result.assisted)
    {
        out.error = "辅助演奏不计入全球排行榜";
        return out;
    }

    const int totalJudged = result.totalJudgments > 0
        ? result.totalJudgments
        : (result.perfectCount + result.greatCount + result.goodCount + result.badCount + result.missCount);

    nlohmann::json payload = {
        { "chart_id", result.chartId },
        { "chart_title", result.chartTitle },
        { "chart_hash", result.chartHash.empty() ? result.chartId : result.chartHash },
        { "difficulty", result.difficulty },
        { "difficulty_level", result.difficultyLevel },
        { "score", result.score },
        { "accuracy", result.accuracy },
        { "max_combo", result.maxCombo },
        { "grade", GradeToStr(result.grade) },
        { "perfect_count", result.perfectCount },
        { "great_count", result.greatCount },
        { "good_count", result.goodCount },
        { "bad_count", result.badCount },
        { "miss_count", result.missCount },
        { "total_judgments", totalJudged },
        { "synergy_count", result.synergyCount },
        { "is_full_combo", result.isFullCombo },
        { "is_all_perfect", result.isAllPerfect },
        { "assisted", result.assisted },
        { "playback_rate", result.playbackRate }
    };

    const std::string replayRaw = ReadWholeFileUtf8(result.replayFile);
    if (!replayRaw.empty())
        payload["replay_data"] = replayRaw;

    const std::string bodyStr = payload.dump();
    if (!IsAuthenticated())
    {
        EnqueueOfflineScorePayload(bodyStr);
        out.queuedOffline = true;
        out.error         = "未登录，已缓存至待同步队列";
        return out;
    }

    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/scores", bodyStr, true));
    if (res.IsNetworkError() || res.statusCode >= 500)
    {
        m_status.store(OnlineStatus::Offline);
        EnqueueOfflineScorePayload(bodyStr);
        out.queuedOffline = true;
        out.error         = "网络离线，成绩已加入待同步队列";
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "成绩提交被拒绝");
            return out;
        }
        out.ok      = true;
        out.scoreId = j.value("score_id", "");
        out.rank    = j.value("rank", 0);
        out.pp      = j.value("pp", 0.0);
        out.totalPP = j.value("total_pp", 0.0);
        out.newBest = j.value("new_best", false);
        {
            std::lock_guard lock(m_stateMutex);
            m_user.totalPP = out.totalPP;
        }
        SaveToConfig();
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("成绩响应解析失败: ") + e.what();
        return out;
    }
}

int APIClient::FlushPendingScores()
{
    if (!IsAuthenticated())
        return 0;
    const std::string qPath = ResolveQueuePath();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(qPath, ec))
        return 0;

    nlohmann::json arr;
    try
    {
        std::ifstream ifs(qPath);
        arr = nlohmann::json::parse(ifs);
        if (!arr.is_array() || arr.empty())
            return 0;
    }
    catch (...)
    {
        return 0;
    }

    nlohmann::json remaining = nlohmann::json::array();
    int flushed = 0;
    auto transport = GetTransport();
    for (const auto& item : arr)
    {
        const auto res = transport->Send(MakeRequest("POST", "/api/v1/scores", item.dump(), true));
        if (res.IsNetworkError() || res.statusCode >= 500)
        {
            m_status.store(OnlineStatus::Offline);
            remaining.push_back(item);
        }
        else if (res.IsSuccess())
        {
            m_status.store(OnlineStatus::Online);
            ++flushed;
        }
        // 4xx validation errors are dropped rather than retried forever
    }

    if (remaining.empty())
        std::filesystem::remove(qPath, ec);
    else
        sakura::utils::AtomicWrite(qPath, remaining.dump(2));
    return flushed;
}

LeaderboardResult APIClient::FetchLeaderboard(const std::string& chartId, const std::string& difficulty, int limit)
{
    LeaderboardResult out;
    const std::string path = "/api/v1/leaderboards/" + UrlEncode(chartId)
        + "?difficulty=" + UrlEncode(difficulty)
        + "&scope=global&limit=" + std::to_string(std::max(1, limit));

    const auto res = GetTransport()->Send(MakeRequest("GET", path, {}, true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：无法获取全球排行榜" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "获取排行榜失败");
            return out;
        }
        out.ok = true;
        if (j.contains("my_rank") && j["my_rank"].is_number_integer())
            out.myRank = j["my_rank"].get<int>();
        if (j.contains("entries") && j["entries"].is_array())
        {
            for (const auto& item : j["entries"])
            {
                LeaderboardRow row;
                row.rank         = item.value("rank", 0);
                row.scoreId      = item.value("score_id", "");
                row.userId       = item.value("user_id", "");
                row.username     = item.value("username", "Unknown");
                row.score        = item.value("score", 0);
                row.accuracy     = item.value("accuracy", 0.0f);
                row.maxCombo     = item.value("max_combo", 0);
                row.grade        = item.value("grade", "D");
                row.pp           = item.value("pp", 0.0);
                row.isFullCombo  = item.value("is_full_combo", false);
                row.isAllPerfect = item.value("is_all_perfect", false);
                row.hasReplay    = item.value("has_replay", false);
                row.playedAt     = item.value("played_at", 0LL);
                out.entries.push_back(std::move(row));
            }
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("排行榜解析失败: ") + e.what();
        return out;
    }
}

bool APIClient::DownloadReplay(const std::string& scoreId, const std::string& destPath)
{
    if (scoreId.empty() || destPath.empty())
        return false;
    const auto res = GetTransport()->Send(MakeRequest("GET", "/api/v1/replays/" + UrlEncode(scoreId), {}, false));
    if (!res.IsSuccess() || res.body.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(destPath).parent_path(), ec);
    return sakura::utils::AtomicWrite(destPath, res.body);
}

void APIClient::EnsureWorker()
{
    std::lock_guard lock(m_workerMutex);
    if (m_workerRunning)
        return;
    m_stopping      = false;
    m_workerRunning = true;
    m_worker = std::thread([this]()
    {
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock ulock(m_workerMutex);
                m_workerCv.wait(ulock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_stopping && m_tasks.empty())
                    break;
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            if (task)
            {
                try { task(); } catch (...) {}
            }
        }
    });
}

void APIClient::PostTask(std::function<void()> workerTask)
{
    EnsureWorker();
    {
        std::lock_guard lock(m_workerMutex);
        m_tasks.push_back(std::move(workerTask));
    }
    m_workerCv.notify_one();
}

void APIClient::PostMainCallback(std::function<void()> mainCallback)
{
    std::lock_guard lock(m_cbMutex);
    m_callbacks.push_back(std::move(mainCallback));
}

void APIClient::PumpCallbacks()
{
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard lock(m_cbMutex);
        batch.swap(m_callbacks);
    }
    for (auto& fn : batch)
    {
        if (fn) fn();
    }
}

void APIClient::LoginGuestAsync(const std::string& nickname, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, nickname, cb = std::move(cb)]() mutable
    {
        auto res = LoginGuest(nickname);
        if (res.ok) FlushPendingScores();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RegisterAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, username, password, cb = std::move(cb)]() mutable
    {
        auto res = Register(username, password);
        if (res.ok) FlushPendingScores();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::LoginAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb)
{
    m_status.store(OnlineStatus::Connecting);
    PostTask([this, username, password, cb = std::move(cb)]() mutable
    {
        auto res = Login(username, password);
        if (res.ok) FlushPendingScores();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RefreshProfileAsync(std::function<void(AuthResult)> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        auto res = RefreshProfile();
        if (res.ok) FlushPendingScores();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SubmitScoreAsync(const sakura::game::GameResult& result, std::function<void(ScoreSubmitResult)> cb)
{
    PostTask([this, result, cb = std::move(cb)]() mutable
    {
        auto res = SubmitScore(result);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchLeaderboardAsync(const std::string& chartId, const std::string& difficulty, int limit,
                                      std::function<void(LeaderboardResult)> cb)
{
    PostTask([this, chartId, difficulty, limit, cb = std::move(cb)]() mutable
    {
        auto res = FetchLeaderboard(chartId, difficulty, limit);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::DownloadReplayAsync(const std::string& scoreId, const std::string& destPath,
                                    std::function<void(bool, std::string)> cb)
{
    PostTask([this, scoreId, destPath, cb = std::move(cb)]() mutable
    {
        const bool ok = DownloadReplay(scoreId, destPath);
        if (cb) PostMainCallback([cb = std::move(cb), ok, destPath]() mutable { cb(ok, destPath); });
    });
}

} // namespace sakura::net
