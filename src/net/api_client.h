#pragma once

#include "game/chart.h"
#include "game/course.h"
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
    std::string danBadge;
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
    std::string danBadge;
};

struct LeaderboardResult
{
    bool                        ok = false;
    std::string                 error;
    std::optional<int>          myRank;
    std::vector<LeaderboardRow> entries;
};

enum class MarketInstallState
{
    NotInstalled,
    UpdateAvailable,
    Installed
};

struct MarketDifficultyInfo
{
    std::string name;
    float       level     = 1.0f;
    std::string chartFile;
    int         noteCount = 0;
};

struct MarketChartItem
{
    std::string                       id;
    std::string                       chartSlug;
    std::string                       uploaderId;
    std::string                       uploaderName;
    std::string                       title;
    std::string                       artist;
    std::string                       charter;
    std::string                       description;
    float                             bpm          = 120.0f;
    float                             minLevel     = 1.0f;
    float                             maxLevel     = 1.0f;
    std::vector<std::string>          tags;
    std::vector<MarketDifficultyInfo> difficulties;
    std::string                       packageSha256;
    std::string                       contentHash;
    std::size_t                       packageSize  = 0;
    int                               downloads    = 0;
    double                            ratingAvg    = 0.0;
    int                               ratingCount  = 0;
    int                               myRating     = 0;
    long long                         createdAt    = 0;
    long long                         updatedAt    = 0;
};

struct MarketQuery
{
    std::string q;
    std::string tag;
    std::string sort     = "newest"; // newest | popular | rating
    float       minLevel = 0.0f;
    float       maxLevel = 20.0f;
    int         page     = 1;
    int         pageSize = 20;
};

struct MarketListResult
{
    bool                         ok    = false;
    std::string                  error;
    int                          total = 0;
    std::vector<MarketChartItem> charts;
};

struct MarketPublishResult
{
    bool            ok = false;
    std::string     error;
    MarketChartItem chart;
};

struct MarketDownloadResult
{
    bool        ok = false;
    std::string error;
    std::string installedFolder;
    std::string chartId;
};

struct MarketRateResult
{
    bool        ok          = false;
    std::string error;
    double      ratingAvg   = 0.0;
    int         ratingCount = 0;
    int         myRating    = 0;
};

struct CloudSyncResult
{
    bool        ok                 = false;
    std::string error;
    int         scoresPushed       = 0;
    int         scoresPulled       = 0;
    int         achievementsPulled = 0;
    int         coursesPulled      = 0;
    long long   syncedAt           = 0;
};

struct DailyInfoResult
{
    bool                             ok              = false;
    bool                             offlineFallback = false;
    std::string                      error;
    sakura::game::DailyChallengeSpec spec;
    LeaderboardResult                leaderboard;
};

struct DailyScoreSubmitResult
{
    bool        ok      = false;
    std::string error;
    int         rank    = 0;
    bool        newBest = false;
};

// v1.6.0 Online Multiplayer Arena Rooms
struct ArenaPlayerState
{
    std::string userId;
    std::string username;
    std::string danBadge;
    bool        isHost    = false;
    bool        ready     = false;
    bool        finished  = false;
    int         score     = 0;
    float       accuracy  = 0.0f;
    int         combo     = 0;
    int         maxCombo  = 0;
    std::string grade     = "D";
    int         placement = 0;
};

struct ArenaRoomState
{
    std::string                   code;
    std::string                   name;
    std::string                   hostId;
    std::string                   hostName;
    std::string                   status = "waiting"; // waiting | playing | finished
    std::string                   chartId;
    std::string                   chartTitle;
    std::string                   difficulty;
    float                         difficultyLevel = 1.0f;
    std::string                   chartHash;
    long long                     startedAt       = 0;
    long long                     updatedAt       = 0;
    std::vector<ArenaPlayerState> players;
};

struct ArenaRoomResult
{
    bool           ok = false;
    std::string    error;
    ArenaRoomState room;
};

struct ArenaRoomListResult
{
    bool                        ok = false;
    std::string                 error;
    std::vector<ArenaRoomState> rooms;
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

