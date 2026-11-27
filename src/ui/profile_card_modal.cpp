// profile_card_modal.cpp — 月夜墨樱在线玩家生涯档案卡与劲敌操作弹窗实现 (v2.5.0)

#include "ui/profile_card_modal.h"

#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/input.h"
#include "game/chart_utils.h"
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
constexpr sakura::core::NormRect kModalRect{ 0.17f, 0.11f, 0.66f, 0.78f };
constexpr int kVisibleScoreRows = 6;
constexpr sakura::core::Color kGold { 250, 216, 157, 255 };
constexpr sakura::core::Color kPink { 246, 178, 206, 255 };
constexpr sakura::core::Color kMint { 152, 236, 194, 255 };
constexpr sakura::core::Color kCyan { 138, 218, 248, 255 };
constexpr sakura::core::Color kCoral{ 255, 134, 150, 255 };
constexpr sakura::core::Color kDim  { 168, 178, 204, 255 };
constexpr sakura::core::Color kWhite{ 248, 242, 248, 255 };

std::string FmtDec(double val, int prec = 2)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << val;
    return oss.str();
}

bool InRect(sakura::core::NormRect r, float x, float y)
{
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}
} // namespace

ProfileCardModal::ProfileCardModal(sakura::core::FontHandle font)
    : m_font(font)
{
}

ProfileCardModal::~ProfileCardModal()
{
    *m_alive = false;
}

void ProfileCardModal::EnsureButtons()
{
    if (m_btnClose)
        return;
    if (m_font == 0)
        m_font = sakura::core::ResourceManager::GetInstance().GetDefaultFontHandle();

    auto makeBtn = [this](sakura::core::NormRect r, const std::string& txt, ButtonVariant var, std::function<void()> fn)
    {
        auto b = std::make_unique<Button>(r, txt, m_font, 0.0175f, 0.009f);
        b->SetTextAlign(sakura::core::TextAlign::Center);
        VisualStyle::ApplyButton(b.get(), var);
        b->SetOnClick(std::move(fn));
        return b;
    };

    m_btnToggleRival = makeBtn({ 0.195f, 0.818f, 0.152f, 0.046f }, "✦ 加为劲敌", ButtonVariant::Primary, [this]()
    {
        if (m_busy || m_loading || m_profile.userId.empty()) return;
        auto& api = sakura::net::APIClient::GetInstance();
        if (!api.IsAuthenticated())
        {
            ToastManager::Instance().Show("请先在右上角账号抽屉登录在线账号", ToastType::Info, 3.0f);
            return;
        }
        m_busy = true;
        const bool removeNow = m_profile.isRival;
        const std::string tid = m_profile.userId;
        auto alive = m_alive;
        auto doneCb = [this, alive](sakura::net::RivalActionResult res)
        {
            if (!*alive) return;
            m_busy = false;
            if (!res.ok)
            {
                m_statusMsg = res.error;
                RefreshButtonLabels();
                return;
            }
            m_profile.isRival       = res.isRival;
            m_profile.isMutual      = res.isMutual;
            m_profile.followerCount = res.followerCount;
            m_statusMsg = res.isRival ? "已将该选手加入劲敌名录" : "已从劲敌名录移除";
            RefreshButtonLabels();
            if (m_onRivalChanged) m_onRivalChanged();
        };
        if (removeNow) api.RemoveRivalAsync(tid, doneCb);
        else           api.AddRivalAsync(tid, doneCb);
    });

    m_btnTargetRival = makeBtn({ 0.357f, 0.818f, 0.162f, 0.046f }, "◎ 设为目标劲敌", ButtonVariant::Accent, [this]()
    {
        if (m_profile.userId.empty()) return;
        auto& cfg = sakura::core::Config::GetInstance();
        const std::string curTarget = cfg.Get<std::string>(sakura::core::ConfigKeys::kTargetRivalId, "");
        if (curTarget == m_profile.userId)
        {
            cfg.Set(sakura::core::ConfigKeys::kTargetRivalId, std::string(""));
            cfg.Set(sakura::core::ConfigKeys::kTargetRivalName, std::string(""));
            cfg.Save();
            m_statusMsg = "已取消目标劲敌锁定";
        }
        else
        {
            cfg.Set(sakura::core::ConfigKeys::kTargetRivalId, m_profile.userId);
            cfg.Set(sakura::core::ConfigKeys::kTargetRivalName, m_profile.username);
            cfg.Save();
            m_statusMsg = "已锁定目标劲敌：" + m_profile.username;
            ToastManager::Instance().Show("已锁定目标劲敌 " + m_profile.username + " · 选歌页将展示实时分差", ToastType::Success, 3.0f);
        }
        RefreshButtonLabels();
        if (m_onRivalChanged) m_onRivalChanged();
    });

    m_btnChallengeGhost = makeBtn({ 0.529f, 0.818f, 0.162f, 0.046f }, "▶ 挑战幽灵回放", ButtonVariant::Secondary, [this]()
    {
        if (m_selectedScoreRow < 0 || m_selectedScoreRow >= static_cast<int>(m_profile.bestScores.size()))
            return;
        const auto& item = m_profile.bestScores[static_cast<std::size_t>(m_selectedScoreRow)];
        if (!item.hasReplay || item.scoreId.empty())
        {
            m_statusMsg = "该代表成绩未附带线上回放文件";
            return;
        }
        if (m_onChallengeGhost)
        {
            const auto itemCopy = item;
            const std::string rivalName = m_profile.username;
            Close();
            m_onChallengeGhost(itemCopy, rivalName);
        }
    });

    m_btnClose = makeBtn({ 0.701f, 0.818f, 0.104f, 0.046f }, "关 闭", ButtonVariant::Secondary, [this]() { Close(); });
}

