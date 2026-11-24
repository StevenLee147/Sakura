// scene_market.cpp — 在线谱面市场场景实现（0.0~1.0 归一化琉璃卡片布局）

#include "scene_market.h"
#include "scene_menu.h"
#include "scene_select.h"
#include "core/config.h"
#include "core/input.h"
#include "core/paths.h"
#include "core/resource_manager.h"
#include "game/chart_loader.h"
#include "ui/toast.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace sakura::scene
{

using namespace sakura::core;
using namespace sakura::ui;
using namespace sakura::net;

namespace
{
constexpr Color kPink{ 246, 178, 206, 255 };
constexpr Color kGold{ 250, 216, 157, 255 };
constexpr Color kCyan{ 138, 218, 248, 255 };
constexpr Color kMint{ 152, 236, 194, 255 };
constexpr Color kDim { 168, 178, 204, 255 };
constexpr Color kWhite{ 248, 242, 248, 255 };

std::string FmtFloat(double val, int prec = 1)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << val;
    return oss.str();
}

std::string FmtBytes(std::size_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL)
        return FmtFloat(static_cast<double>(bytes) / (1024.0 * 1024.0), 2) + " MB";
    if (bytes >= 1024ULL)
        return FmtFloat(static_cast<double>(bytes) / 1024.0, 1) + " KB";
    return std::to_string(bytes) + " B";
}

std::unique_ptr<Button> MakeBtn(NormRect rect, const std::string& label, FontHandle font,
                                std::function<void()> onClick,
                                ButtonVariant variant = ButtonVariant::Secondary,
                                float fontSize = 0.020f)
{
    auto btn = std::make_unique<Button>(rect, label, font, fontSize, 0.008f);
    btn->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(btn.get(), variant);
    btn->SetOnClick(std::move(onClick));
    return btn;
}
} // namespace

SceneMarket::SceneMarket(SceneManager& manager)
    : m_manager(manager)
{
}

