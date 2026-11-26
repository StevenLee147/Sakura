#include "net/api_client.h"

#include "core/config.h"
#include "core/paths.h"
#include "data/database.h"
#include "game/achievement_manager.h"
#include "game/chart_loader.h"
#include "game/course.h"
#include "utils/archive.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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

std::string JsonStringOr(const nlohmann::json& obj, const char* key, const std::string& fallback = {})
{
    if (obj.contains(key) && obj[key].is_null())
        return fallback;
    return obj.value(key, fallback);
}

LeaderboardRow ParseLeaderboardRowJson(const nlohmann::json& item)
{
    LeaderboardRow row;
    row.rank         = item.value("rank", 0);
    row.scoreId      = JsonStringOr(item, "score_id", "");
    row.userId       = JsonStringOr(item, "user_id", "");
    row.username     = JsonStringOr(item, "username", "Unknown");
    row.score        = item.value("score", 0);
    row.accuracy     = item.value("accuracy", 0.0f);
    row.maxCombo     = item.value("max_combo", 0);
    row.grade        = JsonStringOr(item, "grade", "D");
    row.pp           = (item.contains("pp") && item["pp"].is_number()) ? item["pp"].get<double>() : 0.0;
    row.isFullCombo  = (item.contains("is_full_combo") && item["is_full_combo"].is_number_integer())
        ? (item["is_full_combo"].get<int>() != 0)
        : item.value("is_full_combo", false);
    row.isAllPerfect = (item.contains("is_all_perfect") && item["is_all_perfect"].is_number_integer())
        ? (item["is_all_perfect"].get<int>() != 0)
        : item.value("is_all_perfect", false);
    row.hasReplay    = (item.contains("has_replay") && item["has_replay"].is_number_integer())
        ? (item["has_replay"].get<int>() != 0)
        : item.value("has_replay", false);
    row.playedAt     = item.value("played_at", 0LL);
    row.danBadge     = (item.contains("dan_badge") && item["dan_badge"].is_null())
        ? std::string{}
        : item.value("dan_badge", "");
    return row;
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
    m_user.danBadge = cfg.Get<std::string>("online.dan_badge", "");
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
    cfg.Set("online.dan_badge", m_user.danBadge);
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

        const auto& u     = j.at("user");
        out.user.id       = u.value("id", "");
        out.user.username = u.value("username", "");
        out.user.isGuest  = u.value("is_guest", false);
        out.user.totalPP  = u.value("total_pp", 0.0);
        out.user.danBadge = (u.contains("dan_badge") && u["dan_badge"].is_null())
            ? std::string{}
            : u.value("dan_badge", "");

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
                out.entries.push_back(ParseLeaderboardRowJson(item));
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

namespace
{
std::string ToLowerCopy(std::string s)
{
    for (char& ch : s)
    {
        if (static_cast<unsigned char>(ch) < 128)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

sakura::game::Grade ParseGradeStr(const std::string& s)
{
    using sakura::game::Grade;
    if (s == "SS") return Grade::SS;
    if (s == "S")  return Grade::S;
    if (s == "A")  return Grade::A;
    if (s == "B")  return Grade::B;
    if (s == "C")  return Grade::C;
    return Grade::D;
}

MarketChartItem ParseMarketChartJson(const nlohmann::json& j)
{
    MarketChartItem item;
    item.id            = j.value("id", "");
    item.chartSlug     = j.value("chart_slug", item.id);
    item.uploaderId    = j.value("uploader_id", "");
    item.uploaderName  = j.value("uploader_name", "Unknown");
    item.title         = j.value("title", "Untitled");
    item.artist        = j.value("artist", "Unknown");
    item.charter       = j.value("charter", "Unknown");
    item.description   = j.value("description", "");
    item.bpm           = j.value("bpm", 120.0f);
    item.minLevel      = j.value("min_level", 1.0f);
    item.maxLevel      = j.value("max_level", 1.0f);
    item.packageSha256 = j.value("package_sha256", "");
    item.contentHash   = j.value("content_hash", "");
    item.packageSize   = static_cast<std::size_t>(j.value("package_size", 0ULL));
    item.downloads     = j.value("downloads", 0);
    item.ratingAvg     = j.value("rating_avg", 0.0);
    item.ratingCount   = j.value("rating_count", 0);
    if (j.contains("my_rating") && j["my_rating"].is_number_integer())
        item.myRating = j["my_rating"].get<int>();
    item.createdAt     = j.value("created_at", 0LL);
    item.updatedAt     = j.value("updated_at", 0LL);

    if (j.contains("tags") && j["tags"].is_array())
    {
        for (const auto& t : j["tags"])
            if (t.is_string()) item.tags.push_back(t.get<std::string>());
    }
    if (j.contains("difficulties") && j["difficulties"].is_array())
    {
        for (const auto& d : j["difficulties"])
        {
            MarketDifficultyInfo di;
            di.name      = d.value("name", "Normal");
            di.level     = d.value("level", 1.0f);
            di.chartFile = d.value("chart_file", "");
            di.noteCount = d.value("note_count", 0);
            item.difficulties.push_back(std::move(di));
        }
    }
    return item;
}

std::string SanitizeFolderSlug(std::string raw)
{
    if (raw.empty()) return "market_chart";
    for (char& c : raw)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 128 && !(std::isalnum(uc) || c == '-' || c == '_'))
            c = '_';
    }
    return raw;
}
}

MarketInstallState APIClient::GetInstalledMarketState(const MarketChartItem& item,
                                                      const std::string& chartsRootDir,
                                                      std::string* outInstalledChartId)
{
    namespace fs = std::filesystem;
    const fs::path root = chartsRootDir.empty() ? fs::path(sakura::core::Paths::User("charts")) : fs::path(chartsRootDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return MarketInstallState::NotInstalled;

    for (const auto& entry : fs::directory_iterator(root, ec))
    {
        if (ec || !entry.is_directory(ec)) continue;
        const fs::path metaFile = entry.path() / ".market_meta.json";
        if (!fs::is_regular_file(metaFile, ec)) continue;
        try
        {
            std::ifstream ifs(metaFile);
            const auto meta = nlohmann::json::parse(ifs);
            if (meta.value("market_id", "") == item.id)
            {
                if (outInstalledChartId)
                    *outInstalledChartId = meta.value("local_chart_id", item.chartSlug);
                const std::string localSha = ToLowerCopy(meta.value("package_sha256", ""));
                if (!item.packageSha256.empty() && localSha != ToLowerCopy(item.packageSha256))
                    return MarketInstallState::UpdateAvailable;
                return MarketInstallState::Installed;
            }
        }
        catch (...) {}
    }
    return MarketInstallState::NotInstalled;
}

MarketPublishResult APIClient::PublishChartPackage(const std::string& sakuraFilePath, const std::string& description)
{
    MarketPublishResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号后再发布谱面";
        return out;
    }
    const std::string pkgBytes = ReadWholeFileUtf8(sakuraFilePath);
    if (pkgBytes.empty())
    {
        out.error = "无法读取 .sakura 谱面包文件";
        return out;
    }

    std::string path = "/api/v1/charts";
    if (!description.empty())
        path += "?description=" + UrlEncode(description);

    HttpRequest req = MakeRequest("POST", path, {}, true);
    req.headers.emplace_back("Content-Type", "application/octet-stream");
    req.body      = pkgBytes;
    req.timeoutMs = 30000;

    const auto res = GetTransport()->Send(req);
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "网络连接失败，无法上传谱面" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "谱面上传失败");
            return out;
        }
        out.ok    = true;
        out.chart = ParseMarketChartJson(j.at("chart"));
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("发布响应解析失败: ") + e.what();
        return out;
    }
}