void ProfileCardModal::RefreshButtonLabels()
{
    EnsureButtons();
    const auto me = sakura::net::APIClient::GetInstance().GetCurrentUser();
    const bool isSelf = (!me.id.empty() && me.id == m_profile.userId);
    m_btnToggleRival->SetEnabled(!m_loading && !m_busy && !isSelf && !m_profile.userId.empty());
    m_btnToggleRival->SetText(m_profile.isRival ? "✓ 已是劲敌 (移除)" : "✦ 加为劲敌");

    const std::string targetId = sakura::core::Config::GetInstance().Get<std::string>(
        sakura::core::ConfigKeys::kTargetRivalId, "");
    const bool isTarget = (!m_profile.userId.empty() && targetId == m_profile.userId);
    m_btnTargetRival->SetEnabled(!m_loading && !isSelf && !m_profile.userId.empty());
    m_btnTargetRival->SetText(isTarget ? "★ 已锁目标劲敌" : "◎ 设为目标劲敌");

    const bool canGhost = !m_loading &&
        m_selectedScoreRow >= 0 &&
        m_selectedScoreRow < static_cast<int>(m_profile.bestScores.size()) &&
        m_profile.bestScores[static_cast<std::size_t>(m_selectedScoreRow)].hasReplay &&
        static_cast<bool>(m_onChallengeGhost);
    m_btnChallengeGhost->SetEnabled(canGhost);
}

void ProfileCardModal::Open(const std::string& userId, const std::string& fallbackUsername)
{
    if (userId.empty())
        return;
    EnsureButtons();
    m_open             = true;
    m_loading          = true;
    m_busy             = false;
    m_targetUserId     = userId;
    m_profile          = {};
    m_profile.userId   = userId;
    m_profile.username = fallbackUsername;
    m_selectedScoreRow = 0;
    m_scrollOffset     = 0;
    m_statusMsg        = "正在同步选手生涯档案与 B15 代表作...";
    RefreshButtonLabels();

    auto alive = m_alive;
    sakura::net::APIClient::GetInstance().FetchUserProfileAsync(userId, [this, alive, userId](sakura::net::UserProfileResult res)
    {
        if (!*alive || !m_open || m_targetUserId != userId)
            return;
        m_loading = false;
        if (!res.ok)
        {
            m_statusMsg = "无法加载玩家名片：" + res.error;
            RefreshButtonLabels();
            return;
        }
        m_profile   = std::move(res.profile);
        m_statusMsg.clear();
        RefreshButtonLabels();
    });
}

void ProfileCardModal::Close()
{
    m_open = false;
}