void SceneMarket::OnEnter()
{
    *m_alive = true;
    m_font   = ResourceManager::GetInstance().GetDefaultFontHandle();
    m_time   = 0.0f;

    m_particles.Clear();
    m_petalEmitter = m_particles.EmitContinuous(0.5f, -0.02f, 2.8f, sakura::effects::ParticlePresets::SakuraPetal());

    m_searchInput = std::make_unique<TextInput>(NormRect{ 0.04f, 0.160f, 0.242f, 0.048f }, m_font, 0.021f, 64);
    m_searchInput->SetPlaceholder("搜索曲名、谱师、标签  /  Ctrl+F");
    m_searchInput->SetOnSubmit([this](const std::string&) { RefreshMarket(); });

    const char* sortLabels[3] = { "最新", "热门", "高分" };
    const char* sortKeys[3]   = { "newest", "popular", "rating" };
    for (int i = 0; i < 3; ++i)
    {
        const std::string key = sortKeys[i];
        m_sortButtons[i] = MakeBtn({ 0.290f + i * 0.056f, 0.160f, 0.052f, 0.048f }, sortLabels[i], m_font,
            [this, key]() {
                m_sortMode = key;
                UpdateSortButtonStyles();
                RefreshMarket();
            });
    }
    UpdateSortButtonStyles();

    m_minLevelSlider = std::make_unique<Slider>(NormRect{ 0.555f, 0.178f, 0.102f, 0.032f }, 0.0f, 20.0f, 0.0f, 0.5f, m_font, 0.0165f);
    m_maxLevelSlider = std::make_unique<Slider>(NormRect{ 0.722f, 0.178f, 0.102f, 0.032f }, 0.0f, 20.0f, 20.0f, 0.5f, m_font, 0.0165f);
    m_minLevelSlider->SetValueFormatter([](float v) { return FmtFloat(v, 1); });
    m_maxLevelSlider->SetValueFormatter([](float v) { return FmtFloat(v, 1); });

    m_btnApplyFilter = MakeBtn({ 0.878f, 0.174f, 0.062f, 0.038f }, "筛选", m_font,
        [this]() {
            if (m_minLevelSlider->GetValue() > m_maxLevelSlider->GetValue())
                m_maxLevelSlider->SetValue(m_minLevelSlider->GetValue());
            RefreshMarket();
        }, ButtonVariant::Accent, 0.017f);

    m_btnUploadPackage = MakeBtn({ 0.595f, 0.064f, 0.155f, 0.044f }, "上传 .sakura 谱面包", m_font,
        [this]() { OpenUploadFileDialog(); }, ButtonVariant::Accent, 0.019f);
    m_btnRefresh = MakeBtn({ 0.760f, 0.064f, 0.095f, 0.044f }, "刷新 / F5", m_font,
        [this]() { RefreshMarket(); }, ButtonVariant::Secondary, 0.019f);
    m_btnGoLibrary = MakeBtn({ 0.865f, 0.064f, 0.095f, 0.044f }, "本地曲库", m_font,
        [this]() { m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager), TransitionType::SilkShutter, 0.36f); },
        ButtonVariant::Secondary, 0.019f);
    m_btnBack = MakeBtn({ 0.04f, 0.909f, 0.145f, 0.055f }, "返回主页 / Esc", m_font,
        [this]() { m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.36f); },
        ButtonVariant::Secondary, 0.020f);

    for (int i = 0; i < 5; ++i)
    {
        const int stars = i + 1;
        m_starButtons[i] = MakeBtn({ 0.515f + i * 0.086f, 0.652f, 0.078f, 0.038f },
            "★ " + std::to_string(stars), m_font,
            [this, stars]() { SubmitRating(stars); }, ButtonVariant::Secondary, 0.0175f);
    }

    m_downloadBar = std::make_unique<ProgressBar>(NormRect{ 0.515f, 0.728f, 0.425f, 0.032f }, 0.008f);
    m_downloadBar->SetShowPercentage(true, m_font, 0.016f);
    m_downloadBar->SetValueImmediate(0.0f);

    m_btnDownload = MakeBtn({ 0.515f, 0.785f, 0.210f, 0.054f }, "一键下载并安装", m_font,
        [this]() { StartDownloadSelected(); }, ButtonVariant::Primary, 0.020f);
    m_btnPlayInstalled = MakeBtn({ 0.735f, 0.785f, 0.205f, 0.054f }, "前往曲库演奏 →", m_font,
        [this]() { PlaySelectedInLibrary(); }, ButtonVariant::Accent, 0.020f);

    UpdateDetailButtons();
    RefreshMarket();
}

void SceneMarket::OnExit()
{
    *m_alive = false;
    if (m_petalEmitter >= 0)
        m_particles.StopEmitter(m_petalEmitter);
    m_particles.Clear();
    if (m_searchInput)
        m_searchInput->SetFocused(false);
}

void SceneMarket::UpdateSortButtonStyles()
{
    const char* keys[3] = { "newest", "popular", "rating" };
    for (int i = 0; i < 3; ++i)
    {
        if (!m_sortButtons[i]) continue;
        VisualStyle::ApplyButton(m_sortButtons[i].get(),
            m_sortMode == keys[i] ? ButtonVariant::Primary : ButtonVariant::Secondary);
    }
}

void SceneMarket::RefreshInstallStates()
{
    m_installStates.assign(m_items.size(), MarketInstallState::NotInstalled);
    m_installedChartIds.assign(m_items.size(), {});
    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        std::string localId;
        m_installStates[i] = APIClient::GetInstalledMarketState(m_items[i], {}, &localId);
        m_installedChartIds[i] = std::move(localId);
    }
    UpdateDetailButtons();
}

