// account_drawer.cpp — 全场景滑出式在线账号与云同步抽屉实现 (v2.3.0)

#include "ui/account_drawer.h"

#include "audio/audio_manager.h"
#include "core/input.h"
#include "game/pp_calculator.h"
#include "ui/toast.h"
#include "ui/visual_style.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace sakura::ui
{

namespace
{
constexpr float kDrawerWidth = 0.345f;
constexpr float kDrawerTopY  = 0.042f;
constexpr sakura::core::Color kGold { 250, 216, 157, 255 };
constexpr sakura::core::Color kPink { 246, 178, 206, 255 };
constexpr sakura::core::Color kMint { 152, 236, 194, 255 };
constexpr sakura::core::Color kCoral{ 255, 134, 150, 255 };
constexpr sakura::core::Color kDim  { 168, 178, 204, 255 };
constexpr sakura::core::Color kWhite{ 248, 242, 248, 255 };

bool PointInRect(sakura::core::NormRect r, float x, float y)
{
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}

std::unique_ptr<Button> MakeDrawerBtn(sakura::core::NormRect rect,
                                      const std::string& label,
                                      sakura::core::FontHandle font,
                                      ButtonVariant variant,
                                      std::function<void()> onClick,
                                      float fontSize = 0.0175f)
{
    auto b = std::make_unique<Button>(rect, label, font, fontSize, 0.009f);
    b->SetTextAlign(sakura::core::TextAlign::Center);
    VisualStyle::ApplyButton(b.get(), variant);
    b->SetOnClick(std::move(onClick));
    return b;
}
} // namespace

AccountDrawer::AccountDrawer(sakura::core::FontHandle font)
    : m_font(font)
{
}

AccountDrawer::~AccountDrawer()
{
    *m_alive = false;
    Close();
}

sakura::core::NormRect AccountDrawer::DrawerRect() const
{
    const float t = std::clamp(m_animProgress, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    const float x = 1.0f - kDrawerWidth * eased;
    return { x, kDrawerTopY, kDrawerWidth, 1.0f - kDrawerTopY };
}

sakura::core::NormRect AccountDrawer::SavedSlotRect(int slot) const
{
    const auto dr = DrawerRect();
    return { dr.x + 0.018f, 0.718f + static_cast<float>(slot) * 0.044f, dr.width - 0.036f, 0.038f };
}

sakura::core::NormRect AccountDrawer::SavedRemoveRect(int slot) const
{
    const auto sr = SavedSlotRect(slot);
    return { sr.x + sr.width - 0.030f, sr.y + 0.005f, 0.024f, 0.028f };
}

void AccountDrawer::RefreshSavedSlots()
{
    m_savedAccounts = sakura::net::APIClient::GetInstance().GetSavedAccounts();
    if (m_savedAccounts.size() > 5)
        m_savedAccounts.resize(5);
}

void AccountDrawer::EnsureControls()
{
    if (m_inputUsername)
        return;
    if (m_font == 0)
        m_font = sakura::core::ResourceManager::GetInstance().GetDefaultFontHandle();

    auto& api = sakura::net::APIClient::GetInstance();
    m_inputServer      = std::make_unique<TextInput>(sakura::core::NormRect{ 0.673f, 0.228f, 0.309f, 0.038f }, m_font, 0.0165f, 128);
    m_inputUsername    = std::make_unique<TextInput>(sakura::core::NormRect{ 0.673f, 0.288f, 0.309f, 0.038f }, m_font, 0.0170f, 32);
    m_inputPassword    = std::make_unique<TextInput>(sakura::core::NormRect{ 0.673f, 0.348f, 0.309f, 0.038f }, m_font, 0.0170f, 64);
    m_inputNewPassword = std::make_unique<TextInput>(sakura::core::NormRect{ 0.673f, 0.408f, 0.309f, 0.038f }, m_font, 0.0170f, 64);

    m_inputServer->SetText(api.GetServerUrl());
    m_inputServer->SetPlaceholder("在线服地址 (默认 https://steven-lee.tech:233)");
    m_inputUsername->SetPlaceholder("用户名 / 昵称 (2~24 字符)");
    m_inputPassword->SetPlaceholder("登录密码 (至少 6 位，访客免填)");
    m_inputPassword->SetPasswordMode(true);
    m_inputNewPassword->SetPlaceholder("新密码 (至少 6 位)");
    m_inputNewPassword->SetPasswordMode(true);
    m_inputNewPassword->SetVisible(false);

    auto onAuthDone = [this](const sakura::net::AuthResult& r)
    {
        m_busy = false;
        RefreshSavedSlots();
        if (r.ok)
        {
            m_changePwdMode = false;
            if (m_inputUsername)    m_inputUsername->SetText(r.user.username);
            if (m_inputPassword)    m_inputPassword->SetText("");
            if (m_inputNewPassword) m_inputNewPassword->SetText("");
            m_statusMessage = "已在线连接：" + r.user.username;
            ToastManager::Instance().Show("欢迎回来，" + r.user.username, ToastType::Success, 2.8f);
        }
        else
        {
            m_statusMessage = r.error.empty() ? "认证请求未通过" : r.error;
        }
        UpdateControlLayout();
    };

    m_btnGuest = MakeDrawerBtn({ 0.673f, 0.410f, 0.309f, 0.040f }, "一键创建访客通行证", m_font, ButtonVariant::Accent,
        [this, onAuthDone]()
        {
            if (m_busy) return;
            auto& client = sakura::net::APIClient::GetInstance();
            client.SetServerUrl(m_inputServer->GetText());
            m_busy = true;
            m_statusMessage = "正在创建访客身份...";
            UpdateControlLayout();
            auto alive = m_alive;
            client.LoginGuestAsync(m_inputUsername->GetText(), [alive, onAuthDone](sakura::net::AuthResult r)
            { if (*alive) onAuthDone(r); });
        });

    m_btnUpgradeGuest = MakeDrawerBtn({ 0.673f, 0.410f, 0.309f, 0.040f }, "✦ 升级为正式账号（无损保留战绩）", m_font, ButtonVariant::Primary,
        [this]()
        {
            if (m_busy) return;
            const std::string uname = m_inputUsername ? m_inputUsername->GetText() : "";
            const std::string pwd   = m_inputPassword ? m_inputPassword->GetText() : "";
            if (uname.size() < 2) { m_statusMessage = "请输入 2~24 字符的期望用户名"; return; }
            if (pwd.size() < 6)   { m_statusMessage = "请设置至少 6 位登录密码"; return; }
            auto& client = sakura::net::APIClient::GetInstance();
            client.SetServerUrl(m_inputServer->GetText());
            m_busy = true;
            m_statusMessage = "正在原地转正并绑定全部成绩...";
            UpdateControlLayout();
            auto alive = m_alive;
            client.UpgradeGuestAsync(uname, pwd, [this, alive](sakura::net::AuthResult r)
            {
                if (!*alive) return;
                m_busy = false;
                RefreshSavedSlots();
                if (r.ok)
                {
                    if (m_inputUsername) m_inputUsername->SetText(r.user.username);
                    if (m_inputPassword) m_inputPassword->SetText("");
                    m_statusMessage = "已转正为正式账号：" + r.user.username;
                    ToastManager::Instance().Show("访客转正成功！战绩与 Rating 已完整保留", ToastType::Success, 3.4f);
                }
                else
                {
                    m_statusMessage = r.error.empty() ? "升级正式账号失败" : r.error;
                }
                UpdateControlLayout();
            });
        });

    m_btnToggleChangePwd = MakeDrawerBtn({ 0.673f, 0.410f, 0.309f, 0.040f }, "✦ 修改登录密码", m_font, ButtonVariant::Accent,
        [this]()
        {
            if (m_busy) return;
            m_changePwdMode = !m_changePwdMode;
            if (m_inputPassword)    m_inputPassword->SetText("");
            if (m_inputNewPassword) m_inputNewPassword->SetText("");
            m_statusMessage = m_changePwdMode ? "请输入旧密码与至少 6 位新密码" : "";
            UpdateControlLayout();
        });

    m_btnSubmitNewPwd = MakeDrawerBtn({ 0.673f, 0.458f, 0.309f, 0.040f }, "确认修改密码", m_font, ButtonVariant::Primary,
        [this]()
        {
            if (m_busy) return;
            const std::string oldP = m_inputPassword ? m_inputPassword->GetText() : "";
            const std::string newP = m_inputNewPassword ? m_inputNewPassword->GetText() : "";
            if (oldP.empty() || newP.size() < 6) { m_statusMessage = "请输入旧密码及至少 6 位新密码"; return; }
            m_busy = true;
            m_statusMessage = "正在校验并更新密码...";
            UpdateControlLayout();
            auto alive = m_alive;
            sakura::net::APIClient::GetInstance().ChangePasswordAsync(oldP, newP, [this, alive](sakura::net::AuthResult r)
            {
                if (!*alive) return;
                m_busy = false;
                if (r.ok)
                {
                    m_changePwdMode = false;
                    if (m_inputPassword)    m_inputPassword->SetText("");
                    if (m_inputNewPassword) m_inputNewPassword->SetText("");
                    m_statusMessage = "密码已更新，双令牌已轮换";
                    ToastManager::Instance().Show("密码修改成功", ToastType::Success, 3.0f);
                }
                else
                {
                    m_statusMessage = r.error.empty() ? "修改密码失败" : r.error;
                }
                UpdateControlLayout();
            });
        });

    m_btnLogin = MakeDrawerBtn({ 0.673f, 0.458f, 0.150f, 0.040f }, "登录账号", m_font, ButtonVariant::Primary,
        [this, onAuthDone]()
        {
            if (m_busy) return;
            auto& client = sakura::net::APIClient::GetInstance();
            client.SetServerUrl(m_inputServer->GetText());
            m_busy = true;
            m_statusMessage = "正在登录验证...";
            UpdateControlLayout();
            auto alive = m_alive;
            client.LoginAsync(m_inputUsername->GetText(), m_inputPassword->GetText(),
                [alive, onAuthDone](sakura::net::AuthResult r) { if (*alive) onAuthDone(r); });
        });

    m_btnRegister = MakeDrawerBtn({ 0.832f, 0.458f, 0.150f, 0.040f }, "注册账号", m_font, ButtonVariant::Secondary,
        [this, onAuthDone]()
        {
            if (m_busy) return;
            auto& client = sakura::net::APIClient::GetInstance();
            client.SetServerUrl(m_inputServer->GetText());
            m_busy = true;
            m_statusMessage = "正在创建新账号...";
            UpdateControlLayout();
            auto alive = m_alive;
            client.RegisterAsync(m_inputUsername->GetText(), m_inputPassword->GetText(),
                [alive, onAuthDone](sakura::net::AuthResult r) { if (*alive) onAuthDone(r); });
        });

    m_btnCloudSync = MakeDrawerBtn({ 0.673f, 0.506f, 0.309f, 0.038f }, "↻ 立即同步云端存档", m_font, ButtonVariant::Secondary,
        [this]()
        {
            auto& client = sakura::net::APIClient::GetInstance();
            if (!client.IsAuthenticated())
            {
                m_statusMessage = "请先登录在线账号后再同步云存档";
                return;
            }
            m_statusMessage = "正在与云端幂等合并成绩与成就...";
            auto alive = m_alive;
            client.SyncCloudSaveAsync([this, alive](sakura::net::CloudSyncResult res)
            {
                if (!*alive) return;
                if (res.ok)
                {
                    m_statusMessage = "云同步完成：推送 " + std::to_string(res.scoresPushed) + " 项 / 拉取 " + std::to_string(res.scoresPulled + res.achievementsPulled) + " 项";
                    ToastManager::Instance().Show(m_statusMessage, ToastType::Success, 3.2f);
                }
                else
                {
                    m_statusMessage = "云同步暂不可用：" + res.error;
                }
            });
        });

    m_btnLogout = MakeDrawerBtn({ 0.673f, 0.552f, 0.150f, 0.038f }, "退出登录", m_font, ButtonVariant::Danger,
        [this]()
        {
            if (m_busy) return;
            m_busy = true;
            m_changePwdMode = false;
            m_statusMessage = "正在注销会话...";
            UpdateControlLayout();
            auto alive = m_alive;
            sakura::net::APIClient::GetInstance().LogoutAsync([this, alive]()
            {
                if (!*alive) return;
                m_busy = false;
                if (m_inputPassword)    m_inputPassword->SetText("");
                if (m_inputNewPassword) m_inputNewPassword->SetText("");
                RefreshSavedSlots();
                m_statusMessage = "已退出当前在线账号";
                UpdateControlLayout();
            });
        });

    m_btnClose = MakeDrawerBtn({ 0.832f, 0.552f, 0.150f, 0.038f }, "收起面板", m_font, ButtonVariant::Secondary,
        [this]() { Close(); });
}

void AccountDrawer::UpdateControlLayout()
{
    EnsureControls();
    const auto dr   = DrawerRect();
    const float x0  = dr.x + 0.018f;
    const float w   = dr.width - 0.036f;
    const float hw  = (w - 0.009f) * 0.5f;
    const auto& api = sakura::net::APIClient::GetInstance();
    const auto st   = api.GetAuthState();
    const auto user = api.GetCurrentUser();
    const bool authed = (st == sakura::net::AuthState::AuthenticatedOnline || st == sakura::net::AuthState::AuthenticatedOffline);

    m_inputServer->SetBounds({ x0, 0.228f, w, 0.038f });
    m_inputUsername->SetBounds({ x0, 0.286f, w, 0.038f });
    m_inputPassword->SetBounds({ x0, 0.344f, w, 0.038f });
    m_inputNewPassword->SetBounds({ x0, 0.402f, w, 0.038f });
    m_inputNewPassword->SetVisible(m_changePwdMode && authed && !user.isGuest);

    const float btnBaseY = m_inputNewPassword->IsVisible() ? 0.452f : 0.398f;
    if (m_changePwdMode && authed && !user.isGuest)
    {
        m_btnGuest->SetVisible(false);
        m_btnUpgradeGuest->SetVisible(false);
        m_btnLogin->SetVisible(false);
        m_btnRegister->SetVisible(false);
        m_btnSubmitNewPwd->SetBounds({ x0, btnBaseY, hw, 0.040f });
        m_btnSubmitNewPwd->SetVisible(true);
        m_btnSubmitNewPwd->SetEnabled(!m_busy);
        m_btnToggleChangePwd->SetBounds({ x0 + hw + 0.009f, btnBaseY, hw, 0.040f });
        m_btnToggleChangePwd->SetText("取消改密");
        m_btnToggleChangePwd->SetVisible(true);
        m_btnToggleChangePwd->SetEnabled(!m_busy);
    }
    else if (authed && user.isGuest)
    {
        m_btnGuest->SetVisible(false);
        m_btnToggleChangePwd->SetVisible(false);
        m_btnSubmitNewPwd->SetVisible(false);
        m_btnUpgradeGuest->SetBounds({ x0, btnBaseY, w, 0.040f });
        m_btnUpgradeGuest->SetVisible(true);
        m_btnUpgradeGuest->SetEnabled(!m_busy);
        m_btnLogin->SetBounds({ x0, btnBaseY + 0.048f, hw, 0.038f });
        m_btnLogin->SetText("切换登录");
        m_btnLogin->SetVisible(true);
        m_btnLogin->SetEnabled(!m_busy);
        m_btnRegister->SetBounds({ x0 + hw + 0.009f, btnBaseY + 0.048f, hw, 0.038f });
        m_btnRegister->SetText("注册新号");
        m_btnRegister->SetVisible(true);
        m_btnRegister->SetEnabled(!m_busy);
    }
    else if (authed && !user.isGuest)
    {
        m_btnGuest->SetVisible(false);
        m_btnUpgradeGuest->SetVisible(false);
        m_btnSubmitNewPwd->SetVisible(false);
        m_btnToggleChangePwd->SetBounds({ x0, btnBaseY, w, 0.040f });
        m_btnToggleChangePwd->SetText("✦ 修改登录密码");
        m_btnToggleChangePwd->SetVisible(true);
        m_btnToggleChangePwd->SetEnabled(!m_busy);
        m_btnLogin->SetBounds({ x0, btnBaseY + 0.048f, hw, 0.038f });
        m_btnLogin->SetText("重新验证");
        m_btnLogin->SetVisible(true);
        m_btnLogin->SetEnabled(!m_busy);
        m_btnRegister->SetBounds({ x0 + hw + 0.009f, btnBaseY + 0.048f, hw, 0.038f });
        m_btnRegister->SetText("注册新号");
        m_btnRegister->SetVisible(true);
        m_btnRegister->SetEnabled(!m_busy);
    }
    else
    {
        m_btnUpgradeGuest->SetVisible(false);
        m_btnToggleChangePwd->SetVisible(false);
        m_btnSubmitNewPwd->SetVisible(false);
        m_btnGuest->SetBounds({ x0, btnBaseY, w, 0.040f });
        m_btnGuest->SetVisible(true);
        m_btnGuest->SetEnabled(!m_busy);
        m_btnLogin->SetBounds({ x0, btnBaseY + 0.048f, hw, 0.038f });
        m_btnLogin->SetText(st == sakura::net::AuthState::SessionExpired ? "验证重登" : "登录账号");
        m_btnLogin->SetVisible(true);
        m_btnLogin->SetEnabled(!m_busy);
        m_btnRegister->SetBounds({ x0 + hw + 0.009f, btnBaseY + 0.048f, hw, 0.038f });
        m_btnRegister->SetText("注册账号");
        m_btnRegister->SetVisible(true);
        m_btnRegister->SetEnabled(!m_busy);
    }

    const float syncY = btnBaseY + 0.094f;
    m_btnCloudSync->SetBounds({ x0, syncY, w, 0.038f });
    m_btnCloudSync->SetEnabled(!m_busy && authed);
    m_btnLogout->SetBounds({ x0, syncY + 0.045f, hw, 0.038f });
    m_btnLogout->SetEnabled(!m_busy && st != sakura::net::AuthState::Unauthenticated);
    m_btnClose->SetBounds({ x0 + hw + 0.009f, syncY + 0.045f, hw, 0.038f });
}

void AccountDrawer::Open()
{
    EnsureControls();
    m_open          = true;
    m_busy          = false;
    m_changePwdMode = false;
    RefreshSavedSlots();
    auto& api = sakura::net::APIClient::GetInstance();
    m_inputServer->SetText(api.GetServerUrl());
    m_inputPassword->SetText("");
    m_inputNewPassword->SetText("");
    const auto user = api.GetCurrentUser();
    if (!user.username.empty())
        m_inputUsername->SetText(user.username);
    m_statusMessage = (api.GetAuthState() == sakura::net::AuthState::SessionExpired)
        ? "登录凭据已过期，请重新输入密码验证"
        : "";
    UpdateControlLayout();
}

void AccountDrawer::Close()
{
    m_open          = false;
    m_changePwdMode = false;
    if (m_inputServer)      m_inputServer->SetFocused(false);
    if (m_inputUsername)    m_inputUsername->SetFocused(false);
    if (m_inputPassword)    m_inputPassword->SetFocused(false);
    if (m_inputNewPassword) m_inputNewPassword->SetFocused(false);
}

void AccountDrawer::Toggle()
{
    if (m_open) Close();
    else Open();
}

void AccountDrawer::Update(float dt)
{
    const float target = m_open ? 1.0f : 0.0f;
    m_animProgress += (target - m_animProgress) * std::min(1.0f, dt * 16.0f);
    if (!m_open && m_animProgress < 0.005f)
    {
        m_animProgress = 0.0f;
        return;
    }

    EnsureControls();
    auto& api = sakura::net::APIClient::GetInstance();
    const auto st   = api.GetAuthState();
    const auto user = api.GetCurrentUser();
    if (st != m_lastState || user.id != m_lastUserId)
    {
        m_lastState  = st;
        m_lastUserId = user.id;
        RefreshSavedSlots();
    }
    UpdateControlLayout();

    if (!m_open)
        return;

    const auto [mx, my] = sakura::core::Input::GetMousePosition();
    m_hoveredSlot   = -1;
    m_hoveredRemove = -1;
    if (!m_busy)
    {
        for (int i = 0; i < static_cast<int>(m_savedAccounts.size()); ++i)
        {
            if (PointInRect(SavedRemoveRect(i), mx, my)) { m_hoveredRemove = i; break; }
            if (PointInRect(SavedSlotRect(i), mx, my))   { m_hoveredSlot   = i; break; }
        }
    }

    m_inputServer->Update(dt);
    m_inputUsername->Update(dt);
    m_inputPassword->Update(dt);
    if (m_inputNewPassword->IsVisible()) m_inputNewPassword->Update(dt);
    if (m_btnGuest->IsVisible())           m_btnGuest->Update(dt);
    if (m_btnUpgradeGuest->IsVisible())    m_btnUpgradeGuest->Update(dt);
    if (m_btnToggleChangePwd->IsVisible()) m_btnToggleChangePwd->Update(dt);
    if (m_btnSubmitNewPwd->IsVisible())    m_btnSubmitNewPwd->Update(dt);
    if (m_btnLogin->IsVisible())           m_btnLogin->Update(dt);
    if (m_btnRegister->IsVisible())        m_btnRegister->Update(dt);
    m_btnCloudSync->Update(dt);
    m_btnLogout->Update(dt);
    m_btnClose->Update(dt);
}

void AccountDrawer::Render(sakura::core::Renderer& renderer)
{
    if (!IsOpen())
        return;
    EnsureControls();

    VisualStyle::DrawScrim(renderer, 0.52f * std::clamp(m_animProgress, 0.0f, 1.0f));
    const auto dr = DrawerRect();
    VisualStyle::DrawGlassCard(renderer, dr,
        { 22, 26, 50, 246 }, { 12, 16, 34, 250 },
        { 250, 216, 157, 210 }, { 246, 178, 206, 110 }, 0.016f, 0.65f);

    const float x0 = dr.x + 0.018f;
    const float w  = dr.width - 0.036f;
    VisualStyle::DrawBadgePill(renderer, { x0, 0.058f, 0.142f, 0.024f },
        "SAKURA PASSPORT", m_font, 0.012f, { 48, 36, 72, 220 }, kGold, kGold);
    renderer.DrawText(m_font, "在线账号与多端云同步", x0, 0.088f, 0.023f, kWhite);

    auto& api = sakura::net::APIClient::GetInstance();
    const auto st   = api.GetAuthState();
    const auto user = api.GetCurrentUser();
    const sakura::core::NormRect idCard{ x0, 0.122f, w, 0.078f };
    renderer.DrawRoundedGradientRect(idCard, 0.010f,
        { 32, 28, 60, 225 }, { 22, 24, 48, 225 }, { 18, 20, 40, 235 }, { 14, 18, 36, 235 });
    renderer.DrawRoundedRectOutlineGradient(idCard, 0.010f, { 250, 216, 157, 145 }, { 246, 178, 206, 65 }, 0.0012f);

    std::string stText = "○ 未登录 · 支持访客秒进或注册正式账号";
    sakura::core::Color stCol = kDim;
    if (st == sakura::net::AuthState::AuthenticatedOnline)
    {
        stText = "● 在线已连接  ·  " + (user.isGuest ? std::string("访客通行证") : std::string("正式认证账号"));
        stCol  = kMint;
    }
    else if (st == sakura::net::AuthState::AuthenticatedOffline)
    {
        stText = "◈ 离线保活中  ·  DPAPI 本地加密凭据已保护";
        stCol  = kGold;
    }
    else if (st == sakura::net::AuthState::SessionExpired)
    {
        stText = "! 凭据已过期  ·  请重新输入密码验证";
        stCol  = kCoral;
    }
    VisualStyle::DrawTextFit(renderer, m_font, stText, x0 + 0.012f, 0.132f, 0.0135f, w - 0.024f, stCol);

    if (!user.username.empty())
    {
        const std::string badge = user.danBadge.empty() ? "" : ("[" + user.danBadge + "] ");
        const std::string summary = badge + user.username + "   ·   ★ "
            + sakura::game::PPCalculator::FormatRating(user.sakuraRating);
        VisualStyle::DrawTextFit(renderer, m_font, summary, x0 + 0.012f, 0.160f, 0.0185f, w - 0.024f, kGold);
    }
    else
    {
        renderer.DrawText(m_font, "登录后开启全球榜、市场评分与云端存档", x0 + 0.012f, 0.164f, 0.0145f, kPink);
    }

    renderer.DrawText(m_font, "服务器地址", x0, 0.208f, 0.0128f, kDim);
    renderer.DrawText(m_font, "用户名 / 昵称", x0, 0.268f, 0.0128f, kDim);
    renderer.DrawText(m_font, m_changePwdMode ? "当前旧密码" : "登录密码", x0, 0.326f, 0.0128f, kDim);
    if (m_inputNewPassword->IsVisible())
        renderer.DrawText(m_font, "设置新密码", x0, 0.384f, 0.0128f, kGold);

    m_inputServer->Render(renderer);
    m_inputUsername->Render(renderer);
    m_inputPassword->Render(renderer);
    if (m_inputNewPassword->IsVisible()) m_inputNewPassword->Render(renderer);
    if (m_btnGuest->IsVisible())           m_btnGuest->Render(renderer);
    if (m_btnUpgradeGuest->IsVisible())    m_btnUpgradeGuest->Render(renderer);
    if (m_btnToggleChangePwd->IsVisible()) m_btnToggleChangePwd->Render(renderer);
    if (m_btnSubmitNewPwd->IsVisible())    m_btnSubmitNewPwd->Render(renderer);
    if (m_btnLogin->IsVisible())           m_btnLogin->Render(renderer);
    if (m_btnRegister->IsVisible())        m_btnRegister->Render(renderer);
    m_btnCloudSync->Render(renderer);
    m_btnLogout->Render(renderer);
    m_btnClose->Render(renderer);

    if (!m_statusMessage.empty())
    {
        VisualStyle::DrawTextFit(renderer, m_font, m_statusMessage, x0, 0.658f, 0.0142f, w, kPink);
    }

    VisualStyle::DrawGildedDivider(renderer, x0, x0 + w, 0.688f, 0.68f);
    renderer.DrawText(m_font, "SAVED ACCOUNTS · 最近登录槽位 (免密快切)", x0, 0.696f, 0.0125f, kDim);
    if (m_savedAccounts.empty())
    {
        renderer.DrawText(m_font, "暂无已保存的本机加密会话槽位", x0, 0.732f, 0.014f, kDim);
    }
    else
    {
        for (int i = 0; i < static_cast<int>(m_savedAccounts.size()); ++i)
        {
            const auto& acc = m_savedAccounts[static_cast<std::size_t>(i)];
            const auto sr   = SavedSlotRect(i);
            const auto rr   = SavedRemoveRect(i);
            const bool active = (!user.id.empty() && acc.userId == user.id);
            const bool hov    = (i == m_hoveredSlot);
            renderer.DrawRoundedRect(sr, 0.008f,
                active ? sakura::core::Color{ 70, 44, 82, 228 }
                       : (hov ? sakura::core::Color{ 36, 44, 74, 220 } : sakura::core::Color{ 18, 22, 42, 205 }),
                true);
            renderer.DrawRoundedRectOutlineGradient(sr, 0.008f,
                active ? kGold : (hov ? kPink : sakura::core::Color{ 150, 160, 200, 70 }),
                { 110, 120, 160, 35 }, 0.0010f);
            const std::string tag = acc.isGuest ? "[访客] " : "[正式] ";
            const std::string line = tag + acc.username + "  ★ " + sakura::game::PPCalculator::FormatRating(acc.sakuraRating);
            VisualStyle::DrawTextFit(renderer, m_font, line, sr.x + 0.010f, sr.y + 0.009f, 0.0145f, sr.width - 0.048f,
                active ? kGold : kWhite);
            VisualStyle::DrawBadgePill(renderer, rr, "×", m_font, 0.014f,
                i == m_hoveredRemove ? sakura::core::Color{ 110, 38, 56, 235 } : sakura::core::Color{ 32, 36, 58, 195 },
                i == m_hoveredRemove ? kCoral : kDim,
                i == m_hoveredRemove ? kCoral : kDim);
        }
    }
}

bool AccountDrawer::HandleEvent(const SDL_Event& event)
{
    if (!m_open)
        return false;
    EnsureControls();

    const bool typing = m_inputServer->IsFocused() || m_inputUsername->IsFocused() ||
                        m_inputPassword->IsFocused() || (m_inputNewPassword->IsVisible() && m_inputNewPassword->IsFocused());
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !typing)
    {
        sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
        Close();
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        const auto [mx, my] = sakura::core::Input::GetMousePosition();
        const auto dr = DrawerRect();
        if (mx < dr.x && my > kDrawerTopY)
        {
            Close();
            return true;
        }
        if (!m_busy)
        {
            for (int i = 0; i < static_cast<int>(m_savedAccounts.size()); ++i)
            {
                if (PointInRect(SavedRemoveRect(i), mx, my))
                {
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    sakura::net::APIClient::GetInstance().RemoveSavedAccount(m_savedAccounts[static_cast<std::size_t>(i)].userId);
                    RefreshSavedSlots();
                    m_statusMessage = "已移除本地保存的账号槽位";
                    return true;
                }
                if (PointInRect(SavedSlotRect(i), mx, my))
                {
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    const std::string uid  = m_savedAccounts[static_cast<std::size_t>(i)].userId;
                    const std::string name = m_savedAccounts[static_cast<std::size_t>(i)].username;
                    m_busy = true;
                    m_statusMessage = "正在切换至账号：" + name + "...";
                    UpdateControlLayout();
                    auto alive = m_alive;
                    sakura::net::APIClient::GetInstance().SwitchSavedAccountAsync(uid, [this, alive, name](sakura::net::AuthResult r)
                    {
                        if (!*alive) return;
                        m_busy = false;
                        RefreshSavedSlots();
                        if (r.ok)
                        {
                            m_inputServer->SetText(sakura::net::APIClient::GetInstance().GetServerUrl());
                            m_inputUsername->SetText(r.user.username);
                            m_statusMessage = "已切换账号：" + r.user.username;
                            ToastManager::Instance().Show("已切换至 " + r.user.username, ToastType::Success, 2.5f);
                        }
                        else
                        {
                            m_inputUsername->SetText(name);
                            m_inputPassword->SetFocused(true);
                            m_statusMessage = r.error.empty() ? "会话已过期，请输入密码验证" : r.error;
                        }
                        UpdateControlLayout();
                    });
                    return true;
                }
            }
        }
    }

    bool consumed = false;
    consumed = m_inputServer->HandleEvent(event) || consumed;
    consumed = m_inputUsername->HandleEvent(event) || consumed;
    consumed = m_inputPassword->HandleEvent(event) || consumed;
    if (m_inputNewPassword->IsVisible())
        consumed = m_inputNewPassword->HandleEvent(event) || consumed;
    if (consumed && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING))
        return true;

    if (m_btnGuest->IsVisible()           && m_btnGuest->HandleEvent(event))           return true;
    if (m_btnUpgradeGuest->IsVisible()    && m_btnUpgradeGuest->HandleEvent(event))    return true;
    if (m_btnToggleChangePwd->IsVisible() && m_btnToggleChangePwd->HandleEvent(event)) return true;
    if (m_btnSubmitNewPwd->IsVisible()    && m_btnSubmitNewPwd->HandleEvent(event))    return true;
    if (m_btnLogin->IsVisible()           && m_btnLogin->HandleEvent(event))           return true;
    if (m_btnRegister->IsVisible()        && m_btnRegister->HandleEvent(event))        return true;
    if (m_btnCloudSync->HandleEvent(event)) return true;
    if (m_btnLogout->HandleEvent(event))    return true;
    if (m_btnClose->HandleEvent(event))     return true;

    return true; // Modal drawer blocks click-through to underlying scene
}

} // namespace sakura::ui
