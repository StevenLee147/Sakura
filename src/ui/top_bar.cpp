// top_bar.cpp — 全场景统一月夜磨砂状态顶栏实现 (v2.3.0)

#include "ui/top_bar.h"

#include "audio/audio_manager.h"
#include "core/input.h"
#include "core/layout.h"
#include "game/pp_calculator.h"
#include "net/api_client.h"
#include "ui/toast.h"
#include "ui/visual_style.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace sakura::ui
{

namespace
{
constexpr float kBarHeight = 0.038f;
constexpr sakura::core::NormRect kBackPillRect    { 0.012f, 0.004f, 0.052f, 0.029f };
constexpr sakura::core::NormRect kAccountPillRect { 0.772f, 0.004f, 0.214f, 0.030f };
constexpr sakura::core::NormRect kPendingPillRect { 0.674f, 0.004f, 0.090f, 0.030f };
constexpr sakura::core::NormRect kTransferPillRect{ 0.548f, 0.004f, 0.118f, 0.030f };

bool HitRect(sakura::core::NormRect r, float x, float y)
{
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}
} // namespace

TopBar::TopBar(std::string sceneTitle, sakura::core::FontHandle font, std::function<void()> onBack)
    : m_title(std::move(sceneTitle))
    , m_font(font)
    , m_onBack(std::move(onBack))
    , m_drawer(std::make_unique<AccountDrawer>(font))
{
}

TopBar::~TopBar() = default;

void TopBar::OpenAccountDrawer()
{
    if (m_drawer)
        m_drawer->Open();
}

void TopBar::CloseAccountDrawer()
{
    if (m_drawer)
        m_drawer->Close();
}

void TopBar::Update(float dt)
{
    m_lastTickNs = SDL_GetTicksNS();
    m_pulse += dt;
    sakura::net::APIClient::GetInstance().PumpCallbacks();
    if (m_font == 0)
        m_font = sakura::core::ResourceManager::GetInstance().GetDefaultFontHandle();
    if (m_drawer)
        m_drawer->Update(dt);

    const auto [mx, my] = sakura::core::Input::GetMousePosition();
    m_hoverBack    = static_cast<bool>(m_onBack) && HitRect(kBackPillRect, mx, my);
    m_hoverPending = (sakura::net::APIClient::GetInstance().GetPendingScoreCount() > 0) && HitRect(kPendingPillRect, mx, my);
    m_hoverAccount = HitRect(kAccountPillRect, mx, my);
}

void TopBar::Render(sakura::core::Renderer& renderer)
{
    const Uint64 nowNs = SDL_GetTicksNS();
    if (m_lastTickNs == 0 || (nowNs > m_lastTickNs && (nowNs - m_lastTickNs) > 4000000ULL))
    {
        const float autoDt = (m_lastTickNs == 0)
            ? (1.0f / 60.0f)
            : std::min(0.1f, static_cast<float>(nowNs - m_lastTickNs) * 1e-9f);
        Update(autoDt);
    }
    if (m_font == 0)
        m_font = sakura::core::ResourceManager::GetInstance().GetDefaultFontHandle();

    // Full-bleed translucent frosted top strip (0.0 ~ 0.038, non-overlapping scene content)
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, kBarHeight },
        { 10, 14, 28, 184 }, { 14, 18, 36, 184 },
        { 12, 16, 32, 142 }, { 16, 20, 40, 142 });
    VisualStyle::DrawGildedDivider(renderer, 0.0f, 1.0f, kBarHeight, 0.42f);

    const float fontScale = sakura::core::Layout::ShortSideRatio(renderer);
    const float smallFont = 0.0128f * fontScale;

    float titleX = 0.016f;
    if (m_onBack)
    {
        VisualStyle::DrawBadgePill(
            renderer,
            kBackPillRect,
            "← 返回",
            m_font,
            smallFont,
            m_hoverBack ? sakura::core::Color{ 52, 40, 78, 228 } : sakura::core::Color{ 24, 30, 54, 198 },
            m_hoverBack ? sakura::core::Color{ 250, 216, 157, 195 } : sakura::core::Color{ 166, 176, 214, 90 },
            m_hoverBack ? sakura::core::Color{ 250, 216, 157, 255 } : sakura::core::Color{ 232, 236, 248, 235 });
        titleX = 0.074f;
    }
    if (!m_title.empty())
    {
        renderer.DrawText(m_font, m_title, titleX, 0.010f, smallFont, { 188, 196, 222, 210 });
    }

    auto& api = sakura::net::APIClient::GetInstance();
    const int activeTransfers = api.GetActiveTransferCount();
    if (activeTransfers > 0)
    {
        const float prog = std::clamp(api.GetActiveTransferProgress(), 0.0f, 1.0f);
        const int pct    = static_cast<int>(std::lround(prog * 100.0f));
        VisualStyle::DrawBadgePill(
            renderer,
            kTransferPillRect,
            "↓ 同步 " + std::to_string(pct) + "%",
            m_font,
            smallFont * 0.95f,
            { 24, 38, 64, 215 },
            { 138, 218, 248, 175 },
            { 138, 218, 248, 255 });
        renderer.DrawRoundedRect(
            { kTransferPillRect.x + 0.008f, kTransferPillRect.y + kTransferPillRect.height - 0.005f,
              (kTransferPillRect.width - 0.016f) * std::max(0.06f, prog), 0.003f },
            0.0015f, { 250, 216, 157, 240 }, true);
    }

    const std::size_t pendingCount = api.GetPendingScoreCount();
    if (pendingCount > 0)
    {
        VisualStyle::DrawBadgePill(
            renderer,
            kPendingPillRect,
            "待同步 " + std::to_string(pendingCount),
            m_font,
            smallFont * 0.95f,
            m_hoverPending ? sakura::core::Color{ 76, 52, 34, 232 } : sakura::core::Color{ 48, 36, 28, 210 },
            { 250, 216, 157, 185 },
            { 250, 216, 157, 255 });
    }

    // Right-most player identity & online status capsule
    const auto st   = api.GetAuthState();
    const auto user = api.GetCurrentUser();
    sakura::core::Color dotCol{ 156, 164, 192, 220 };
    sakura::core::Color borderCol{ 168, 178, 214, 115 };
    std::string capLabel = "登录 / 在线账号";

    if (st == sakura::net::AuthState::AuthenticatedOnline)
    {
        dotCol    = { 142, 238, 192, 255 };
        borderCol = { 250, 216, 157, 195 };
        const std::string badge = user.danBadge.empty() ? "" : ("[" + user.danBadge + "] ");
        capLabel = badge + user.username + " · ★ " + sakura::game::PPCalculator::FormatRating(user.sakuraRating);
    }
    else if (st == sakura::net::AuthState::AuthenticatedOffline)
    {
        dotCol    = { 250, 216, 157, 255 };
        borderCol = { 250, 216, 157, 165 };
        capLabel  = (user.username.empty() ? std::string("本地账号") : user.username) + " · 离线保活";
    }
    else if (st == sakura::net::AuthState::SessionExpired)
    {
        dotCol    = { 255, 132, 148, 255 };
        borderCol = { 255, 132, 148, 195 };
        capLabel  = (user.username.empty() ? std::string("会话过期") : user.username) + " · 需重登";
    }

    renderer.DrawRoundedGradientRect(
        kAccountPillRect, 0.012f,
        m_hoverAccount ? sakura::core::Color{ 54, 38, 76, 236 } : sakura::core::Color{ 26, 28, 54, 218 },
        m_hoverAccount ? sakura::core::Color{ 38, 30, 62, 236 } : sakura::core::Color{ 18, 22, 42, 218 },
        { 18, 22, 42, 228 }, { 14, 18, 36, 228 });
    renderer.DrawRoundedRectOutlineGradient(
        kAccountPillRect, 0.012f,
        m_hoverAccount ? sakura::core::Color{ 250, 216, 157, 235 } : borderCol,
        { 246, 178, 206, 85 }, 0.0012f);

    const float pulseR = 0.0036f + 0.0005f * std::sin(m_pulse * 3.2f);
    renderer.DrawCircleFilled(kAccountPillRect.x + 0.012f, kAccountPillRect.y + kAccountPillRect.height * 0.5f, pulseR, dotCol, 16);
    VisualStyle::DrawTextFit(
        renderer, m_font, capLabel,
        kAccountPillRect.x + 0.022f, kAccountPillRect.y + 0.006f,
        smallFont, kAccountPillRect.width - 0.030f,
        { 248, 244, 252, 255 });
}

