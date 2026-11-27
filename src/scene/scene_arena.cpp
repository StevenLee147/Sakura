// scene_arena.cpp — 在线多人对战大厅与赛后领奖台场景实现 (v1.6.0)

#include "scene_arena.h"
#include "scene_game.h"
#include "scene_menu.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/input.h"
#include "core/paths.h"
#include "data/database.h"
#include "game/chart_loader.h"
#include "game/chart_utils.h"
#include "game/play_session.h"
#include "game/replay.h"
#include "ui/toast.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace sakura::scene
{

using namespace sakura::core;
using namespace sakura::ui;
using namespace sakura::game;
using namespace sakura::net;

namespace
{
constexpr Color kPink  { 246, 178, 206, 255 };
constexpr Color kGold  { 250, 216, 157, 255 };
constexpr Color kSilver{ 222, 232, 248, 255 };
constexpr Color kBronze{ 238, 176, 132, 255 };
constexpr Color kCyan  { 138, 218, 248, 255 };
constexpr Color kMint  { 152, 236, 194, 255 };
constexpr Color kCoral { 255, 134, 150, 255 };
constexpr Color kDim   { 168, 178, 204, 255 };
constexpr Color kWhite { 248, 242, 248, 255 };

// 大厅左右双栏归一化布局
constexpr NormRect LOBBY_LEFT_PANEL  { 0.04f, 0.17f, 0.50f, 0.70f };
constexpr NormRect LOBBY_RIGHT_PANEL { 0.56f, 0.17f, 0.40f, 0.70f };

constexpr int   VISIBLE_ROOM_ROWS = 5;
constexpr float ROOM_ROW_X        = 0.056f;
constexpr float ROOM_ROW_Y0       = 0.238f;
constexpr float ROOM_ROW_W        = 0.468f;
constexpr float ROOM_ROW_H        = 0.096f;
constexpr float ROOM_ROW_STRIDE   = 0.106f;

// 房间内与领奖台双栏归一化布局
constexpr NormRect ROOM_LEFT_PANEL   { 0.04f, 0.165f, 0.34f, 0.720f };
constexpr NormRect ROOM_RIGHT_PANEL  { 0.40f, 0.165f, 0.56f, 0.720f };

constexpr int   MAX_ARENA_PLAYERS = 8;
constexpr float PLAYER_CARD_X     = 0.418f;
constexpr float PLAYER_CARD_Y0    = 0.218f;
constexpr float PLAYER_CARD_W     = 0.524f;
constexpr float PLAYER_CARD_H     = 0.054f;
constexpr float PLAYER_CARD_STRIDE = 0.058f;

constexpr int kQuickPhraseCount = 6;
constexpr const char* kQuickPhraseLabels[kQuickPhraseCount] = {
    "请多指教！",
    "准备好了，随时开局！",
    "稍等，正在同步房间谱面~",
    "这首定数好高，手下留情！",
    "精彩演奏！",
    "再来一局！"
};

// 选曲按钮在大厅态与房间态的归一化矩形
constexpr NormRect LOBBY_PREV_SONG_RECT { 0.590f, 0.514f, 0.042f, 0.050f };
constexpr NormRect LOBBY_NEXT_SONG_RECT { 0.888f, 0.514f, 0.042f, 0.050f };
constexpr NormRect LOBBY_PREV_DIFF_RECT { 0.580f, 0.640f, 0.046f, 0.046f };
constexpr NormRect LOBBY_NEXT_DIFF_RECT { 0.894f, 0.640f, 0.046f, 0.046f };

constexpr NormRect ROOM_PREV_SONG_RECT  { 0.066f, 0.614f, 0.038f, 0.048f };
constexpr NormRect ROOM_NEXT_SONG_RECT  { 0.316f, 0.614f, 0.038f, 0.048f };
constexpr NormRect ROOM_PREV_DIFF_RECT  { 0.058f, 0.698f, 0.042f, 0.044f };
constexpr NormRect ROOM_NEXT_DIFF_RECT  { 0.320f, 0.698f, 0.042f, 0.044f };

std::string TrimString(std::string_view sv)
{
    std::size_t first = 0;
    while (first < sv.size() && std::isspace(static_cast<unsigned char>(sv[first])))
        ++first;
    std::size_t last = sv.size();
    while (last > first && std::isspace(static_cast<unsigned char>(sv[last - 1])))
        --last;
    return std::string(sv.substr(first, last - first));
}

std::string NormalizeRoomCode(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw)
    {
        const unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isspace(uc))
            continue;
        if (uc < 128)
            out.push_back(static_cast<char>(std::toupper(uc)));
        else
            out.push_back(ch);
    }
    return out;
}

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

const char* QuickPhraseById(int phraseId)
{
    if (phraseId >= 0 && phraseId < kQuickPhraseCount)
        return kQuickPhraseLabels[phraseId];
    return kQuickPhraseLabels[0];
}

template <typename MsgT>
std::string ExtractArenaChatUser(const MsgT& msg)
{
    if constexpr (requires { msg.username; })
    {
        if (!msg.username.empty())
            return msg.username;
    }
    if constexpr (requires { msg.userId; })
    {
        if (!msg.userId.empty())
            return msg.userId;
    }
    return "Player";
}

template <typename MsgT>
std::string ExtractArenaChatText(const MsgT& msg)
{
    if constexpr (requires { msg.text; })
    {
        if (!msg.text.empty())
            return msg.text;
    }
    if constexpr (requires { msg.phraseId; })
    {
        return QuickPhraseById(static_cast<int>(msg.phraseId));
    }
    return {};
}

std::string FormatScore(int score)
{
    const bool neg = score < 0;
    const long long absVal = neg ? -static_cast<long long>(score) : static_cast<long long>(score);
    const std::string raw = std::to_string(absVal);
    std::string out;
    out.reserve(raw.size() + raw.size() / 3 + 1);
    int count = 0;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it)
    {
        if (count > 0 && count % 3 == 0)
            out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    if (neg)
        out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

std::unique_ptr<Button> MakeBtn(NormRect rect, const std::string& label, FontHandle font,
                                std::function<void()> onClick,
                                ButtonVariant variant = ButtonVariant::Secondary,
                                float fontSize = 0.020f)
{
    auto btn = std::make_unique<Button>(rect, label, font, fontSize, 0.010f);
    btn->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(btn.get(), variant);
    btn->SetOnClick(std::move(onClick));
    return btn;
}

const ChartInfo* FindChartInList(const std::vector<ChartInfo>& charts,
                                 std::string_view chartId,
                                 std::string_view chartTitle = {})
{
    for (const auto& c : charts)
    {
        if (!chartId.empty() && c.id == chartId)
            return &c;
    }
    if (!chartTitle.empty())
    {
        for (const auto& c : charts)
        {
            if (!c.title.empty() && c.title == chartTitle)
                return &c;
        }
    }
    return nullptr;
}

std::string ComputeSelectedChartHash(const ChartInfo& chart, const DifficultyInfo* diff)
{
    if (diff && !chart.folderPath.empty() && !diff->chartFile.empty())
    {
        ChartLoader loader;
        if (auto data = loader.LoadChartData(chart.folderPath + "/" + diff->chartFile))
            return Replay::Hash(*data, chart.offset);
    }
    return chart.id;
}

bool MatchesCurrentUser(const ArenaPlayerState& p, const OnlineUser& me)
{
    if (!me.id.empty())
        return p.userId == me.id;
    if (!me.username.empty())
        return p.username == me.username;
    return false;
}

bool IsCurrentUserRoomHost(const ArenaRoomState& room, const OnlineUser& me)
{
    if (!me.id.empty() && room.hostId == me.id)
        return true;
    for (const auto& p : room.players)
    {
        if (MatchesCurrentUser(p, me) && p.isHost)
            return true;
    }
    return false;
}

const ArenaPlayerState* FindCurrentUserInRoom(const ArenaRoomState& room, const OnlineUser& me)
{
    for (const auto& p : room.players)
    {
        if (MatchesCurrentUser(p, me))
            return &p;
    }
    return nullptr;
}

Color GradeBadgeColor(std::string_view grade)
{
    if (grade == "SS") return kGold;
    if (grade == "S")  return kPink;
    if (grade == "A")  return kCyan;
    if (grade == "B")  return kMint;
    if (grade == "C")  return Color{ 210, 198, 240, 255 };
    return kDim;
}

std::vector<ArenaPlayerState> BuildRankedPlayers(const ArenaRoomState& room)
{
    std::vector<ArenaPlayerState> list = room.players;
    bool hasScores = (room.status == "playing" || room.status == "finished");
    if (!hasScores)
    {
        for (const auto& p : list)
        {
            if (p.score > 0 || p.finished || p.placement > 0)
            {
                hasScores = true;
                break;
            }
        }
    }

    if (hasScores)
    {
        std::stable_sort(list.begin(), list.end(), [](const ArenaPlayerState& a, const ArenaPlayerState& b)
        {
            if (a.placement > 0 && b.placement > 0 && a.placement != b.placement)
                return a.placement < b.placement;
            if (a.score != b.score)
                return a.score > b.score;
            if (std::abs(a.accuracy - b.accuracy) > 0.001f)
                return a.accuracy > b.accuracy;
            if (a.maxCombo != b.maxCombo)
                return a.maxCombo > b.maxCombo;
            if (a.finished != b.finished)
                return a.finished > b.finished;
            if (a.isHost != b.isHost)
                return a.isHost > b.isHost;
            return false;
        });
    }
    else
    {
        std::stable_sort(list.begin(), list.end(), [](const ArenaPlayerState& a, const ArenaPlayerState& b)
        {
            if (a.isHost != b.isHost)
                return a.isHost > b.isHost;
            if (a.ready != b.ready)
                return a.ready > b.ready;
            return false;
        });
    }

    if (static_cast<int>(list.size()) > MAX_ARENA_PLAYERS)
        list.resize(static_cast<std::size_t>(MAX_ARENA_PLAYERS));
    return list;
}

int ComputeRoomScrollOffset(int selectedRow, int totalRooms)
{
    if (totalRooms <= VISIBLE_ROOM_ROWS)
        return 0;
    const int centered = selectedRow - (VISIBLE_ROOM_ROWS / 2);
    return std::clamp(centered, 0, totalRooms - VISIBLE_ROOM_ROWS);
}
} // namespace

SceneArena::SceneArena(SceneManager& mgr, std::string initialRoomCode)
    : m_manager(mgr)
    , m_initialRoomCode(NormalizeRoomCode(initialRoomCode))
{
}

