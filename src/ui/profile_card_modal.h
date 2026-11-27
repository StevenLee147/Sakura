#pragma once

// profile_card_modal.h — 月夜墨樱在线玩家生涯档案卡与劲敌操作弹窗 (v2.5.0)

#include "core/renderer.h"
#include "core/resource_manager.h"
#include "net/api_client.h"
#include "ui/button.h"

#include <functional>
#include <memory>
#include <string>

namespace sakura::ui
{

class ProfileCardModal
{
public:
    explicit ProfileCardModal(sakura::core::FontHandle font = 0);
    ~ProfileCardModal();

    void Open(const std::string& userId, const std::string& fallbackUsername = {});
    void Close();
    bool IsOpen() const { return m_open; }

    void SetOnRivalChanged(std::function<void()> cb) { m_onRivalChanged = std::move(cb); }
    void SetOnChallengeGhost(std::function<void(const sakura::net::UserProfileScoreItem&, const std::string&)> cb)
    {
        m_onChallengeGhost = std::move(cb);
    }

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);
    bool HandleEvent(const SDL_Event& event);

private:
    void EnsureButtons();
    void RefreshButtonLabels();

    sakura::core::FontHandle       m_font = 0;
    std::shared_ptr<bool>          m_alive = std::make_shared<bool>(true);
    bool                           m_open = false;
    bool                           m_loading = false;
    bool                           m_busy = false;
    std::string                    m_targetUserId;
    std::string                    m_statusMsg;
    sakura::net::UserProfileData   m_profile;
    int                            m_selectedScoreRow = 0;
    int                            m_scrollOffset = 0;

    std::unique_ptr<Button>        m_btnToggleRival;
    std::unique_ptr<Button>        m_btnTargetRival;
    std::unique_ptr<Button>        m_btnChallengeGhost;
    std::unique_ptr<Button>        m_btnClose;

    std::function<void()>          m_onRivalChanged;
    std::function<void(const sakura::net::UserProfileScoreItem&, const std::string&)> m_onChallengeGhost;
};

} // namespace sakura::ui
