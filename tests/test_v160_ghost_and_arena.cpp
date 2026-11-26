// tests/test_v160_ghost_and_arena.cpp — v1.6.0 幽灵对手确定性伴跑引擎与多人对战房间客户端测试

#include "test_framework.h"

#include "core/config.h"
#include "game/ghost_runner.h"
#include "game/play_session.h"
#include "game/replay.h"
#include "net/api_client.h"
#include "net/http_client.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace sakura::game;
using sakura::core::Config;
using sakura::net::APIClient;
using sakura::net::ArenaPlayerState;
using sakura::net::ArenaRoomState;
using sakura::net::HttpRequest;
using sakura::net::HttpResponse;
using sakura::net::MockHttpTransport;
using sakura::tests::Matchers::WithinAbs;

namespace
{

struct ScopedV160TempEnv
{
    std::filesystem::path dir;
    nlohmann::json        configSnapshot;
    std::string           previousConfigPath;

    ScopedV160TempEnv()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v160_" + std::to_string(stamp));
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);

        auto& cfg          = Config::GetInstance();
        configSnapshot     = cfg.GetRoot();
        previousConfigPath = cfg.GetFilePath();
    }

    ~ScopedV160TempEnv()
    {
        auto& cfg = Config::GetInstance();
        if (!previousConfigPath.empty())
            cfg.Load(previousConfigPath);
        cfg.Restore(configSnapshot);

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

ChartData BuildDualZoneChart()
{
    ChartData chart;
    chart.version      = 3;
    chart.timingPoints = { { 0, 120.0f, 4, 4 } };

    KeyboardNote k0; k0.time = 500;  k0.lane = 0; k0.type = NoteType::Tap;
    KeyboardNote k1; k1.time = 1000; k1.lane = 1; k1.type = NoteType::Hold; k1.duration = 1000;
    KeyboardNote k2; k2.time = 2100; k2.lane = 2; k2.type = NoteType::Tap;
    KeyboardNote k3; k3.time = 2450; k3.lane = 4; k3.type = NoteType::Tap; k3.linkId = 1;
    chart.keyboardNotes = { k0, k1, k2, k3 };

    MouseNote m0; m0.time = 800;  m0.x = 0.25f; m0.y = 0.30f; m0.type = NoteType::Circle;
    MouseNote m1; m1.time = 1400; m1.x = 0.35f; m1.y = 0.50f; m1.type = NoteType::Slider;
    m1.sliderDuration = 800;
    m1.sliderPath     = { { 0.75f, 0.65f } };
    MouseNote m2; m2.time = 2450; m2.x = 0.55f; m2.y = 0.40f; m2.type = NoteType::Circle;
    m2.linkId = 1; m2.requireLane = 4;
    chart.mouseNotes = { m0, m1, m2 };

    return chart;
}

Replay RecordPerfectDualZoneReplay(const ChartData& chart)
{
    const std::vector<SessionInput> script = {
        { 500,  InputKind::LaneDown,  0 },
        { 540,  InputKind::LaneUp,    0 },
        { 800,  InputKind::MouseDown, 0, 0.25f, 0.30f },
        { 840,  InputKind::MouseUp },
        { 1000, InputKind::LaneDown,  1 },
        { 1400, InputKind::MouseDown, 0, 0.35f, 0.50f },
        { 1800, InputKind::Pointer,   0, 0.55f, 0.58f },
        { 2005, InputKind::Tick },
        { 2010, InputKind::LaneUp,    1 },
        { 2100, InputKind::LaneDown,  2 },
        { 2150, InputKind::LaneUp,    2 },
        { 2185, InputKind::Pointer,   0, 0.75f, 0.65f },
        { 2205, InputKind::Tick },
        { 2260, InputKind::MouseUp },
        { 2450, InputKind::LaneDown,  4 },
        { 2452, InputKind::MouseDown, 0, 0.55f, 0.40f },
        { 2490, InputKind::MouseUp },
        { 2495, InputKind::LaneUp,    4 }
    };

    ChartData recChart = chart;
    PlaySession recSession;
    recSession.Initialize(recChart, false, 0, 0);
    for (const auto& ev : script)
        recSession.Submit(ev);
    recSession.Finish();

    REQUIRE(recSession.Complete());
    REQUIRE(recSession.Score().GetScore() == 1000000);

    Replay replay;
    replay.chartId    = "sakura_awakening";
    replay.chartHash  = Replay::Hash(chart, 0);
    replay.difficulty = 2;
    replay.rate       = 1.0f;
    replay.startMs    = 0;
    replay.inputs     = recSession.Recording();
    return replay;
}

std::string ExtractBearerToken(const HttpRequest& req)
{
    for (const auto& [k, v] : req.headers)
    {
        if (k == "Authorization" && v.rfind("Bearer ", 0) == 0)
            return v.substr(7);
    }
    return {};
}

nlohmann::json SerializeMockPlayer(const ArenaPlayerState& p)
{
    return {
        { "user_id", p.userId },
        { "userId", p.userId },
        { "username", p.username },
        { "dan_badge", p.danBadge },
        { "danBadge", p.danBadge },
        { "is_host", p.isHost },
        { "isHost", p.isHost },
        { "ready", p.ready },
        { "finished", p.finished },
        { "score", p.score },
        { "accuracy", p.accuracy },
        { "combo", p.combo },
        { "max_combo", p.maxCombo },
        { "maxCombo", p.maxCombo },
        { "grade", p.grade },
        { "placement", p.placement }
    };
}

nlohmann::json SerializeMockRoom(const ArenaRoomState& room)
{
    nlohmann::json playersArr = nlohmann::json::array();
    for (const auto& p : room.players)
        playersArr.push_back(SerializeMockPlayer(p));

    return {
        { "code", room.code },
        { "name", room.name },
        { "room_name", room.name },
        { "host_id", room.hostId },
        { "hostId", room.hostId },
        { "host_name", room.hostName },
        { "hostName", room.hostName },
        { "status", room.status },
        { "chart_id", room.chartId },
        { "chartId", room.chartId },
        { "chart_title", room.chartTitle },
        { "chartTitle", room.chartTitle },
        { "difficulty", room.difficulty },
        { "difficulty_level", room.difficultyLevel },
        { "difficultyLevel", room.difficultyLevel },
        { "chart_hash", room.chartHash },
        { "chartHash", room.chartHash },
        { "started_at", room.startedAt },
        { "startedAt", room.startedAt },
        { "updated_at", room.updatedAt },
        { "updatedAt", room.updatedAt },
        { "players", std::move(playersArr) }
    };
}

} // namespace

