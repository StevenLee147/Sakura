#pragma once
#include "scene.h"
#include "scene_manager.h"
#include "game/chart.h"
#include "game/course.h"
#include "game/play_session.h"
#include "net/api_client.h"
#include "ui/button.h"
#include "ui/text_input.h"
#include "effects/background.h"
#include "effects/particle_system.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sakura::scene {
class SceneSelect final : public Scene {
public:
    explicit SceneSelect(SceneManager& manager) : m_manager(manager) {}
    sakura::game::PlayMode CurrentMode() const { return m_options.mode; }
    // Selects a visible chart by id (offline recording preselection); false when it is not listed.
    bool SelectChart(const std::string& id, int difficulty);
    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;
private:
    SceneManager& m_manager;
    std::vector<sakura::game::ChartInfo> m_charts;
    std::vector<int> m_visible;
    std::set<std::string> m_favorites;
    std::vector<sakura::game::SongCollection> m_collections;
    int m_collectionFilter = 0;
    int m_activeCollectionIndex = 0;
    int m_selected = -1, m_difficulty = 0, m_scroll = 0, m_sort = 0;
    bool m_rebuild = false;
    float m_previewTimer = 0, m_time = 0;
    bool m_previewPlaying = false;
    sakura::core::FontHandle m_font = 0;
    sakura::core::TextureHandle m_cover = 0;
    sakura::effects::BackgroundRenderer m_bgCover;
    sakura::effects::ParticleSystem m_particles;
    int m_petalEmitter = -1;
    std::optional<sakura::game::GameResult> m_best;
    int m_noteCount = 0, m_chartEnd = 0;
    sakura::game::PlayOptions m_options;
    std::optional<sakura::game::DailyChallengeSpec> m_dailyActiveSpec;
    std::string m_dailyResolvedChartId;
    std::optional<int> m_dailyMyRank;
    bool m_playAsDaily = false;
    std::unique_ptr<sakura::ui::TextInput> m_search, m_startInput, m_endInput;
    std::vector<std::unique_ptr<sakura::ui::Button>> m_buttons, m_detailButtons;
    struct DialogState { std::mutex mutex; bool ready = false; std::string path; };
    std::shared_ptr<DialogState> m_dialog = std::make_shared<DialogState>();
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_showGlobalLeaderboard = false;
    bool m_leaderboardLoading = false;
    std::string m_leaderboardStatus;
    std::optional<int> m_globalMyRank;
    std::vector<sakura::net::LeaderboardRow> m_globalEntries;
    void FetchGlobalLeaderboard();
    void WatchOnlineReplay(const sakura::net::LeaderboardRow& row);
    void CreatePresetCollection();
    void ToggleCurrentInCollection();
    void ActivateDailyChallenge();
    bool IsSelectedMatchingDaily() const;
    void Scan();
    void Filter();
    void Select(int index, int difficulty = -1);
    void SelectRelative(int delta);
    void BuildDetail();
    void Start(sakura::game::PlayMode mode);
    void ImportFolder();
    void AcceptImport(const std::string& path);
    void Favorite();
    void Back();
    std::unique_ptr<sakura::ui::Button> Button(sakura::core::NormRect rect, const std::string& label,
        std::function<void()> action, bool primary = false, float fontSize = 0.021f);
};
}