MarketListResult APIClient::FetchMarketCharts(const MarketQuery& query)
{
    MarketListResult out;
    std::ostringstream qs;
    qs << "/api/v1/charts?sort=" << UrlEncode(query.sort.empty() ? "newest" : query.sort)
       << "&minLevel=" << query.minLevel
       << "&maxLevel=" << query.maxLevel
       << "&page=" << std::max(1, query.page)
       << "&pageSize=" << std::max(1, query.pageSize);
    if (!query.q.empty())   qs << "&q=" << UrlEncode(query.q);
    if (!query.tag.empty()) qs << "&tag=" << UrlEncode(query.tag);

    const auto res = GetTransport()->Send(MakeRequest("GET", qs.str(), {}, true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：无法连接在线谱面市场" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "获取谱面市场列表失败");
            return out;
        }
        out.ok    = true;
        out.total = j.value("total", 0);
        if (j.contains("charts") && j["charts"].is_array())
        {
            for (const auto& item : j["charts"])
                out.charts.push_back(ParseMarketChartJson(item));
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("市场响应解析失败: ") + e.what();
        return out;
    }
}

MarketDownloadResult APIClient::DownloadAndInstallChart(const MarketChartItem& item,
                                                        const std::string& chartsRootDir,
                                                        std::function<void(float)> onProgress)
{
    MarketDownloadResult out;
    if (item.id.empty())
    {
        out.error = "无效的市场谱面 ID";
        return out;
    }

    HttpRequest req = MakeRequest("GET", "/api/v1/charts/" + UrlEncode(item.id) + "/download", {}, false);
    req.timeoutMs = 30000;
    const std::size_t fallbackTotal = item.packageSize;
    if (onProgress)
    {
        req.onProgress = [onProgress, fallbackTotal](std::size_t dl, std::size_t total)
        {
            const std::size_t denom = total > 0 ? total : fallbackTotal;
            const float frac = denom > 0
                ? std::clamp(static_cast<float>(dl) / static_cast<float>(denom), 0.0f, 1.0f)
                : 0.5f;
            onProgress(frac);
        };
    }

    const auto res = GetTransport()->Send(req);
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "下载失败：网络不可达" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess() || res.body.empty())
    {
        out.error = "下载谱面包失败 (HTTP " + std::to_string(res.statusCode) + ")";
        return out;
    }

    std::string expectedSha = res.GetHeader("X-Sakura-Sha256");
    if (expectedSha.empty())
        expectedSha = item.packageSha256;
    const std::string actualSha = sakura::utils::ComputeSha256Hex(res.body);
    if (!expectedSha.empty() && ToLowerCopy(actualSha) != ToLowerCopy(expectedSha))
    {
        out.error = "SHA-256 完整性校验失败，已拦截安装";
        return out;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path rootDir = chartsRootDir.empty()
        ? fs::path(sakura::core::Paths::User("charts"))
        : fs::path(chartsRootDir);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path stageRoot = rootDir.parent_path() / "imports" / ("mkt-" + std::to_string(stamp));
    const fs::path pkgFile   = stageRoot / "download.sakura";
    const fs::path unpackDir = stageRoot / "unpacked";
    fs::create_directories(unpackDir, ec);

    auto cleanupStage = [&]() { fs::remove_all(stageRoot, ec); };

    if (!sakura::utils::AtomicWrite(pkgFile, res.body) ||
        !sakura::utils::ExtractZipArchive(pkgFile, unpackDir))
    {
        cleanupStage();
        out.error = ".sakura 谱面包解压失败";
        return out;
    }

    fs::path chartFolder = unpackDir;
    if (!fs::is_regular_file(chartFolder / "info.json", ec))
    {
        for (const auto& sub : fs::directory_iterator(unpackDir, ec))
        {
            if (sub.is_directory(ec) && fs::is_regular_file(sub.path() / "info.json", ec))
            {
                chartFolder = sub.path();
                break;
            }
        }
    }

    sakura::game::ChartLoader loader;
    const auto info = loader.LoadChartInfo((chartFolder / "info.json").string());
    if (!info || !fs::is_regular_file(chartFolder / info->musicFile, ec))
    {
        cleanupStage();
        out.error = "谱面包缺少有效的 info.json 或音频文件";
        return out;
    }
    for (const auto& diff : info->difficulties)
    {
        const auto cdata = loader.LoadChartData(info->folderPath + "/" + diff.chartFile);
        if (!cdata || !loader.ValidateChartData(*cdata))
        {
            cleanupStage();
            out.error = "难度谱面校验不通过：" + diff.name;
            return out;
        }
    }

    const nlohmann::json meta = {
        { "market_id", item.id },
        { "local_chart_id", info->id },
        { "package_sha256", actualSha },
        { "updated_at", item.updatedAt }
    };
    sakura::utils::AtomicWrite(chartFolder / ".market_meta.json", meta.dump(2));

    const fs::path destDir = rootDir / ("market_" + SanitizeFolderSlug(item.id));
    fs::create_directories(rootDir, ec);
    if (fs::exists(destDir, ec))
        fs::remove_all(destDir, ec);
    fs::rename(chartFolder, destDir, ec);
    if (ec)
    {
        ec.clear();
        fs::copy(chartFolder, destDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            cleanupStage();
            out.error = "无法写入本地曲库目录";
            return out;
        }
    }

    cleanupStage();
    out.ok              = true;
    out.installedFolder = destDir.generic_string();
    out.chartId         = info->id;
    return out;
}

MarketRateResult APIClient::RateMarketChart(const std::string& marketChartId, int rating, const std::string& comment)
{
    MarketRateResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号";
        return out;
    }
    const nlohmann::json body = { { "rating", rating }, { "comment", comment } };
    const auto res = GetTransport()->Send(
        MakeRequest("POST", "/api/v1/charts/" + UrlEncode(marketChartId) + "/rate", body.dump(), true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：无法提交评价" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "评分提交失败");
            return out;
        }
        out.ok          = true;
        out.ratingAvg   = j.value("rating_avg", 0.0);
        out.ratingCount = j.value("rating_count", 0);
        out.myRating    = j.value("my_rating", rating);
        return out;
    }
    catch (const std::exception& e)
    {
        out.error = std::string("评分解析失败: ") + e.what();
        return out;
    }
}