void SceneArena::OnEnter()
{
    LOG_INFO("[SceneArena] 进入在线多人对战场景 (initialRoomCode={})", m_initialRoomCode);

    *m_alive         = true;
    m_font           = ResourceManager::GetInstance().GetDefaultFontHandle();
    m_topBar         = std::make_unique<TopBar>("SEASON & ARENA  ·  排位与对战", m_font,
        [this]()
        {
            if (m_currentRoom.has_value()) LeaveCurrentRoom();
            else m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.38f);
        });
    m_profileModal   = std::make_unique<ProfileCardModal>(m_font);
    m_arenaTabMode   = m_initialRoomCode.empty() ? 0 : 1;
    m_seasonSpec     = SeasonEngine::ComputeSeasonSpec();
    m_seasonStanding = SeasonEngine::ComputeLocalStanding(m_seasonSpec, sakura::data::Database::GetInstance().GetAllBestScores());
    m_seasonMyRank.reset();
    m_seasonLbEntries.clear();
    m_seasonLbScroll = 0;
    m_time           = 0.0f;
    m_pollTimer      = 0.0f;
    m_chatCooldown   = 0.0f;
    m_busy           = false;
    m_launchingMatch = false;
    m_chartSyncing   = false;
    m_chartSyncProgress     = 0.0f;
    m_roomChartCheckKey.clear();
    m_roomChartMissing      = false;
    m_roomChartHashMismatch = false;
    m_currentRoom.reset();
    m_rooms.clear();
    m_selectedRoomRow = 0;

    m_particles.Clear();
    m_petalEmitter = m_particles.EmitContinuous(
        0.5f, -0.02f, 2.8f, sakura::effects::ParticlePresets::SakuraPetal());

    m_selectedChartIdx = 0;
    m_selectedDiffIdx  = 0;
    RescanLocalCharts();
    if (!m_charts.empty())
    {
        const std::string lastChart = Config::GetInstance().Get<std::string>("library.last_chart", "");
        if (!lastChart.empty())
        {
            for (int i = 0; i < static_cast<int>(m_charts.size()); ++i)
            {
                if (m_charts[i].id == lastChart)
                {
                    m_selectedChartIdx = i;
                    break;
                }
            }
        }
        const int lastDiff = Config::GetInstance().Get<int>("library.last_difficulty", 0);
        if (!m_charts[m_selectedChartIdx].difficulties.empty())
        {
            m_selectedDiffIdx = std::clamp(
                lastDiff, 0, static_cast<int>(m_charts[m_selectedChartIdx].difficulties.size()) - 1);
        }
    }

    // 输入框（6位房间码 & 房间名称）
    m_codeInput = std::make_unique<TextInput>(NormRect{ 0.580f, 0.248f, 0.218f, 0.048f }, m_font, 0.022f, 6);
    m_codeInput->SetPlaceholder("6 位邀请码 (如 AB12CD)");
    m_codeInput->SetOnSubmit([this](const std::string& text) { JoinRoom(text); });

    m_roomNameInput = std::make_unique<TextInput>(NormRect{ 0.580f, 0.398f, 0.360f, 0.046f }, m_font, 0.020f, 32);
    m_roomNameInput->SetPlaceholder("樱华同场竞技房 · 欢迎加入");
    m_roomNameInput->SetOnSubmit([this](const std::string&) { CreateRoom(); });

    // 大厅态按钮
    m_btnBack = MakeBtn({ 0.04f, 0.904f, 0.145f, 0.054f }, "返回主页 / Esc", m_font,
        [this]()
        {
            m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.38f);
        }, ButtonVariant::Secondary, 0.020f);

    m_btnJoinSelected = MakeBtn({ 0.058f, 0.796f, 0.286f, 0.052f }, "加入选中房间  →", m_font,
        [this]()
        {
            if (m_selectedRoomRow >= 0 && m_selectedRoomRow < static_cast<int>(m_rooms.size()))
                JoinRoom(m_rooms[m_selectedRoomRow].code);
        }, ButtonVariant::Primary, 0.020f);

    m_btnRefreshLobby = MakeBtn({ 0.358f, 0.796f, 0.164f, 0.052f }, "刷新列表 / F5", m_font,
        [this]() { RefreshLobby(); }, ButtonVariant::Secondary, 0.019f);

    m_btnJoinByCode = MakeBtn({ 0.810f, 0.248f, 0.130f, 0.048f }, "邀请码加入", m_font,
        [this]()
        {
            if (m_codeInput)
                JoinRoom(m_codeInput->GetText());
        }, ButtonVariant::Accent, 0.019f);

    m_btnCreateRoom = MakeBtn({ 0.580f, 0.782f, 0.360f, 0.058f }, "✦ 创建对战房间  /  CREATE ROOM", m_font,
        [this]() { CreateRoom(); }, ButtonVariant::Primary, 0.020f);

    m_btnTabSeason = MakeBtn({ 0.360f, 0.052f, 0.156f, 0.042f }, "✦ 樱华排位赛", m_font,
        [this]() { m_arenaTabMode = 0; RefreshSeasonData(); }, ButtonVariant::Primary, 0.0175f);
    m_btnTabRooms = MakeBtn({ 0.526f, 0.052f, 0.156f, 0.042f }, "◈ 多人对战房", m_font,
        [this]() { m_arenaTabMode = 1; RefreshLobby(); }, ButtonVariant::Secondary, 0.0175f);
    m_btnRefreshSeason = MakeBtn({ 0.790f, 0.798f, 0.150f, 0.048f }, "刷新排位 / F5", m_font,
        [this]() { RefreshSeasonData(); }, ButtonVariant::Secondary, 0.0175f);
    for (int i = 0; i < 3; ++i)
    {
        const float sy = 0.392f + static_cast<float>(i) * 0.152f;
        m_btnPlaySeasonStage[static_cast<std::size_t>(i)] = MakeBtn(
            { 0.420f, sy + 0.036f, 0.102f, 0.058f }, "出击 →", m_font,
            [this, i]() { LaunchSeasonStage(i); }, ButtonVariant::Primary, 0.0175f);
    }

    // 选曲与难度切换按钮（大厅建房与房主选歌复用）
    auto cycleSong = [this](int delta)
    {
        if (m_charts.empty()) return;
        const int total = static_cast<int>(m_charts.size());
        std::string prefName = "Normal";
        float prefLevel      = 5.0f;
        const auto& prevChart = m_charts[std::clamp(m_selectedChartIdx, 0, total - 1)];
        if (!prevChart.difficulties.empty())
        {
            const int d = std::clamp(m_selectedDiffIdx, 0, static_cast<int>(prevChart.difficulties.size()) - 1);
            prefName  = prevChart.difficulties[d].name;
            prefLevel = prevChart.difficulties[d].level;
        }
        m_selectedChartIdx = (m_selectedChartIdx + total + delta) % total;
        const auto& nextChart = m_charts[m_selectedChartIdx];
        m_selectedDiffIdx = ResolveMatchingDifficultyIndex(nextChart, prefName, prefLevel);
        if (!nextChart.difficulties.empty())
            m_selectedDiffIdx = std::clamp(m_selectedDiffIdx, 0, static_cast<int>(nextChart.difficulties.size()) - 1);
        else
            m_selectedDiffIdx = 0;
    };

    auto cycleDiff = [this](int delta)
    {
        if (m_charts.empty()) return;
        const auto& chart = m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)];
        const int diffCount = static_cast<int>(chart.difficulties.size());
        if (diffCount <= 1) return;
        m_selectedDiffIdx = (m_selectedDiffIdx + diffCount + delta) % diffCount;
    };

    m_btnPrevSong = MakeBtn(LOBBY_PREV_SONG_RECT, "◀", m_font,
        [cycleSong]() { cycleSong(-1); }, ButtonVariant::Secondary, 0.019f);
    m_btnNextSong = MakeBtn(LOBBY_NEXT_SONG_RECT, "▶", m_font,
        [cycleSong]() { cycleSong(+1); }, ButtonVariant::Secondary, 0.019f);
    m_btnPrevDiff = MakeBtn(LOBBY_PREV_DIFF_RECT, "◀", m_font,
        [cycleDiff]() { cycleDiff(-1); }, ButtonVariant::Secondary, 0.018f);
    m_btnNextDiff = MakeBtn(LOBBY_NEXT_DIFF_RECT, "▶", m_font,
        [cycleDiff]() { cycleDiff(+1); }, ButtonVariant::Secondary, 0.018f);

    // 房间内专属按钮
    m_btnLeaveRoom = MakeBtn({ 0.04f, 0.904f, 0.160f, 0.054f }, "← 离开房间 / Esc", m_font,
        [this]() { LeaveCurrentRoom(); }, ButtonVariant::Danger, 0.0195f);

    m_btnApplyChart = MakeBtn({ 0.058f, 0.758f, 0.304f, 0.050f }, "同步房间课题曲  ↻", m_font,
        [this]() { ApplyHostChartSelection(); }, ButtonVariant::Accent, 0.019f);

    m_btnReadyOrStart = MakeBtn({ 0.72f, 0.904f, 0.24f, 0.054f }, "✦ 准备就绪  /  READY", m_font,
        [this]() { ToggleReadyOrStart(); }, ButtonVariant::Primary, 0.0205f);

    // 6 条预设快捷短语紧凑按钮（3 列 x 2 行）
    for (int i = 0; i < kQuickPhraseCount; ++i)
    {
        const int col = i % 3;
        const int row = i / 3;
        const NormRect phraseRect{
            0.4180f + static_cast<float>(col) * 0.1773f,
            0.8000f + static_cast<float>(row) * 0.0380f,
            0.1693f,
            0.0325f
        };
        m_chatButtons[static_cast<std::size_t>(i)] = MakeBtn(
            phraseRect, kQuickPhraseLabels[i], m_font,
            [this, i]() { SendQuickChat(i); },
            ButtonVariant::Secondary, 0.0130f);
    }

    // 优先检查持久化身份四态：绝不覆盖离线保活或过期会话为临时 SakuraPlayer
    auto& api = APIClient::GetInstance();
    const AuthState enterAuthState = api.GetAuthState();
    if (enterAuthState == AuthState::AuthenticatedOffline)
    {
        auto alive = m_alive;
        api.RefreshProfileAsync([this, alive](AuthResult r)
        {
            if (!*alive) return;
            if (!r.ok && APIClient::GetInstance().GetAuthState() == AuthState::SessionExpired)
            {
                m_statusText = "登录凭据已过期，请返回主菜单重新验证密码";
                ToastManager::Instance().Show(m_statusText, ToastType::Warning, 3.8f);
            }
        });
    }
    else if (enterAuthState == AuthState::SessionExpired)
    {
        const std::string expUser = api.GetCurrentUser().username;
        const std::string expMsg  = expUser.empty()
            ? "登录凭据已过期，请返回主菜单重新验证密码"
            : ("账号「" + expUser + "」凭据已过期，请返回主菜单重新登录");
        m_statusText = expMsg;
        ToastManager::Instance().Show(expMsg, ToastType::Warning, 4.0f);
    }

    // 若携带赛后回归房间码，立即拉取房间/领奖台实时状态；否则刷新大厅列表
    if (!m_initialRoomCode.empty())
    {
        m_busy = true;
        const std::string targetCode = m_initialRoomCode;
        m_statusText = "正在同步房间赛果与席位状态 [" + targetCode + "]...";
        auto alive = m_alive;
        api.FetchArenaRoomStateAsync(targetCode, [this, alive, targetCode](ArenaRoomResult res)
        {
            if (!*alive) return;
            m_busy = false;
            if (res.ok)
            {
                m_currentRoom = std::move(res.room);
                m_pollTimer   = 0.0f;
                for (int i = 0; i < static_cast<int>(m_charts.size()); ++i)
                {
                    if (m_charts[i].id == m_currentRoom->chartId)
                    {
                        m_selectedChartIdx = i;
                        m_selectedDiffIdx  = ResolveMatchingDifficultyIndex(
                            m_charts[i], m_currentRoom->difficulty, m_currentRoom->difficultyLevel);
                        break;
                    }
                }
                m_statusText = (m_currentRoom->status == "finished")
                    ? "全员完赛 · 赛果领奖台已揭晓"
                    : ("已同步房间 [" + m_currentRoom->code + "] 实时状态");
                UpdateRoomChartSyncState(*m_currentRoom);
                TryLaunchArenaMatch(*m_currentRoom);
            }
            else
            {
                m_statusText = "房间 [" + targetCode + "] 已关闭或无法连接：" + res.error;
                m_initialRoomCode.clear();
                RefreshLobby();
            }
        }, 0);
    }
    else
    {
        RefreshSeasonData();
        RefreshLobby();
    }
}

void SceneArena::OnExit()
{
    *m_alive = false;
    m_profileModal.reset();
    m_topBar.reset();
    if (m_petalEmitter >= 0)
    {
        m_particles.StopEmitter(m_petalEmitter);
        m_petalEmitter = -1;
    }
    m_particles.Clear();

    if (m_codeInput)     m_codeInput->SetFocused(false);
    if (m_roomNameInput) m_roomNameInput->SetFocused(false);

    m_codeInput.reset();
    m_roomNameInput.reset();
    m_btnBack.reset();
    m_btnRefreshLobby.reset();
    m_btnCreateRoom.reset();
    m_btnJoinByCode.reset();
    m_btnJoinSelected.reset();
    m_btnLeaveRoom.reset();
    m_btnPrevSong.reset();
    m_btnNextSong.reset();
    m_btnPrevDiff.reset();
    m_btnNextDiff.reset();
    m_btnApplyChart.reset();
    m_btnReadyOrStart.reset();
    for (auto& btn : m_chatButtons)
        btn.reset();
    m_btnTabSeason.reset();
    m_btnTabRooms.reset();
    m_btnRefreshSeason.reset();
    for (auto& btn : m_btnPlaySeasonStage)
        btn.reset();
}

void SceneArena::RefreshSeasonData()
{
    m_seasonSpec     = SeasonEngine::ComputeSeasonSpec();
    const auto allPb = sakura::data::Database::GetInstance().GetAllBestScores();
    m_seasonStanding = SeasonEngine::ComputeLocalStanding(m_seasonSpec, allPb);
    auto& api = APIClient::GetInstance();
    m_seasonStatusText = "正在同步当季全球排位天梯...";
    auto alive = m_alive;

    if (api.IsAuthenticated())
    {
        for (const auto& pb : allPb)
        {
            const int slot = SeasonEngine::MatchSeasonSlot(m_seasonSpec, pb.chartId, pb.difficulty);
            if (slot >= 0 && !pb.assisted)
                api.SubmitSeasonScoreAsync(m_seasonSpec.seasonId, pb, {});
        }
    }

    api.FetchCurrentSeasonAsync(m_seasonSpec.seasonId, [this, alive](SeasonCurrentResult res)
    {
        if (!*alive || !res.ok) return;
        if (res.myStanding.present && res.myStanding.totalSP >= m_seasonStanding.totalSP)
        {
            m_seasonStanding.stageScores     = res.myStanding.stageScores;
            m_seasonStanding.stageAccuracies = res.myStanding.stageAccuracies;
            m_seasonStanding.stageSP         = res.myStanding.stageSP;
            m_seasonStanding.totalSP         = res.myStanding.totalSP;
            m_seasonStanding.tier            = SeasonEngine::EvaluateTier(res.myStanding.totalSP);
            m_seasonMyRank                   = res.myStanding.rank;
            auto& cfg = Config::GetInstance();
            cfg.Set("online.season_id", m_seasonSpec.seasonId);
            cfg.Set("online.season_total_sp", res.myStanding.totalSP);
            cfg.Save();
        }
    });

    api.FetchSeasonLeaderboardAsync(m_seasonSpec.seasonId, 100, [this, alive](SeasonLeaderboardResult lb)
    {
        if (!*alive) return;
        if (!lb.ok)
        {
            m_seasonStatusText = "离线推演模式 · 完赛课题后联网自动同步全球排位";
            return;
        }
        if (lb.myRank.has_value())
            m_seasonMyRank = lb.myRank;
        m_seasonLbEntries  = std::move(lb.entries);
        m_seasonStatusText = m_seasonLbEntries.empty()
            ? "当季天梯虚位以待 · 完成三阶课题抢占极星席位"
            : "点击右侧选手可查看生涯名片与宿命劲敌状态";
    });
}

void SceneArena::LaunchSeasonStage(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 3 || m_launchingMatch)
        return;
    const auto& st = m_seasonSpec.stages[static_cast<std::size_t>(slotIndex)];
    const ChartInfo* target = FindChartInList(m_charts, st.chartId, st.chartTitle);
    if (!target)
    {
        ToastManager::Instance().Show("本地曲库未找到当季课题曲：" + st.chartTitle, ToastType::Warning, 3.2f);
        return;
    }
    int diffIdx = ResolveMatchingDifficultyIndex(*target, st.difficulty, st.difficultyLevel);
    if (!target->difficulties.empty())
        diffIdx = std::clamp(diffIdx, 0, static_cast<int>(target->difficulties.size()) - 1);
    else
        diffIdx = 0;

    m_launchingMatch      = true;
    PlayOptions opts;
    opts.mode             = PlayMode::Standard;
    opts.rate             = 1.0f;
    opts.isSeasonMatch    = true;
    opts.seasonId         = m_seasonSpec.seasonId;
    opts.seasonSlotIndex  = slotIndex;
    m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager, *target, diffIdx, opts), TransitionType::SakuraSweep, 0.42f);
}

void SceneArena::RescanLocalCharts()
{
    ChartLoader loader;
    m_charts = loader.ScanCharts("resources/charts");
    auto userCharts = loader.ScanCharts(Paths::User("charts"));
    for (auto& c : userCharts)
    {
        auto existing = std::find_if(m_charts.begin(), m_charts.end(),
            [&](const ChartInfo& other) { return !c.id.empty() && other.id == c.id; });
        if (existing == m_charts.end())
            m_charts.push_back(std::move(c));
        else
            *existing = std::move(c);
    }
    std::stable_sort(m_charts.begin(), m_charts.end(), [](const ChartInfo& a, const ChartInfo& b)
    {
        const std::string& ta = a.title.empty() ? a.id : a.title;
        const std::string& tb = b.title.empty() ? b.id : b.title;
        if (ta != tb) return ta < tb;
        return a.id < b.id;
    });

    if (!m_charts.empty())
    {
        m_selectedChartIdx = std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1);
        if (!m_charts[m_selectedChartIdx].difficulties.empty())
        {
            m_selectedDiffIdx = std::clamp(
                m_selectedDiffIdx, 0, static_cast<int>(m_charts[m_selectedChartIdx].difficulties.size()) - 1);
        }
        else
        {
            m_selectedDiffIdx = 0;
        }
    }
    else
    {
        m_selectedChartIdx = 0;
        m_selectedDiffIdx  = 0;
    }
}

