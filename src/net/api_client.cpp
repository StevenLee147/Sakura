#include "net/api_client.h"

#include "core/config.h"
#include "core/paths.h"
#include "data/database.h"
#include "game/achievement_manager.h"
#include "game/chart_loader.h"
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

    const long long nowSec = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    long long localSettingsTs = cfg.Get<long long>("online.settings_updated_at", 0LL);

    nlohmann::json settingsObj = {
        { "note_speed", cfg.Get<float>("gameplay.note_speed", 5.0f) },
        { "mouse_approach_ms", cfg.Get<int>("gameplay.mouse_approach_ms", 1000) },
        { "global_offset_ms", cfg.Get<int>("audio.global_offset_ms", 0) },
        { "pitch_lock", cfg.Get<bool>("audio.pitch_lock", true) },
        { "key_lane_0", cfg.Get<int>("input.key_lane_0", 4) },
        { "key_lane_1", cfg.Get<int>("input.key_lane_1", 22) },
        { "key_lane_2", cfg.Get<int>("input.key_lane_2", 7) },
        { "key_lane_3", cfg.Get<int>("input.key_lane_3", 9) },
        { "key_thumb",  cfg.Get<int>("input.key_thumb", 44) }
    };

    const nlohmann::json pushBody = {
        { "scores", scoresArr },
        { "achievements", achArr },
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
                { "settings", settingsObj },
                { "settings_updated_at", nowSec }
            };
            GetTransport()->Send(MakeRequest("POST", "/api/v1/sync/push", seedBody.dump(), true));
            cfg.Set("online.settings_updated_at", nowSec);
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

} // namespace sakura::net
