#pragma once

// top_bar.h — 全场景统一月夜磨砂状态顶栏与在线账号胶囊 (v2.3.0)

#include "core/renderer.h"
#include "core/resource_manager.h"
#include "ui/account_drawer.h"

#include <functional>
#include <memory>
#include <string>

namespace sakura::ui
{

class TopBar
{
public:
    explicit TopBar(std::string sceneTitle = {},
                    sakura::core::FontHandle font = 0,
                    std::function<void()> onBack = {});
    ~TopBar();

    void SetTitle(std::string title) { m_title = std::move(title); }
    void SetOnBack(std::function<void()> onBack) { m_onBack = std::move(onBack); }

    bool IsDrawerOpen() const { return m_drawer && m_drawer->IsBlockingInput(); }
    void OpenAccountDrawer();
    void CloseAccountDrawer();

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);
    void RenderDrawer(sakura::core::Renderer& renderer);
    bool HandleEvent(const SDL_Event& event);

private:
    std::string                   m_title;
    sakura::core::FontHandle      m_font = 0;
    std::function<void()>         m_onBack;
    std::unique_ptr<AccountDrawer> m_drawer;
    float                         m_pulse = 0.0f;
    Uint64                        m_lastTickNs = 0;
    bool                          m_hoverBack = false;
    bool                          m_hoverPending = false;
    bool                          m_hoverAccount = false;
};

} // namespace sakura::ui
