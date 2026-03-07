#pragma once

// scene_stats.h — 玩家统计场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "ui/scroll_list.h"
#include "game/achievement_manager.h"
#include "game/pp_calculator.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace sakura::scene
{

class SceneStats final : public Scene
{
public:
    explicit SceneStats(SceneManager& mgr);

    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    struct StatsSnapshot
    {
        double totalPP = 0.0;
        long long playCount = 0;
        double playTimeSeconds = 0.0;
        int totalNotes = 0;
        double averageAccuracy = 0.0;
        int fullComboCount = 0;
        int allPerfectCount = 0;
        std::array<int, 6> gradeDistribution = { 0, 0, 0, 0, 0, 0 };
    };

    SceneManager& m_manager;

    sakura::core::FontHandle m_fontTitle = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontBody  = sakura::core::INVALID_HANDLE;
    sakura::core::FontHandle m_fontSmall = sakura::core::INVALID_HANDLE;

    std::unique_ptr<sakura::ui::Button> m_btnBack;
    std::unique_ptr<sakura::ui::Button> m_btnAchievements;
    std::unique_ptr<sakura::ui::Button> m_btnCloseOverlay;
    std::unique_ptr<sakura::ui::ScrollList> m_achievementList;

    bool m_showAchievements = false;

    StatsSnapshot m_stats;
    std::vector<sakura::game::GameResult> m_recentScores;
    std::vector<sakura::game::PPPlay> m_topPPPlays;
    std::vector<sakura::game::AchievementProgress> m_achievements;

    void RefreshData();
    void RefreshAchievementList();
    static std::string FormatDuration(double seconds);
};

} // namespace sakura::scene