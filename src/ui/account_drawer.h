#pragma once

// account_drawer.h — 全场景滑出式在线账号与云同步抽屉 (v2.3.0)

#include "core/auth_store.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "net/api_client.h"
#include "ui/button.h"
#include "ui/text_input.h"

#include <memory>
#include <string>
#include <vector>

namespace sakura::ui
{

class AccountDrawer
{
public:
    explicit AccountDrawer(sakura::core::FontHandle font = 0);
    ~AccountDrawer();

    void Open();
    void Close();
    void Toggle();

    bool IsOpen() const { return m_open || m_animProgress > 0.01f; }
    bool IsBlockingInput() const { return m_open; }

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);
    bool HandleEvent(const SDL_Event& event);

private:
    void EnsureControls();
    void RefreshSavedSlots();
    void UpdateControlLayout();
    sakura::core::NormRect DrawerRect() const;
    sakura::core::NormRect SavedSlotRect(int slot) const;
    sakura::core::NormRect SavedRemoveRect(int slot) const;

    sakura::core::FontHandle m_font = 0;
    std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);
    bool                     m_open = false;
    float                    m_animProgress = 0.0f;
    bool                     m_busy = false;
    bool                     m_changePwdMode = false;
    std::string              m_statusMessage;

    sakura::net::AuthState   m_lastState = sakura::net::AuthState::Unauthenticated;
    std::string              m_lastUserId;
    std::vector<sakura::core::SavedAccountEntry> m_savedAccounts;
    int                      m_hoveredSlot = -1;
    int                      m_hoveredRemove = -1;

    std::unique_ptr<TextInput> m_inputServer;
    std::unique_ptr<TextInput> m_inputUsername;
    std::unique_ptr<TextInput> m_inputPassword;
    std::unique_ptr<TextInput> m_inputNewPassword;

    std::unique_ptr<Button>    m_btnGuest;
    std::unique_ptr<Button>    m_btnUpgradeGuest;
    std::unique_ptr<Button>    m_btnToggleChangePwd;
    std::unique_ptr<Button>    m_btnSubmitNewPwd;
    std::unique_ptr<Button>    m_btnLogin;
    std::unique_ptr<Button>    m_btnRegister;
    std::unique_ptr<Button>    m_btnCloudSync;
    std::unique_ptr<Button>    m_btnLogout;
    std::unique_ptr<Button>    m_btnClose;
};

} // namespace sakura::ui
