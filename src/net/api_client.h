#pragma once

#include "core/auth_store.h"
#include "game/chart.h"
#include "game/course.h"
#include "net/http_client.h"
#include "net/offline_queue.h"
#include "net/online_cache.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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

enum class AuthState
{
    Unauthenticated,
    AuthenticatedOnline,
    AuthenticatedOffline,
    SessionExpired
};

struct OnlineUser
{
    std::string id;
    std::string username;
    bool        isGuest      = true;
    double      totalPP      = 0.0;
    double      sakuraRating = 0.0;
    std::string danBadge;
};

struct AuthResult
{
    bool        ok = false;
    std::string error;
    std::string errorCode;
    OnlineUser  user;
    std::string token;
    std::string refreshToken;
    long long   expiresAt = 0;
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
    long long   playedAt     = 0;
    double      sakuraRating = 0.0;
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
    bool        ok            = false;
    bool        queuedOffline = false;
    std::string error;
    int         rank          = 0;
    bool        newBest       = false;
};

// v1.6.0 / v2.2.0 Online Multiplayer Arena Rooms
struct ArenaChatMessage
{
    int         id       = 0;
    std::string userId;
    std::string username;
    int         phraseId = 0;
    std::string text;
    long long   sentAt   = 0;
};

struct ArenaPlayerState
{
    std::string userId;
    std::string username;
    std::string danBadge;
    bool        isHost     = false;
    bool        ready      = false;
    bool        finished   = false;
    int         score      = 0;
    float       accuracy   = 0.0f;
    int         combo      = 0;
    int         maxCombo   = 0;
    std::string grade      = "D";
    int         placement  = 0;
    bool        connected  = true;
    long long   lastSeenAt = 0;
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
    int                           revision        = 0;
    std::string                   marketChartId;
    std::size_t                   packageSize     = 0;
    std::vector<ArenaChatMessage> messages;
};

struct ArenaRoomResult
{
    bool           ok        = false;
    bool           unchanged = false;
    std::string    error;
    ArenaRoomState room;
};

struct ArenaRoomListResult
{
    bool                        ok = false;
    std::string                 error;
    std::vector<ArenaRoomState> rooms;
};

// v2.5.0 Player Social Profiles, Rivals & Global Sakura Rating Ladder
struct UserProfileScoreItem
{
    std::string scoreId;
    std::string chartId;
    std::string chartTitle;
    std::string difficulty;
    float       difficultyLevel = 1.0f;
    int         score           = 0;
    float       accuracy        = 0.0f;
    int         maxCombo        = 0;
    std::string grade           = "D";
    double      pp              = 0.0;
    double      chartRating     = 0.0;
    bool        isFullCombo     = false;
    bool        isAllPerfect    = false;
    bool        hasReplay       = false;
    long long   playedAt        = 0;
};

struct UserProfileData
{
    std::string                       userId;
    std::string                       username;
    bool                              isGuest       = false;
    std::string                       bio;
    std::string                       featuredTitle;
    std::string                       danBadge;
    double                            sakuraRating  = 0.0;
    double                            totalPP       = 0.0;
    int                               playCount     = 0;
    int                               followerCount = 0;
    int                               rivalCount    = 0;
    bool                              isRival       = false;
    bool                              isMutual      = false;
    long long                         createdAt     = 0;
    std::vector<UserProfileScoreItem> bestScores;
};

struct UserProfileResult
{
    bool            ok = false;
    std::string     error;
    UserProfileData profile;
};

struct RivalActionResult
{
    bool        ok            = false;
    std::string error;
    bool        isRival       = false;
    bool        isMutual      = false;
    int         rivalCount    = 0;
    int         followerCount = 0;
};

struct RivalSummaryItem
{
    std::string userId;
    std::string username;
    std::string bio;
    std::string featuredTitle;
    std::string danBadge;
    double      sakuraRating = 0.0;
    double      totalPP      = 0.0;
    int         playCount    = 0;
    bool        isMutual     = false;
    long long   addedAt      = 0;
};

struct RivalListResult
{
    bool                          ok = false;
    std::string                   error;
    std::vector<RivalSummaryItem> rivals;
};

struct RatingLadderRow
{
    int         rank         = 0;
    std::string userId;
    std::string username;
    std::string featuredTitle;
    std::string danBadge;
    double      sakuraRating = 0.0;
    double      totalPP      = 0.0;
    int         playCount    = 0;
    bool        isRival      = false;
};

