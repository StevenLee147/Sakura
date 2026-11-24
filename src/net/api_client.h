#pragma once

#include "game/chart.h"
#include "net/http_client.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sakura::net
{

enum class OnlineStatus
{
    Offline,
    Connecting,
    Online
};

struct OnlineUser
{
    std::string id;
    std::string username;
    bool        isGuest = true;
    double      totalPP = 0.0;
};

struct AuthResult
{
    bool        ok = false;
    std::string error;
    OnlineUser  user;
    std::string token;
};

struct ScoreSubmitResult
{
    bool        ok            = false;
    bool        queuedOffline = false;
    std::string error;
    std::string scoreId;
    int         rank    = 0;
    double      pp      = 0.0;
    double      totalPP = 0.0;
    bool        newBest = false;
};

struct LeaderboardRow
{
    int         rank = 0;
    std::string scoreId;
    std::string userId;
    std::string username;
    int         score = 0;
    float       accuracy = 0.0f;
    int         maxCombo = 0;
    std::string grade = "D";
    double      pp = 0.0;
    bool        isFullCombo = false;
    bool        isAllPerfect = false;
    bool        hasReplay = false;
    long long   playedAt = 0;
};

struct LeaderboardResult
{
    bool                        ok = false;
    std::string                 error;
    std::optional<int>          myRank;
    std::vector<LeaderboardRow> entries;
};

class APIClient
{
public:
    static APIClient& GetInstance();

    explicit APIClient(std::shared_ptr<IHttpTransport> transport = nullptr,
                       std::string queueFilePath = {});
    ~APIClient();

    APIClient(const APIClient&)            = delete;
    APIClient& operator=(const APIClient&) = delete;

    void LoadFromConfig();
    void SaveToConfig() const;
    void Shutdown();

    void SetTransport(std::shared_ptr<IHttpTransport> transport);
    std::shared_ptr<IHttpTransport> GetTransport() const;

    void SetServerUrl(const std::string& url);
    std::string GetServerUrl() const;

    bool IsAuthenticated() const;
    OnlineStatus GetStatus() const { return m_status.load(); }
    OnlineUser GetCurrentUser() const;
    std::string GetAuthToken() const;
    void Logout();

    std::size_t GetPendingScoreCount() const;

    // Synchronous calls (used by worker threads, --verify-online, and unit tests)
    bool CheckHealth();
    AuthResult LoginGuest(const std::string& nickname = {});
    AuthResult Register(const std::string& username, const std::string& password);
    AuthResult Login(const std::string& username, const std::string& password);
    AuthResult RefreshProfile();
    ScoreSubmitResult SubmitScore(const sakura::game::GameResult& result);
    int FlushPendingScores();
    LeaderboardResult FetchLeaderboard(const std::string& chartId, const std::string& difficulty, int limit = 50);
    bool DownloadReplay(const std::string& scoreId, const std::string& destPath);

    // Asynchronous calls (marshal completion callbacks onto main thread via PumpCallbacks)
    void PostTask(std::function<void()> workerTask);
    void PostMainCallback(std::function<void()> mainCallback);
    void PumpCallbacks();

    void LoginGuestAsync(const std::string& nickname, std::function<void(AuthResult)> cb);
    void RegisterAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb);
    void LoginAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb);
    void RefreshProfileAsync(std::function<void(AuthResult)> cb = {});
    void SubmitScoreAsync(const sakura::game::GameResult& result, std::function<void(ScoreSubmitResult)> cb);
    void FetchLeaderboardAsync(const std::string& chartId, const std::string& difficulty, int limit,
                               std::function<void(LeaderboardResult)> cb);
    void DownloadReplayAsync(const std::string& scoreId, const std::string& destPath,
                             std::function<void(bool, std::string)> cb);

private:
    void EnsureWorker();
    std::string ResolveQueuePath() const;
    HttpRequest MakeRequest(const std::string& method, const std::string& path, const std::string& jsonBody = {},
                            bool withAuth = true) const;
    AuthResult HandleAuthResponse(const HttpResponse& res);
    void EnqueueOfflineScorePayload(const std::string& payloadJson);

    mutable std::mutex              m_stateMutex;
    std::shared_ptr<IHttpTransport> m_transport;
    std::string                     m_serverUrl = "https://steven-lee.tech:233";
    std::string                     m_authToken;
    OnlineUser                      m_user;
    std::string                     m_customQueuePath;
    std::atomic<OnlineStatus>       m_status{ OnlineStatus::Offline };

    std::mutex                      m_workerMutex;
    std::condition_variable         m_workerCv;
    std::deque<std::function<void()>> m_tasks;
    std::thread                     m_worker;
    bool                            m_workerRunning = false;
    bool                            m_stopping      = false;

    std::mutex                      m_cbMutex;
    std::deque<std::function<void()>> m_callbacks;
};

} // namespace sakura::net