void TopBar::RenderDrawer(sakura::core::Renderer& renderer)
{
    if (m_drawer)
        m_drawer->Render(renderer);
}

bool TopBar::HandleEvent(const SDL_Event& event)
{
    if (m_drawer && m_drawer->IsBlockingInput())
        return m_drawer->HandleEvent(event);

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        const auto [mx, my] = sakura::core::Input::GetMousePosition();
        if (HitRect(kAccountPillRect, mx, my))
        {
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
            if (m_drawer)
                m_drawer->Toggle();
            return true;
        }
        if (sakura::net::APIClient::GetInstance().GetPendingScoreCount() > 0 && HitRect(kPendingPillRect, mx, my))
        {
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
            ToastManager::Instance().Show("正在后台冲刷待同步离线记录...", ToastType::Info, 2.2f);
            sakura::net::APIClient::GetInstance().FlushPendingActionsAsync([](int flushed)
            {
                if (flushed > 0)
                    ToastManager::Instance().Show("已成功补交 " + std::to_string(flushed) + " 条离线记录", ToastType::Success, 3.0f);
            });
            return true;
        }
        if (m_onBack && HitRect(kBackPillRect, mx, my))
        {
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
            m_onBack();
            return true;
        }
    }
    return false;
}

} // namespace sakura::ui