void SceneMarket::SelectItem(int index)
{
    if (m_items.empty())
    {
        m_selected = -1;
        UpdateDetailButtons();
        return;
    }
    m_selected = std::clamp(index, 0, static_cast<int>(m_items.size()) - 1);
    if (m_selected < m_scroll) m_scroll = m_selected;
    if (m_selected >= m_scroll + 6) m_scroll = m_selected - 5;
    if (!m_downloading && m_downloadBar)
    {
        const bool installed = (m_installStates[m_selected] == MarketInstallState::Installed);
        m_downloadBar->SetValueImmediate(installed ? 1.0f : 0.0f);
    }
    UpdateDetailButtons();
}

void SceneMarket::UpdateDetailButtons()
{
    const bool valid = (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()));
    for (int i = 0; i < 5; ++i)
    {
        if (!m_starButtons[i]) continue;
        m_starButtons[i]->SetEnabled(valid && !m_downloading);
        const int myR = valid ? m_items[m_selected].myRating : 0;
        VisualStyle::ApplyButton(m_starButtons[i].get(),
            (valid && myR == i + 1) ? ButtonVariant::Accent : ButtonVariant::Secondary);
    }
    if (!m_btnDownload || !m_btnPlayInstalled) return;

    if (!valid)
    {
        m_btnDownload->SetEnabled(false);
        m_btnPlayInstalled->SetEnabled(false);
        return;
    }

    const auto state = m_installStates[m_selected];
    m_btnDownload->SetEnabled(!m_downloading);
    if (m_downloading)
        m_btnDownload->SetText("正在下载校验...");
    else if (state == MarketInstallState::Installed)
        m_btnDownload->SetText("已安装 · 重新校验");
    else if (state == MarketInstallState::UpdateAvailable)
        m_btnDownload->SetText("发现更新 · 一键升级");
    else
        m_btnDownload->SetText("一键下载并安装");

    m_btnPlayInstalled->SetEnabled(!m_downloading && state != MarketInstallState::NotInstalled);
}

void SceneMarket::RefreshMarket()
{
    m_loading = true;
    m_statusMessage = "正在连接在线谱面市场...";
    MarketQuery q;
    q.q        = m_searchInput ? m_searchInput->GetText() : "";
    q.sort     = m_sortMode;
    q.minLevel = m_minLevelSlider ? m_minLevelSlider->GetValue() : 0.0f;
    q.maxLevel = m_maxLevelSlider ? m_maxLevelSlider->GetValue() : 20.0f;
    q.page     = 1;
    q.pageSize = 30;

    auto alive = m_alive;
    APIClient::GetInstance().FetchMarketChartsAsync(q, [this, alive](MarketListResult res)
    {
        if (!*alive) return;
        m_loading = false;
        if (!res.ok)
        {
            m_items.clear();
            m_installStates.clear();
            m_installedChartIds.clear();
            m_selected = -1;
            m_total    = 0;
            m_statusMessage = "离线或暂不可用：" + res.error;
            UpdateDetailButtons();
            return;
        }
        m_total = res.total;
        m_items = std::move(res.charts);
        RefreshInstallStates();
        if (m_items.empty())
        {
            m_selected = -1;
            m_statusMessage = "市场暂无匹配谱面 · 可在工房点击「发布市场」上传首张作品";
        }
        else
        {
            m_statusMessage.clear();
            SelectItem(std::clamp(m_selected < 0 ? 0 : m_selected, 0, static_cast<int>(m_items.size()) - 1));
        }
    });
}