struct RatingLadderResult
{
    bool                         ok       = false;
    std::string                  error;
    std::optional<int>           myRank;
    double                       myRating = 0.0;
    std::vector<RatingLadderRow> entries;
};

// v3.0.0 Sakura Season Ranked
struct SeasonStageOnlineInfo
{
    int         slotIndex       = 0;
    std::string slotRole;
    std::string roleTitle;
    std::string chartId;
    std::string chartTitle;
    std::string difficulty;
    float       difficultyLevel = 1.0f;
    int         maxStageSP      = 1000;
};

struct SeasonMyOnlineStanding
{
    bool               present           = false;
    std::array<int, 3> stageScores{ 0, 0, 0 };
    std::array<float, 3> stageAccuracies{ 0.0f, 0.0f, 0.0f };
    std::array<int, 3> stageSP{ 0, 0, 0 };
    int                totalSP           = 0;
    std::string        rankTier          = "青铜樱";
    int                tierIndex         = 1;
    std::string        nextTierName      = "白银月";
    int                nextTierThreshold = 1200;
    int                spToNextTier      = 1200;
    std::optional<int> rank;
};

struct SeasonCurrentResult
{
    bool                               ok = false;
    std::string                        error;
    std::string                        seasonId;
    std::string                        themeTitle;
    std::vector<SeasonStageOnlineInfo> stages;
    SeasonMyOnlineStanding             myStanding;
};

struct SeasonSubmitResult
{
    bool        ok        = false;
    std::string error;
    std::string seasonId;
    int         slotIndex = 0;
    int         stageSP   = 0;
    int         totalSP   = 0;
    std::string rankTier;
    int         tierIndex = 1;
    int         rank      = 0;
    bool        newBest   = false;
};

struct SeasonLeaderboardRow
{
    int                rank      = 0;
    std::string        userId;
    std::string        username;
    std::string        featuredTitle;
    std::string        danBadge;
    int                totalSP   = 0;
    std::string        rankTier;
    std::array<int, 3> stageSP{ 0, 0, 0 };
};

struct SeasonLeaderboardResult
{
    bool                              ok = false;
    std::string                       error;
    std::string                       seasonId;
    std::optional<int>                myRank;
    std::vector<SeasonLeaderboardRow> entries;
};

class APIClient
{
public:
    static APIClient& GetInstance();

    explicit APIClient(std::shared_ptr<IHttpTransport> transport = nullptr,
                       std::string queueFilePath = {},
                       std::string authStorePath = {});
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
    AuthState GetAuthState() const;
    OnlineStatus GetStatus() const { return m_status.load(); }
    OnlineUser GetCurrentUser() const;
    std::string GetAuthToken() const;
    std::string GetRefreshToken() const;
    long long GetTokenExpiresAt() const;
    sakura::core::AuthStore& GetAuthStore();
    const sakura::core::AuthStore& GetAuthStore() const;
    OnlineCache& GetOnlineCache();
    const OnlineCache& GetOnlineCache() const;
    OfflineQueue& GetOfflineQueue();
    const OfflineQueue& GetOfflineQueue() const;
    void Logout();

    std::size_t GetPendingScoreCount() const;
    int GetActiveTransferCount() const;
    float GetActiveTransferProgress() const;
    uint64_t NextRequestSeq();
    std::optional<LeaderboardResult> GetCachedLeaderboard(const std::string& chartId,
                                                          const std::string& difficulty,
                                                          std::string_view scope = "global") const;
    static std::string FormatScoreDeltaText(std::string_view rivalName, int myBestScore, int rivalScore);

    // Synchronous calls (used by worker threads, --verify-online, and unit tests)
    bool CheckHealth();
    AuthResult LoginGuest(const std::string& nickname = {});
    AuthResult Register(const std::string& username, const std::string& password);
    AuthResult Login(const std::string& username, const std::string& password);
    AuthResult RefreshProfile();
    AuthResult RefreshSession();
    AuthResult UpgradeGuest(const std::string& username, const std::string& password);
    AuthResult ChangePassword(const std::string& oldPassword, const std::string& newPassword);
    std::vector<sakura::core::SavedAccountEntry> GetSavedAccounts() const;
    AuthResult SwitchSavedAccount(const std::string& userId);
    bool RemoveSavedAccount(const std::string& userId);
    ScoreSubmitResult SubmitScore(const sakura::game::GameResult& result);
    bool EnqueueCourseRecordOffline(const sakura::game::CourseRecord& record);
    int FlushPendingActions();
    int FlushPendingScores();
    LeaderboardResult FetchLeaderboard(const std::string& chartId,
                                       const std::string& difficulty,
                                       int limit = 50,
                                       std::string_view scope = "global");
    bool DownloadReplay(const std::string& scoreId, const std::string& destPath);

