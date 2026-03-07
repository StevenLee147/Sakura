// achievement_manager.cpp — 成就系统管理器实现

#include "achievement_manager.h"

#include "data/database.h"
#include "game/chart_loader.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>

using json = nlohmann::json;

namespace sakura::game
{

namespace
{

bool IsGradeAtLeast(Grade actual, Grade expected)
{
    auto rank = [](Grade grade) -> int
    {
        switch (grade)
        {
            case Grade::SS: return 5;
            case Grade::S:  return 4;
            case Grade::A:  return 3;
            case Grade::B:  return 2;
            case Grade::C:  return 1;
            default:        return 0;
        }
    };

    return rank(actual) >= rank(expected);
}

template <typename T>
T SafeGet(const json& object, const char* key, T defaultValue)
{
    try
    {
        if (object.contains(key) && !object[key].is_null())
            return object[key].get<T>();
    }
    catch (const json::exception& ex)
    {
        LOG_WARN("[AchievementManager] 字段 '{}' 解析失败: {}", key, ex.what());
    }
    return defaultValue;
}

} // namespace

AchievementManager& AchievementManager::GetInstance()
{
    static AchievementManager instance;
    return instance;
}

bool AchievementManager::LoadAchievements(std::string_view path)
{
    std::ifstream file(path.data());
    if (!file.is_open())
    {
        LOG_ERROR("[AchievementManager] 无法打开成就定义文件: {}", path);
        return false;
    }

    json root;
    try
    {
        file >> root;
    }
    catch (const json::parse_error& ex)
    {
        LOG_ERROR("[AchievementManager] achievements.json 解析失败: {}", ex.what());
        return false;
    }

    std::vector<AchievementDefinition> definitions;
    if (root.contains("achievements") && root["achievements"].is_array())
    {
        for (const auto& item : root["achievements"])
        {
            AchievementDefinition definition;
            definition.id          = SafeGet<std::string>(item, "id", "");
            definition.title       = SafeGet<std::string>(item, "title", definition.id);
            definition.description = SafeGet<std::string>(item, "description", "");
            definition.target      = std::max(1, SafeGet<int>(item, "target", 1));

            if (!definition.id.empty())
                definitions.push_back(std::move(definition));
        }
    }

    if (definitions.empty())
    {
        LOG_ERROR("[AchievementManager] 未加载到任何成就定义");
        return false;
    }

    m_definitions = std::move(definitions);
    m_loadedPath  = std::string(path);
    m_loaded      = true;
    RefreshUnlockedCache();

    LOG_INFO("[AchievementManager] 已加载 {} 个成就定义", m_definitions.size());
    return true;
}

std::vector<AchievementProgress> AchievementManager::CheckAndUnlock(const GameResult& result)
{
    if (!m_loaded && !LoadAchievements())
        return {};

    (void)result;
    RefreshUnlockedCache();

    auto& database = sakura::data::Database::GetInstance();
    std::vector<AchievementProgress> unlocked;

    for (const auto& definition : m_definitions)
    {
        if (m_unlockedAt.contains(definition.id))
            continue;

        AchievementProgress progress = BuildProgress(definition);
        if (!progress.unlocked && progress.current >= progress.target)
        {
            if (database.SaveAchievement(definition.id))
            {
                RefreshUnlockedCache();
                progress = BuildProgress(definition);
                unlocked.push_back(std::move(progress));
            }
        }
    }

    return unlocked;
}

std::vector<AchievementProgress> AchievementManager::GetAll() const
{
    std::vector<AchievementProgress> progress;
    progress.reserve(m_definitions.size());

    for (const auto& definition : m_definitions)
        progress.push_back(BuildProgress(definition));

    return progress;
}

std::vector<AchievementProgress> AchievementManager::GetUnlocked() const
{
    std::vector<AchievementProgress> progress;
    for (const auto& definition : m_definitions)
    {
        auto item = BuildProgress(definition);
        if (item.unlocked)
            progress.push_back(std::move(item));
    }
    return progress;
}

std::optional<AchievementProgress> AchievementManager::GetProgress(std::string_view id) const
{
    auto it = std::find_if(m_definitions.begin(), m_definitions.end(),
        [&](const AchievementDefinition& definition) { return definition.id == id; });

    if (it == m_definitions.end())
        return std::nullopt;

    return BuildProgress(*it);
}

AchievementProgress AchievementManager::BuildProgress(const AchievementDefinition& definition) const
{
    AchievementProgress progress;
    progress.definition = definition;
    progress.target     = definition.target;

    auto unlockedIt = m_unlockedAt.find(definition.id);
    if (unlockedIt != m_unlockedAt.end())
    {
        progress.unlocked   = true;
        progress.unlockedAt = unlockedIt->second;
    }

    const auto& database = sakura::data::Database::GetInstance();
    const auto bestScores = database.GetAllBestScores();

    if (definition.id == "first_play")
    {
        progress.current = static_cast<int>(database.GetTotalPlayCount() > 0 ? 1 : 0);
    }
    else if (definition.id == "first_fc")
    {
        progress.current = database.HasAnyFullCombo() ? 1 : 0;
    }
    else if (definition.id == "first_ap")
    {
        progress.current = database.HasAnyAllPerfect() ? 1 : 0;
    }
    else if (definition.id == "combo_100" || definition.id == "combo_500" || definition.id == "combo_1000")
    {
        progress.current = database.GetHighestCombo();
    }
    else if (definition.id == "play_10" || definition.id == "play_50" || definition.id == "play_100")
    {
        progress.current = static_cast<int>(database.GetTotalPlayCount());
    }
    else if (definition.id == "all_charts")
    {
        std::unordered_set<std::string> playedCharts;
        for (const auto& score : bestScores)
            playedCharts.insert(score.chartId);
        progress.current = static_cast<int>(playedCharts.size());
        progress.target  = std::max(1, CountTotalChartFolders());
    }
    else if (definition.id == "all_s")
    {
        progress.current = static_cast<int>(std::count_if(bestScores.begin(), bestScores.end(),
            [](const GameResult& score) { return IsGradeAtLeast(score.grade, Grade::S); }));
        progress.target  = std::max(1, CountTotalChartDifficulties());
    }
    else if (definition.id == "accuracy_99")
    {
        progress.current = static_cast<int>(std::floor(database.GetHighestAccuracy()));
    }
    else if (definition.id == "score_999k")
    {
        progress.current = database.GetHighestScore();
    }

    if (progress.unlocked)
        progress.current = std::max(progress.current, progress.target);

    progress.progress = std::clamp(
        progress.target > 0 ? static_cast<float>(progress.current) / static_cast<float>(progress.target) : 0.0f,
        0.0f,
        1.0f);
    return progress;
}

void AchievementManager::RefreshUnlockedCache()
{
    m_unlockedAt.clear();
    for (const auto& record : sakura::data::Database::GetInstance().GetAchievements())
        m_unlockedAt.emplace(record.id, record.unlockedAt);
}

int AchievementManager::CountTotalChartFolders() const
{
    ChartLoader loader;
    return static_cast<int>(loader.ScanCharts("resources/charts").size());
}

int AchievementManager::CountTotalChartDifficulties() const
{
    ChartLoader loader;
    int total = 0;
    for (const auto& chart : loader.ScanCharts("resources/charts"))
        total += static_cast<int>(chart.difficulties.size());
    return total;
}

} // namespace sakura::game