void SceneArena::UpdateRoomChartSyncState(const sakura::net::ArenaRoomState& room)
{
    const std::string checkKey = room.chartId + "\n" + room.chartTitle + "\n" + room.difficulty + "\n" +
        FmtFloat(room.difficultyLevel, 1) + "\n" + room.chartHash + "\n" + room.marketChartId + "\n" +
        std::to_string(m_charts.size());
    if (checkKey == m_roomChartCheckKey)
        return;

    m_roomChartCheckKey     = checkKey;
    m_roomChartMissing      = false;
    m_roomChartHashMismatch = false;

    const ChartInfo* localChart = FindChartInList(m_charts, room.chartId, room.chartTitle);
    if (!localChart)
    {
        m_roomChartMissing = true;
    }
    else if (!room.marketChartId.empty() && !room.chartHash.empty() && !localChart->difficulties.empty())
    {
        int dIdx = ResolveMatchingDifficultyIndex(*localChart, room.difficulty, room.difficultyLevel);
        dIdx = std::clamp(dIdx, 0, static_cast<int>(localChart->difficulties.size()) - 1);
        const std::string localHash = ComputeSelectedChartHash(*localChart, &localChart->difficulties[dIdx]);
        if (!localHash.empty() && localHash != room.chartHash)
            m_roomChartHashMismatch = true;
    }

    const auto me = APIClient::GetInstance().GetCurrentUser();
    if (!IsCurrentUserRoomHost(room, me) && !m_chartSyncing)
    {
        if (m_roomChartMissing || m_roomChartHashMismatch)
        {
            if (!room.marketChartId.empty())
            {
                m_statusText = m_roomChartHashMismatch
                    ? "本地谱面版本与房主不一致 · 点击右下角一键同步房间谱面"
                    : "本地缺少房主所选谱面，可从谱面市场直接同步";
            }
            else
            {
                m_statusText = "房主使用的是未发布自制谱，请让房主先发布至谱面市场或更换曲目";
            }
        }
    }
}

void SceneArena::StartRoomChartSync()
{
    if (!m_currentRoom.has_value() || m_chartSyncing || m_launchingMatch)
        return;

    const auto& room = *m_currentRoom;
    auto& api        = APIClient::GetInstance();
    if (IsCurrentUserRoomHost(room, api.GetCurrentUser()))
        return;

    if (room.marketChartId.empty())
    {
        m_statusText = "房主使用的是未发布自制谱，请让房主先发布至谱面市场或更换曲目";
        ToastManager::Instance().Show(m_statusText, ToastType::Warning, 3.8f);
        return;
    }

    const std::string code          = room.code;
    const std::string marketChartId = room.marketChartId;
    const std::string songTitle     = room.chartTitle.empty() ? room.chartId : room.chartTitle;

    m_chartSyncing      = true;
    m_chartSyncProgress = 0.0f;
    m_statusText        = "正在同步房间谱面「" + songTitle + "」... 0%";

    auto alive = m_alive;
    api.SendArenaChatAsync(code, 2, [this, alive, code](auto chatRes)
    {
        if (!*alive) return;
        if constexpr (requires { chatRes.ok; chatRes.room; })
        {
            if (chatRes.ok && m_currentRoom.has_value() && m_currentRoom->code == code && !chatRes.room.code.empty())
                m_currentRoom = std::move(chatRes.room);
        }
    });

    api.DownloadMarketChartByIdAsync(
        marketChartId,
        Paths::User("charts"),
        [this, alive, songTitle](float progress)
        {
            if (!*alive) return;
            m_chartSyncProgress = std::clamp(progress, 0.0f, 1.0f);
            const int pct = std::clamp(static_cast<int>(std::lround(m_chartSyncProgress * 100.0f)), 0, 100);
            m_statusText = "正在同步房间谱面「" + songTitle + "」... " + std::to_string(pct) + "%";
        },
        [this, alive, songTitle](auto res)
        {
            if (!*alive) return;
            m_chartSyncing = false;
            if (!res.ok)
            {
                m_chartSyncProgress = 0.0f;
                m_statusText = "同步房间谱面失败：" + res.error;
                ToastManager::Instance().Show("谱面同步失败：" + res.error, ToastType::Error, 3.8f);
                return;
            }

            m_chartSyncProgress = 1.0f;
            m_roomChartCheckKey.clear();
            RescanLocalCharts();
            if (m_currentRoom.has_value())
            {
                UpdateRoomChartSyncState(*m_currentRoom);
                for (int i = 0; i < static_cast<int>(m_charts.size()); ++i)
                {
                    if (m_charts[i].id == m_currentRoom->chartId)
                    {
                        m_selectedChartIdx = i;
                        m_selectedDiffIdx  = ResolveMatchingDifficultyIndex(
                            m_charts[i], m_currentRoom->difficulty, m_currentRoom->difficultyLevel);
                        break;
                    }
                }
                if (m_currentRoom->status == "playing" && !m_launchingMatch)
                {
                    TryLaunchArenaMatch(*m_currentRoom);
                    return;
                }
            }
            m_statusText = "已同步房间谱面「" + songTitle + "」· 点击右下角准备就绪";
            ToastManager::Instance().Show("房间谱面同步完成：" + songTitle, ToastType::Success, 3.0f);
        });
}

void SceneArena::SendQuickChat(int phraseId)
{
    if (!m_currentRoom.has_value() || m_launchingMatch || m_chatCooldown > 0.0f)
        return;
    if (phraseId < 0 || phraseId >= kQuickPhraseCount)
        return;

    const std::string code = m_currentRoom->code;
    if (code.empty())
        return;

    m_chatCooldown = 1.5f;
    auto alive = m_alive;
    APIClient::GetInstance().SendArenaChatAsync(code, phraseId, [this, alive, code](auto res)
    {
        if (!*alive) return;
        if constexpr (requires { res.ok; })
        {
            if (!res.ok)
            {
                if constexpr (requires { res.error; })
                {
                    if (!res.error.empty())
                        ToastManager::Instance().Show("短语发送提示：" + res.error, ToastType::Warning, 2.2f);
                }
                return;
            }
        }
        if constexpr (requires { res.unchanged; })
        {
            if (res.unchanged) return;
        }
        if constexpr (requires { res.room; })
        {
            if (m_currentRoom.has_value() && m_currentRoom->code == code && !res.room.code.empty())
                m_currentRoom = std::move(res.room);
        }
    });
}

void SceneArena::RefreshLobby()
{
    if (m_busy) return;
    m_busy = true;
    m_statusText = "正在刷新公开对战房间列表...";

    std::string prevSelectedCode;
    if (m_selectedRoomRow >= 0 && m_selectedRoomRow < static_cast<int>(m_rooms.size()))
        prevSelectedCode = m_rooms[m_selectedRoomRow].code;

    auto alive = m_alive;
    APIClient::GetInstance().ListArenaRoomsAsync([this, alive, prevSelectedCode](ArenaRoomListResult res)
    {
        if (!*alive) return;
        m_busy = false;
        if (!res.ok)
        {
            m_statusText = "离线或无法获取房间列表：" + res.error;
            return;
        }

        m_rooms = std::move(res.rooms);
        if (m_rooms.empty())
        {
            m_selectedRoomRow = 0;
            m_statusText = "当前暂无公开房间 · 可在右侧一键创建新对战房间";
            return;
        }

        int restoredRow = std::clamp(m_selectedRoomRow, 0, static_cast<int>(m_rooms.size()) - 1);
        if (!prevSelectedCode.empty())
        {
            for (int i = 0; i < static_cast<int>(m_rooms.size()); ++i)
            {
                if (m_rooms[i].code == prevSelectedCode)
                {
                    restoredRow = i;
                    break;
                }
            }
        }
        m_selectedRoomRow = restoredRow;
        if (APIClient::GetInstance().GetAuthState() == AuthState::SessionExpired)
        {
            m_statusText = "大厅共有 " + std::to_string(m_rooms.size()) + " 间 · 登录凭据已过期，请返回主菜单重新登录";
        }
        else
        {
            m_statusText = "大厅已同步 · 当前共有 " + std::to_string(m_rooms.size()) + " 个公开对战房间";
        }
    });
}

void SceneArena::CreateRoom()
{
    if (m_busy) return;
    if (m_charts.empty())
    {
        m_statusText = "本地曲库为空，无法创建对战房间";
        ToastManager::Instance().Show("本地暂无可用谱面，无法创建房间", ToastType::Warning, 3.2f);
        return;
    }

    auto& api = APIClient::GetInstance();
    const AuthState authState = api.GetAuthState();
    if (authState == AuthState::SessionExpired)
    {
        const std::string expUser = api.GetCurrentUser().username;
        const std::string msg     = expUser.empty()
            ? "登录凭据已过期，请返回主菜单重新验证密码"
            : ("账号「" + expUser + "」凭据已过期，请返回主菜单重新验证密码");
        m_statusText = msg;
        ToastManager::Instance().Show(msg, ToastType::Warning, 3.6f);
        return;
    }

    if (authState == AuthState::Unauthenticated)
    {
        m_busy = true;
        m_statusText = "正在以访客身份自动鉴权...";
        auto alive = m_alive;
        api.LoginGuestAsync("SakuraPlayer", [this, alive](AuthResult auth)
        {
            if (!*alive) return;
            m_busy = false;
            if (!auth.ok)
            {
                m_statusText = "访客自动登录失败：" + auth.error;
                ToastManager::Instance().Show("访客鉴权失败：" + auth.error, ToastType::Error, 3.5f);
                return;
            }
            DoCreateRoom();
        });
        return;
    }

    // AuthenticatedOnline 与 AuthenticatedOffline 均直接复用本地持久化账号
    DoCreateRoom();
}

void SceneArena::DoCreateRoom()
{
    if (m_busy || m_charts.empty()) return;

    auto& api          = APIClient::GetInstance();
    const int chartIdx = std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1);
    const auto& chart  = m_charts[chartIdx];
    const DifficultyInfo* diffPtr = nullptr;
    if (!chart.difficulties.empty())
    {
        const int dIdx = std::clamp(m_selectedDiffIdx, 0, static_cast<int>(chart.difficulties.size()) - 1);
        diffPtr = &chart.difficulties[dIdx];
    }

    const std::string diffName   = diffPtr ? diffPtr->name : "Normal";
    const float       diffLevel  = diffPtr ? diffPtr->level : 1.0f;
    const std::string chartTitle = chart.title.empty() ? chart.id : chart.title;
    const std::string chartHash  = ComputeSelectedChartHash(chart, diffPtr);

    std::string roomName = m_roomNameInput ? TrimString(m_roomNameInput->GetText()) : "";
    if (roomName.empty())
    {
        const std::string uname = api.GetCurrentUser().username;
        roomName = uname.empty() ? "樱华同场竞技房" : (uname + " 的樱华对战房");
    }

    m_busy = true;
    m_statusText = "正在创建多人对战房间...";
    auto alive = m_alive;
    api.CreateArenaRoomAsync(roomName, chart.id, chartTitle, diffName, diffLevel, chartHash,
        [this, alive](ArenaRoomResult res)
        {
            if (!*alive) return;
            m_busy = false;
            if (!res.ok)
            {
                if (APIClient::GetInstance().GetAuthState() == AuthState::SessionExpired)
                {
                    m_statusText = "登录凭据已过期，请返回主菜单重新验证密码";
                    ToastManager::Instance().Show(m_statusText, ToastType::Warning, 3.6f);
                    return;
                }
                m_statusText = "创建房间失败：" + res.error;
                ToastManager::Instance().Show("创建房间失败：" + res.error, ToastType::Error, 3.5f);
                return;
            }

            if (m_codeInput)     m_codeInput->SetFocused(false);
            if (m_roomNameInput) m_roomNameInput->SetFocused(false);
            m_currentRoom    = std::move(res.room);
            m_pollTimer      = 0.0f;
            m_launchingMatch = false;
            m_statusText     = "房间已创建 · 邀请码 [" + m_currentRoom->code + "]";
            ToastManager::Instance().Show("房间已建立，邀请码：" + m_currentRoom->code, ToastType::Success, 3.2f);
        });
}

void SceneArena::JoinRoom(const std::string& code)
{
    if (m_busy) return;
    const std::string cleanCode = NormalizeRoomCode(code);
    if (cleanCode.empty())
    {
        m_statusText = "请输入 6 位房间邀请码";
        ToastManager::Instance().Show("请输入 6 位房间邀请码", ToastType::Warning, 2.8f);
        return;
    }

    auto& api = APIClient::GetInstance();
    const AuthState authState = api.GetAuthState();
    if (authState == AuthState::SessionExpired)
    {
        const std::string expUser = api.GetCurrentUser().username;
        const std::string msg     = expUser.empty()
            ? "登录凭据已过期，请返回主菜单重新验证密码"
            : ("账号「" + expUser + "」凭据已过期，请返回主菜单重新验证密码");
        m_statusText = msg;
        ToastManager::Instance().Show(msg, ToastType::Warning, 3.6f);
        return;
    }

    if (authState == AuthState::Unauthenticated)
    {
        m_busy = true;
        m_statusText = "正在以访客身份自动鉴权...";
        auto alive = m_alive;
        api.LoginGuestAsync("SakuraPlayer", [this, alive, cleanCode](AuthResult auth)
        {
            if (!*alive) return;
            m_busy = false;
            if (!auth.ok)
            {
                m_statusText = "访客自动登录失败：" + auth.error;
                ToastManager::Instance().Show("访客鉴权失败：" + auth.error, ToastType::Error, 3.5f);
                return;
            }
            DoJoinRoom(cleanCode);
        });
        return;
    }

    // AuthenticatedOnline 与 AuthenticatedOffline 均直接复用本地持久化账号
    DoJoinRoom(cleanCode);
}

void SceneArena::DoJoinRoom(const std::string& cleanCode)
{
    if (m_busy || cleanCode.empty()) return;

    m_busy = true;
    m_statusText = "正在加入房间 [" + cleanCode + "]...";
    auto alive = m_alive;
    APIClient::GetInstance().JoinArenaRoomAsync(cleanCode, [this, alive, cleanCode](ArenaRoomResult res)
    {
        if (!*alive) return;
        m_busy = false;
        if (!res.ok)
        {
            if (APIClient::GetInstance().GetAuthState() == AuthState::SessionExpired)
            {
                m_statusText = "登录凭据已过期，请返回主菜单重新验证密码";
                ToastManager::Instance().Show(m_statusText, ToastType::Warning, 3.6f);
                return;
            }
            m_statusText = "加入房间 [" + cleanCode + "] 失败：" + res.error;
            ToastManager::Instance().Show("加入房间失败：" + res.error, ToastType::Error, 3.5f);
            return;
        }

        if (m_codeInput)     m_codeInput->SetFocused(false);
        if (m_roomNameInput) m_roomNameInput->SetFocused(false);
        m_currentRoom    = std::move(res.room);
        m_pollTimer      = 0.0f;
        m_launchingMatch = false;
        for (int i = 0; i < static_cast<int>(m_charts.size()); ++i)
        {
            if (m_charts[i].id == m_currentRoom->chartId)
            {
                m_selectedChartIdx = i;
                m_selectedDiffIdx  = ResolveMatchingDifficultyIndex(
                    m_charts[i], m_currentRoom->difficulty, m_currentRoom->difficultyLevel);
                break;
            }
        }
        m_roomChartCheckKey.clear();
        m_statusText = "已进入房间 [" + m_currentRoom->code + "]";
        UpdateRoomChartSyncState(*m_currentRoom);
        ToastManager::Instance().Show("已加入房间 [" + m_currentRoom->code + "]", ToastType::Success, 2.8f);
        TryLaunchArenaMatch(*m_currentRoom);
    });
}