CloudSyncResult APIClient::SyncCloudSave()
{
    CloudSyncResult out;
    if (!IsAuthenticated())
    {
        out.error = "尚未登录在线账号";
        return out;
    }

    FlushPendingScores();

    auto& db  = sakura::data::Database::GetInstance();
    auto& cfg = sakura::core::Config::GetInstance();

    nlohmann::json scoresArr = nlohmann::json::array();
    const auto localScores = db.GetAllBestScores();
    for (const auto& s : localScores)
    {
        scoresArr.push_back({
            { "chart_id", s.chartId },
            { "chart_title", s.chartTitle },
            { "chart_hash", s.chartHash.empty() ? s.chartId : s.chartHash },
            { "difficulty", s.difficulty },
            { "difficulty_level", s.difficultyLevel },
            { "score", s.score },
            { "accuracy", s.accuracy },
            { "max_combo", s.maxCombo },
            { "grade", GradeToStr(s.grade) },
            { "perfect_count", s.perfectCount },
            { "great_count", s.greatCount },
            { "good_count", s.goodCount },
            { "bad_count", s.badCount },
            { "miss_count", s.missCount },
            { "is_full_combo", s.isFullCombo },
            { "is_all_perfect", s.isAllPerfect },
            { "played_at", s.playedAt }
        });
    }

    nlohmann::json achArr = nlohmann::json::array();
    const auto localAchs = db.GetAchievements();
    for (const auto& a : localAchs)
    {
        achArr.push_back({ { "id", a.id }, { "unlocked_at", a.unlockedAt } });
    }

    nlohmann::json coursesArr = nlohmann::json::array();
    const auto localCourses = db.GetAllCourseRecords();
    for (const auto& c : localCourses)
    {
        coursesArr.push_back({
            { "course_id", c.courseId },
            { "badge_text", c.badgeText },
            { "dan_rank", c.danRank },
            { "total_score", c.totalScore },
            { "avg_accuracy", c.avgAccuracy },
            { "remaining_life", c.remainingLife },
            { "cleared", c.cleared },
            { "cleared_at", c.clearedAt }
        });
    }

    const long long nowSec = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const long long origLocalSettingsTs = cfg.Get<long long>("online.settings_updated_at", 0LL);
    long long localSettingsTs           = origLocalSettingsTs;

    const auto localCols = sakura::game::CollectionManager::LoadFromConfig();
    if (!localCols.empty() && localSettingsTs == 0)
        localSettingsTs = nowSec;

    nlohmann::json colsArr = nlohmann::json::array();
    for (const auto& col : localCols)
    {
        colsArr.push_back({
            { "id", col.id },
            { "name", col.name },
            { "chart_ids", col.chartIds },
            { "updated_at", col.updatedAt }
        });
    }

    nlohmann::json settingsObj = {
        { "note_speed", cfg.Get<float>("gameplay.note_speed", 5.0f) },
        { "mouse_approach_ms", cfg.Get<int>("gameplay.mouse_approach_ms", 1000) },
        { "global_offset_ms", cfg.Get<int>("audio.global_offset_ms", 0) },
        { "pitch_lock", cfg.Get<bool>("audio.pitch_lock", true) },
        { "key_lane_0", cfg.Get<int>("input.key_lane_0", 4) },
        { "key_lane_1", cfg.Get<int>("input.key_lane_1", 22) },
        { "key_lane_2", cfg.Get<int>("input.key_lane_2", 7) },
        { "key_lane_3", cfg.Get<int>("input.key_lane_3", 9) },
        { "key_thumb",  cfg.Get<int>("input.key_thumb", 44) },
        { "collections", colsArr }
    };

    const nlohmann::json pushBody = {
        { "scores", scoresArr },
        { "achievements", achArr },
        { "course_records", coursesArr },
        { "settings", localSettingsTs > 0 ? settingsObj : nlohmann::json::object() },
        { "settings_updated_at", localSettingsTs }
    };

    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/sync/push", pushBody.dump(), true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：云端同步暂不可用" : res.error;
        return out;
    }
    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = j.value("error", "云端同步失败");
            return out;
        }

        out.ok           = true;
        out.scoresPushed = j.value("scores_merged", 0);
        out.syncedAt     = j.value("synced_at", nowSec);

        if (j.contains("scores") && j["scores"].is_array())
        {
            for (const auto& rs : j["scores"])
            {
                sakura::game::GameResult gr;
                gr.chartId         = rs.value("chart_id", "");
                gr.chartTitle      = rs.value("chart_title", gr.chartId);
                gr.chartHash       = rs.value("chart_hash", "");
                gr.difficulty      = rs.value("difficulty", "Normal");
                gr.difficultyLevel = rs.value("difficulty_level", 1.0f);
                gr.score           = rs.value("score", 0);
                gr.accuracy        = rs.value("accuracy", 0.0f);
                gr.maxCombo        = rs.value("max_combo", 0);
                gr.grade           = ParseGradeStr(rs.value("grade", "D"));
                gr.perfectCount    = rs.value("perfect_count", 0);
                gr.greatCount      = rs.value("great_count", 0);
                gr.goodCount       = rs.value("good_count", 0);
                gr.badCount        = rs.value("bad_count", 0);
                gr.missCount       = rs.value("miss_count", 0);
                gr.isFullCombo     = rs.value("is_full_combo", false);
                gr.isAllPerfect    = rs.value("is_all_perfect", false);
                gr.playedAt        = rs.value("played_at", 0LL);
                if (db.MergeCloudScore(gr))
                    ++out.scoresPulled;
            }
        }

        if (j.contains("achievements") && j["achievements"].is_array())
        {
            for (const auto& ra : j["achievements"])
            {
                const std::string aid = ra.value("id", "");
                const long long ats   = ra.value("unlocked_at", 0LL);
                if (db.MergeCloudAchievement(aid, ats))
                    ++out.achievementsPulled;
            }
            sakura::game::AchievementManager::GetInstance().ReloadUnlocked();
        }

        if (j.contains("course_records") && j["course_records"].is_array())
        {
            for (const auto& rc : j["course_records"])
            {
                if (!rc.is_object()) continue;
                sakura::game::CourseRecord cr;
                cr.courseId      = JsonStringOr(rc, "course_id", "");
                cr.badgeText     = JsonStringOr(rc, "badge_text", "");
                cr.danRank       = rc.value("dan_rank", 0);
                cr.totalScore    = rc.value("total_score", 0);
                cr.avgAccuracy   = rc.value("avg_accuracy", 0.0f);
                cr.remainingLife = rc.value("remaining_life", 0.0f);
                cr.cleared       = (rc.contains("cleared") && rc["cleared"].is_number_integer())
                    ? (rc["cleared"].get<int>() != 0)
                    : rc.value("cleared", false);
                cr.clearedAt     = rc.value("cleared_at", 0LL);
                if (!cr.courseId.empty() && db.MergeCloudCourseRecord(cr))
                    ++out.coursesPulled;
            }
        }

        if (const auto highest = db.GetHighestClearedCourse(); highest.has_value() && highest->cleared)
        {
            std::string badge = highest->badgeText;
            if (badge.empty())
            {
                if (const auto* def = sakura::game::FindOfficialCourse(highest->courseId))
                    badge = def->badgeText;
            }
            if (!badge.empty())
            {
                {
                    std::lock_guard lock(m_stateMutex);
                    m_user.danBadge = badge;
                }
                cfg.Set("online.dan_badge", badge);
            }
        }

        const long long remoteSetTs = j.value("settings_updated_at", 0LL);
        if (remoteSetTs > localSettingsTs && j.contains("settings") && j["settings"].is_object())
        {
            const auto& rs = j["settings"];
            if (rs.contains("note_speed") && rs["note_speed"].is_number())
                cfg.Set("gameplay.note_speed", std::clamp(rs["note_speed"].get<float>(), 0.5f, 15.0f));
            if (rs.contains("mouse_approach_ms") && rs["mouse_approach_ms"].is_number_integer())
                cfg.Set("gameplay.mouse_approach_ms", std::clamp(rs["mouse_approach_ms"].get<int>(), 400, 2000));
            if (rs.contains("global_offset_ms") && rs["global_offset_ms"].is_number_integer())
                cfg.Set("audio.global_offset_ms", std::clamp(rs["global_offset_ms"].get<int>(), -500, 500));
            if (rs.contains("pitch_lock") && rs["pitch_lock"].is_boolean())
                cfg.Set("audio.pitch_lock", rs["pitch_lock"].get<bool>());
            for (const char* k : { "key_lane_0", "key_lane_1", "key_lane_2", "key_lane_3", "key_thumb" })
            {
                if (rs.contains(k) && rs[k].is_number_integer())
                {
                    const int sc = rs[k].get<int>();
                    if (sc > 0 && sc < 512)
                        cfg.Set(std::string("input.") + k, sc);
                }
            }
            cfg.Set("online.settings_updated_at", remoteSetTs);
        }
        else if (localSettingsTs == 0)
        {
            // Seed cloud settings if neither local nor cloud had a timestamp yet
            const nlohmann::json seedBody = {
                { "scores", nlohmann::json::array() },
                { "achievements", nlohmann::json::array() },
                { "course_records", nlohmann::json::array() },
                { "settings", settingsObj },
                { "settings_updated_at", nowSec }
            };
            GetTransport()->Send(MakeRequest("POST", "/api/v1/sync/push", seedBody.dump(), true));
            cfg.Set("online.settings_updated_at", nowSec);
        }
        else if (origLocalSettingsTs == 0)
        {
            cfg.Set("online.settings_updated_at", localSettingsTs);
        }

        if (j.contains("settings") && j["settings"].is_object() &&
            j["settings"].contains("collections") && j["settings"]["collections"].is_array())
        {
            std::vector<sakura::game::SongCollection> remoteCols;
            for (const auto& item : j["settings"]["collections"])
            {
                if (!item.is_object()) continue;
                sakura::game::SongCollection col;
                col.id        = JsonStringOr(item, "id", "");
                col.name      = JsonStringOr(item, "name", "");
                col.updatedAt = item.value("updated_at", 0LL);
                if (item.contains("chart_ids") && item["chart_ids"].is_array())
                {
                    for (const auto& cid : item["chart_ids"])
                    {
                        if (cid.is_string())
                            col.chartIds.push_back(cid.get<std::string>());
                    }
                }
                if (!col.id.empty() || !col.name.empty())
                    remoteCols.push_back(std::move(col));
            }
            const auto merged = sakura::game::CollectionManager::MergeCollections(
                sakura::game::CollectionManager::LoadFromConfig(), remoteCols);
            sakura::game::CollectionManager::SaveToConfig(merged);
        }

        cfg.Set("online.last_cloud_sync_ts", out.syncedAt);
        cfg.Save();
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("云同步解析失败: ") + e.what();
        return out;
    }
}