void ProfileCardModal::Update(float dt)
{
    if (!m_open) return;
    EnsureButtons();
    m_btnToggleRival->Update(dt);
    m_btnTargetRival->Update(dt);
    m_btnChallengeGhost->Update(dt);
    m_btnClose->Update(dt);
}

void ProfileCardModal::Render(sakura::core::Renderer& renderer)
{
    if (!m_open) return;
    EnsureButtons();

    VisualStyle::DrawScrim(renderer, 0.68f);
    VisualStyle::DrawGlassCard(renderer, kModalRect,
        { 26, 30, 58, 246 }, { 14, 18, 36, 250 },
        { 250, 216, 157, 218 }, { 246, 178, 206, 118 }, 0.018f, 0.66f);

    const float x0 = kModalRect.x + 0.025f;
    const float w  = kModalRect.width - 0.050f;

    VisualStyle::DrawBadgePill(renderer, { x0, 0.132f, 0.145f, 0.026f },
        "PLAYER CARD · 选手名片", m_font, 0.0125f, { 52, 38, 76, 224 }, kGold, kGold);
    if (m_profile.isMutual)
    {
        VisualStyle::DrawBadgePill(renderer, { x0 + 0.155f, 0.132f, 0.138f, 0.026f },
            "✦ 宿命劲敌 · MUTUAL", m_font, 0.0122f, { 68, 38, 74, 228 }, kPink, kPink);
    }

    const std::string dispName = m_profile.username.empty() ? m_profile.userId : m_profile.username;
    VisualStyle::DrawTextFit(renderer, m_font, dispName, x0, 0.168f, 0.036f, 0.30f, kWhite);

    float badgeX = x0;
    if (!m_profile.danBadge.empty())
    {
        VisualStyle::DrawBadgePill(renderer, { badgeX, 0.214f, 0.078f, 0.028f },
            "[" + m_profile.danBadge + "]", m_font, 0.0135f, { 58, 40, 82, 226 }, kGold, kGold);
        badgeX += 0.086f;
    }
    if (!m_profile.featuredTitle.empty())
    {
        VisualStyle::DrawBadgePill(renderer, { badgeX, 0.214f, 0.165f, 0.028f },
            m_profile.featuredTitle, m_font, 0.0135f, { 34, 44, 74, 220 }, kCyan, kCyan);
    }

    const std::string ratingStr = "★ " + sakura::game::PPCalculator::FormatRating(m_profile.sakuraRating);
    renderer.DrawText(m_font, ratingStr, x0 + w, 0.166f, 0.034f, kGold, sakura::core::TextAlign::Right);
    renderer.DrawText(m_font,
        FmtDec(m_profile.totalPP, 1) + " PP  ·  游玩 " + std::to_string(m_profile.playCount) +
        " 局  ·  粉丝 " + std::to_string(m_profile.followerCount) + " / 劲敌 " + std::to_string(m_profile.rivalCount),
        x0 + w, 0.214f, 0.0155f, kDim, sakura::core::TextAlign::Right);

    const std::string bioText = m_profile.bio.empty() ? "这位演奏者尚未留下个性签名。" : ("「" + m_profile.bio + "」");
    VisualStyle::DrawTextFit(renderer, m_font, bioText, x0, 0.252f, 0.0165f, w, kPink);
    VisualStyle::DrawGildedDivider(renderer, x0, x0 + w, 0.286f, 0.72f);

    renderer.DrawText(m_font, "BEST 15 REPRESENTATIVE PLAYS · 代表成绩矩阵", x0, 0.298f, 0.0135f, kGold);
    if (m_loading)
    {
        renderer.DrawText(m_font, "正在拉取线上代表作...", 0.50f, 0.52f, 0.022f, kDim, sakura::core::TextAlign::Center);
    }
    else if (m_profile.bestScores.empty())
    {
        renderer.DrawText(m_font, "该选手暂无公开排行榜成绩", 0.50f, 0.52f, 0.022f, kDim, sakura::core::TextAlign::Center);
    }
    else
    {
        const int total = static_cast<int>(m_profile.bestScores.size());
        for (int slot = 0; slot < kVisibleScoreRows && (m_scrollOffset + slot) < total; ++slot)
        {
            const int idx    = m_scrollOffset + slot;
            const auto& item = m_profile.bestScores[static_cast<std::size_t>(idx)];
            const float ry   = 0.326f + static_cast<float>(slot) * 0.074f;
            const bool sel   = (idx == m_selectedScoreRow);
            const sakura::core::NormRect rowRect{ x0, ry, w, 0.066f };

            renderer.DrawRoundedRect(rowRect, 0.009f,
                sel ? sakura::core::Color{ 72, 46, 86, 228 } : sakura::core::Color{ 18, 24, 46, 204 }, true);
            renderer.DrawRoundedRectOutlineGradient(rowRect, 0.009f,
                sel ? kGold : sakura::core::Color{ 156, 166, 208, 65 }, { 110, 120, 160, 32 }, 0.0011f);

            renderer.DrawText(m_font, "#" + std::to_string(idx + 1), x0 + 0.012f, ry + 0.012f, 0.018f, sel ? kGold : kPink);
            const std::string titleLine = (item.chartTitle.empty() ? item.chartId : item.chartTitle) +
                "  [" + sakura::game::FormatDifficultyBadge(item.difficulty, item.difficultyLevel) + "]";
            VisualStyle::DrawTextFit(renderer, m_font, titleLine, x0 + 0.052f, ry + 0.009f, 0.0185f, 0.305f, kWhite);

            std::string subLine = "Rating " + FmtDec(item.chartRating, 2) + "  ·  " + FmtDec(item.pp, 1) + " PP";
            if (item.isAllPerfect) subLine += "  ·  ✦ AP";
            else if (item.isFullCombo) subLine += "  ·  ★ FC";
            if (item.hasReplay) subLine += "  ·  含回放";
            VisualStyle::DrawTextFit(renderer, m_font, subLine, x0 + 0.052f, ry + 0.036f, 0.0138f, 0.305f, item.hasReplay ? kMint : kDim);

            renderer.DrawText(m_font, std::to_string(item.score) + "  (" + FmtDec(item.accuracy, 2) + "%)",
                x0 + w - 0.058f, ry + 0.020f, 0.0175f, kGold, sakura::core::TextAlign::Right);
            VisualStyle::DrawBadgePill(renderer, { x0 + w - 0.048f, ry + 0.015f, 0.038f, 0.034f },
                item.grade, m_font, 0.0145f, { 34, 36, 62, 224 }, kGold, kGold);
        }
    }

    if (!m_statusMsg.empty())
        VisualStyle::DrawTextFit(renderer, m_font, m_statusMsg, x0, 0.782f, 0.015f, w, kPink);

    m_btnToggleRival->Render(renderer);
    m_btnTargetRival->Render(renderer);
    m_btnChallengeGhost->Render(renderer);
    m_btnClose->Render(renderer);
}