void SceneArena::LeaveCurrentRoom()
{
    if (!m_currentRoom.has_value()) return;

    const std::string code = m_currentRoom->code;
    m_currentRoom.reset();
    m_initialRoomCode.clear();
    m_pollTimer      = 0.0f;
    m_chatCooldown   = 0.0f;
    m_launchingMatch = false;
    m_chartSyncing   = false;
    m_chartSyncProgress     = 0.0f;
    m_roomChartCheckKey.clear();
    m_roomChartMissing      = false;
    m_roomChartHashMismatch = false;
    m_busy           = false;
    m_statusText     = "已离开房间 [" + code + "]";

    auto alive = m_alive;
    APIClient::GetInstance().LeaveArenaRoomAsync(code, [this, alive](bool)
    {
        if (!*alive) return;
        RefreshLobby();
    });
}

void SceneArena::PollRoomState()
{
    if (!m_currentRoom.has_value() || m_launchingMatch)
        return;

    const std::string code = m_currentRoom->code;
    if (code.empty())
        return;

    const auto sinceRevision = m_currentRoom->revision;
    auto alive = m_alive;
    APIClient::GetInstance().FetchArenaRoomStateAsync(code, [this, alive, code](ArenaRoomResult res)
    {
        if (!*alive) return;
        if (!m_currentRoom.has_value() || m_currentRoom->code != code)
            return;

        if (!res.ok)
        {
            m_statusText = "房间同步延迟：" + res.error;
            return;
        }

        if (res.unchanged)
            return;

        const std::string prevStatus = m_currentRoom->status;
        m_currentRoom = std::move(res.room);
        UpdateRoomChartSyncState(*m_currentRoom);
        if (prevStatus != "finished" && m_currentRoom->status == "finished")
            m_statusText = "全员完赛 · 赛果领奖台已揭晓";

        if (m_currentRoom->status == "playing" && !m_launchingMatch)
            TryLaunchArenaMatch(*m_currentRoom);
    }, sinceRevision);
}

void SceneArena::ApplyHostChartSelection()
{
    if (!m_currentRoom.has_value() || m_busy || m_charts.empty())
        return;

    auto& api = APIClient::GetInstance();
    if (!IsCurrentUserRoomHost(*m_currentRoom, api.GetCurrentUser()))
        return;
    if (m_currentRoom->status == "playing")
        return;

    const int chartIdx = std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1);
    const auto& chart  = m_charts[chartIdx];
    const DifficultyInfo* diffPtr = nullptr;
    if (!chart.difficulties.empty())
    {
        const int dIdx = std::clamp(m_selectedDiffIdx, 0, static_cast<int>(chart.difficulties.size()) - 1);
        diffPtr = &chart.difficulties[dIdx];
    }

    const std::string diffName   = diffPtr ? diffPtr->name : "Normal";
    const float       diffLevel  = diffPtr ? diffPtr->level : 1.0f;
    const std::string chartTitle = chart.title.empty() ? chart.id : chart.title;
    const std::string chartHash  = ComputeSelectedChartHash(chart, diffPtr);
    const std::string code       = m_currentRoom->code;

    m_busy = true;
    m_statusText = "正在同步切换房间课题曲...";
    auto alive = m_alive;
    api.SelectArenaChartAsync(code, chart.id, chartTitle, diffName, diffLevel, chartHash,
        [this, alive](ArenaRoomResult res)
        {
            if (!*alive) return;
            m_busy = false;
            if (!res.ok)
            {
                m_statusText = "切换课题曲失败：" + res.error;
                ToastManager::Instance().Show("切换曲目失败：" + res.error, ToastType::Warning, 3.2f);
                return;
            }
            m_currentRoom = std::move(res.room);
            m_statusText  = "已更新房间课题曲：" + m_currentRoom->chartTitle + " [" + m_currentRoom->difficulty + "]";
            ToastManager::Instance().Show("已切换课题曲：" + m_currentRoom->chartTitle, ToastType::Info, 2.5f);
        });
}

void SceneArena::ToggleReadyOrStart()
{
    if (!m_currentRoom.has_value() || m_busy || m_launchingMatch || m_chartSyncing)
        return;

    UpdateRoomChartSyncState(*m_currentRoom);
    const auto& room   = *m_currentRoom;
    auto& api          = APIClient::GetInstance();
    const auto me      = api.GetCurrentUser();
    const bool isHost  = IsCurrentUserRoomHost(room, me);

    if (!isHost && (m_roomChartMissing || m_roomChartHashMismatch))
    {
        if (!room.marketChartId.empty())
        {
            StartRoomChartSync();
        }
        else
        {
            m_statusText = "房主使用的是未发布自制谱，请让房主先发布至谱面市场或更换曲目";
            ToastManager::Instance().Show(m_statusText, ToastType::Warning, 3.8f);
        }
        return;
    }

    if (room.status == "playing")
    {
        TryLaunchArenaMatch(room);
        return;
    }

    const auto* meInfo = FindCurrentUserInRoom(room, me);

    const bool nextReady  = isHost ? true : !(meInfo && meInfo->ready);
    const bool startMatch = isHost;
    const std::string code = room.code;

    m_busy = true;
    m_statusText = startMatch ? "正在发起全员同步开局..." : (nextReady ? "正在提交准备状态..." : "正在取消准备状态...");
    auto alive = m_alive;
    api.SetArenaReadyAsync(code, nextReady, startMatch, [this, alive, startMatch, nextReady](ArenaRoomResult res)
    {
        if (!*alive) return;
        m_busy = false;
        if (!res.ok)
        {
            m_statusText = (startMatch ? "无法开始对局：" : "准备状态更新失败：") + res.error;
            ToastManager::Instance().Show(res.error, ToastType::Warning, 3.2f);
            return;
        }

        m_currentRoom = std::move(res.room);
        if (m_currentRoom->status == "playing" && !m_launchingMatch)
        {
            TryLaunchArenaMatch(*m_currentRoom);
            return;
        }
        m_statusText = startMatch
            ? "已发出开局指令"
            : (nextReady ? "已准备就绪，等待房主开始对战" : "已取消准备");
    });
}

void SceneArena::TryLaunchArenaMatch(const sakura::net::ArenaRoomState& room)
{
    if (room.status != "playing" || m_launchingMatch)
        return;

    const auto me = APIClient::GetInstance().GetCurrentUser();
    if (const auto* meState = FindCurrentUserInRoom(room, me))
    {
        if (meState->finished)
            return;
    }
    else if (!room.players.empty() && !me.id.empty())
    {
        return;
    }

    const ChartInfo* targetChart = FindChartInList(m_charts, room.chartId, room.chartTitle);
    if (!targetChart)
    {
        const std::string titleOrId = room.chartTitle.empty() ? room.chartId : room.chartTitle;
        const std::string missingMsg = "本地缺少房间课题谱面：" + titleOrId + "（请先安装该谱面）";
        if (m_statusText != missingMsg)
        {
            m_statusText = missingMsg;
            ToastManager::Instance().Show(missingMsg, ToastType::Error, 4.0f);
        }
        return;
    }

    int diffIdx = ResolveMatchingDifficultyIndex(*targetChart, room.difficulty, room.difficultyLevel);
    if (!targetChart->difficulties.empty())
        diffIdx = std::clamp(diffIdx, 0, static_cast<int>(targetChart->difficulties.size()) - 1);
    else
        diffIdx = 0;

    m_launchingMatch = true;
    PlayOptions opts;
    opts.mode          = PlayMode::Standard;
    opts.isArenaMatch  = true;
    opts.arenaRoomCode = room.code;

    LOG_INFO("[SceneArena] 同步进入多人对局: room={} chart={} diffIdx={}", room.code, targetChart->id, diffIdx);
    m_manager.SwitchScene(
        std::make_unique<SceneGame>(m_manager, *targetChart, diffIdx, opts),
        TransitionType::SakuraSweep, 0.42f);
}

void SceneArena::OnUpdate(float dt)
{
    m_time += dt;
    m_chatCooldown = std::max(0.0f, m_chatCooldown - dt);
    m_particles.Update(dt);

    if (m_currentRoom.has_value())
    {
        if (!m_launchingMatch)
        {
            m_pollTimer += dt;
            if (m_pollTimer >= 1.0f)
            {
                m_pollTimer = 0.0f;
                PollRoomState();
            }
        }

        UpdateRoomChartSyncState(*m_currentRoom);
        const auto& room   = *m_currentRoom;
        const auto me      = APIClient::GetInstance().GetCurrentUser();
        const bool isHost  = IsCurrentUserRoomHost(room, me);
        const auto* meInfo = FindCurrentUserInRoom(room, me);
        const bool meDone  = meInfo && meInfo->finished;
        const bool meReady = meInfo && meInfo->ready;
        const bool needChartSync = !isHost && (m_roomChartMissing || m_roomChartHashMismatch);

        if (m_btnLeaveRoom)
        {
            m_btnLeaveRoom->SetEnabled(!m_launchingMatch);
            m_btnLeaveRoom->Update(dt);
        }

        if (m_btnReadyOrStart)
        {
            if (!isHost && m_chartSyncing)
            {
                const int pct = std::clamp(static_cast<int>(std::lround(m_chartSyncProgress * 100.0f)), 0, 100);
                m_btnReadyOrStart->SetText("同步中 " + std::to_string(pct) + "%");
                VisualStyle::ApplyButton(m_btnReadyOrStart.get(), ButtonVariant::Accent);
                m_btnReadyOrStart->SetEnabled(false);
            }
            else if (needChartSync)
            {
                if (!room.marketChartId.empty())
                {
                    m_btnReadyOrStart->SetText("⬇ 一键同步房间谱面");
                    VisualStyle::ApplyButton(m_btnReadyOrStart.get(), ButtonVariant::Accent);
                    m_btnReadyOrStart->SetEnabled(!m_busy && !m_launchingMatch);
                }
                else
                {
                    m_btnReadyOrStart->SetText("! 缺少未发布自制谱");
                    VisualStyle::ApplyButton(m_btnReadyOrStart.get(), ButtonVariant::Secondary);
                    m_btnReadyOrStart->SetEnabled(false);
                }
            }
            else if (room.status == "playing")
            {
                m_btnReadyOrStart->SetText(meDone ? "等待全员完赛结算..." : "进入对局  →");
                VisualStyle::ApplyButton(m_btnReadyOrStart.get(), ButtonVariant::Secondary);
                m_btnReadyOrStart->SetEnabled(!m_busy && !m_launchingMatch && !meDone);
            }
            else if (isHost)
            {
                m_btnReadyOrStart->SetText(room.status == "finished"
                    ? "✦ 再战一局  /  START MATCH"
                    : "✦ 开始对战  /  START MATCH");
                VisualStyle::ApplyButton(m_btnReadyOrStart.get(), ButtonVariant::Primary);
                m_btnReadyOrStart->SetEnabled(!m_busy && !m_launchingMatch);
            }
            else
            {
                m_btnReadyOrStart->SetText(meReady
                    ? "✓ 已准备  ·  点击取消"
                    : "✦ 准备就绪  /  READY");
                VisualStyle::ApplyButton(m_btnReadyOrStart.get(),
                    meReady ? ButtonVariant::Accent : ButtonVariant::Primary);
                m_btnReadyOrStart->SetEnabled(!m_busy && !m_launchingMatch);
            }
            m_btnReadyOrStart->Update(dt);
        }

        const bool canSendChat = !m_launchingMatch && m_chatCooldown <= 0.0f;
        for (auto& btn : m_chatButtons)
        {
            if (!btn) continue;
            btn->SetEnabled(canSendChat);
            btn->Update(dt);
        }

        if (isHost)
        {
            const bool canSwitch = !m_busy && !m_charts.empty() && room.status != "playing";
            if (m_btnPrevSong)   { m_btnPrevSong->SetBounds(ROOM_PREV_SONG_RECT); m_btnPrevSong->SetEnabled(canSwitch && m_charts.size() > 1); m_btnPrevSong->Update(dt); }
            if (m_btnNextSong)   { m_btnNextSong->SetBounds(ROOM_NEXT_SONG_RECT); m_btnNextSong->SetEnabled(canSwitch && m_charts.size() > 1); m_btnNextSong->Update(dt); }
            const int diffCount = (!m_charts.empty())
                ? static_cast<int>(m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)].difficulties.size())
                : 0;
            if (m_btnPrevDiff)   { m_btnPrevDiff->SetBounds(ROOM_PREV_DIFF_RECT); m_btnPrevDiff->SetEnabled(canSwitch && diffCount > 1); m_btnPrevDiff->Update(dt); }
            if (m_btnNextDiff)   { m_btnNextDiff->SetBounds(ROOM_NEXT_DIFF_RECT); m_btnNextDiff->SetEnabled(canSwitch && diffCount > 1); m_btnNextDiff->Update(dt); }
            if (m_btnApplyChart) { m_btnApplyChart->SetEnabled(canSwitch); m_btnApplyChart->Update(dt); }
        }
    }
    else
    {
        if (m_btnTabSeason)
        {
            VisualStyle::ApplyButton(m_btnTabSeason.get(), m_arenaTabMode == 0 ? ButtonVariant::Primary : ButtonVariant::Secondary);
            m_btnTabSeason->Update(dt);
        }
        if (m_btnTabRooms)
        {
            VisualStyle::ApplyButton(m_btnTabRooms.get(), m_arenaTabMode == 1 ? ButtonVariant::Primary : ButtonVariant::Secondary);
            m_btnTabRooms->Update(dt);
        }
        if (m_btnBack) m_btnBack->Update(dt);
        if (m_arenaTabMode == 0)
        {
            if (m_btnRefreshSeason) m_btnRefreshSeason->Update(dt);
            for (auto& btn : m_btnPlaySeasonStage)
                if (btn) btn->Update(dt);
            return;
        }

        if (m_codeInput)     m_codeInput->Update(dt);
        if (m_roomNameInput) m_roomNameInput->Update(dt);

        if (m_btnBack)         m_btnBack->Update(dt);
        if (m_btnRefreshLobby) { m_btnRefreshLobby->SetEnabled(!m_busy); m_btnRefreshLobby->Update(dt); }
        if (m_btnJoinSelected)
        {
            const bool hasRoom = !m_rooms.empty() &&
                                 m_selectedRoomRow >= 0 &&
                                 m_selectedRoomRow < static_cast<int>(m_rooms.size());
            m_btnJoinSelected->SetEnabled(!m_busy && hasRoom);
            m_btnJoinSelected->Update(dt);
        }
        if (m_btnJoinByCode) { m_btnJoinByCode->SetEnabled(!m_busy); m_btnJoinByCode->Update(dt); }
        if (m_btnCreateRoom) { m_btnCreateRoom->SetEnabled(!m_busy && !m_charts.empty()); m_btnCreateRoom->Update(dt); }

        const bool canSelectSong = !m_busy && m_charts.size() > 1;
        const int diffCount = (!m_charts.empty())
            ? static_cast<int>(m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)].difficulties.size())
            : 0;
        if (m_btnPrevSong) { m_btnPrevSong->SetBounds(LOBBY_PREV_SONG_RECT); m_btnPrevSong->SetEnabled(canSelectSong); m_btnPrevSong->Update(dt); }
        if (m_btnNextSong) { m_btnNextSong->SetBounds(LOBBY_NEXT_SONG_RECT); m_btnNextSong->SetEnabled(canSelectSong); m_btnNextSong->Update(dt); }
        if (m_btnPrevDiff) { m_btnPrevDiff->SetBounds(LOBBY_PREV_DIFF_RECT); m_btnPrevDiff->SetEnabled(!m_busy && diffCount > 1); m_btnPrevDiff->Update(dt); }
        if (m_btnNextDiff) { m_btnNextDiff->SetBounds(LOBBY_NEXT_DIFF_RECT); m_btnNextDiff->SetEnabled(!m_busy && diffCount > 1); m_btnNextDiff->Update(dt); }
    }
}

