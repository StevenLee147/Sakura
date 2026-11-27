#pragma once

// scene_stats.h — 玩家统计场景

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/button.h"
#include "ui/profile_card_modal.h"
#include "ui/scroll_list.h"
#include "ui/text_input.h"
#include "ui/top_bar.h"
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

    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<sakura::ui::TopBar> m_topBar;
    std::unique_ptr<sakura::ui::ProfileCardModal> m_profileModal;
    std::unique_ptr<sakura::ui::Button> m_btnBack;
    std::unique_ptr<sakura::ui::Button> m_btnSocialLadder;
    std::unique_ptr<sakura::ui::Button> m_btnExportB30;
    std::unique_ptr<sakura::ui::Button> m_btnToggleB30Overlay;
    std::unique_ptr<sakura::ui::Button> m_btnAchievements;
    std::unique_ptr<sakura::ui::Button> m_btnCloseOverlay;
    std::unique_ptr<sakura::ui::Button> m_btnSwitchLadderTab;
    std::unique_ptr<sakura::ui::Button> m_btnSaveProfile;
    std::unique_ptr<sakura::ui::TextInput> m_inputTitle;
    std::unique_ptr<sakura::ui::TextInput> m_inputBio;
    std::unique_ptr<sakura::ui::ScrollList> m_achievementList;

    bool m_showAchievements = false;
    bool m_showB30Overlay   = false;
    int  m_socialOverlayMode = 0; // 0=none, 1=Top 100 Rating Ladder, 2=My Rivals
    int  m_socialScroll = 0;
    std::vector<sakura::net::RatingLadderRow> m_ladderRows;
    std::optional<int> m_ladderMyRank;
    std::vector<sakura::net::RivalSummaryItem> m_rivalRows;
    std::string m_socialStatus;

    StatsSnapshot m_stats;
    sakura::game::RatingSummary m_ratingSummary;
    std::string m_danBadge;
    std::string m_featuredTitle;
    std::string m_playerBio;

    void FetchSocialData();
    std::vector<sakura::game::GameResult> m_recentScores;
    std::vector<sakura::game::PPPlay> m_topPPPlays;
    std::vector<sakura::game::AchievementProgress> m_achievements;

    void RefreshData();
    void RefreshAchievementList();
    void RenderB30MatrixContent(sakura::core::Renderer& renderer, bool exportCardMode = false);
    bool ExportCareerCardPng(const std::string& outputPath = {});
    static std::string FormatDuration(double seconds);
};

} // namespace sakura::scene