    // v2.5.0 Player Profiles, Rivals & Rating Ladder
    UserProfileResult FetchUserProfile(const std::string& userId);
    UserProfileResult UpdateMyProfile(const std::string& bio, const std::string& featuredTitle);
    RivalActionResult AddRival(const std::string& targetUserId);
    RivalActionResult RemoveRival(const std::string& targetUserId);
    RivalListResult FetchRivals();
    RatingLadderResult FetchRatingLadder(int limit = 100);

    // v3.0.0 Sakura Season Ranked
    SeasonCurrentResult FetchCurrentSeason(const std::string& seasonId = {});
    SeasonSubmitResult SubmitSeasonScore(const std::string& seasonId, const sakura::game::GameResult& result);
    SeasonLeaderboardResult FetchSeasonLeaderboard(const std::string& seasonId = {}, int limit = 100);

    // v1.2.0 Online Chart Market & Multi-Device Cloud Sync
    static MarketInstallState GetInstalledMarketState(const MarketChartItem& item,
                                                      const std::string& chartsRootDir = {},
                                                      std::string* outInstalledChartId = nullptr);
    MarketPublishResult PublishChartPackage(const std::string& sakuraFilePath, const std::string& description = {});
    MarketListResult FetchMarketCharts(const MarketQuery& query = {});
    MarketDownloadResult DownloadAndInstallChart(const MarketChartItem& item,
                                                 const std::string& chartsRootDir = {},
                                                 std::function<void(float)> onProgress = {});
    MarketDownloadResult DownloadMarketChartById(const std::string& marketChartId,
                                                 const std::string& chartsRootDir = {},
                                                 std::function<void(float)> onProgress = {});
    MarketRateResult RateMarketChart(const std::string& marketChartId, int rating, const std::string& comment = {});
    CloudSyncResult SyncCloudSave();

    // v1.4.0 Daily Challenge & Dan Badge
    DailyInfoResult FetchDailyChallenge(const std::string& dateUtc8 = {}, int limit = 20);
    DailyScoreSubmitResult SubmitDailyScore(const std::string& dateUtc8, const sakura::game::GameResult& result);

    // v1.6.0 / v2.2.0 Multiplayer Arena Rooms
    ArenaRoomResult CreateArenaRoom(const std::string& roomName,
                                    const std::string& chartId,
                                    const std::string& chartTitle,
                                    const std::string& difficulty,
                                    float difficultyLevel,
                                    const std::string& chartHash = {},
                                    const std::string& marketChartId = {});
    ArenaRoomListResult ListArenaRooms();
    ArenaRoomResult JoinArenaRoom(const std::string& code);
    bool LeaveArenaRoom(const std::string& code);
    ArenaRoomResult SelectArenaChart(const std::string& code,
                                     const std::string& chartId,
                                     const std::string& chartTitle,
                                     const std::string& difficulty,
                                     float difficultyLevel,
                                     const std::string& chartHash = {},
                                     const std::string& marketChartId = {});
    ArenaRoomResult SetArenaReady(const std::string& code, bool ready, bool startMatch = false);
    ArenaRoomResult ReportArenaProgress(const std::string& code,
                                        int score,
                                        float accuracy,
                                        int combo,
                                        int maxCombo,
                                        const std::string& grade,
                                        bool finished);
    ArenaRoomResult FetchArenaRoomState(const std::string& code, int sinceRevision = 0);
    ArenaRoomResult SendArenaChat(const std::string& code, int phraseId);

    // Asynchronous calls (marshal completion callbacks onto main thread via PumpCallbacks)
    void PostTask(std::function<void()> workerTask);
    void PostTransferTask(std::function<void()> transferTask);
    void PostMainCallback(std::function<void()> mainCallback);
    void PumpCallbacks();