void SceneArena::OnRender(sakura::core::Renderer& renderer)
{
    VisualStyle::DrawSceneBackground(renderer);
    VisualStyle::DrawSakuraLandscape(renderer, m_time, 0.64f);
    renderer.DrawGradientRect({ 0.0f, 0.0f, 1.0f, 1.0f },
        { 8, 12, 26, 128 }, { 12, 16, 34, 98 },
        { 8, 12, 26, 178 }, { 10, 14, 30, 160 });
    if (m_font == INVALID_HANDLE)
        return;

    auto& api = APIClient::GetInstance();
    const auto me = api.GetCurrentUser();
    const auto [mx, my] = Input::GetMousePosition();

    // ── 顶部标题栏与在线身份卡片 ──────────────────────────────────────────────
    VisualStyle::DrawBadgePill(renderer,
        { 0.04f, 0.030f, 0.165f, 0.026f },
        "SAKURA ARENA",
        m_font, 0.013f,
        { 48, 36, 70, 212 }, { 250, 216, 157, 162 }, kGold);

    const std::string pageTitle = m_currentRoom.has_value()
        ? (m_currentRoom->status == "finished"
            ? "PODIUM / 多人赛果领奖台"
            : "ARENA ROOM / 多人对战房间")
        : (m_arenaTabMode == 0 ? "SEASON RANKED / 樱华赛季排位赛" : "ARENA / 在线多人对战大厅");
    renderer.DrawText(m_font, pageTitle, 0.04f, 0.060f, 0.033f, kWhite);

    const std::string subLine = m_statusText.empty()
        ? "最高 8 人实时同场竞速 · 6 位邀请码秒进 · 同步开局与赛后领奖台"
        : m_statusText;
    VisualStyle::DrawTextFit(renderer, m_font, subLine,
        0.04f, 0.112f, 0.0175f, 0.64f, m_busy ? kGold : kDim);

    {   // 右上角在线通行证状态铭牌
        const NormRect userCard{ 0.70f, 0.040f, 0.26f, 0.068f };
        VisualStyle::DrawGlassCard(renderer, userCard,
            { 30, 28, 58, 214 }, { 16, 18, 38, 228 },
            { 250, 216, 157, 168 }, { 246, 178, 206, 84 }, 0.013f, 0.42f);
        renderer.DrawText(m_font, "ONLINE PASS / 当前选手身份", 0.716f, 0.050f, 0.012f, kDim);
        const AuthState passState = api.GetAuthState();
        const std::string badgePrefix = me.danBadge.empty() ? "" : ("[" + me.danBadge + "] ");
        if (passState == AuthState::AuthenticatedOnline)
        {
            const std::string userLabel = "● " + badgePrefix + me.username +
                (me.isGuest ? " (访客)" : "");
            VisualStyle::DrawTextFit(renderer, m_font, userLabel,
                0.716f, 0.072f, 0.0175f, 0.228f, kMint);
        }
        else if (passState == AuthState::AuthenticatedOffline)
        {
            const std::string userLabel = "◈ " + badgePrefix + me.username + " · 离线保活";
            VisualStyle::DrawTextFit(renderer, m_font, userLabel,
                0.716f, 0.072f, 0.0165f, 0.228f, kGold);
        }
        else if (passState == AuthState::SessionExpired)
        {
            const std::string expLabel = "! " + (me.username.empty() ? std::string("在线会话") : me.username) + " · 需重登";
            VisualStyle::DrawTextFit(renderer, m_font, expLabel,
                0.716f, 0.072f, 0.0165f, 0.228f, kCoral);
        }
        else
        {
            VisualStyle::DrawTextFit(renderer, m_font, "○ 未登录 · 建房/入房自动启用访客",
                0.716f, 0.072f, 0.016f, 0.228f, kGold);
        }
    }

    // =========================================================================
    // 视图 1：大厅视图 (!m_currentRoom.has_value())
    // =========================================================================
    if (!m_currentRoom.has_value())
    {
        if (m_btnTabSeason) m_btnTabSeason->Render(renderer);
        if (m_btnTabRooms)  m_btnTabRooms->Render(renderer);

        if (m_arenaTabMode == 0)
        {
            VisualStyle::DrawGlassCard(renderer, LOBBY_LEFT_PANEL,
                { 24, 28, 56, 228 }, { 12, 16, 36, 240 },
                { 250, 216, 157, 190 }, { 246, 178, 206, 92 }, 0.018f, 0.56f);

            const auto& tier = m_seasonStanding.tier;
            const Color tierCol{ tier.colorR, tier.colorG, tier.colorB, 255 };
            VisualStyle::DrawBadgePill(renderer, { 0.058f, 0.186f, 0.165f, 0.026f },
                "SEASON " + m_seasonSpec.seasonId, m_font, 0.0128f, { 52, 38, 76, 224 }, kGold, kGold);
            VisualStyle::DrawTextFit(renderer, m_font, m_seasonSpec.themeTitle, 0.232f, 0.184f, 0.0205f, 0.290f, kWhite);

            const NormRect tierBanner{ 0.058f, 0.224f, 0.464f, 0.138f };
            renderer.DrawRoundedGradientRect(tierBanner, 0.013f,
                { 34, 28, 62, 226 }, { 20, 24, 50, 226 }, { 18, 20, 42, 236 }, { 14, 18, 36, 236 });
            renderer.DrawRoundedRectOutlineGradient(tierBanner, 0.013f, tierCol, kPink.WithAlpha(110), 0.0014f);

            renderer.DrawText(m_font, "CURRENT TIER · 当季樱华段位", 0.076f, 0.238f, 0.0128f, kDim);
            renderer.DrawText(m_font, "✦ 「" + tier.tierName + "」", 0.076f, 0.264f, 0.032f, tierCol);
            const std::string spRight = std::to_string(m_seasonStanding.totalSP) + " / 3500 SP" +
                (m_seasonMyRank.has_value() ? ("  ·  #" + std::to_string(*m_seasonMyRank)) : "");
            renderer.DrawText(m_font, spRight, 0.504f, 0.268f, 0.021f, kGold, TextAlign::Right);

            const float denom = static_cast<float>(std::max(1, tier.nextTierThreshold - tier.minSP));
            const float prog  = tier.spToNextTier == 0
                ? 1.0f
                : std::clamp(static_cast<float>(m_seasonStanding.totalSP - tier.minSP) / denom, 0.03f, 1.0f);
            renderer.DrawRoundedRect({ 0.076f, 0.318f, 0.428f, 0.014f }, 0.007f, { 26, 32, 58, 220 }, true);
            renderer.DrawRoundedGradientRect({ 0.076f, 0.318f, 0.428f * prog, 0.014f }, 0.007f, tierCol, kGold, tierCol, kGold);
            const std::string nextHint = tier.spToNextTier == 0
                ? "已登顶最高段位「极星神乐」· 冲击 3500 满分神座"
                : ("距离晋升「" + tier.nextTierName + "」还需 " + std::to_string(tier.spToNextTier) + " SP");
            renderer.DrawText(m_font, nextHint, 0.076f, 0.338f, 0.0128f, kDim);

            for (int i = 0; i < 3; ++i)
            {
                const auto& st = m_seasonSpec.stages[static_cast<std::size_t>(i)];
                const float sy = 0.382f + static_cast<float>(i) * 0.152f;
                const NormRect card{ 0.058f, sy, 0.464f, 0.136f };
                const Color roleCol = (i == 0) ? kMint : ((i == 1) ? kGold : kPink);
                VisualStyle::DrawGlassCard(renderer, card,
                    { 20, 26, 50, 218 }, { 14, 18, 36, 232 }, roleCol.WithAlpha(165), { 120, 130, 180, 48 }, 0.012f, 0.36f);
                VisualStyle::DrawBadgePill(renderer, { 0.072f, sy + 0.014f, 0.106f, 0.025f },
                    st.roleTitle, m_font, 0.0122f, { 38, 32, 66, 224 }, roleCol, roleCol);
                VisualStyle::DrawBadgePill(renderer, { 0.186f, sy + 0.014f, 0.152f, 0.025f },
                    FormatDifficultyBadge(st.difficulty, st.difficultyLevel), m_font, 0.0122f, { 34, 38, 68, 224 }, kGold, kGold);
                VisualStyle::DrawTextFit(renderer, m_font, st.chartTitle, 0.072f, sy + 0.048f, 0.021f, 0.335f, kWhite);
                const int mySc = m_seasonStanding.stageScores[static_cast<std::size_t>(i)];
                const int mySp = m_seasonStanding.stageSP[static_cast<std::size_t>(i)];
                const std::string recLine = mySc > 0
                    ? ("最佳 " + FormatScore(mySc) + " (" + FmtFloat(m_seasonStanding.stageAccuracies[static_cast<std::size_t>(i)], 2) +
                       "%)  ·  " + std::to_string(mySp) + " / " + std::to_string(st.maxStageSP) + " SP")
                    : ("尚未完赛 · 满额上限 " + std::to_string(st.maxStageSP) + " SP");
                VisualStyle::DrawTextFit(renderer, m_font, recLine, 0.072f, sy + 0.090f, 0.0142f, 0.335f, mySp > 0 ? kGold : kDim);
                if (m_btnPlaySeasonStage[static_cast<std::size_t>(i)])
                    m_btnPlaySeasonStage[static_cast<std::size_t>(i)]->Render(renderer);
            }

            VisualStyle::DrawGlassCard(renderer, LOBBY_RIGHT_PANEL,
                { 24, 30, 58, 226 }, { 14, 18, 36, 240 },
                { 250, 216, 157, 182 }, { 158, 240, 255, 84 }, 0.018f, 0.56f);
            VisualStyle::DrawBadgePill(renderer, { 0.580f, 0.186f, 0.155f, 0.026f },
                "SEASON LADDER", m_font, 0.0125f, { 46, 36, 74, 222 }, kCyan, kCyan);
            renderer.DrawText(m_font, "当季全球 SP 排位榜", 0.745f, 0.184f, 0.0195f, kWhite);
            VisualStyle::DrawTextFit(renderer, m_font, m_seasonStatusText, 0.580f, 0.222f, 0.0135f, 0.360f, kDim);
            VisualStyle::DrawGildedDivider(renderer, 0.580f, 0.940f, 0.250f, 0.72f);

            if (m_seasonLbEntries.empty())
            {
                renderer.DrawText(m_font, "暂无排位上榜选手", 0.760f, 0.480f, 0.021f, kWhite, TextAlign::Center);
                renderer.DrawText(m_font, "点击左侧三阶课题「出击 →」率先登榜", 0.760f, 0.520f, 0.0145f, kDim, TextAlign::Center);
            }
            else
            {
                const int totalLb = static_cast<int>(m_seasonLbEntries.size());
                for (int s = 0; s < 8 && (m_seasonLbScroll + s) < totalLb; ++s)
                {
                    const auto& row = m_seasonLbEntries[static_cast<std::size_t>(m_seasonLbScroll + s)];
                    const float ry  = 0.262f + static_cast<float>(s) * 0.064f;
                    const auto rowTier = SeasonEngine::EvaluateTier(row.totalSP);
                    const Color rCol{ rowTier.colorR, rowTier.colorG, rowTier.colorB, 255 };
                    renderer.DrawRoundedRect({ 0.580f, ry, 0.360f, 0.056f }, 0.009f, { 18, 24, 46, 208 }, true);
                    renderer.DrawText(m_font, "#" + std::to_string(row.rank), 0.592f, ry + 0.016f, 0.0175f,
                        row.rank == 1 ? kGold : kPink);
                    const std::string uname = (row.danBadge.empty() ? "" : ("[" + row.danBadge + "] ")) + row.username;
                    VisualStyle::DrawTextFit(renderer, m_font, uname, 0.638f, ry + 0.008f, 0.0165f, 0.180f, kWhite);
                    VisualStyle::DrawTextFit(renderer, m_font, "「" + row.rankTier + "」", 0.638f, ry + 0.031f, 0.0128f, 0.160f, rCol);
                    renderer.DrawText(m_font, std::to_string(row.totalSP) + " SP", 0.928f, ry + 0.016f, 0.0175f, kGold, TextAlign::Right);
                }
            }

            if (m_btnRefreshSeason) m_btnRefreshSeason->Render(renderer);
            if (m_btnBack)          m_btnBack->Render(renderer);
            m_particles.Render(renderer);
            if (m_profileModal) m_profileModal->Render(renderer);
            if (m_topBar) { m_topBar->Render(renderer); m_topBar->RenderDrawer(renderer); }
            return;
        }
        // ── 左栏 ({0.04, 0.17, 0.50, 0.70})：公开房间列表 ────────────────────
        VisualStyle::DrawGlassCard(renderer, LOBBY_LEFT_PANEL,
            { 22, 28, 54, 222 }, { 12, 16, 34, 236 },
            { 250, 216, 157, 176 }, { 164, 156, 212, 72 },
            0.018f, 0.54f);

        VisualStyle::DrawBadgePill(renderer,
            { 0.058f, 0.188f, 0.130f, 0.025f },
            "OPEN ROOMS", m_font, 0.0125f,
            { 50, 38, 76, 218 }, { 250, 216, 157, 152 }, kGold);
        renderer.DrawText(m_font, "公开对战房间", 0.198f, 0.186f, 0.021f, kWhite);
        renderer.DrawText(m_font,
            "共 " + std::to_string(m_rooms.size()) + " 间",
            0.522f, 0.190f, 0.015f, kGold, TextAlign::Right);
        VisualStyle::DrawGildedDivider(renderer, 0.058f, 0.522f, 0.224f, 0.72f);

        const int totalRooms = static_cast<int>(m_rooms.size());
        if (totalRooms == 0)
        {
            renderer.DrawText(m_font,
                m_busy ? "正在搜寻在线对战房间..." : "当前暂无公开对战房间",
                0.29f, 0.455f, 0.024f, kWhite, TextAlign::Center);
            renderer.DrawText(m_font,
                "可在右侧选择曲目创建新房间，或输入好友的 6 位邀请码秒进",
                0.29f, 0.500f, 0.0155f, kDim, TextAlign::Center);
        }
        else
        {
            const int scrollOffset = ComputeRoomScrollOffset(m_selectedRoomRow, totalRooms);
            for (int slot = 0; slot < VISIBLE_ROOM_ROWS && (scrollOffset + slot) < totalRooms; ++slot)
            {
                const int idx      = scrollOffset + slot;
                const auto& r      = m_rooms[idx];
                const float rowY   = ROOM_ROW_Y0 + static_cast<float>(slot) * ROOM_ROW_STRIDE;
                const NormRect row { ROOM_ROW_X, rowY, ROOM_ROW_W, ROOM_ROW_H };
                const bool sel     = (idx == m_selectedRoomRow);
                const bool hov     = (mx >= row.x && mx <= row.x + row.width &&
                                      my >= row.y && my < row.y + row.height);

                if (sel)
                {
                    VisualStyle::DrawGlassCard(renderer, row,
                        { 84, 50, 90, 238 }, { 44, 32, 68, 242 },
                        { 250, 216, 157, 230 }, { 246, 178, 206, 140 }, 0.012f, 0.48f);
                    renderer.DrawRoundedGradientRect(
                        { row.x + 0.0045f, rowY + 0.014f, 0.0034f, ROOM_ROW_H - 0.028f },
                        0.0017f, kGold, kPink, kGold, kPink);
                }
                else
                {
                    VisualStyle::DrawGlassCard(renderer, row,
                        hov ? Color{ 38, 46, 78, 224 } : Color{ 18, 24, 46, 204 },
                        hov ? Color{ 24, 30, 54, 232 } : Color{ 12, 16, 32, 218 },
                        hov ? Color{ 246, 178, 206, 145 } : Color{ 166, 176, 214, 68 },
                        { 112, 122, 168, 36 }, 0.011f, 0.32f);
                }

                // [CODE] 胶囊
                VisualStyle::DrawBadgePill(renderer,
                    { row.x + 0.014f, rowY + 0.012f, 0.084f, 0.028f },
                    "[" + r.code + "]", m_font, 0.0135f,
                    { 52, 38, 74, 224 }, kGold, kGold);

                // 房间名
                const std::string dispName = r.name.empty() ? ("Arena " + r.code) : r.name;
                VisualStyle::DrawTextFit(renderer, m_font, dispName,
                    row.x + 0.106f, rowY + 0.010f, 0.0205f, 0.198f, kWhite);

                // 状态胶囊 (WAITING / PLAYING / PODIUM)
                std::string stLabel = "WAITING";
                Color stColor = kMint;
                if (r.status == "playing")
                {
                    stLabel = "PLAYING";
                    stColor = kPink;
                }
                else if (r.status == "finished")
                {
                    stLabel = "PODIUM";
                    stColor = kGold;
                }
                VisualStyle::DrawBadgePill(renderer,
                    { row.x + 0.312f, rowY + 0.012f, 0.078f, 0.026f },
                    stLabel, m_font, 0.012f,
                    { 28, 34, 58, 215 }, stColor, stColor);

                // N/8 人数
                const int pCount = static_cast<int>(r.players.size());
                const std::string capStr = std::to_string(pCount) + "/" + std::to_string(MAX_ARENA_PLAYERS);
                renderer.DrawText(m_font, capStr,
                    row.x + row.width - 0.014f, rowY + 0.014f, 0.0165f,
                    pCount >= MAX_ARENA_PLAYERS ? kCoral : kCyan, TextAlign::Right);

                // 房主 + 曲名 + 难度
                const std::string hostLabel = r.hostName.empty() ? "Unknown" : r.hostName;
                const std::string songLabel = r.chartTitle.empty() ? r.chartId : r.chartTitle;
                const std::string diffBadge = FormatDifficultyBadge(
                    r.difficulty.empty() ? "Normal" : r.difficulty, r.difficultyLevel);
                const std::string metaLine = "房主 " + hostLabel + "  ·  " + songLabel + "  [" + diffBadge + "]";
                VisualStyle::DrawTextFit(renderer, m_font, metaLine,
                    row.x + 0.016f, rowY + 0.054f, 0.0148f, row.width - 0.032f,
                    sel ? Color{ 230, 220, 242, 242 } : kDim);
            }
        }

        if (m_btnJoinSelected) m_btnJoinSelected->Render(renderer);
        if (m_btnRefreshLobby) m_btnRefreshLobby->Render(renderer);

        // ── 右栏 ({0.56, 0.17, 0.40, 0.70})：邀请码加入 & 选曲创建房间 ────────
        VisualStyle::DrawGlassCard(renderer, LOBBY_RIGHT_PANEL,
            { 24, 30, 58, 226 }, { 14, 18, 36, 240 },
            { 250, 216, 157, 182 }, { 246, 178, 206, 82 },
            0.018f, 0.56f);

        VisualStyle::DrawBadgePill(renderer,
            { 0.580f, 0.188f, 0.135f, 0.025f },
            "JOIN BY CODE", m_font, 0.0125f,
            { 50, 38, 76, 218 }, { 250, 216, 157, 152 }, kGold);
        renderer.DrawText(m_font, "输入 6 位邀请码直达房间", 0.724f, 0.186f, 0.0195f, kWhite);
        renderer.DrawText(m_font, "房间邀请码（6 位字母或数字）", 0.580f, 0.224f, 0.014f, kDim);

        if (m_codeInput)     m_codeInput->Render(renderer);
        if (m_btnJoinByCode) m_btnJoinByCode->Render(renderer);

        VisualStyle::DrawGildedDivider(renderer, 0.580f, 0.940f, 0.318f, 0.74f);

        VisualStyle::DrawBadgePill(renderer,
            { 0.580f, 0.336f, 0.145f, 0.025f },
            "CREATE ROOM", m_font, 0.0125f,
            { 54, 36, 76, 220 }, { 246, 178, 206, 162 }, kPink);
        renderer.DrawText(m_font, "创建新对战房间", 0.735f, 0.334f, 0.0195f, kWhite);
        renderer.DrawText(m_font, "房间名称（选填）", 0.580f, 0.374f, 0.014f, kDim);
        if (m_roomNameInput) m_roomNameInput->Render(renderer);

        // 曲目选择卡片
        const std::string songCountTag = m_charts.empty()
            ? "0 / 0"
            : (std::to_string(m_selectedChartIdx + 1) + " / " + std::to_string(m_charts.size()));
        renderer.DrawText(m_font, "初始课题曲目", 0.580f, 0.460f, 0.014f, kDim);
        renderer.DrawText(m_font, songCountTag, 0.940f, 0.460f, 0.014f, kGold, TextAlign::Right);

        const NormRect lobbySongBox{ 0.580f, 0.484f, 0.360f, 0.110f };
        renderer.DrawRoundedGradientRect(lobbySongBox, 0.012f,
            { 18, 24, 46, 214 }, { 24, 28, 54, 214 },
            { 12, 16, 34, 228 }, { 16, 20, 40, 228 });
        renderer.DrawRoundedRectOutlineGradient(lobbySongBox, 0.012f,
            { 250, 216, 157, 118 }, { 140, 150, 196, 48 }, 0.0012f);

        if (m_charts.empty())
        {
            renderer.DrawText(m_font, "本地曲库暂无谱面",
                0.760f, 0.524f, 0.019f, kCoral, TextAlign::Center);
        }
        else
        {
            const auto& selChart = m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)];
            const std::string sTitle = selChart.title.empty() ? selChart.id : selChart.title;
            const std::string sMeta  = (selChart.artist.empty() ? "Unknown Artist" : selChart.artist) +
                                       "  ·  " + FmtFloat(selChart.bpm, 0) + " BPM";
            VisualStyle::DrawTextFit(renderer, m_font, sTitle,
                0.640f, 0.504f, 0.0215f, 0.240f, kWhite);
            VisualStyle::DrawTextFit(renderer, m_font, sMeta,
                0.640f, 0.546f, 0.0145f, 0.240f, kDim);
        }

        if (m_btnPrevSong) m_btnPrevSong->Render(renderer);
        if (m_btnNextSong) m_btnNextSong->Render(renderer);

        // 难度选择栏
        renderer.DrawText(m_font, "课题难度", 0.580f, 0.614f, 0.014f, kDim);
        const NormRect diffBox{ 0.634f, 0.640f, 0.252f, 0.046f };
        std::string diffLabel = "暂无难度";
        if (!m_charts.empty())
        {
            const auto& selChart = m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)];
            if (!selChart.difficulties.empty())
            {
                const auto& d = selChart.difficulties[std::clamp(
                    m_selectedDiffIdx, 0, static_cast<int>(selChart.difficulties.size()) - 1)];
                diffLabel = FormatDifficultyBadge(d.name, d.level);
            }
        }
        VisualStyle::DrawBadgePill(renderer, diffBox, diffLabel, m_font, 0.016f,
            { 46, 36, 74, 224 }, { 246, 178, 206, 175 }, kGold);
        if (m_btnPrevDiff) m_btnPrevDiff->Render(renderer);
        if (m_btnNextDiff) m_btnNextDiff->Render(renderer);

        const AuthState hintState = api.GetAuthState();
        std::string authHint = "最多支持 8 名选手同场竞技 · 建房后可随时切换课题曲";
        if (hintState == AuthState::AuthenticatedOffline)
            authHint = "当前离线保活已复用本地账号 · 网络恢复后可直接建房对战";
        else if (hintState == AuthState::SessionExpired)
            authHint = "登录凭据已过期 · 请先返回主菜单验证密码后再建房或入房";
        else if (hintState == AuthState::Unauthenticated)
            authHint = "未登录时点击建房将自动创建并持久化访客通行证";
        renderer.DrawText(m_font, authHint, 0.760f, 0.720f, 0.0138f,
            hintState == AuthState::SessionExpired ? kCoral : kDim, TextAlign::Center);

        if (m_btnCreateRoom) m_btnCreateRoom->Render(renderer);
        if (m_btnBack)       m_btnBack->Render(renderer);

        renderer.DrawText(m_font,
            "↑ ↓ 选择房间   ·   双击或 Enter 加入选中   ·   F5 刷新大厅   ·   Esc 返回主页",
            0.21f, 0.920f, 0.0155f, kDim);

        m_particles.Render(renderer);
        if (m_topBar) { m_topBar->Render(renderer); m_topBar->RenderDrawer(renderer); }
        return;
    }

    // =========================================================================
    // 视图 2：房间与赛后领奖台视图 (m_currentRoom.has_value())
    // =========================================================================
    const auto& room  = *m_currentRoom;
    const bool isHost = IsCurrentUserRoomHost(room, me);

    // ── 左栏 ({0.04, 0.165, 0.34, 0.715})：房间码胶囊 + 状态 + 课题曲 + 房主选曲 ─
    VisualStyle::DrawGlassCard(renderer, ROOM_LEFT_PANEL,
        { 24, 30, 58, 226 }, { 14, 18, 36, 240 },
        { 250, 216, 157, 184 }, { 164, 156, 212, 76 },
        0.018f, 0.56f);

    // 房间状态胶囊 (WAITING / PLAYING / PODIUM · 赛果领奖台)
    std::string roomStatusLabel = "WAITING · 备战中";
    Color roomStatusColor       = kMint;
    if (room.status == "playing")
    {
        roomStatusLabel = "PLAYING · 竞技中";
        roomStatusColor = kPink;
    }
    else if (room.status == "finished")
    {
        roomStatusLabel = "PODIUM · 赛果领奖台";
        roomStatusColor = kGold;
    }
    VisualStyle::DrawBadgePill(renderer,
        { 0.058f, 0.184f, 0.196f, 0.030f },
        roomStatusLabel, m_font, 0.013f,
        { 40, 34, 68, 226 }, roomStatusColor, roomStatusColor);

    // 6 位房间邀请码胶囊
    VisualStyle::DrawBadgePill(renderer,
        { 0.262f, 0.184f, 0.100f, 0.030f },
        "[" + room.code + "]", m_font, 0.0145f,
        { 64, 44, 82, 232 }, kGold, kGold);

    // 醒目房间码与房间信息铭牌
    const NormRect codeBanner{ 0.058f, 0.224f, 0.304f, 0.082f };
    renderer.DrawRoundedGradientRect(codeBanner, 0.011f,
        { 32, 26, 56, 218 }, { 20, 24, 48, 218 },
        { 18, 18, 38, 230 }, { 14, 18, 36, 230 });
    renderer.DrawRoundedRectOutlineGradient(codeBanner, 0.011f,
        { 250, 216, 157, 140 }, { 246, 178, 206, 62 }, 0.0012f);
    renderer.DrawText(m_font, "ROOM CODE / 6 位邀请码", 0.072f, 0.234f, 0.0125f, kDim);
    renderer.DrawText(m_font, room.code, 0.072f, 0.254f, 0.033f, kGold);
    renderer.DrawText(m_font,
        std::to_string(room.players.size()) + " / " + std::to_string(MAX_ARENA_PLAYERS) + " 选手",
        0.348f, 0.236f, 0.0145f, kCyan, TextAlign::Right);
    VisualStyle::DrawTextFit(renderer, m_font,
        "房主: " + (room.hostName.empty() ? "Unknown" : room.hostName),
        0.195f, 0.268f, 0.014f, 0.153f, kDim);

    const std::string dispRoomName = room.name.empty() ? ("Arena Room " + room.code) : room.name;
    VisualStyle::DrawTextFit(renderer, m_font, dispRoomName,
        0.058f, 0.318f, 0.021f, 0.304f, kWhite);
    VisualStyle::DrawGildedDivider(renderer, 0.058f, 0.362f, 0.354f, 0.72f);

    // 当前房间课题曲目卡片
    const NormRect stageCard{ 0.058f, 0.368f, 0.304f, 0.172f };
    VisualStyle::DrawGlassCard(renderer, stageCard,
        { 34, 28, 62, 224 }, { 18, 20, 42, 236 },
        { 246, 178, 206, 165 }, { 140, 150, 196, 58 }, 0.013f, 0.38f);

    renderer.DrawText(m_font, "CURRENT SONG / 当前对战课题曲", 0.072f, 0.380f, 0.0125f, kDim);
    const ChartInfo* localRoomChart = FindChartInList(m_charts, room.chartId, room.chartTitle);
    const bool needChartSync = !isHost && (localRoomChart == nullptr || m_roomChartMissing || m_roomChartHashMismatch);
    std::string installTag = "● 本地已安装";
    Color installColor     = kMint;
    if (m_chartSyncing)
    {
        const int pct = std::clamp(static_cast<int>(std::lround(m_chartSyncProgress * 100.0f)), 0, 100);
        installTag   = "↻ 同步中 " + std::to_string(pct) + "%";
        installColor = kCyan;
    }
    else if (needChartSync)
    {
        installTag   = !room.marketChartId.empty() ? "⬇ 可一键同步" : "! 未发布自制谱";
        installColor = !room.marketChartId.empty() ? kGold : kCoral;
    }
    renderer.DrawText(m_font, installTag, 0.348f, 0.380f, 0.0125f, installColor, TextAlign::Right);

    const std::string roomSongTitle = !room.chartTitle.empty()
        ? room.chartTitle
        : (localRoomChart && !localRoomChart->title.empty() ? localRoomChart->title : room.chartId);
    VisualStyle::DrawTextFit(renderer, m_font, roomSongTitle,
        0.072f, 0.408f, 0.024f, 0.276f, kWhite);

    std::string roomSongMeta = localRoomChart
        ? ((localRoomChart->artist.empty() ? "Unknown Artist" : localRoomChart->artist) +
           "  ·  " + FmtFloat(localRoomChart->bpm, 0) + " BPM")
        : ("谱面 ID: " + room.chartId);
    if (!localRoomChart && room.packageSize > 0)
        roomSongMeta += "  ·  " + FmtBytes(static_cast<std::size_t>(room.packageSize));
    VisualStyle::DrawTextFit(renderer, m_font, roomSongMeta,
        0.072f, 0.448f, 0.0145f, 0.276f, kDim);

    const std::string roomDiffBadge = FormatDifficultyBadge(
        room.difficulty.empty() ? "Normal" : room.difficulty, room.difficultyLevel);
    VisualStyle::DrawBadgePill(renderer,
        { 0.072f, 0.486f, 0.165f, 0.036f },
        roomDiffBadge, m_font, 0.0145f,
        { 52, 38, 78, 226 }, kGold, kGold);

    // 房主切歌控件 / 队员就绪说明
    if (isHost)
    {
        renderer.DrawText(m_font, "HOST SONG SWITCHER / 房主切换课题曲", 0.058f, 0.562f, 0.013f, kGold);
        const NormRect switchBox{ 0.058f, 0.588f, 0.304f, 0.096f };
        renderer.DrawRoundedRect(switchBox, 0.011f, { 16, 22, 42, 208 }, true);
        renderer.DrawRoundedRectOutlineGradient(switchBox, 0.011f,
            { 250, 216, 157, 108 }, { 140, 150, 196, 44 }, 0.0011f);

        if (m_charts.empty())
        {
            renderer.DrawText(m_font, "本地暂无可选谱面", 0.210f, 0.626f, 0.016f, kCoral, TextAlign::Center);
        }
        else
        {
            const auto& cand = m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)];
            VisualStyle::DrawTextFit(renderer, m_font,
                cand.title.empty() ? cand.id : cand.title,
                0.112f, 0.604f, 0.0185f, 0.196f, kWhite);
            VisualStyle::DrawTextFit(renderer, m_font,
                cand.artist.empty() ? "Unknown Artist" : cand.artist,
                0.112f, 0.642f, 0.0135f, 0.196f, kDim);
        }
        if (m_btnPrevSong) m_btnPrevSong->Render(renderer);
        if (m_btnNextSong) m_btnNextSong->Render(renderer);

        std::string candDiffLabel = "Normal";
        if (!m_charts.empty())
        {
            const auto& cand = m_charts[std::clamp(m_selectedChartIdx, 0, static_cast<int>(m_charts.size()) - 1)];
            if (!cand.difficulties.empty())
            {
                const auto& d = cand.difficulties[std::clamp(
                    m_selectedDiffIdx, 0, static_cast<int>(cand.difficulties.size()) - 1)];
                candDiffLabel = FormatDifficultyBadge(d.name, d.level);
            }
        }
        VisualStyle::DrawBadgePill(renderer,
            { 0.106f, 0.698f, 0.208f, 0.044f },
            candDiffLabel, m_font, 0.0145f,
            { 42, 34, 68, 220 }, { 246, 178, 206, 160 }, kPink);
        if (m_btnPrevDiff)   m_btnPrevDiff->Render(renderer);
        if (m_btnNextDiff)   m_btnNextDiff->Render(renderer);
        if (m_btnApplyChart) m_btnApplyChart->Render(renderer);

        renderer.DrawText(m_font, "切换后点击「同步房间课题曲」广播给全员",
            0.210f, 0.828f, 0.0132f, kDim, TextAlign::Center);
    }
    else
    {
        int readyCount = 0;
        for (const auto& p : room.players)
        {
            if (p.ready || p.isHost)
                ++readyCount;
        }
        const NormRect guestBox{ 0.058f, 0.566f, 0.304f, 0.282f };
        renderer.DrawRoundedRect(guestBox, 0.012f, { 16, 22, 42, 202 }, true);
        renderer.DrawRoundedRectOutlineGradient(guestBox, 0.012f,
            { 250, 216, 157, 98 }, { 140, 150, 196, 42 }, 0.0011f);

        renderer.DrawText(m_font, "MATCH SYNC / 对战同步指引", 0.074f, 0.584f, 0.013f, kGold);
        renderer.DrawText(m_font,
            "就绪人数：" + std::to_string(readyCount) + " / " + std::to_string(room.players.size()),
            0.074f, 0.616f, 0.0195f, kMint);

        if (m_chartSyncing)
        {
            const int pct = std::clamp(static_cast<int>(std::lround(m_chartSyncProgress * 100.0f)), 0, 100);
            renderer.DrawText(m_font,
                "正在从谱面市场下载并校验安装包... " + std::to_string(pct) + "%",
                0.074f, 0.658f, 0.0145f, kCyan);
            const NormRect barBg{ 0.074f, 0.688f, 0.272f, 0.016f };
            renderer.DrawRoundedRect(barBg, 0.008f, { 24, 30, 56, 224 }, true);
            const float fillW = std::clamp(m_chartSyncProgress, 0.03f, 1.0f) * barBg.width;
            renderer.DrawRoundedGradientRect(
                { barBg.x, barBg.y, fillW, barBg.height }, 0.008f,
                kPink, kGold, kPink, kGold);
            renderer.DrawText(m_font, "· 下载完成后将自动重扫曲库并切换至准备态", 0.074f, 0.720f, 0.0138f, kDim);
            renderer.DrawText(m_font, "· 已自动向房间发送同步提示短语", 0.074f, 0.756f, 0.0138f, kDim);
        }
        else if (needChartSync && !room.marketChartId.empty())
        {
            renderer.DrawText(m_font,
                m_roomChartHashMismatch ? "! 本地谱面版本与房主不一致" : "! 本地缺少房主所选谱面，可从谱面市场同步",
                0.074f, 0.660f, 0.0145f, kGold);
            renderer.DrawText(m_font, "· 点击右下角「⬇ 一键同步房间谱面」自动安装", 0.074f, 0.698f, 0.0142f, kCyan);
            renderer.DrawText(m_font, "· 同步期间将自动广播「稍等，正在同步房间谱面~」", 0.074f, 0.736f, 0.0138f, kDim);
            renderer.DrawText(m_font, "· 安装校验完成后自动恢复「准备就绪」按钮", 0.074f, 0.774f, 0.0138f, kDim);
        }
        else if (needChartSync && room.marketChartId.empty())
        {
            renderer.DrawText(m_font, "! 房主选用了未发布的本地自制谱面", 0.074f, 0.660f, 0.0150f, kCoral);
            renderer.DrawText(m_font, "· 该谱面尚未上架在线谱面市场，无法自动拉取", 0.074f, 0.698f, 0.0138f, kDim);
            renderer.DrawText(m_font, "· 请提示房主先发布至谱面市场，或切换其他课题曲", 0.074f, 0.736f, 0.0138f, kGold);
            renderer.DrawText(m_font, "· 您也可在「本地曲库 → 导入谱面」手动导入后返回", 0.074f, 0.774f, 0.0138f, kDim);
        }
        else
        {
            renderer.DrawText(m_font, "· 确认本地已安装当前课题谱面", 0.074f, 0.668f, 0.0145f, kDim);
            renderer.DrawText(m_font, "· 点击右下角「准备就绪」等待房主开局", 0.074f, 0.706f, 0.0145f, kDim);
            renderer.DrawText(m_font, "· 房主开局后将自动同步切入演奏舞台", 0.074f, 0.744f, 0.0145f, kDim);
            renderer.DrawText(m_font, "· 全员完赛后揭晓金/银/铜赛果领奖台", 0.074f, 0.782f, 0.0145f, kDim);
        }
    }

    // ── 右栏 ({0.40, 0.165, 0.56, 0.715})：8 人实时排名席位与赛果领奖台 ──────
    VisualStyle::DrawGlassCard(renderer, ROOM_RIGHT_PANEL,
        { 22, 28, 54, 226 }, { 12, 16, 34, 240 },
        room.status == "finished" ? kGold : Color{ 250, 216, 157, 178 },
        { 168, 158, 214, 78 },
        0.018f, 0.58f);

    VisualStyle::DrawBadgePill(renderer,
        { 0.418f, 0.182f, 0.175f, 0.026f },
        room.status == "finished" ? "PODIUM · 赛果领奖台" : "RANKED PLAYERS",
        m_font, 0.0125f,
        { 54, 38, 76, 224 }, kGold, kGold);

    const std::string rightHeader = (room.status == "finished")
        ? "PODIUM · 赛果领奖台最终名次"
        : ((room.status == "playing") ? "实时竞速积分榜" : "房间选手席位");
    renderer.DrawText(m_font, rightHeader, 0.604f, 0.178f, 0.0200f, kWhite);
    renderer.DrawText(m_font,
        "● 增量同步 r" + std::to_string(room.revision),
        0.942f, 0.183f, 0.0132f, kCyan, TextAlign::Right);
    VisualStyle::DrawGildedDivider(renderer, 0.418f, 0.942f, 0.211f, 0.74f);

    const auto rankedPlayers = BuildRankedPlayers(room);
    const bool showPodiumMedals = (room.status == "finished" || room.status == "playing" ||
        std::any_of(rankedPlayers.begin(), rankedPlayers.end(),
            [](const ArenaPlayerState& p) { return p.score > 0 || p.finished || p.placement > 0; }));

    for (int slot = 0; slot < MAX_ARENA_PLAYERS; ++slot)
    {
        const float rowY = PLAYER_CARD_Y0 + static_cast<float>(slot) * PLAYER_CARD_STRIDE;
        const NormRect card{ PLAYER_CARD_X, rowY, PLAYER_CARD_W, PLAYER_CARD_H };

        if (slot >= static_cast<int>(rankedPlayers.size()))
        {
            renderer.DrawRoundedRect(card, 0.009f, { 14, 18, 36, 134 }, true);
            renderer.DrawRoundedRectOutlineGradient(card, 0.009f,
                { 140, 150, 192, 40 }, { 98, 108, 148, 22 }, 0.0010f);
            renderer.DrawText(m_font,
                "#" + std::to_string(slot + 1) + "   空置席位 · 等待选手加入",
                card.x + 0.016f, rowY + 0.017f, 0.0142f, Color{ 132, 142, 172, 144 });
            continue;
        }

        const auto& p     = rankedPlayers[slot];
        const int rank    = (showPodiumMedals && p.placement > 0) ? p.placement : (slot + 1);
        const bool isMe   = MatchesCurrentUser(p, me);

        // #1 金 / #2 银 / #3 铜 领奖台高光配色
        Color topTint    { 24, 30, 56, 214 };
        Color botTint    { 14, 18, 36, 226 };
        Color topBorder  = isMe ? kCyan : Color{ 166, 176, 214, 78 };
        Color botBorder  { 112, 122, 168, 40 };
        Color medalColor = kDim;
        std::string rankText = "#" + std::to_string(rank);

        if (rank == 1)
        {
            topTint    = { 88, 64, 34, 236 };
            botTint    = { 44, 32, 24, 242 };
            topBorder  = { 255, 224, 132, 242 };
            botBorder  = { 246, 178, 102, 148 };
            medalColor = kGold;
            rankText   = "♛ #1";
        }
        else if (rank == 2)
        {
            topTint    = { 54, 66, 92, 232 };
            botTint    = { 28, 36, 56, 238 };
            topBorder  = { 224, 234, 252, 228 };
            botBorder  = { 158, 176, 214, 132 };
            medalColor = kSilver;
            rankText   = "✦ #2";
        }
        else if (rank == 3)
        {
            topTint    = { 76, 48, 42, 230 };
            botTint    = { 38, 26, 28, 238 };
            topBorder  = { 240, 176, 132, 224 };
            botBorder  = { 184, 122, 88, 128 };
            medalColor = kBronze;
            rankText   = "✦ #3";
        }

        VisualStyle::DrawGlassCard(renderer, card,
            topTint, botTint, topBorder, botBorder, 0.010f, rank <= 3 ? 0.44f : 0.28f);

        if (rank <= 3)
        {
            renderer.DrawRoundedGradientRect(
                { card.x + 0.0045f, rowY + 0.009f, 0.0034f, PLAYER_CARD_H - 0.018f },
                0.0017f, medalColor, kPink, medalColor, kPink);
        }

        // 名次徽章
        VisualStyle::DrawBadgePill(renderer,
            { card.x + 0.010f, rowY + 0.011f, 0.054f, 0.032f },
            rankText, m_font, 0.0135f,
            { 28, 26, 48, 224 }, medalColor, medalColor);

        // 在线 / 断线保位指示圆点
        const Color connCol = p.connected ? kMint : kCoral;
        renderer.DrawCircleFilled(card.x + 0.072f, rowY + 0.018f, 0.0038f, connCol, 16);

        // [danBadge] + 用户名 + 连击/断线保位副行
        float nameX = card.x + 0.080f;
        if (!p.danBadge.empty())
        {
            VisualStyle::DrawBadgePill(renderer,
                { nameX, rowY + 0.008f, 0.062f, 0.021f },
                "[" + p.danBadge + "]", m_font, 0.0112f,
                { 54, 38, 76, 224 }, kGold, kGold);
            nameX += 0.067f;
        }

        const std::string dispUser = (p.username.empty() ? p.userId : p.username) + (isMe ? " (我)" : "");
        const float maxNameW = std::max(0.08f, (card.x + 0.278f) - nameX);
        VisualStyle::DrawTextFit(renderer, m_font, dispUser,
            nameX, rowY + 0.006f, 0.0172f, maxNameW,
            !p.connected ? kCoral : (isMe ? kCyan : kWhite));

        std::string subStatusLine;
        Color subStatusColor = kDim;
        if (!p.connected)
        {
            subStatusLine  = "⟳ 断线重连保位中 (60s)";
            subStatusColor = kCoral;
        }
        else if (p.maxCombo > 0 || p.combo > 0)
        {
            subStatusLine = "● 在线  ·  连击 " + std::to_string(p.combo) + "x / 最大 " + std::to_string(p.maxCombo) + "x";
        }
        else
        {
            subStatusLine = p.isHost ? "● 在线  ·  房间主持人" : "● 在线  ·  同场挑战者";
        }
        VisualStyle::DrawTextFit(renderer, m_font, subStatusLine,
            card.x + 0.080f, rowY + 0.031f, 0.0122f, 0.198f, subStatusColor);

        // 分数与准确率
        renderer.DrawText(m_font, FormatScore(p.score),
            card.x + 0.376f, rowY + 0.007f, 0.0185f,
            rank == 1 ? kGold : kWhite, TextAlign::Right);
        renderer.DrawText(m_font, FmtFloat(p.accuracy, 2) + "%",
            card.x + 0.376f, rowY + 0.031f, 0.0128f, kGold, TextAlign::Right);

        // 评级徽章 (Grade)
        const std::string gradeStr = p.grade.empty() ? "D" : p.grade;
        const Color gColor = GradeBadgeColor(gradeStr);
        VisualStyle::DrawBadgePill(renderer,
            { card.x + 0.384f, rowY + 0.011f, 0.040f, 0.032f },
            gradeStr, m_font, 0.0138f,
            { 28, 32, 56, 224 }, gColor, gColor);

        // Ready / Host / Finished / Reconnecting 状态胶囊
        std::string pillText = "○ 未准备";
        Color pillColor      = kDim;
        if (!p.connected)
        {
            pillText  = "⟳ 重连中";
            pillColor = kCoral;
        }
        else if (p.finished)
        {
            pillText  = p.isHost ? "★ 房主·完赛" : "✓ 已完赛";
            pillColor = kGold;
        }
        else if (room.status == "playing")
        {
            pillText  = "● 演奏中";
            pillColor = kPink;
        }
        else if (p.isHost)
        {
            pillText  = p.ready ? "★ 房主·就绪" : "★ 房主";
            pillColor = kGold;
        }
        else if (p.ready)
        {
            pillText  = "● 已准备";
            pillColor = kMint;
        }
        VisualStyle::DrawBadgePill(renderer,
            { card.x + 0.430f, rowY + 0.012f, 0.084f, 0.030f },
            pillText, m_font, 0.0118f,
            { 28, 34, 58, 220 }, pillColor, pillColor);
    }

    // ── 房间快捷短语消息流与 6 枚预设短语按钮 ─────────────────────────────────
    VisualStyle::DrawGildedDivider(renderer, 0.418f, 0.942f, 0.688f, 0.62f);
    const NormRect chatFeedBox{ 0.418f, 0.696f, 0.524f, 0.096f };
    renderer.DrawRoundedRect(chatFeedBox, 0.010f, { 14, 18, 36, 196 }, true);
    renderer.DrawRoundedRectOutlineGradient(chatFeedBox, 0.010f,
        { 250, 216, 157, 92 }, { 140, 150, 196, 40 }, 0.0010f);

    renderer.DrawText(m_font, "QUICK CHAT · 房间即时短语", 0.430f, 0.703f, 0.0118f, kGold);
    if (m_chatCooldown > 0.01f)
    {
        renderer.DrawText(m_font,
            "冷却 " + FmtFloat(m_chatCooldown, 1) + "s",
            0.930f, 0.703f, 0.0115f, kDim, TextAlign::Right);
    }

    if (room.messages.empty())
    {
        renderer.DrawText(m_font,
            "暂无房间短语消息 · 点击下方快捷短语与同房选手打招呼吧",
            0.430f, 0.742f, 0.0135f, kDim);
    }
    else
    {
        const int totalMsgs = static_cast<int>(room.messages.size());
        const int showMsgs  = std::min(3, totalMsgs);
        const int firstMsg  = totalMsgs - showMsgs;
        for (int mIdx = 0; mIdx < showMsgs; ++mIdx)
        {
            const auto& msg    = room.messages[static_cast<std::size_t>(firstMsg + mIdx)];
            const bool isLatest = (mIdx == showMsgs - 1);
            const float msgY   = 0.723f + static_cast<float>(mIdx) * 0.022f;
            const std::string line = "[" + ExtractArenaChatUser(msg) + "]: " + ExtractArenaChatText(msg);
            VisualStyle::DrawTextFit(renderer, m_font, line,
                0.430f, msgY, 0.0132f, 0.500f,
                isLatest ? kWhite : kDim);
        }
    }

    for (auto& btn : m_chatButtons)
    {
        if (btn)
            btn->Render(renderer);
    }

    if (m_btnLeaveRoom)    m_btnLeaveRoom->Render(renderer);
    if (m_btnReadyOrStart) m_btnReadyOrStart->Render(renderer);

    renderer.DrawText(m_font,
        "Space / Enter 准备或开局   ·   F5 立即同步   ·   Esc 离开房间",
        0.215f, 0.920f, 0.0155f, kDim);

    m_particles.Render(renderer);
    if (m_profileModal) m_profileModal->Render(renderer);
    if (m_topBar) { m_topBar->Render(renderer); m_topBar->RenderDrawer(renderer); }
}