void SceneMarket::StartDownloadSelected()
{
    if (m_downloading || m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        return;

    const auto item = m_items[m_selected];
    m_downloading = true;
    if (m_downloadBar) m_downloadBar->SetValueImmediate(0.02f);
    UpdateDetailButtons();

    auto alive = m_alive;
    APIClient::GetInstance().DownloadAndInstallChartAsync(
        item, {},
        [this, alive](float progress)
        {
            if (!*alive) return;
            if (m_downloadBar) m_downloadBar->SetValue(progress);
        },
        [this, alive, title = item.title](MarketDownloadResult res)
        {
            if (!*alive) return;
            m_downloading = false;
            if (res.ok)
            {
                if (m_downloadBar) m_downloadBar->SetValueImmediate(1.0f);
                if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
                    m_items[m_selected].downloads += 1;
                RefreshInstallStates();
                ToastManager::Instance().Show("已校验 SHA-256 并安装至曲库：" + title, ToastType::Success, 3.8f);
            }
            else
            {
                if (m_downloadBar) m_downloadBar->SetValueImmediate(0.0f);
                UpdateDetailButtons();
                ToastManager::Instance().Show("下载安装失败：" + res.error, ToastType::Error, 4.2f);
            }
        });
}

void SceneMarket::SubmitRating(int stars)
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        return;
    auto& api = APIClient::GetInstance();
    if (!api.IsAuthenticated())
    {
        ToastManager::Instance().Show("请先在主菜单右上角登录账号或启用访客模式后再评分", ToastType::Info, 3.5f);
        return;
    }
    const std::string id = m_items[m_selected].id;
    auto alive = m_alive;
    api.RateMarketChartAsync(id, stars, {}, [this, alive, id](MarketRateResult res)
    {
        if (!*alive) return;
        if (!res.ok)
        {
            ToastManager::Instance().Show("评分提交失败：" + res.error, ToastType::Warning, 3.2f);
            return;
        }
        for (auto& item : m_items)
        {
            if (item.id == id)
            {
                item.ratingAvg   = res.ratingAvg;
                item.ratingCount = res.ratingCount;
                item.myRating    = res.myRating;
                break;
            }
        }
        UpdateDetailButtons();
        ToastManager::Instance().Show("已提交 " + std::to_string(res.myRating) + " 星评价", ToastType::Success, 2.5f);
    });
}

void SceneMarket::OpenUploadFileDialog()
{
    if (m_uploading) return;
    if (!APIClient::GetInstance().IsAuthenticated())
    {
        ToastManager::Instance().Show("请先在主菜单右上角登录在线账号后再上传谱面", ToastType::Info, 3.6f);
        return;
    }
    static const SDL_DialogFileFilter kFilters[] = {
        { "Sakura Chart Package (.sakura)", "sakura;zip" }
    };
    auto* ctx = new std::shared_ptr<UploadDialogState>(m_dialog);
    SDL_ShowOpenFileDialog([](void* userdata, const char* const* filelist, int)
    {
        std::unique_ptr<std::shared_ptr<UploadDialogState>> holder(
            static_cast<std::shared_ptr<UploadDialogState>*>(userdata));
        std::lock_guard lock((*holder)->mutex);
        (*holder)->path  = (filelist && filelist[0]) ? filelist[0] : "";
        (*holder)->ready = true;
    }, ctx, SDL_GetKeyboardFocus(), kFilters, 1, nullptr, false);
}

void SceneMarket::UploadPackageFile(const std::string& filePath)
{
    if (filePath.empty() || m_uploading) return;
    m_uploading = true;
    ToastManager::Instance().Show("正在校验并上传谱面包至市场...", ToastType::Info, 2.5f);
    auto alive = m_alive;
    APIClient::GetInstance().PublishChartPackageAsync(filePath, {}, [this, alive](MarketPublishResult res)
    {
        if (!*alive) return;
        m_uploading = false;
        if (res.ok)
        {
            ToastManager::Instance().Show("发布成功：" + res.chart.title, ToastType::Success, 4.0f);
            RefreshMarket();
        }
        else
        {
            ToastManager::Instance().Show("发布未通过校验：" + res.error, ToastType::Error, 4.5f);
        }
    });
}

