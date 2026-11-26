#pragma once

// auth_store.h — 物理隔离凭据保险箱与多账号 MRU 会话存储
// 独立于 settings.json 持久化至 config/auth_session.json，令牌经 DPAPI / 认证混淆加密落盘。

#include "core/config.h"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sakura::core
{

struct SavedAccountEntry
{
    std::string userId;
    std::string username;
    bool        isGuest               = true;
    double      totalPP               = 0.0;
    double      sakuraRating          = 0.0;
    std::string danBadge;
    std::string serverUrl;
    std::string encryptedAccessToken;
    std::string encryptedRefreshToken;
    long long   expiresAt             = 0;
    long long   lastUsedAt            = 0;
};

struct AuthSessionData
{
    std::string                    serverUrl = "https://steven-lee.tech:233";
    std::string                    activeUserId;
    std::string                    accessToken;
    std::string                    refreshToken;
    long long                      expiresAt = 0;
    SavedAccountEntry              profile;
    std::vector<SavedAccountEntry> savedAccounts;
};

class AuthStore
{
public:
    static constexpr std::size_t MAX_SAVED_ACCOUNTS = 5;

    static AuthStore& GetInstance();

    explicit AuthStore(std::filesystem::path customStorePath = {});

    AuthStore(const AuthStore&)            = delete;
    AuthStore& operator=(const AuthStore&) = delete;

    void SetCustomStorePath(std::filesystem::path customStorePath);
    std::filesystem::path ResolveStorePath() const;

    bool Load(Config* legacyConfigToMigrate = nullptr);
    bool Save() const;

    bool HasActiveSession() const;
    AuthSessionData GetSession() const;

    void SetServerUrl(const std::string& serverUrl);
    std::string GetServerUrl() const;

    void UpsertActiveSession(const AuthSessionData& session);
    void UpdateProfileMetadata(double totalPP,
                               double sakuraRating,
                               const std::string& danBadge,
                               const std::string& username = {},
                               std::optional<bool> isGuest = std::nullopt);
    void ClearActiveTokensKeepIdentity();
    void ClearActiveSession(bool removeFromSavedSlots = false);

    std::vector<SavedAccountEntry> GetSavedAccounts() const;
    bool SwitchToSavedAccount(const std::string& userId, AuthSessionData* outSession = nullptr);
    bool RemoveSavedAccount(const std::string& userId);

    static std::string ProtectSecret(std::string_view plaintext);
    static std::optional<std::string> UnprotectSecret(std::string_view protectedText);

private:
    std::filesystem::path ResolveStorePathUnlocked() const;
    bool HasActiveSessionUnlocked() const;
    bool SaveUnlocked() const;
    long long NextMonotonicTimestampUnlocked(long long floorHint = 0);
    void SortAndTrimSavedAccountsUnlocked();
    void UpsertActiveSessionUnlocked(const AuthSessionData& session);

    mutable std::mutex    m_mutex;
    std::filesystem::path m_customStorePath;
    AuthSessionData       m_session;
    long long             m_lastIssuedTimestamp = 0;
};

} // namespace sakura::core