void SceneArena::OnEvent(const SDL_Event& event)
{
    if (m_topBar && m_topBar->HandleEvent(event))
        return;
    if (m_profileModal)
    {
        m_profileModal->Update(1.0f / 60.0f);
        if (m_profileModal->HandleEvent(event))
            return;
    }
    if (m_currentRoom.has_value())
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            const auto [mx, my] = Input::GetMousePosition();
            const auto ranked = BuildRankedPlayers(*m_currentRoom);
            for (int slot = 0; slot < static_cast<int>(ranked.size()) && slot < MAX_ARENA_PLAYERS; ++slot)
            {
                const float rowY = PLAYER_CARD_Y0 + static_cast<float>(slot) * PLAYER_CARD_STRIDE;
                if (mx >= PLAYER_CARD_X && mx <= PLAYER_CARD_X + PLAYER_CARD_W &&
                    my >= rowY && my < rowY + PLAYER_CARD_H && !ranked[slot].userId.empty())
                {
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    m_profileModal->Open(ranked[slot].userId, ranked[slot].username);
                    return;
                }
            }
        }
    }
    if (m_currentRoom.has_value())
    {
        if (m_btnLeaveRoom && m_btnLeaveRoom->HandleEvent(event))
            return;
        if (m_btnReadyOrStart && m_btnReadyOrStart->HandleEvent(event))
            return;
        for (auto& btn : m_chatButtons)
        {
            if (btn && btn->HandleEvent(event))
                return;
        }

        const auto me = APIClient::GetInstance().GetCurrentUser();
        if (IsCurrentUserRoomHost(*m_currentRoom, me) && m_currentRoom->status != "playing")
        {
            if (m_btnPrevSong   && m_btnPrevSong->HandleEvent(event))   return;
            if (m_btnNextSong   && m_btnNextSong->HandleEvent(event))   return;
            if (m_btnPrevDiff   && m_btnPrevDiff->HandleEvent(event))   return;
            if (m_btnNextDiff   && m_btnNextDiff->HandleEvent(event))   return;
            if (m_btnApplyChart && m_btnApplyChart->HandleEvent(event)) return;
        }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE)
            {
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                LeaveCurrentRoom();
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_F5)
            {
                m_pollTimer = 0.0f;
                PollRoomState();
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_RETURN ||
                event.key.scancode == SDL_SCANCODE_KP_ENTER ||
                event.key.scancode == SDL_SCANCODE_SPACE)
            {
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                ToggleReadyOrStart();
                return;
            }
        }
        return;
    }

    // ── 大厅态事件处理 ────────────────────────────────────────────────────────
    if (m_btnTabSeason && m_btnTabSeason->HandleEvent(event)) return;
    if (m_btnTabRooms  && m_btnTabRooms->HandleEvent(event))  return;
    if (m_arenaTabMode == 0)
    {
        if (m_btnBack && m_btnBack->HandleEvent(event)) return;
        if (m_btnRefreshSeason && m_btnRefreshSeason->HandleEvent(event)) return;
        for (auto& btn : m_btnPlaySeasonStage)
            if (btn && btn->HandleEvent(event)) return;
        if (event.type == SDL_EVENT_MOUSE_WHEEL && !m_seasonLbEntries.empty())
        {
            m_seasonLbScroll = std::clamp(m_seasonLbScroll - static_cast<int>(event.wheel.y),
                0, std::max(0, static_cast<int>(m_seasonLbEntries.size()) - 8));
            return;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            const auto [mx, my] = Input::GetMousePosition();
            const int totalLb = static_cast<int>(m_seasonLbEntries.size());
            for (int s = 0; s < 8 && (m_seasonLbScroll + s) < totalLb; ++s)
            {
                const float ry = 0.262f + static_cast<float>(s) * 0.064f;
                if (mx >= 0.580f && mx <= 0.940f && my >= ry && my <= ry + 0.056f)
                {
                    const auto& row = m_seasonLbEntries[static_cast<std::size_t>(m_seasonLbScroll + s)];
                    if (!row.userId.empty() && m_profileModal)
                        m_profileModal->Open(row.userId, row.username);
                    return;
                }
            }
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE)
            {
                m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.38f);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_F5) { RefreshSeasonData(); return; }
        }
        return;
    }

    const bool anyInputFocused = (m_codeInput && m_codeInput->IsFocused()) ||
                                 (m_roomNameInput && m_roomNameInput->IsFocused());
    if (anyInputFocused && event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        if (m_codeInput)     m_codeInput->SetFocused(false);
        if (m_roomNameInput) m_roomNameInput->SetFocused(false);
        return;
    }

    bool inputConsumed = false;
    if (m_codeInput)     inputConsumed = m_codeInput->HandleEvent(event)     || inputConsumed;
    if (m_roomNameInput) inputConsumed = m_roomNameInput->HandleEvent(event) || inputConsumed;
    if (inputConsumed && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_TEXT_INPUT))
        return;

    if (m_btnBack         && m_btnBack->HandleEvent(event))         return;
    if (m_btnJoinSelected && m_btnJoinSelected->HandleEvent(event)) return;
    if (m_btnRefreshLobby && m_btnRefreshLobby->HandleEvent(event)) return;
    if (m_btnJoinByCode   && m_btnJoinByCode->HandleEvent(event))   return;
    if (m_btnCreateRoom   && m_btnCreateRoom->HandleEvent(event))   return;
    if (m_btnPrevSong     && m_btnPrevSong->HandleEvent(event))     return;
    if (m_btnNextSong     && m_btnNextSong->HandleEvent(event))     return;
    if (m_btnPrevDiff     && m_btnPrevDiff->HandleEvent(event))     return;
    if (m_btnNextDiff     && m_btnNextDiff->HandleEvent(event))     return;

    const int totalRooms = static_cast<int>(m_rooms.size());

    if (event.type == SDL_EVENT_MOUSE_WHEEL && totalRooms > 0)
    {
        const auto [mx, my] = Input::GetMousePosition();
        if (mx >= LOBBY_LEFT_PANEL.x && mx <= LOBBY_LEFT_PANEL.x + LOBBY_LEFT_PANEL.width &&
            my >= LOBBY_LEFT_PANEL.y && my <= LOBBY_LEFT_PANEL.y + LOBBY_LEFT_PANEL.height)
        {
            const int delta = (event.wheel.y > 0.0f) ? -1 : ((event.wheel.y < 0.0f) ? 1 : 0);
            if (delta != 0)
            {
                const int next = std::clamp(m_selectedRoomRow + delta, 0, totalRooms - 1);
                if (next != m_selectedRoomRow)
                {
                    m_selectedRoomRow = next;
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
                }
            }
            return;
        }
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && totalRooms > 0)
    {
        const auto [mx, my] = Input::GetMousePosition();
        if (mx >= ROOM_ROW_X && mx <= ROOM_ROW_X + ROOM_ROW_W)
        {
            const int scrollOffset = ComputeRoomScrollOffset(m_selectedRoomRow, totalRooms);
            for (int slot = 0; slot < VISIBLE_ROOM_ROWS && (scrollOffset + slot) < totalRooms; ++slot)
            {
                const float rowY = ROOM_ROW_Y0 + static_cast<float>(slot) * ROOM_ROW_STRIDE;
                if (my >= rowY && my < rowY + ROOM_ROW_H)
                {
                    const int idx = scrollOffset + slot;
                    const bool wasSelected = (idx == m_selectedRoomRow);
                    m_selectedRoomRow = idx;
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    if (wasSelected && event.button.clicks >= 2)
                        JoinRoom(m_rooms[idx].code);
                    return;
                }
            }
        }
    }

    if (!anyInputFocused && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
            m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SilkShutter, 0.38f);
            return;
        }
        if (event.key.scancode == SDL_SCANCODE_F5)
        {
            RefreshLobby();
            return;
        }
        if (totalRooms > 0)
        {
            if (event.key.scancode == SDL_SCANCODE_UP)
            {
                m_selectedRoomRow = (m_selectedRoomRow + totalRooms - 1) % totalRooms;
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_DOWN)
            {
                m_selectedRoomRow = (m_selectedRoomRow + 1) % totalRooms;
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_RETURN ||
                event.key.scancode == SDL_SCANCODE_KP_ENTER)
            {
                if (m_selectedRoomRow >= 0 && m_selectedRoomRow < totalRooms)
                {
                    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                    JoinRoom(m_rooms[m_selectedRoomRow].code);
                }
                return;
            }
        }
    }
}

} // namespace sakura::scene