    // v1.2.0 Online Chart Market & Multi-Device Cloud Sync
    static MarketInstallState GetInstalledMarketState(const MarketChartItem& item,
                                                      const std::string& chartsRootDir = {},
                                                      std::string* outInstalledChartId = nullptr);
    MarketPublishResult PublishChartPackage(const std::string& sakuraFilePath, const std::string& description = {});
    MarketListResult FetchMarketCharts(const MarketQuery& query = {});
    MarketDownloadResult DownloadAndInstallChart(const MarketChartItem& item,
                                                 const std::string& chartsRootDir = {},
                                                 std::function<void(float)> onProgress = {});
    MarketRateResult RateMarketChart(const std::string& marketChartId, int rating, const std::string& comment = {});
    CloudSyncResult SyncCloudSave();

    // v1.4.0 Daily Challenge & Dan Badge
    DailyInfoResult FetchDailyChallenge(const std::string& dateUtc8 = {}, int limit = 20);
    DailyScoreSubmitResult SubmitDailyScore(const std::string& dateUtc8, const sakura::game::GameResult& result);

    // v1.6.0 Multiplayer Arena Rooms
    ArenaRoomResult CreateArenaRoom(const std::string& roomName,
                                    const std::string& chartId,
                                    const std::string& chartTitle,
                                    const std::string& difficulty,
                                    float difficultyLevel,
                                    const std::string& chartHash = {});
    ArenaRoomListResult ListArenaRooms();
    ArenaRoomResult JoinArenaRoom(const std::string& code);
    bool LeaveArenaRoom(const std::string& code);
    ArenaRoomResult SelectArenaChart(const std::string& code,
                                     const std::string& chartId,
                                     const std::string& chartTitle,
                                     const std::string& difficulty,
                                     float difficultyLevel,
                                     const std::string& chartHash = {});
    ArenaRoomResult SetArenaReady(const std::string& code, bool ready, bool startMatch = false);
    ArenaRoomResult ReportArenaProgress(const std::string& code,
                                        int score,
                                        float accuracy,
                                        int combo,
                                        int maxCombo,
                                        const std::string& grade,
                                        bool finished);
    ArenaRoomResult FetchArenaRoomState(const std::string& code);

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
    void PublishChartPackageAsync(const std::string& sakuraFilePath, const std::string& description,
                                  std::function<void(MarketPublishResult)> cb);
    void FetchMarketChartsAsync(const MarketQuery& query, std::function<void(MarketListResult)> cb);
    void DownloadAndInstallChartAsync(const MarketChartItem& item, const std::string& chartsRootDir,
                                      std::function<void(float)> onProgress,
                                      std::function<void(MarketDownloadResult)> cb);
    void RateMarketChartAsync(const std::string& marketChartId, int rating, const std::string& comment,
                              std::function<void(MarketRateResult)> cb);
    void SyncCloudSaveAsync(std::function<void(CloudSyncResult)> cb = {});
    void FetchDailyChallengeAsync(const std::string& dateUtc8, int limit, std::function<void(DailyInfoResult)> cb);
    void SubmitDailyScoreAsync(const std::string& dateUtc8, const sakura::game::GameResult& result,
                               std::function<void(DailyScoreSubmitResult)> cb);
    void CreateArenaRoomAsync(const std::string& roomName, const std::string& chartId, const std::string& chartTitle,
                              const std::string& difficulty, float difficultyLevel, const std::string& chartHash,
                              std::function<void(ArenaRoomResult)> cb);
    void ListArenaRoomsAsync(std::function<void(ArenaRoomListResult)> cb);
    void JoinArenaRoomAsync(const std::string& code, std::function<void(ArenaRoomResult)> cb);
    void LeaveArenaRoomAsync(const std::string& code, std::function<void(bool)> cb = {});
    void SelectArenaChartAsync(const std::string& code, const std::string& chartId, const std::string& chartTitle,
                               const std::string& difficulty, float difficultyLevel, const std::string& chartHash,
                               std::function<void(ArenaRoomResult)> cb);
    void SetArenaReadyAsync(const std::string& code, bool ready, bool startMatch,
                            std::function<void(ArenaRoomResult)> cb);
    void ReportArenaProgressAsync(const std::string& code, int score, float accuracy, int combo, int maxCombo,
                                  const std::string& grade, bool finished,
                                  std::function<void(ArenaRoomResult)> cb = {});
    void FetchArenaRoomStateAsync(const std::string& code, std::function<void(ArenaRoomResult)> cb);

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
