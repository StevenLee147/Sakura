#pragma once

// scene_arena.h — 在线多人对战大厅与赛后领奖台场景 (v1.6.0)

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "effects/particle_system.h"
#include "game/chart.h"
#include "net/api_client.h"
#include "ui/button.h"
#include "ui/text_input.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sakura::scene
{

class SceneArena final : public Scene
{
public:
    explicit SceneArena(SceneManager& mgr, std::string initialRoomCode = {});

    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

    bool IsInRoom() const { return m_currentRoom.has_value(); }

private:
    SceneManager& m_manager;
    std::string   m_initialRoomCode;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    sakura::core::FontHandle m_font = sakura::core::INVALID_HANDLE;
    sakura::effects::ParticleSystem m_particles;
    int   m_petalEmitter = -1;
    float m_time         = 0.0f;
    float m_pollTimer    = 0.0f;
    bool  m_busy         = false;
    bool  m_launchingMatch = false;
    std::string m_statusText;

    std::vector<sakura::game::ChartInfo>   m_charts;
    int                                    m_selectedChartIdx = 0;
    int                                    m_selectedDiffIdx  = 0;
    std::vector<sakura::net::ArenaRoomState> m_rooms;
    int                                    m_selectedRoomRow  = 0;
    std::optional<sakura::net::ArenaRoomState> m_currentRoom;

    std::unique_ptr<sakura::ui::TextInput> m_codeInput;
    std::unique_ptr<sakura::ui::TextInput> m_roomNameInput;

    // 大厅态按钮
    std::unique_ptr<sakura::ui::Button> m_btnBack;
    std::unique_ptr<sakura::ui::Button> m_btnRefreshLobby;
    std::unique_ptr<sakura::ui::Button> m_btnCreateRoom;
    std::unique_ptr<sakura::ui::Button> m_btnJoinByCode;
    std::unique_ptr<sakura::ui::Button> m_btnJoinSelected;

    // 房间内按钮
    std::unique_ptr<sakura::ui::Button> m_btnLeaveRoom;
    std::unique_ptr<sakura::ui::Button> m_btnPrevSong;
    std::unique_ptr<sakura::ui::Button> m_btnNextSong;
    std::unique_ptr<sakura::ui::Button> m_btnPrevDiff;
    std::unique_ptr<sakura::ui::Button> m_btnNextDiff;
    std::unique_ptr<sakura::ui::Button> m_btnApplyChart;
    std::unique_ptr<sakura::ui::Button> m_btnReadyOrStart;

    void RefreshLobby();
    void CreateRoom();
    void DoCreateRoom();
    void JoinRoom(const std::string& code);
    void DoJoinRoom(const std::string& cleanCode);
    void LeaveCurrentRoom();
    void PollRoomState();
    void ApplyHostChartSelection();
    void ToggleReadyOrStart();
    void TryLaunchArenaMatch(const sakura::net::ArenaRoomState& room);
};

} // namespace sakura::scene