bool ProfileCardModal::HandleEvent(const SDL_Event& event)
{
    if (!m_open) return false;
    EnsureButtons();

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
        Close();
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL && !m_profile.bestScores.empty())
    {
        const int maxOff = std::max(0, static_cast<int>(m_profile.bestScores.size()) - kVisibleScoreRows);
        m_scrollOffset = std::clamp(m_scrollOffset - static_cast<int>(event.wheel.y), 0, maxOff);
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        const auto [mx, my] = sakura::core::Input::GetMousePosition();
        if (!InRect(kModalRect, mx, my))
        {
            Close();
            return true;
        }
        const float x0 = kModalRect.x + 0.025f;
        const float w  = kModalRect.width - 0.050f;
        const int total = static_cast<int>(m_profile.bestScores.size());
        for (int slot = 0; slot < kVisibleScoreRows && (m_scrollOffset + slot) < total; ++slot)
        {
            const float ry = 0.326f + static_cast<float>(slot) * 0.074f;
            if (InRect({ x0, ry, w, 0.066f }, mx, my))
            {
                m_selectedScoreRow = m_scrollOffset + slot;
                RefreshButtonLabels();
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                return true;
            }
        }
    }

    if (m_btnToggleRival->HandleEvent(event))    return true;
    if (m_btnTargetRival->HandleEvent(event))    return true;
    if (m_btnChallengeGhost->HandleEvent(event)) return true;
    if (m_btnClose->HandleEvent(event))          return true;
    return true;
}

} // namespace sakura::ui