void SceneMarket::PlaySelectedInLibrary()
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        return;
    const std::string targetId = !m_installedChartIds[m_selected].empty()
        ? m_installedChartIds[m_selected]
        : m_items[m_selected].chartSlug;
    Config::GetInstance().Set("library.last_chart", targetId);
    Config::GetInstance().Set("library.last_difficulty", 0);
    Config::GetInstance().Save();
    m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager), TransitionType::SakuraSweep, 0.40f);
}

void SceneMarket::OnUpdate(float dt)
{
    m_time += dt;
    m_particles.Update(dt);

    std::string chosenUpload;
    {
        std::lock_guard lock(m_dialog->mutex);
        if (m_dialog->ready)
        {
            chosenUpload    = std::move(m_dialog->path);
            m_dialog->ready = false;
        }
    }
    if (!chosenUpload.empty())
        UploadPackageFile(chosenUpload);

    if (m_searchInput)      m_searchInput->Update(dt);
    for (auto& b : m_sortButtons) if (b) b->Update(dt);
    if (m_minLevelSlider)   m_minLevelSlider->Update(dt);
    if (m_maxLevelSlider)   m_maxLevelSlider->Update(dt);
    if (m_btnApplyFilter)   m_btnApplyFilter->Update(dt);
    if (m_btnUploadPackage) m_btnUploadPackage->Update(dt);
    if (m_btnRefresh)       m_btnRefresh->Update(dt);
    if (m_btnGoLibrary)     m_btnGoLibrary->Update(dt);
    if (m_btnBack)          m_btnBack->Update(dt);
    for (auto& b : m_starButtons) if (b) b->Update(dt);
    if (m_downloadBar)      m_downloadBar->Update(dt);
    if (m_btnDownload)      m_btnDownload->Update(dt);
    if (m_btnPlayInstalled) m_btnPlayInstalled->Update(dt);
}

