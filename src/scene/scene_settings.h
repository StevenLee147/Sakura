#pragma once

#include "scene.h"
#include "scene_manager.h"
#include "ui/button.h"
#include "ui/ui_base.h"
#include <array>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

namespace sakura::scene
{
class SceneSettings final : public Scene
{
public:
    explicit SceneSettings(SceneManager& manager) : m_manager(manager) {}
    int CurrentPage() const { return m_page; }
    bool CanClose() override;
    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;
private:
    struct Row
    {
        std::string title, hint;
        std::unique_ptr<sakura::ui::UIBase> control;
    };
    struct DialogState { std::mutex mutex; std::string directory; };
    SceneManager& m_manager;
    sakura::core::FontHandle m_font = 0;
    std::array<std::vector<Row>, 6> m_pages;
    std::array<std::unique_ptr<sakura::ui::Button>, 6> m_tabs;
    std::vector<std::unique_ptr<sakura::ui::Button>> m_footer;
    std::vector<std::unique_ptr<sakura::ui::Button>> m_modalButtons;
    std::shared_ptr<DialogState> m_dialog = std::make_shared<DialogState>();
    nlohmann::json m_saved;
    int m_page = 0;
    int m_listening = -1;
    float m_listenTime = 0;
    bool m_dirty = false;
    bool m_closeRequested = false;
    int m_modal = 0; // unsaved / reset / restore
    std::string m_restoreDirectory, m_lastBackupDirectory;
    std::array<int, 6> m_keys{};
    void BuildPages();
    void AddSlider(int page, const std::string& title, const std::string& hint, const std::string& key,
                   float low, float high, float step, bool integral = false);
    void AddToggle(int page, const std::string& title, const std::string& hint, const std::string& key);
    void AddChoice(int page, const std::string& title, const std::string& hint, const std::string& key,
                   const std::vector<std::string>& labels, const std::vector<nlohmann::json>& values);
    void AddAction(int page, const std::string& title, const std::string& hint, const std::string& label,
                   std::function<void()> action);
    void SyncAudio();
    bool Save();
    void Back();
    void ShowModal(int kind);
    bool CreateBackup();
    void RestoreBackup();
};
}
