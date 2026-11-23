// test_v1_stress.cpp — v1.0.0 正式版边界异常与超长高密度谱面压力回归测试

#include "test_framework.h"
#include "core/config.h"
#include "data/database.h"
#include "effects/video_decoder.h"
#include "game/chart_loader.h"
#include "game/chart_utils.h"
#include "game/play_session.h"
#include "game/replay.h"
#include "game/score.h"
#include "utils/archive.h"
#include "utils/file_io.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace sakura::game;

namespace
{
struct StressTempDir
{
    fs::path dir;
    StressTempDir()
        : dir(fs::temp_directory_path() / ("sakura-v1-stress-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::create_directories(dir);
    }
    ~StressTempDir()
    {
        sakura::data::Database::GetInstance().Shutdown();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
} // namespace

TEST_CASE("v1.0 stress: empty chart and zero-note difficulty never grant FC/AP or crash", "[v1][stress][empty]")
{
    ChartData emptyChart;
    emptyChart.version = 3;
    emptyChart.timingPoints = {{0, 140.0f, 4, 4}};

    ChartLoader loader;
    REQUIRE(loader.ValidateChartData(emptyChart));
    REQUIRE(JudgmentCount(emptyChart) == 0);

    PlaySession session;
    session.Initialize(emptyChart, true);
    REQUIRE(session.Total() == 0);
    session.Submit({1000, InputKind::Tick});
    session.Finish();
    REQUIRE(session.Score().GetScore() == 0);
    const auto res = session.Score().GetResult("empty_id", "Empty", "Easy", 0, 1.0f, 1.0);
    REQUIRE(!res.isFullCombo);
    REQUIRE(!res.isAllPerfect);
}

TEST_CASE("v1.0 stress: 1660-note high-density hybrid v3 chart maintains exact 1,000,000 cap", "[v1][stress][high-density]")
{
    ChartData marathon;
    marathon.version = 3;
    marathon.timingPoints = {{0, 180.0f, 4, 4}, {60000, 200.0f, 4, 4}, {120000, 220.0f, 4, 4}};
    marathon.svPoints = {{15000, 1.35f, "ease_out"}, {45000, 0.85f, "linear"}, {90000, 1.50f, "ease_in_out"}};

    int nextLinkId = 1;
    for (int i = 0; i < 1200; ++i)
    {
        const int t = 1000 + i * 150;
        const int lane = (i % 9 == 0) ? kThumbLaneIndex : (i % 4);
        const bool isHold = (i % 10 == 3);
        const int linkId = (i % 4 == 0 && i / 4 < 460) ? nextLinkId++ : 0;
        marathon.keyboardNotes.push_back(KeyboardNote{
            t, lane, isHold ? NoteType::Hold : NoteType::Tap, isHold ? 80 : 0, linkId
        });
    }

    int mouseLinkCursor = 1;
    for (int m = 0; m < 460; ++m)
    {
        const int idxKb = m * 4;
        const int t = 1000 + idxKb * 150;
        MouseNote mn;
        mn.time = t;
        mn.x = 0.20f + 0.60f * static_cast<float>(m % 7) / 6.0f;
        mn.y = 0.20f + 0.60f * static_cast<float>((m * 3) % 7) / 6.0f;
        if (m % 6 == 2)
        {
            mn.type = NoteType::Slider;
            mn.sliderDuration = 320;
            mn.sliderPath = {{
                std::clamp(mn.x + 0.08f, 0.1f, 0.9f),
                std::clamp(mn.y + 0.06f, 0.1f, 0.9f)
            }, {
                std::clamp(mn.x - 0.05f, 0.1f, 0.9f),
                std::clamp(mn.y + 0.10f, 0.1f, 0.9f)
            }};
        }
        else
        {
            mn.type = NoteType::Circle;
            if (m % 5 == 1) mn.requireLane = kThumbLaneIndex;
        }
        if (idxKb < 1200 && marathon.keyboardNotes[static_cast<size_t>(idxKb)].linkId > 0)
            mn.linkId = mouseLinkCursor++;
        marathon.mouseNotes.push_back(std::move(mn));
    }

    REQUIRE(marathon.keyboardNotes.size() + marathon.mouseNotes.size() == 1660);
    ChartLoader loader;
    REQUIRE(loader.ValidateChartData(marathon));

    PlaySession autoSession;
    autoSession.Initialize(marathon, true);
    const int endTime = ChartEndTime(marathon) + 500;
    for (int t = 0; t <= endTime; t += 11)
        autoSession.Submit({t, InputKind::Tick});
    autoSession.Finish();

    REQUIRE(autoSession.Complete());
    REQUIRE(autoSession.Score().GetMissCount() == 0);
    REQUIRE(autoSession.Score().GetScore() == 1000000);
    REQUIRE(autoSession.Score().GetFeverCount() >= 1);
    REQUIRE(autoSession.Score().GetSynergyCount() > 50);
    REQUIRE(autoSession.Score().GetThumbHitCount() > 100);
}

TEST_CASE("v1.0 stress: malformed UTF-8, truncated JSON, and path traversal fail safely", "[v1][stress][corrupt-json]")
{
    StressTempDir tmp;
    ChartLoader loader;

    const auto truncPath = tmp.dir / "truncated.json";
    REQUIRE(sakura::utils::AtomicWrite(truncPath, "{\"version\":3,\"timing_points\":[{\"time\":0,\"bpm\":150"));
    REQUIRE(!loader.LoadChartData(truncPath.string()));

    const auto badUtf8Path = tmp.dir / "bad_utf8.json";
    const std::string invalidUtf8 = {
        '{', '"', 'i', 'd', '"', ':', '"', 'x', '"', ',', '"', 't', 'i', 't', 'l', 'e', '"', ':', '"',
        static_cast<char>(0xFF), static_cast<char>(0xFE), static_cast<char>(0xC0), '"', '}'
    };
    {
        std::ofstream out(badUtf8Path, std::ios::binary);
        out.write(invalidUtf8.data(), static_cast<std::streamsize>(invalidUtf8.size()));
    }
    REQUIRE(!loader.LoadChartInfo(badUtf8Path.string()));

    const auto traversalInfo = tmp.dir / "info.json";
    REQUIRE(sakura::utils::AtomicWrite(traversalInfo,
        R"({"version":3,"id":"trav","title":"T","artist":"A","charter":"C","bpm":120,"offset":0,"music_file":"m.ogg","video_file":"../secret.mp4","difficulties":[{"name":"N","level":4,"chart_file":"n.json"}]})"));
    REQUIRE(!loader.LoadChartInfo(traversalInfo.string()));
}

TEST_CASE("v1.0 stress: corrupted SQLite database automatically backs up to .corrupt.bak and rebuilds", "[v1][stress][sqlite]")
{
    StressTempDir tmp;
    const auto dbPath = tmp.dir / "sakura.db";
    {
        std::ofstream garbage(dbPath, std::ios::binary);
        garbage << "NOT_A_VALID_SQLITE3_HEADER_CORRUPTED_BYTES_0123456789";
    }

    auto& db = sakura::data::Database::GetInstance();
    db.Shutdown();
    REQUIRE(db.Initialize(dbPath.string()));
    REQUIRE(fs::exists(fs::path(dbPath.string() + ".corrupt.bak")));

    GameResult res;
    res.chartId = "recovered_song";
    res.chartTitle = "Recovered";
    res.difficulty = "Expert";
    res.difficultyLevel = 10.5f;
    res.score = 995000;
    res.accuracy = 99.4f;
    res.maxCombo = 420;
    res.grade = Grade::SS;
    REQUIRE(db.SaveScore(res));
    REQUIRE(db.GetTotalPlayCount() == 1);
    REQUIRE(db.GetBestScore("recovered_song", "Expert")->score == 995000);
    db.Shutdown();
}

TEST_CASE("v1.0 stress: missing cover, audio, and video assets degrade gracefully", "[v1][stress][missing-media]")
{
    StressTempDir tmp;
    const auto chartDir = tmp.dir / "missing_assets_chart";
    fs::create_directories(chartDir);

    REQUIRE(sakura::utils::AtomicWrite(chartDir / "info.json",
        R"({"version":3,"id":"missing_assets","title":"Missing Media","artist":"QA","charter":"QA","bpm":128,"offset":0,"music_file":"missing.ogg","cover_file":"missing.png","background_file":"missing_bg.png","video_file":"missing_bga.mp4","difficulties":[{"name":"Normal","level":4.0,"chart_file":"normal.json"}]})"));
    REQUIRE(sakura::utils::AtomicWrite(chartDir / "normal.json",
        R"({"version":3,"timing_points":[{"time":0,"bpm":128}],"keyboard_notes":[{"time":500,"lane":0,"type":"tap"}],"mouse_notes":[]})"));

    ChartLoader loader;
    const auto info = loader.LoadChartInfo((chartDir / "info.json").string());
    REQUIRE(info.has_value());
    REQUIRE(info->videoFile == "missing_bga.mp4");

    sakura::effects::VideoDecoder decoder;
    REQUIRE(!decoder.Open(info->folderPath + "/" + info->videoFile, true));
    REQUIRE(decoder.IsFallbackActive());
}