void SceneMarket::OnRender(Renderer& r)
{
    VisualStyle::DrawSceneBackground(r);
    VisualStyle::DrawSakuraLandscape(r, m_time, 0.62f);
    r.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        { 8, 12, 26, 130 }, { 12, 16, 34, 100 }, { 8, 12, 26, 178 }, { 10, 14, 30, 160 });

    VisualStyle::DrawBadgePill(r, { 0.04f, 0.032f, 0.155f, 0.026f }, "ATELIER MARKET",
        m_font, 0.013f, { 48, 36, 70, 205 }, { 250, 216, 157, 155 }, kGold);
    r.DrawText(m_font, "MARKET / 在线谱面市场", 0.04f, 0.062f, 0.038f, kWhite);
    r.DrawText(m_font, "探索社区创作者谱面 · SHA-256 自动校验安装与星级评价", 0.04f, 0.114f, 0.0185f, kDim);

    if (m_searchInput)      m_searchInput->Render(r);
    for (auto& b : m_sortButtons) if (b) b->Render(r);
    if (m_btnUploadPackage) m_btnUploadPackage->Render(r);
    if (m_btnRefresh)       m_btnRefresh->Render(r);
    if (m_btnGoLibrary)     m_btnGoLibrary->Render(r);

    // 左侧谱面市场卡片列表
    const auto [mx, my] = Input::GetMousePosition();
    for (int row = 0; row < 6 && row + m_scroll < static_cast<int>(m_items.size()); ++row)
    {
        const int idx   = row + m_scroll;
        const auto& c   = m_items[idx];
        const float y   = 0.228f + row * 0.105f;
        const bool sel  = (idx == m_selected);
        const bool hov  = (mx >= 0.04f && mx <= 0.46f && my >= y && my < y + 0.092f);
        const float ind = sel ? 0.008f : (hov ? 0.004f : 0.0f);
        const NormRect card{ 0.04f + ind, y, 0.42f - ind, 0.092f };

        if (sel)
        {
            VisualStyle::DrawGlassCard(r, card,
                { 82, 52, 88, 238 }, { 44, 34, 68, 242 },
                { 250, 216, 157, 225 }, { 246, 178, 206, 135 }, 0.012f, 0.55f);
            r.DrawRoundedGradientRect({ card.x + 0.005f, y + 0.015f, 0.0035f, 0.062f },
                0.0017f, kGold, kPink, kGold, kPink, 6, true);
        }
        else
        {
            VisualStyle::DrawGlassCard(r, card,
                hov ? Color{ 36, 44, 74, 224 } : Color{ 22, 28, 52, 208 },
                hov ? Color{ 24, 30, 54, 232 } : Color{ 14, 18, 36, 220 },
                hov ? Color{ 246, 178, 206, 135 } : Color{ 166, 176, 214, 68 },
                { 112, 122, 168, 36 }, 0.011f, 0.34f);
        }

        VisualStyle::DrawTextFit(r, m_font, c.title, card.x + 0.020f, y + 0.011f, 0.026f, 0.255f, kWhite);
        VisualStyle::DrawTextFit(r, m_font,
            c.artist + "  ·  谱师 " + c.charter + "  ·  " + FmtFloat(c.bpm, 0) + " BPM",
            card.x + 0.020f, y + 0.052f, 0.016f, 0.255f, kDim);

        const std::string lvStr = (std::abs(c.maxLevel - c.minLevel) < 0.05f)
            ? ("Lv." + FmtFloat(c.maxLevel, 1))
            : ("Lv." + FmtFloat(c.minLevel, 0) + "~" + FmtFloat(c.maxLevel, 0));
        VisualStyle::DrawBadgePill(r, { card.x + card.width - 0.105f, y + 0.012f, 0.092f, 0.026f },
            lvStr, m_font, 0.0135f,
            sel ? Color{ 104, 64, 98, 220 } : Color{ 36, 44, 74, 195 },
            sel ? kGold : Color{ 174, 184, 218, 95 }, sel ? kGold : kPink);

        std::string statLine = (c.ratingCount > 0 ? ("★ " + FmtFloat(c.ratingAvg, 1)) : "★ 新上架")
            + "  ·  ↓ " + std::to_string(c.downloads);
        const auto st = m_installStates[idx];
        if (st == MarketInstallState::Installed) statLine = "● 已安装  ·  " + statLine;
        else if (st == MarketInstallState::UpdateAvailable) statLine = "▲ 可更新  ·  " + statLine;
        r.DrawText(m_font, statLine, card.x + card.width - 0.014f, y + 0.055f, 0.0145f,
            st == MarketInstallState::Installed ? kMint : (st == MarketInstallState::UpdateAvailable ? kGold : kDim),
            TextAlign::Right);
    }

    if (m_items.empty())
    {
        VisualStyle::DrawGlassCard(r, { 0.04f, 0.36f, 0.42f, 0.20f },
            { 24, 30, 56, 215 }, { 14, 18, 36, 228 }, { 250, 216, 157, 130 }, { 140, 150, 192, 48 }, 0.014f, 0.42f);
        r.DrawText(m_font, m_loading ? "正在同步在线曲库..." : "暂无符合筛选条件的市场谱面",
            0.25f, 0.415f, 0.024f, kWhite, TextAlign::Center);
        VisualStyle::DrawTextFit(r, m_font, m_statusMessage, 0.065f, 0.478f, 0.0165f, 0.37f, kDim);
    }

    r.DrawText(m_font,
        "共 " + std::to_string(m_total) + " 张在线谱面   ·   ↑ ↓ 浏览   /   Enter 一键安装   /   拖拽 .sakura 直接上传",
        0.04f, 0.868f, 0.016f, kDim);

    // 右侧难度过滤与谱面详情卡片
    VisualStyle::DrawGlassCard(r, { 0.49f, 0.160f, 0.47f, 0.715f },
        { 24, 30, 56, 220 }, { 14, 18, 36, 236 }, { 250, 216, 157, 175 }, { 168, 158, 214, 70 }, 0.018f, 0.56f);

    r.DrawText(m_font, "下限", 0.512f, 0.182f, 0.0155f, kDim);
    r.DrawText(m_font, "上限", 0.688f, 0.182f, 0.0155f, kDim);
    if (m_minLevelSlider) m_minLevelSlider->Render(r);
    if (m_maxLevelSlider) m_maxLevelSlider->Render(r);
    if (m_btnApplyFilter) m_btnApplyFilter->Render(r);
    VisualStyle::DrawGildedDivider(r, 0.512f, 0.940f, 0.224f, 0.65f);

    if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
    {
        const auto& c = m_items[m_selected];
        const auto st = m_installStates[m_selected];
        const std::string stBadge = st == MarketInstallState::Installed ? "INSTALLED / 已安装"
            : (st == MarketInstallState::UpdateAvailable ? "UPDATE / 可更新" : "CLOUD PACKAGE");
        VisualStyle::DrawBadgePill(r, { 0.515f, 0.242f, 0.155f, 0.026f }, stBadge, m_font, 0.013f,
            { 44, 34, 68, 210 }, st == MarketInstallState::Installed ? kMint : kGold,
            st == MarketInstallState::Installed ? kMint : kGold);

        r.DrawText(m_font, FmtBytes(c.packageSize) + "  ·  SHA256 " + c.packageSha256.substr(0, 10),
            0.940f, 0.245f, 0.0145f, kDim, TextAlign::Right);

        VisualStyle::DrawTextFit(r, m_font, c.title, 0.515f, 0.282f, 0.035f, 0.425f, kWhite);
        VisualStyle::DrawTextFit(r, m_font, c.artist + "   ·   谱师 " + c.charter + " (@" + c.uploaderName + ")",
            0.515f, 0.336f, 0.0195f, 0.425f, kDim);

        const std::string ratingText = (c.ratingCount > 0)
            ? ("★ " + FmtFloat(c.ratingAvg, 2) + " (" + std::to_string(c.ratingCount) + " 人评价)")
            : "★ 暂无评分";
        r.DrawText(m_font,
            FmtFloat(c.bpm, 0) + " BPM   ·   " + ratingText + "   ·   累计下载 " + std::to_string(c.downloads),
            0.515f, 0.376f, 0.018f, kGold);

        // 难度胶囊预览
        r.DrawText(m_font, "INCLUDED DIFFICULTIES / 包含难度", 0.515f, 0.420f, 0.0145f, kDim);
        const int showDiffs = std::min(4, static_cast<int>(c.difficulties.size()));
        for (int i = 0; i < showDiffs; ++i)
        {
            const auto& d = c.difficulties[i];
            const float pillW = 0.102f;
            VisualStyle::DrawBadgePill(r, { 0.515f + i * 0.108f, 0.450f, pillW, 0.034f },
                d.name + " " + FmtFloat(d.level, 1), m_font, 0.0145f,
                { 36, 42, 72, 210 }, { 246, 178, 206, 145 }, kPink);
        }

        // 简介与标签
        r.DrawRoundedRect({ 0.515f, 0.502f, 0.425f, 0.110f }, 0.010f, { 16, 20, 38, 175 }, true, 8);
        std::string desc = c.description.empty() ? "创作者未填写额外谱面说明。" : c.description;
        VisualStyle::DrawTextFit(r, m_font, desc, 0.528f, 0.518f, 0.0175f, 0.398f, kWhite);
        if (!c.tags.empty())
        {
            std::string tagLine = "标签：";
            for (std::size_t i = 0; i < c.tags.size(); ++i)
                tagLine += (i > 0 ? "  #" : "#") + c.tags[i];
            VisualStyle::DrawTextFit(r, m_font, tagLine, 0.528f, 0.568f, 0.0155f, 0.398f, kCyan);
        }

        r.DrawText(m_font, "RATE THIS CHART / 为谱面打分", 0.515f, 0.625f, 0.0145f, kDim);
        for (auto& sb : m_starButtons) if (sb) sb->Render(r);

        r.DrawText(m_font, m_downloading ? "DOWNLOADING / 正在流式下载并核验 SHA-256..." : "PACKAGE INTEGRITY / 完整性校验就绪",
            0.515f, 0.704f, 0.014f, m_downloading ? kGold : kDim);
        if (m_downloadBar)      m_downloadBar->Render(r);
        if (m_btnDownload)      m_btnDownload->Render(r);
        if (m_btnPlayInstalled) m_btnPlayInstalled->Render(r);
    }
    else
    {
        r.DrawText(m_font, "从左侧选择一张谱面查看详情与下载", 0.725f, 0.48f, 0.022f, kDim, TextAlign::Center);
    }

    if (m_btnBack) m_btnBack->Render(r);
    m_particles.Render(r);
}