TEST_CASE("GhostRunner replays dual-zone Replay deterministically, tracks deltaScore, prunes trail, rewinds safely, and loads .skr", "[v160][ghost]")
{
    ScopedV160TempEnv env;
    const ChartData chart = BuildDualZoneChart();
    const Replay replay   = RecordPerfectDualZoneReplay(chart);

    GhostRunner ghost;
    REQUIRE(!ghost.IsActive());
    REQUIRE(!ghost.AdvanceTo(500, 100000).active);

    REQUIRE(ghost.Initialize(chart, replay, "PB"));
    REQUIRE(ghost.IsActive());
    REQUIRE(ghost.GetRivalLabel() == "PB");

    // At t=820ms, k0 (500ms) and m0 (800ms at 0.25, 0.30) are judged; test positive & negative deltaScore
    const auto snapAhead = ghost.AdvanceTo(820, 300000);
    REQUIRE(snapAhead.active);
    REQUIRE(snapAhead.rivalLabel == "PB");
    REQUIRE(snapAhead.ghostScore > 200000);
    REQUIRE(snapAhead.ghostScore < 250000);
    REQUIRE(snapAhead.deltaScore == 300000 - snapAhead.ghostScore);
    REQUIRE(snapAhead.deltaScore > 0);
    REQUIRE(snapAhead.ghostCombo == 2);
    REQUIRE_THAT(snapAhead.cursorX, WithinAbs(0.25, 1e-4));
    REQUIRE_THAT(snapAhead.cursorY, WithinAbs(0.30, 1e-4));

    const auto snapBehind = ghost.AdvanceTo(825, 100000);
    REQUIRE(snapBehind.ghostScore == snapAhead.ghostScore);
    REQUIRE(snapBehind.deltaScore == 100000 - snapBehind.ghostScore);
    REQUIRE(snapBehind.deltaScore < 0);

    // Advance through pointer movement at 1800ms and slider end at 2185ms; verify cursor & 220ms trail pruning
    const auto snapMid = ghost.AdvanceTo(1805, 500000);
    REQUIRE_THAT(snapMid.cursorX, WithinAbs(0.55, 1e-4));
    REQUIRE_THAT(snapMid.cursorY, WithinAbs(0.58, 1e-4));
    REQUIRE(!ghost.GetRecentTrail().empty());

    const auto snapSlider = ghost.AdvanceTo(2190, 750000);
    REQUIRE_THAT(snapSlider.cursorX, WithinAbs(0.75, 1e-4));
    REQUIRE_THAT(snapSlider.cursorY, WithinAbs(0.65, 1e-4));
    const auto& prunedTrail = ghost.GetRecentTrail();
    REQUIRE(!prunedTrail.empty());
    for (const auto& pt : prunedTrail)
    {
        // Points older than kTrailRetentionMs (220ms), such as 800ms / 1400ms / 1800ms, must be pruned
        REQUIRE(pt.timeMs >= 2190 - 220);
        REQUIRE(pt.timeMs <= 2190);
    }

    // Complete dual-zone song and reach exact 1,000,000 All-Perfect parity
    const auto snapEnd = ghost.AdvanceTo(2600, 1000000);
    REQUIRE(snapEnd.ghostScore == 1000000);
    REQUIRE(snapEnd.deltaScore == 0);
    REQUIRE(snapEnd.ghostCombo == 9);
    REQUIRE_THAT(snapEnd.ghostAcc, WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(snapEnd.cursorX, WithinAbs(0.55, 1e-4));
    REQUIRE_THAT(snapEnd.cursorY, WithinAbs(0.40, 1e-4));

    // Rewind safety (nowMs < m_lastTimeMs): rewinding from 2600ms back to 600ms resets cleanly and replays up to 600ms
    const auto snapRewound = ghost.AdvanceTo(600, 50000);
    REQUIRE(snapRewound.active);
    REQUIRE(snapRewound.ghostCombo == 1);
    REQUIRE(snapRewound.ghostScore > 100000);
    REQUIRE(snapRewound.ghostScore < 150000);
    REQUIRE(snapRewound.deltaScore == 50000 - snapRewound.ghostScore);
    REQUIRE(snapRewound.deltaScore < 0);
    for (const auto& pt : ghost.GetRecentTrail())
        REQUIRE(pt.timeMs <= 600);

    // Advancing again after rewind still deterministically hits 1,000,000
    const auto snapReplayedEnd = ghost.AdvanceTo(2600, 980000);
    REQUIRE(snapReplayedEnd.ghostScore == 1000000);
    REQUIRE(snapReplayedEnd.deltaScore == -20000);

    // InitializeFromFile round-trip + invalid path handling
    const auto skrPath = env.dir / "pb_dual_zone.skr";
    REQUIRE(replay.Save(skrPath.string()));

    GhostRunner fileGhost;
    REQUIRE(!fileGhost.InitializeFromFile(chart, "", "Online"));
    REQUIRE(!fileGhost.IsActive());
    REQUIRE(!fileGhost.InitializeFromFile(chart, (env.dir / "nonexistent.skr").string(), "Online"));
    REQUIRE(!fileGhost.IsActive());

    REQUIRE(fileGhost.InitializeFromFile(chart, skrPath.string(), "World#1"));
    REQUIRE(fileGhost.IsActive());
    REQUIRE(fileGhost.GetRivalLabel() == "World#1");

    GhostSnapshot fileFinal;
    for (int t = 0; t <= 2600; t += 20)
        fileFinal = fileGhost.AdvanceTo(t, 1000000);
    REQUIRE(fileFinal.ghostScore == 1000000);
    REQUIRE(fileFinal.deltaScore == 0);
    REQUIRE(fileFinal.ghostCombo == 9);
    REQUIRE_THAT(fileFinal.ghostAcc, WithinAbs(100.0, 1e-3));

    fileGhost.Reset();
    REQUIRE(!fileGhost.IsActive());
}

TEST_CASE("APIClient Arena methods drive full 2-player room lifecycle over MockHttpTransport", "[v160][arena][online]")
{
    ScopedV160TempEnv env;
    Config::GetInstance().Load((env.dir / "arena_settings.json").string());

    ArenaRoomState serverRoom;
    bool roomExists = false;

    auto recomputePlacements = [&]()
    {
        std::vector<std::size_t> order(serverRoom.players.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (serverRoom.players[a].score != serverRoom.players[b].score)
                return serverRoom.players[a].score > serverRoom.players[b].score;
            return serverRoom.players[a].accuracy > serverRoom.players[b].accuracy;
        });
        for (std::size_t rankIdx = 0; rankIdx < order.size(); ++rankIdx)
            serverRoom.players[order[rankIdx]].placement = static_cast<int>(rankIdx + 1);
    };

    auto mock = std::make_shared<MockHttpTransport>([&](const HttpRequest& req) {
        HttpResponse res;
        const std::string token = ExtractBearerToken(req);

        if (req.method == "POST" && req.url.ends_with("/api/v1/auth/login"))
        {
            const auto body = nlohmann::json::parse(req.body);
            const std::string uname = body.value("username", "");
            const bool isHostUser = (uname == "HostAoi");
            res.statusCode = 200;
            res.body = nlohmann::json({
                { "ok", true },
                { "token", isHostUser ? "tok-host" : "tok-rival" },
                { "user", {
                    { "id", isHostUser ? "u-host" : "u-rival" },
                    { "username", uname },
                    { "is_guest", false },
                    { "total_pp", isHostUser ? 420.0 : 380.0 },
                    { "dan_badge", isHostUser ? "皆传" : "六段" }
                } }
            }).dump();
            return res;
        }

        if (token != "tok-host" && token != "tok-rival")
        {
            res.statusCode = 401;
            res.body = R"({"ok":false,"error":"Unauthorized"})";
            return res;
        }

        const std::string uid   = (token == "tok-host") ? "u-host" : "u-rival";
        const std::string uname = (token == "tok-host") ? "HostAoi" : "RivalRin";
        const std::string badge = (token == "tok-host") ? "皆传" : "六段";

        auto jStr = [](const nlohmann::json& j, const char* k1, const char* k2, const std::string& def = {}) {
            if (j.contains(k1) && j[k1].is_string()) return j[k1].get<std::string>();
            if (j.contains(k2) && j[k2].is_string()) return j[k2].get<std::string>();
            return def;
        };
        auto jFloat = [](const nlohmann::json& j, const char* k1, const char* k2, float def = 1.0f) {
            if (j.contains(k1) && j[k1].is_number()) return j[k1].get<float>();
            if (j.contains(k2) && j[k2].is_number()) return j[k2].get<float>();
            return def;
        };
        auto jInt = [](const nlohmann::json& j, const char* k1, const char* k2, int def = 0) {
            if (j.contains(k1) && j[k1].is_number_integer()) return j[k1].get<int>();
            if (j.contains(k2) && j[k2].is_number_integer()) return j[k2].get<int>();
            return def;
        };
        auto jBool = [](const nlohmann::json& j, const char* k1, const char* k2, bool def = false) {
            if (j.contains(k1) && j[k1].is_boolean()) return j[k1].get<bool>();
            if (j.contains(k2) && j[k2].is_boolean()) return j[k2].get<bool>();
            return def;
        };

        // CreateArenaRoom
        if (req.method == "POST" && req.url.ends_with("/rooms"))
        {
            const auto body = nlohmann::json::parse(req.body);
            serverRoom = ArenaRoomState{};
            serverRoom.code            = "SKR160";
            serverRoom.name            = jStr(body, "name", "room_name", jStr(body, "roomName", "name", "Sakura Lobby"));
            serverRoom.hostId          = uid;
            serverRoom.hostName        = uname;
            serverRoom.status          = "waiting";
            serverRoom.chartId         = jStr(body, "chart_id", "chartId");
            serverRoom.chartTitle      = jStr(body, "chart_title", "chartTitle");
            serverRoom.difficulty      = jStr(body, "difficulty", "difficulty_name", "Normal");
            serverRoom.difficultyLevel = jFloat(body, "difficulty_level", "difficultyLevel", 1.0f);
            serverRoom.chartHash       = jStr(body, "chart_hash", "chartHash");
            serverRoom.updatedAt       = 1795700000LL;

            ArenaPlayerState hostPlayer;
            hostPlayer.userId    = uid;
            hostPlayer.username  = uname;
            hostPlayer.danBadge  = badge;
            hostPlayer.isHost    = true;
            hostPlayer.ready     = true;
            hostPlayer.placement = 1;
            serverRoom.players   = { hostPlayer };
            roomExists           = true;

            res.statusCode = 201;
            res.body = nlohmann::json({ { "ok", true }, { "room", SerializeMockRoom(serverRoom) } }).dump();
            return res;
        }

        // ListArenaRooms
        if (req.method == "GET" && (req.url.ends_with("/rooms") || req.url.find("/rooms?") != std::string::npos))
        {
            nlohmann::json arr = nlohmann::json::array();
            if (roomExists)
                arr.push_back(SerializeMockRoom(serverRoom));
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "rooms", std::move(arr) } }).dump();
            return res;
        }

        // JoinArenaRoom
        if (req.method == "POST" && req.url.find("/SKR160/join") != std::string::npos)
        {
            ArenaPlayerState p;
            p.userId    = uid;
            p.username  = uname;
            p.danBadge  = badge;
            p.isHost    = false;
            p.ready     = false;
            p.placement = static_cast<int>(serverRoom.players.size() + 1);
            serverRoom.players.push_back(p);
            serverRoom.updatedAt += 1;
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "room", SerializeMockRoom(serverRoom) } }).dump();
            return res;
        }

        // SelectArenaChart
        if ((req.method == "POST" || req.method == "PUT" || req.method == "PATCH") &&
            (req.url.find("/SKR160/select") != std::string::npos || req.url.find("/SKR160/chart") != std::string::npos))
        {
            const auto body = nlohmann::json::parse(req.body);
            serverRoom.chartId         = jStr(body, "chart_id", "chartId");
            serverRoom.chartTitle      = jStr(body, "chart_title", "chartTitle");
            serverRoom.difficulty      = jStr(body, "difficulty", "difficulty_name");
            serverRoom.difficultyLevel = jFloat(body, "difficulty_level", "difficultyLevel");
            serverRoom.chartHash       = jStr(body, "chart_hash", "chartHash");
            serverRoom.updatedAt += 1;
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "room", SerializeMockRoom(serverRoom) } }).dump();
            return res;
        }

        // SetArenaReady
        if (req.method == "POST" && (req.url.find("/SKR160/ready") != std::string::npos || req.url.find("/SKR160/start") != std::string::npos))
        {
            const auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
            const bool readyFlag = jBool(body, "ready", "isReady", true);
            const bool startFlag = jBool(body, "start", "start_match", jBool(body, "startMatch", "start", req.url.ends_with("/start")));
            for (auto& p : serverRoom.players)
            {
                if (p.userId == uid)
                    p.ready = readyFlag;
            }
            if (startFlag)
            {
                serverRoom.status    = "playing";
                serverRoom.startedAt = 1795700100LL;
            }
            serverRoom.updatedAt += 1;
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "room", SerializeMockRoom(serverRoom) } }).dump();
            return res;
        }

        // ReportArenaProgress
        if (req.method == "POST" && req.url.find("/SKR160/progress") != std::string::npos)
        {
            const auto body = nlohmann::json::parse(req.body);
            for (auto& p : serverRoom.players)
            {
                if (p.userId == uid)
                {
                    p.score    = jInt(body, "score", "score");
                    p.accuracy = jFloat(body, "accuracy", "accuracy");
                    p.combo    = jInt(body, "combo", "combo");
                    p.maxCombo = jInt(body, "max_combo", "maxCombo");
                    p.grade    = jStr(body, "grade", "grade", "D");
                    p.finished = jBool(body, "finished", "isFinished", false);
                }
            }
            recomputePlacements();
            const bool allDone = !serverRoom.players.empty() &&
                std::all_of(serverRoom.players.begin(), serverRoom.players.end(),
                            [](const ArenaPlayerState& p) { return p.finished; });
            if (allDone)
                serverRoom.status = "finished";
            serverRoom.updatedAt += 1;
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "room", SerializeMockRoom(serverRoom) } }).dump();
            return res;
        }

        // LeaveArenaRoom
        if (req.method == "POST" && req.url.find("/SKR160/leave") != std::string::npos)
        {
            std::erase_if(serverRoom.players, [&](const ArenaPlayerState& p) { return p.userId == uid; });
            res.statusCode = 200;
            res.body = R"({"ok":true})";
            return res;
        }

        // FetchArenaRoomState
        if (req.method == "GET" && req.url.find("/SKR160") != std::string::npos)
        {
            res.statusCode = 200;
            res.body = nlohmann::json({ { "ok", true }, { "room", SerializeMockRoom(serverRoom) } }).dump();
            return res;
        }

        res.statusCode = 404;
        res.body = R"({"ok":false,"error":"Not found"})";
        return res;
    });

    APIClient hostClient(mock, (env.dir / "host_queue.json").string());
    APIClient guestClient(mock, (env.dir / "guest_queue.json").string());
    hostClient.SetServerUrl("http://127.0.0.1:8787");
    guestClient.SetServerUrl("http://127.0.0.1:8787");

    REQUIRE(hostClient.Login("HostAoi", "pw123456").ok);
    REQUIRE(guestClient.Login("RivalRin", "pw123456").ok);

    // 1. CreateArenaRoom
    const auto created = hostClient.CreateArenaRoom("Sakura Finals", "sakura_breath", "樱落呼吸", "Hard", 7.5f, "hash_sb");
    REQUIRE(created.ok);
    REQUIRE(created.room.code == "SKR160");
    REQUIRE(created.room.name == "Sakura Finals");
    REQUIRE(created.room.status == "waiting");
    REQUIRE(created.room.chartId == "sakura_breath");
    REQUIRE(created.room.players.size() == 1);
    REQUIRE(created.room.players[0].username == "HostAoi");
    REQUIRE(created.room.players[0].isHost);

    // 2. ListArenaRooms
    const auto listed = guestClient.ListArenaRooms();
    REQUIRE(listed.ok);
    REQUIRE(listed.rooms.size() == 1);
    REQUIRE(listed.rooms[0].code == "SKR160");
    REQUIRE(listed.rooms[0].hostName == "HostAoi");

    // 3. JoinArenaRoom
    const auto joined = guestClient.JoinArenaRoom("SKR160");
    REQUIRE(joined.ok);
    REQUIRE(joined.room.players.size() == 2);
    REQUIRE(joined.room.players[1].username == "RivalRin");
    REQUIRE(!joined.room.players[1].isHost);

    // 4. SelectArenaChart
    const auto selected = hostClient.SelectArenaChart("SKR160", "sakura_awakening", "樱光初醒", "Hard", 7.0f, "hash_awk");
    REQUIRE(selected.ok);
    REQUIRE(selected.room.chartId == "sakura_awakening");
    REQUIRE(selected.room.chartTitle == "樱光初醒");
    REQUIRE(selected.room.difficulty == "Hard");
    REQUIRE_THAT(selected.room.difficultyLevel, WithinAbs(7.0, 0.01));
    REQUIRE(selected.room.chartHash == "hash_awk");

    // 5. SetArenaReady (guest toggles ready, then host starts match)
    const auto guestUnready = guestClient.SetArenaReady("SKR160", false, false);
    REQUIRE(guestUnready.ok);
    REQUIRE(guestUnready.room.status == "waiting");

    const auto guestReady = guestClient.SetArenaReady("SKR160", true, false);
    REQUIRE(guestReady.ok);
    REQUIRE(guestReady.room.players[1].ready);

    const auto matchStart = hostClient.SetArenaReady("SKR160", true, true);
    REQUIRE(matchStart.ok);
    REQUIRE(matchStart.room.status == "playing");
    REQUIRE(matchStart.room.startedAt > 0);

    // 6. ReportArenaProgress (mid-song live standings + final completion)
    const auto midHost = hostClient.ReportArenaProgress("SKR160", 480000, 98.5f, 82, 82, "S", false);
    REQUIRE(midHost.ok);
    REQUIRE(midHost.room.status == "playing");

    const auto finHost = hostClient.ReportArenaProgress("SKR160", 992500, 99.4f, 186, 186, "SS", true);
    REQUIRE(finHost.ok);
    REQUIRE(finHost.room.status == "playing");

    const auto finGuest = guestClient.ReportArenaProgress("SKR160", 964000, 97.8f, 142, 150, "S", true);
    REQUIRE(finGuest.ok);
    REQUIRE(finGuest.room.status == "finished");

    // 7. FetchArenaRoomState
    const auto state = guestClient.FetchArenaRoomState("SKR160");
    REQUIRE(state.ok);
    REQUIRE(state.room.status == "finished");
    REQUIRE(state.room.players.size() == 2);

    int hostPlacement  = 0;
    int guestPlacement = 0;
    for (const auto& p : state.room.players)
    {
        if (p.username == "HostAoi")
        {
            REQUIRE(p.finished);
            REQUIRE(p.score == 992500);
            REQUIRE(p.grade == "SS");
            hostPlacement = p.placement;
        }
        if (p.username == "RivalRin")
        {
            REQUIRE(p.finished);
            REQUIRE(p.score == 964000);
            REQUIRE(p.grade == "S");
            guestPlacement = p.placement;
        }
    }
    REQUIRE(hostPlacement == 1);
    REQUIRE(guestPlacement == 2);

    REQUIRE(guestClient.LeaveArenaRoom("SKR160"));

    hostClient.Shutdown();
    guestClient.Shutdown();
}
