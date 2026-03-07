#pragma once

// achievement_manager.h — 成就系统管理器

#include "chart.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sakura::game
{

struct AchievementDefinition
{
    std::string id;
    std::string title;
    std::string description;
    int         target = 1;
};

struct AchievementProgress
{
    AchievementDefinition definition;
    bool                  unlocked   = false;
    long long             unlockedAt = 0;
    int                   current    = 0;
    int                   target     = 1;
    float                 progress   = 0.0f;
};

class AchievementManager
{
public:
    static AchievementManager& GetInstance();

    bool LoadAchievements(std::string_view path = "config/achievements.json");
    std::vector<AchievementProgress> CheckAndUnlock(const GameResult& result);

    std::vector<AchievementProgress> GetAll() const;
    std::vector<AchievementProgress> GetUnlocked() const;
    std::optional<AchievementProgress> GetProgress(std::string_view id) const;

private:
    AchievementManager() = default;

    AchievementProgress BuildProgress(const AchievementDefinition& definition) const;
    void                RefreshUnlockedCache();
    int                 CountTotalChartFolders() const;
    int                 CountTotalChartDifficulties() const;

    std::vector<AchievementDefinition>         m_definitions;
    std::unordered_map<std::string, long long> m_unlockedAt;
    std::string                                m_loadedPath;
    bool                                       m_loaded = false;
};

} // namespace sakura::game