void SceneMarket::OnEvent(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data)
    {
        UploadPackageFile(e.drop.data);
        return;
    }

    if (m_searchInput && m_searchInput->HandleEvent(e))
    {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_TEXT_INPUT)
            return;
    }
    const bool typing = m_searchInput && m_searchInput->IsFocused();

    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !typing)
    {
        if (e.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.36f);
            return;
        }
        if (e.key.scancode == SDL_SCANCODE_F5)
        {
            RefreshMarket();
            return;
        }
        if (e.key.scancode == SDL_SCANCODE_F && (e.key.mod & SDL_KMOD_CTRL))
        {
            if (m_searchInput) m_searchInput->SetFocused(true);
            return;
        }
        if (e.key.scancode == SDL_SCANCODE_UP && !m_items.empty())
        {
            SelectItem(m_selected - 1);
            return;
        }
        if (e.key.scancode == SDL_SCANCODE_DOWN && !m_items.empty())
        {
            SelectItem(m_selected + 1);
            return;
        }
        if (e.key.scancode == SDL_SCANCODE_RETURN && m_selected >= 0)
        {
            if (m_installStates[m_selected] == MarketInstallState::Installed)
                PlaySelectedInLibrary();
            else
                StartDownloadSelected();
            return;
        }
    }

    const auto mouse = Input::GetMousePosition();
    if (e.type == SDL_EVENT_MOUSE_WHEEL && mouse.x < 0.47f)
    {
        m_scroll = std::clamp(m_scroll - static_cast<int>(e.wheel.y), 0,
                              std::max(0, static_cast<int>(m_items.size()) - 6));
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT &&
        mouse.x >= 0.04f && mouse.x <= 0.46f && mouse.y >= 0.228f && mouse.y < 0.858f)
    {
        const int row = static_cast<int>((mouse.y - 0.228f) / 0.105f) + m_scroll;
        if (row >= 0 && row < static_cast<int>(m_items.size()))
        {
            SelectItem(row);
            return;
        }
    }

    for (auto& b : m_sortButtons) if (b && b->HandleEvent(e)) return;
    if (m_minLevelSlider   && m_minLevelSlider->HandleEvent(e))   return;
    if (m_maxLevelSlider   && m_maxLevelSlider->HandleEvent(e))   return;
    if (m_btnApplyFilter   && m_btnApplyFilter->HandleEvent(e))   return;
    if (m_btnUploadPackage && m_btnUploadPackage->HandleEvent(e)) return;
    if (m_btnRefresh       && m_btnRefresh->HandleEvent(e))       return;
    if (m_btnGoLibrary     && m_btnGoLibrary->HandleEvent(e))     return;
    if (m_btnBack          && m_btnBack->HandleEvent(e))          return;
    for (auto& sb : m_starButtons) if (sb && sb->HandleEvent(e))  return;
    if (m_btnDownload      && m_btnDownload->HandleEvent(e))      return;
    if (m_btnPlayInstalled && m_btnPlayInstalled->HandleEvent(e)) return;
}

} // namespace sakura::scene