void APIClient::PublishChartPackageAsync(const std::string& sakuraFilePath, const std::string& description,
                                         std::function<void(MarketPublishResult)> cb)
{
    PostTask([this, sakuraFilePath, description, cb = std::move(cb)]() mutable
    {
        auto res = PublishChartPackage(sakuraFilePath, description);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchMarketChartsAsync(const MarketQuery& query, std::function<void(MarketListResult)> cb)
{
    PostTask([this, query, cb = std::move(cb)]() mutable
    {
        auto res = FetchMarketCharts(query);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::DownloadAndInstallChartAsync(const MarketChartItem& item, const std::string& chartsRootDir,
                                             std::function<void(float)> onProgress,
                                             std::function<void(MarketDownloadResult)> cb)
{
    PostTask([this, item, chartsRootDir, onProgress = std::move(onProgress), cb = std::move(cb)]() mutable
    {
        auto res = DownloadAndInstallChart(item, chartsRootDir, [this, onProgress](float p)
        {
            if (onProgress)
                PostMainCallback([onProgress, p]() { onProgress(p); });
        });
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::RateMarketChartAsync(const std::string& marketChartId, int rating, const std::string& comment,
                                     std::function<void(MarketRateResult)> cb)
{
    PostTask([this, marketChartId, rating, comment, cb = std::move(cb)]() mutable
    {
        auto res = RateMarketChart(marketChartId, rating, comment);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SyncCloudSaveAsync(std::function<void(CloudSyncResult)> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        auto res = SyncCloudSave();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

DailyInfoResult APIClient::FetchDailyChallenge(const std::string& dateUtc8, int limit)
{
    DailyInfoResult out;
    const std::string targetDate = dateUtc8.empty() ? sakura::game::GetCurrentDateUtc8() : dateUtc8;
    const std::string path = "/api/v1/daily?date=" + UrlEncode(targetDate)
        + "&limit=" + std::to_string(std::max(1, limit));

    const auto res = GetTransport()->Send(MakeRequest("GET", path, {}, true));
    if (res.IsNetworkError() || res.statusCode == 404)
    {
        if (res.IsNetworkError())
            m_status.store(OnlineStatus::Offline);
        out.ok              = true;
        out.offlineFallback = true;
        out.spec            = sakura::game::ComputeOfflineDailyChallenge(targetDate);
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = JsonStringOr(j, "error", "获取每日挑战失败");
            return out;
        }

        const nlohmann::json& d = (j.contains("daily") && j["daily"].is_object())
            ? j["daily"]
            : ((j.contains("challenge") && j["challenge"].is_object())
                ? j["challenge"]
                : ((j.contains("spec") && j["spec"].is_object()) ? j["spec"] : j));

        out.ok                   = true;
        out.offlineFallback      = false;
        out.spec.dateUtc8        = JsonStringOr(d, "date", targetDate);
        out.spec.chartId         = JsonStringOr(d, "chart_id", "");
        out.spec.chartTitle      = JsonStringOr(d, "chart_title", "");
        out.spec.difficultyName  = (d.contains("difficulty") && d["difficulty"].is_string())
            ? d["difficulty"].get<std::string>()
            : JsonStringOr(d, "difficulty_name", "Hard");
        out.spec.difficultyLevel = d.value("difficulty_level", 7.0f);
        out.spec.goalText        = JsonStringOr(d, "goal_text", "");
        out.spec.targetScore     = d.value("target_score", 950000);

        const nlohmann::json& lbJson = (j.contains("leaderboard") && j["leaderboard"].is_object())
            ? j["leaderboard"]
            : ((j.contains("entries") || j.contains("my_rank")) ? j : d);

        out.leaderboard.ok = true;
        if (lbJson.contains("my_rank") && lbJson["my_rank"].is_number_integer())
            out.leaderboard.myRank = lbJson["my_rank"].get<int>();
        else if (j.contains("my_rank") && j["my_rank"].is_number_integer())
            out.leaderboard.myRank = j["my_rank"].get<int>();

        if (lbJson.contains("entries") && lbJson["entries"].is_array())
        {
            for (const auto& item : lbJson["entries"])
                out.leaderboard.entries.push_back(ParseLeaderboardRowJson(item));
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("每日挑战响应解析失败: ") + e.what();
        return out;
    }
}

DailyScoreSubmitResult APIClient::SubmitDailyScore(const std::string& dateUtc8,
                                                   const sakura::game::GameResult& result)
{
    DailyScoreSubmitResult out;
    if (!IsAuthenticated())
    {
        out.error = "请先登录在线账号后再提交每日挑战成绩";
        return out;
    }
    if (result.assisted)
    {
        out.error = "辅助演奏不计入每日挑战排行榜";
        return out;
    }

    const std::string targetDate = dateUtc8.empty() ? sakura::game::GetCurrentDateUtc8() : dateUtc8;
    const int totalJudged = result.totalJudgments > 0
        ? result.totalJudgments
        : (result.perfectCount + result.greatCount + result.goodCount + result.badCount + result.missCount);

    nlohmann::json payload = {
        { "date", targetDate },
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

    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/daily/scores", payload.dump(), true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：无法提交每日挑战成绩" : res.error;
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !j.value("ok", false))
        {
            out.error = JsonStringOr(j, "error", "每日挑战成绩提交失败");
            return out;
        }
        out.ok      = true;
        out.rank    = (j.contains("rank") && j["rank"].is_number_integer()) ? j["rank"].get<int>() : 0;
        out.newBest = (j.contains("new_best") && j["new_best"].is_boolean())
            ? j["new_best"].get<bool>()
            : j.value("new_best", false);
        if (j.contains("total_pp") && j["total_pp"].is_number())
        {
            {
                std::lock_guard lock(m_stateMutex);
                m_user.totalPP = j["total_pp"].get<double>();
            }
            SaveToConfig();
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("每日挑战成绩响应解析失败: ") + e.what();
        return out;
    }
}

void APIClient::FetchDailyChallengeAsync(const std::string& dateUtc8, int limit,
                                         std::function<void(DailyInfoResult)> cb)
{
    PostTask([this, dateUtc8, limit, cb = std::move(cb)]() mutable
    {
        auto res = FetchDailyChallenge(dateUtc8, limit);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SubmitDailyScoreAsync(const std::string& dateUtc8, const sakura::game::GameResult& result,
                                      std::function<void(DailyScoreSubmitResult)> cb)
{
    PostTask([this, dateUtc8, result, cb = std::move(cb)]() mutable
    {
        auto res = SubmitDailyScore(dateUtc8, result);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

namespace
{
std::string TrimRoomCode(std::string code)
{
    while (!code.empty() && (code.front() == ' ' || code.front() == '\t' || code.front() == '\r' || code.front() == '\n'))
        code.erase(code.begin());
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t' || code.back() == '\r' || code.back() == '\n'))
        code.pop_back();
    return code;
}

bool JsonBoolOr(const nlohmann::json& obj, const char* key, bool fallback = false)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number_integer())
        return v.get<long long>() != 0;
    return fallback;
}

int JsonIntOr(const nlohmann::json& obj, const char* key, int fallback = 0)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_number_integer())
        return static_cast<int>(v.get<long long>());
    if (v.is_number())
        return static_cast<int>(v.get<double>());
    return fallback;
}

long long JsonInt64Or(const nlohmann::json& obj, const char* key, long long fallback = 0LL)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_number_integer())
        return v.get<long long>();
    if (v.is_number())
        return static_cast<long long>(v.get<double>());
    return fallback;
}

float JsonFloatOr(const nlohmann::json& obj, const char* key, float fallback = 0.0f)
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    const auto& v = obj[key];
    if (v.is_number())
        return static_cast<float>(v.get<double>());
    return fallback;
}

std::string SafeJsonStringOr(const nlohmann::json& obj, const char* key, const std::string& fallback = {})
{
    if (!obj.contains(key) || obj[key].is_null())
        return fallback;
    if (obj[key].is_string())
        return obj[key].get<std::string>();
    return fallback;
}

ArenaRoomState ParseArenaRoomJson(const nlohmann::json& r)
{
    ArenaRoomState room;
    if (!r.is_object())
        return room;

    room.code            = SafeJsonStringOr(r, "code", "");
    room.name            = SafeJsonStringOr(r, "name", "");
    room.hostId          = SafeJsonStringOr(r, "host_id", "");
    room.hostName        = SafeJsonStringOr(r, "host_name", "");
    room.status          = SafeJsonStringOr(r, "status", "waiting");
    room.chartId         = SafeJsonStringOr(r, "chart_id", "");
    room.chartTitle      = SafeJsonStringOr(r, "chart_title", "");
    room.difficulty      = SafeJsonStringOr(r, "difficulty", "");
    room.difficultyLevel = JsonFloatOr(r, "difficulty_level", 1.0f);
    room.chartHash       = SafeJsonStringOr(r, "chart_hash", "");
    room.startedAt       = JsonInt64Or(r, "started_at", 0LL);
    room.updatedAt       = JsonInt64Or(r, "updated_at", 0LL);

    if (r.contains("players") && r["players"].is_array())
    {
        for (const auto& p : r["players"])
        {
            if (!p.is_object())
                continue;
            ArenaPlayerState ps;
            ps.userId    = SafeJsonStringOr(p, "user_id", "");
            ps.username  = SafeJsonStringOr(p, "username", "");
            ps.danBadge  = SafeJsonStringOr(p, "dan_badge", "");
            ps.isHost    = JsonBoolOr(p, "is_host", false);
            ps.ready     = JsonBoolOr(p, "ready", false);
            ps.finished  = JsonBoolOr(p, "finished", false);
            ps.score     = JsonIntOr(p, "score", 0);
            ps.accuracy  = JsonFloatOr(p, "accuracy", 0.0f);
            ps.combo     = JsonIntOr(p, "combo", 0);
            ps.maxCombo  = JsonIntOr(p, "max_combo", 0);
            ps.grade     = SafeJsonStringOr(p, "grade", "D");
            ps.placement = JsonIntOr(p, "placement", 0);
            room.players.push_back(std::move(ps));
        }
    }
    return room;
}

ArenaRoomResult ParseArenaRoomResponse(const HttpResponse& res,
                                       std::atomic<OnlineStatus>& status,
                                       const char* fallbackError)
{
    ArenaRoomResult out;
    if (res.IsNetworkError())
    {
        status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：无法连接至对战服务器" : res.error;
        return out;
    }

    status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", fallbackError);
            return out;
        }
        const nlohmann::json& roomJson = (j.contains("room") && j["room"].is_object()) ? j["room"] : j;
        out.ok   = true;
        out.room = ParseArenaRoomJson(roomJson);
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("对战房间响应解析失败: ") + e.what();
        return out;
    }
}
} // namespace

ArenaRoomResult APIClient::CreateArenaRoom(const std::string& roomName,
                                           const std::string& chartId,
                                           const std::string& chartTitle,
                                           const std::string& difficulty,
                                           float difficultyLevel,
                                           const std::string& chartHash)
{
    const nlohmann::json body = {
        { "name", roomName },
        { "chart_id", chartId },
        { "chart_title", chartTitle },
        { "difficulty", difficulty },
        { "difficulty_level", difficultyLevel },
        { "chart_hash", chartHash }
    };
    const auto res = GetTransport()->Send(MakeRequest("POST", "/api/v1/rooms", body.dump(), true));
    return ParseArenaRoomResponse(res, m_status, "创建对战房间失败");
}

ArenaRoomListResult APIClient::ListArenaRooms()
{
    ArenaRoomListResult out;
    const auto res = GetTransport()->Send(MakeRequest("GET", "/api/v1/rooms", {}, true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        out.error = res.error.empty() ? "离线：无法获取对战房间列表" : res.error;
        return out;
    }

    m_status.store(OnlineStatus::Online);
    try
    {
        const auto j = nlohmann::json::parse(res.body);
        if (!res.IsSuccess() || !JsonBoolOr(j, "ok", false))
        {
            out.error = SafeJsonStringOr(j, "error", "获取对战房间列表失败");
            return out;
        }
        out.ok = true;
        if (j.contains("rooms") && j["rooms"].is_array())
        {
            for (const auto& item : j["rooms"])
            {
                if (item.is_object())
                    out.rooms.push_back(ParseArenaRoomJson(item));
            }
        }
        return out;
    }
    catch (const std::exception& e)
    {
        out.ok    = false;
        out.error = std::string("对战房间列表解析失败: ") + e.what();
        return out;
    }
}

ArenaRoomResult APIClient::JoinArenaRoom(const std::string& code)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/join";
    const auto res = GetTransport()->Send(MakeRequest("POST", path, "{}", true));
    return ParseArenaRoomResponse(res, m_status, "加入对战房间失败");
}

bool APIClient::LeaveArenaRoom(const std::string& code)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
        return false;

    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/leave";
    const auto res = GetTransport()->Send(MakeRequest("POST", path, "{}", true));
    if (res.IsNetworkError())
    {
        m_status.store(OnlineStatus::Offline);
        return false;
    }

    m_status.store(OnlineStatus::Online);
    if (!res.IsSuccess())
        return false;
    if (res.body.empty())
        return true;

    try
    {
        const auto j = nlohmann::json::parse(res.body);
        return JsonBoolOr(j, "ok", true);
    }
    catch (...)
    {
        return false;
    }
}

ArenaRoomResult APIClient::SelectArenaChart(const std::string& code,
                                            const std::string& chartId,
                                            const std::string& chartTitle,
                                            const std::string& difficulty,
                                            float difficultyLevel,
                                            const std::string& chartHash)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const nlohmann::json body = {
        { "chart_id", chartId },
        { "chart_title", chartTitle },
        { "difficulty", difficulty },
        { "difficulty_level", difficultyLevel },
        { "chart_hash", chartHash }
    };
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/select";
    const auto res = GetTransport()->Send(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(res, m_status, "切换房间曲目失败");
}

ArenaRoomResult APIClient::SetArenaReady(const std::string& code, bool ready, bool startMatch)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const nlohmann::json body = {
        { "ready", ready },
        { "start", startMatch }
    };
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/ready";
    const auto res = GetTransport()->Send(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(res, m_status, "更新准备状态失败");
}

ArenaRoomResult APIClient::ReportArenaProgress(const std::string& code,
                                               int score,
                                               float accuracy,
                                               int combo,
                                               int maxCombo,
                                               const std::string& grade,
                                               bool finished)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const nlohmann::json body = {
        { "score", score },
        { "accuracy", accuracy },
        { "combo", combo },
        { "max_combo", maxCombo },
        { "grade", grade.empty() ? std::string("D") : grade },
        { "finished", finished }
    };
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/progress";
    const auto res = GetTransport()->Send(MakeRequest("POST", path, body.dump(), true));
    return ParseArenaRoomResponse(res, m_status, "同步对战进度失败");
}

ArenaRoomResult APIClient::FetchArenaRoomState(const std::string& code)
{
    const std::string trimmed = TrimRoomCode(code);
    if (trimmed.empty())
    {
        ArenaRoomResult out;
        out.error = "房间码不能为空";
        return out;
    }
    const std::string path = "/api/v1/rooms/" + UrlEncode(trimmed) + "/state";
    const auto res = GetTransport()->Send(MakeRequest("GET", path, {}, true));
    return ParseArenaRoomResponse(res, m_status, "获取对战房间状态失败");
}

void APIClient::CreateArenaRoomAsync(const std::string& roomName, const std::string& chartId,
                                     const std::string& chartTitle, const std::string& difficulty,
                                     float difficultyLevel, const std::string& chartHash,
                                     std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, roomName, chartId, chartTitle, difficulty, difficultyLevel, chartHash, cb = std::move(cb)]() mutable
    {
        auto res = CreateArenaRoom(roomName, chartId, chartTitle, difficulty, difficultyLevel, chartHash);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::ListArenaRoomsAsync(std::function<void(ArenaRoomListResult)> cb)
{
    PostTask([this, cb = std::move(cb)]() mutable
    {
        auto res = ListArenaRooms();
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::JoinArenaRoomAsync(const std::string& code, std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, cb = std::move(cb)]() mutable
    {
        auto res = JoinArenaRoom(code);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::LeaveArenaRoomAsync(const std::string& code, std::function<void(bool)> cb)
{
    PostTask([this, code, cb = std::move(cb)]() mutable
    {
        const bool ok = LeaveArenaRoom(code);
        if (cb) PostMainCallback([cb = std::move(cb), ok]() mutable { cb(ok); });
    });
}

void APIClient::SelectArenaChartAsync(const std::string& code, const std::string& chartId,
                                      const std::string& chartTitle, const std::string& difficulty,
                                      float difficultyLevel, const std::string& chartHash,
                                      std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, chartId, chartTitle, difficulty, difficultyLevel, chartHash, cb = std::move(cb)]() mutable
    {
        auto res = SelectArenaChart(code, chartId, chartTitle, difficulty, difficultyLevel, chartHash);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::SetArenaReadyAsync(const std::string& code, bool ready, bool startMatch,
                                   std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, ready, startMatch, cb = std::move(cb)]() mutable
    {
        auto res = SetArenaReady(code, ready, startMatch);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::ReportArenaProgressAsync(const std::string& code, int score, float accuracy,
                                         int combo, int maxCombo, const std::string& grade,
                                         bool finished, std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, score, accuracy, combo, maxCombo, grade, finished, cb = std::move(cb)]() mutable
    {
        auto res = ReportArenaProgress(code, score, accuracy, combo, maxCombo, grade, finished);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

void APIClient::FetchArenaRoomStateAsync(const std::string& code, std::function<void(ArenaRoomResult)> cb)
{
    PostTask([this, code, cb = std::move(cb)]() mutable
    {
        auto res = FetchArenaRoomState(code);
        if (cb) PostMainCallback([cb = std::move(cb), res = std::move(res)]() mutable { cb(std::move(res)); });
    });
}

} // namespace sakura::net
