#pragma once
#include "scene.h"
#include "scene_manager.h"
#include "game/chart.h"
#include "game/play_session.h"
#include "ui/button.h"
#include "ui/text_input.h"
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace sakura::scene {
class SceneSelect final : public Scene {
public:
    explicit SceneSelect(SceneManager& manager) : m_manager(manager) {}
    sakura::game::PlayMode CurrentMode() const { return m_options.mode; }
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
    int m_selected = -1, m_difficulty = 0, m_scroll = 0, m_sort = 0;
    bool m_onlyFavorites = false, m_rebuild = false;
    float m_previewTimer = 0, m_time = 0;
    bool m_previewPlaying = false;
    sakura::core::FontHandle m_font = 0;
    sakura::core::TextureHandle m_cover = 0;
    std::optional<sakura::game::GameResult> m_best;
    int m_noteCount = 0, m_chartEnd = 0;
    sakura::game::PlayOptions m_options;
    std::unique_ptr<sakura::ui::TextInput> m_search, m_startInput, m_endInput;
    std::vector<std::unique_ptr<sakura::ui::Button>> m_buttons, m_detailButtons;
    struct DialogState { std::mutex mutex; bool ready = false; std::string path; };
    std::shared_ptr<DialogState> m_dialog = std::make_shared<DialogState>();
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
        std::function<void()> action, bool primary = false);
};
}