    void LoginGuestAsync(const std::string& nickname, std::function<void(AuthResult)> cb);
    void RegisterAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb);
    void LoginAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb);
    void RefreshProfileAsync(std::function<void(AuthResult)> cb = {});
    void UpgradeGuestAsync(const std::string& username, const std::string& password, std::function<void(AuthResult)> cb);
    void ChangePasswordAsync(const std::string& oldPassword, const std::string& newPassword,
                             std::function<void(AuthResult)> cb);
    void SwitchSavedAccountAsync(const std::string& userId, std::function<void(AuthResult)> cb);
    void LogoutAsync(std::function<void()> cb = {});
    void SubmitScoreAsync(const sakura::game::GameResult& result, std::function<void(ScoreSubmitResult)> cb);
    void FlushPendingActionsAsync(std::function<void(int)> cb = {});
    void FetchLeaderboardAsync(const std::string& chartId, const std::string& difficulty, int limit,
                               std::function<void(LeaderboardResult)> cb,
                               std::string_view scope = "global");
    void FetchUserProfileAsync(const std::string& userId, std::function<void(UserProfileResult)> cb);
    void UpdateMyProfileAsync(const std::string& bio, const std::string& featuredTitle,
                              std::function<void(UserProfileResult)> cb);
    void AddRivalAsync(const std::string& targetUserId, std::function<void(RivalActionResult)> cb);
    void RemoveRivalAsync(const std::string& targetUserId, std::function<void(RivalActionResult)> cb);
    void FetchRivalsAsync(std::function<void(RivalListResult)> cb);
    void FetchRatingLadderAsync(int limit, std::function<void(RatingLadderResult)> cb);
    void FetchCurrentSeasonAsync(const std::string& seasonId, std::function<void(SeasonCurrentResult)> cb);
    void SubmitSeasonScoreAsync(const std::string& seasonId, const sakura::game::GameResult& result,
                                std::function<void(SeasonSubmitResult)> cb);
    void FetchSeasonLeaderboardAsync(const std::string& seasonId, int limit,
                                     std::function<void(SeasonLeaderboardResult)> cb);
    void DownloadReplayAsync(const std::string& scoreId, const std::string& destPath,
                             std::function<void(bool, std::string)> cb);
    void PublishChartPackageAsync(const std::string& sakuraFilePath, const std::string& description,
                                  std::function<void(MarketPublishResult)> cb);
    void FetchMarketChartsAsync(const MarketQuery& query, std::function<void(MarketListResult)> cb);
    void DownloadAndInstallChartAsync(const MarketChartItem& item, const std::string& chartsRootDir,
                                      std::function<void(float)> onProgress,
                                      std::function<void(MarketDownloadResult)> cb);
    void DownloadMarketChartByIdAsync(const std::string& marketChartId, const std::string& chartsRootDir,
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
    void CreateArenaRoomAsync(const std::string& roomName, const std::string& chartId, const std::string& chartTitle,
                              const std::string& difficulty, float difficultyLevel, const std::string& chartHash,
                              const std::string& marketChartId, std::function<void(ArenaRoomResult)> cb);
    void ListArenaRoomsAsync(std::function<void(ArenaRoomListResult)> cb);
    void JoinArenaRoomAsync(const std::string& code, std::function<void(ArenaRoomResult)> cb);
    void LeaveArenaRoomAsync(const std::string& code, std::function<void(bool)> cb = {});
    void SelectArenaChartAsync(const std::string& code, const std::string& chartId, const std::string& chartTitle,
                               const std::string& difficulty, float difficultyLevel, const std::string& chartHash,
                               std::function<void(ArenaRoomResult)> cb);
    void SelectArenaChartAsync(const std::string& code, const std::string& chartId, const std::string& chartTitle,
                               const std::string& difficulty, float difficultyLevel, const std::string& chartHash,
                               const std::string& marketChartId, std::function<void(ArenaRoomResult)> cb);
    void SetArenaReadyAsync(const std::string& code, bool ready, bool startMatch,
                            std::function<void(ArenaRoomResult)> cb);
    void ReportArenaProgressAsync(const std::string& code, int score, float accuracy, int combo, int maxCombo,
                                  const std::string& grade, bool finished,
                                  std::function<void(ArenaRoomResult)> cb = {});
    void FetchArenaRoomStateAsync(const std::string& code,
                                  std::function<void(ArenaRoomResult)> cb,
                                  int sinceRevision = 0);
    void SendArenaChatAsync(const std::string& code, int phraseId,
                            std::function<void(ArenaRoomResult)> cb = {});

private:
    struct SingletonTag {};
    explicit APIClient(SingletonTag);

    struct TaskPool
    {
        std::mutex                        mutex;
        std::condition_variable           cv;
        std::deque<std::function<void()>> tasks;
        std::vector<std::thread>          threads;
        bool                              running  = false;
        bool                              stopping = false;
    };

    struct ChannelState
    {
        mutable std::mutex           mutex;
        uint64_t                     latestSeq = 0;
        std::shared_ptr<CancelToken> activeToken;
    };

    sakura::core::AuthStore& MutableAuthStore() const;
    OnlineCache& MutableOnlineCache() const;
    OfflineQueue& MutableOfflineQueue() const;
    void SyncServerUrlToConfig(const std::string& serverUrl) const;
    void MarkOfflinePreserveSession();
    void MarkOnlineFromAuthenticatedSuccess();
    void MarkAllUserScopedCachesStale();
    static void EnsurePoolStarted(TaskPool& pool, std::size_t threadCount);
    static void ShutdownPool(TaskPool& pool);
    std::pair<uint64_t, std::shared_ptr<CancelToken>> BeginChannelRequest(ChannelState& channel);
    static bool IsChannelRequestCurrent(const ChannelState& channel,
                                        uint64_t seq,
                                        const std::shared_ptr<CancelToken>& token);
    static void CancelChannel(ChannelState& channel);
    void OnTransferFinished() noexcept;
    void ScheduleBackgroundOfflineFlush();
    LeaderboardResult FetchLeaderboardInternal(const std::string& chartId,
                                               const std::string& difficulty,
                                               int limit,
                                               std::string_view scope,
                                               const std::shared_ptr<CancelToken>& cancelToken);
    MarketListResult FetchMarketChartsInternal(const MarketQuery& query,
                                               const std::shared_ptr<CancelToken>& cancelToken);
    HttpRequest MakeRequest(const std::string& method, const std::string& path, const std::string& jsonBody = {},
                            bool withAuth = true) const;
    HttpResponse SendWithAuthRetry(HttpRequest req);
    AuthResult RefreshSessionLocked(HttpResponse* outRawHttpResponse = nullptr);
    AuthResult HandleAuthResponse(const HttpResponse& res, bool preserveMissingTokens = false);

    mutable std::mutex                       m_refreshMutex;
    mutable std::mutex                       m_stateMutex;
    mutable std::mutex                       m_flushMutex;
    std::shared_ptr<IHttpTransport>          m_transport;
    std::shared_ptr<sakura::core::AuthStore> m_ownedAuthStore;
    std::shared_ptr<OnlineCache>             m_ownedOnlineCache;
    std::shared_ptr<OfflineQueue>            m_ownedOfflineQueue;
    OnlineCache*                             m_onlineCache   = nullptr;
    OfflineQueue*                            m_offlineQueue  = nullptr;
    std::string                              m_isolatedAuthStorePath;
    std::string                              m_isolatedCachePath;
    std::string                              m_isolatedQueuePath;
    bool                                     m_cleanupIsolatedAuthStore = false;
    std::string                              m_serverUrl                = "https://steven-lee.tech:233";
    std::string                              m_authToken;
    std::string                              m_refreshToken;
    long long                                m_expiresAt = 0;
    OnlineUser                               m_user;
    AuthState                                m_authState = AuthState::Unauthenticated;
    std::string                              m_customQueuePath;
    std::atomic<OnlineStatus>                m_status{ OnlineStatus::Offline };

    std::atomic<int>                         m_activeTransferCount{ 0 };
    std::atomic<float>                       m_activeTransferProgress{ 0.0f };
    std::atomic<uint64_t>                    m_requestSeq{ 0 };
    std::atomic<bool>                        m_bgFlushScheduled{ false };

    ChannelState                             m_leaderboardChannel;
    ChannelState                             m_marketListChannel;

    TaskPool                                 m_interactivePool;
    TaskPool                                 m_transferPool;

    std::mutex                               m_cbMutex;
    std::deque<std::function<void()>>        m_callbacks;
};

} // namespace sakura::net
