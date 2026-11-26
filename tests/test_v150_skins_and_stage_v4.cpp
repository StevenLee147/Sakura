// tests/test_v150_skins_and_stage_v4.cpp — v1.5.0 自定义皮肤/打击音包与 Chart v4 舞台演出及同步歌词测试

#include "test_framework.h"

#include "core/skin_manager.h"
#include "game/chart_loader.h"
#include "game/chart_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace sakura::game;
using sakura::core::SkinColor;
using sakura::core::SkinDefinition;
using sakura::core::SkinManager;
using sakura::tests::Matchers::WithinAbs;

namespace
{

struct ScopedV150TempEnv
{
    std::filesystem::path dir;

    ScopedV150TempEnv()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v150_" + std::to_string(stamp));
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }

    ~ScopedV150TempEnv()
    {
        auto& mgr = SkinManager::GetInstance();
        mgr.ScanSkins(std::string(SAKURA_SOURCE_DIR) + "/resources/skins",
                      (dir / "empty_user_skins").string());
        mgr.SetActiveSkin("moonlit_sakura");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("SkinManager GetBuiltinPresets provides 4 distinct skins with expected hitsoundSets", "[v150][skin][presets]")
{
    const auto& presets = SkinManager::GetBuiltinPresets();
    REQUIRE(presets.size() == 4);

    constexpr std::array<std::pair<const char*, const char*>, 4> kExpected{{
        { "moonlit_sakura", "default" },
        { "neon_cyber",     "cyber"   },
        { "esports_pure",   "pure"    },
        { "ukiyo_jade",     "ukiyo"   },
    }};

    std::set<std::string> seenIds;
    for (std::size_t i = 0; i < kExpected.size(); ++i)
    {
        REQUIRE(presets[i].id == kExpected[i].first);
        REQUIRE(presets[i].hitsoundSet == kExpected[i].second);
        REQUIRE(presets[i].isBuiltin);
        REQUIRE(!presets[i].name.empty());
        REQUIRE(!presets[i].author.empty());
        seenIds.insert(presets[i].id);
    }
    REQUIRE(seenIds.size() == 4);
    REQUIRE(!(presets[0].outerGemTop == presets[1].outerGemTop));
    REQUIRE(!(presets[1].circleColor == presets[2].circleColor));
    REQUIRE(!(presets[2].outerGemTop == presets[3].outerGemTop));
}

TEST_CASE("SkinManager ParseSkinJson parses array and hex colors, falls back on missing fields, and rejects malformed JSON", "[v150][skin][parse]")
{
    const char* customJson = R"({
        "id": "aurora_glass",
        "name": "Aurora Glass",
        "author": "QA Charter",
        "hitsound_set": "cyber",
        "outer_gem_top": [12, 34, 56, 240],
        "inner_gem_top": [70, 80, 90],
        "circle_color": "#FF66AA",
        "judge_line": "#112233CC",
        "judge_colors": [
            "#FFE096",
            [10, 20, 30, 250]
        ]
    })";

    auto parsed = SkinManager::ParseSkinJson(customJson, "fallback_id", "userdata/skins/aurora_glass", false);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->id == "aurora_glass");
    REQUIRE(parsed->name == "Aurora Glass");
    REQUIRE(parsed->author == "QA Charter");
    REQUIRE(parsed->hitsoundSet == "cyber");
    REQUIRE(!parsed->isBuiltin);
    REQUIRE((parsed->outerGemTop == SkinColor{ 12, 34, 56, 240 }));
    REQUIRE((parsed->innerGemTop == SkinColor{ 70, 80, 90, 255 }));
    REQUIRE((parsed->circleColor == SkinColor{ 255, 102, 170, 255 }));
    REQUIRE((parsed->judgeLine == SkinColor{ 17, 34, 51, 204 }));
    REQUIRE((parsed->judgeColors[0] == SkinColor{ 255, 224, 150, 255 }));
    REQUIRE((parsed->judgeColors[1] == SkinColor{ 10, 20, 30, 250 }));

    // Missing fields fall back gracefully to default moonlit_sakura palette & fallbackId
    const SkinDefinition& defaultPreset = SkinManager::GetBuiltinPresets().front();
    auto minimal = SkinManager::ParseSkinJson(R"({"name": "Minimal Theme"})", "minimal_theme", "skins/minimal", false);
    REQUIRE(minimal.has_value());
    REQUIRE(minimal->id == "minimal_theme");
    REQUIRE(minimal->name == "Minimal Theme");
    REQUIRE(minimal->hitsoundSet == defaultPreset.hitsoundSet);
    REQUIRE(minimal->thumbGemTop == defaultPreset.thumbGemTop);
    REQUIRE(minimal->sliderCore == defaultPreset.sliderCore);
    REQUIRE(minimal->approachEnd == defaultPreset.approachEnd);
    REQUIRE(minimal->judgeColors[4] == defaultPreset.judgeColors[4]);

    // Malformed or non-object JSON is rejected
    REQUIRE(!SkinManager::ParseSkinJson("{ malformed_json: ").has_value());
    REQUIRE(!SkinManager::ParseSkinJson("[1, 2, 3]").has_value());
    REQUIRE(!SkinManager::ParseSkinJson("").has_value());
}

TEST_CASE("SkinManager ScanSkins loads built-in and custom skins, SetActiveSkin falls back safely, and ResolveCustomHitsoundFile resolves WAV", "[v150][skin][scan]")
{
    ScopedV150TempEnv env;
    const auto userSkinsDir  = env.dir / "user_skins";
    const auto customSkinDir = userSkinsDir / "custom_jade_pack";
    const auto hitsoundsDir  = customSkinDir / "hitsounds";
    const auto brokenSkinDir = userSkinsDir / "broken_pack";

    std::filesystem::create_directories(hitsoundsDir);
    std::filesystem::create_directories(brokenSkinDir);

    {
        std::ofstream ofs(customSkinDir / "skin.json", std::ios::binary);
        ofs << R"({
            "id": "custom_jade_pack",
            "name": "Custom Jade Pack",
            "hitsound_set": "ukiyo",
            "circle_color": "#22EEAA"
        })";
    }
    {
        std::ofstream wav(hitsoundsDir / "tap.wav", std::ios::binary);
        const char dummyRiff[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' };
        wav.write(dummyRiff, sizeof(dummyRiff));
    }
    {
        std::ofstream bad(brokenSkinDir / "skin.json", std::ios::binary);
        bad << "{ broken skin manifest";
    }

    auto& mgr = SkinManager::GetInstance();
    const std::string builtinRoot = std::string(SAKURA_SOURCE_DIR) + "/resources/skins";
    mgr.ScanSkins(builtinRoot, userSkinsDir.string());

    for (const char* builtinId : { "moonlit_sakura", "neon_cyber", "esports_pure", "ukiyo_jade" })
    {
        const SkinDefinition* found = mgr.FindSkin(builtinId);
        REQUIRE(found != nullptr);
        REQUIRE(found->isBuiltin);
        REQUIRE(!found->folderPath.empty());
        REQUIRE(std::filesystem::exists(std::filesystem::path(found->folderPath) / "skin.json"));
    }

    const SkinDefinition* customFound = mgr.FindSkin("custom_jade_pack");
    REQUIRE(customFound != nullptr);
    REQUIRE(!customFound->isBuiltin);
    REQUIRE((customFound->circleColor == SkinColor{ 34, 238, 170, 255 }));
    REQUIRE(mgr.FindSkin("broken_pack") == nullptr);

    // SetActiveSkin switches active skin and falls back safely on unknown ID
    REQUIRE(mgr.SetActiveSkin("neon_cyber"));
    REQUIRE(mgr.GetActiveSkin().id == "neon_cyber");
    REQUIRE(mgr.GetActiveSkin().hitsoundSet == "cyber");

    REQUIRE(!mgr.SetActiveSkin("unknown_nonexistent_skin_id"));
    REQUIRE(mgr.GetActiveSkin().id == "moonlit_sakura");

    // ResolveCustomHitsoundFile resolves hitsounds/tap.wav in active custom skin folder
    REQUIRE(mgr.SetActiveSkin("custom_jade_pack"));
    const std::string resolvedTap = mgr.ResolveCustomHitsoundFile("tap.wav");
    REQUIRE(!resolvedTap.empty());
    REQUIRE(std::filesystem::exists(resolvedTap));
    REQUIRE(mgr.ResolveCustomHitsoundFile("missing_sfx.wav").empty());
}

TEST_CASE("ChartLoader loads v4 stageEvents and lyrics, rejects negative or unsorted stage events, and preserves v1-v3 compatibility", "[v150][chart][v4]")
{
    ChartLoader loader;
    const std::string chartsRoot = std::string(SAKURA_SOURCE_DIR) + "/resources/charts/";

    for (const char* chartId : { "sakura_awakening", "instant_bloom" })
    {
        auto info = loader.LoadChartInfo(chartsRoot + chartId + "/info.json");
        REQUIRE(info.has_value());
        REQUIRE(info->version == 4);
        REQUIRE(!info->difficulties.empty());

        for (const auto& diff : info->difficulties)
        {
            auto data = loader.LoadChartData(info->folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(data->version == 4);
            REQUIRE(loader.ValidateChartData(*data));
            REQUIRE(static_cast<int>(data->keyboardNotes.size()) == diff.noteCount);
            REQUIRE(static_cast<int>(data->mouseNotes.size()) == diff.mouseNoteCount);
            REQUIRE(data->stageEvents.size() >= 6);
            REQUIRE(data->stageEvents.size() <= 10);
            REQUIRE(data->lyrics.size() >= 4);
            REQUIRE(data->lyrics.size() <= 6);
            for (const auto& ly : data->lyrics)
            {
                REQUIRE(ly.endTime > ly.time);
                REQUIRE(!ly.text.empty());
                REQUIRE(!ly.subText.empty());
            }
        }
    }

    // Reject negative stage event time in LoadChartData & ValidateChartData
    ScopedV150TempEnv env;
    const auto negChartPath = env.dir / "invalid_negative_stage.json";
    {
        std::ofstream ofs(negChartPath, std::ios::binary);
        ofs << R"({
            "version": 4,
            "timing_points": [{ "time": 0, "bpm": 128.0, "time_signature": [4, 4] }],
            "sv_points": [],
            "keyboard_notes": [{ "time": 1000, "lane": 0, "type": "tap" }],
            "mouse_notes": [],
            "stage_events": [{ "time": -200, "duration": 600, "type": "pulse", "intensity": 0.5 }]
        })";
    }
    REQUIRE(!loader.LoadChartData(negChartPath.string()).has_value());

    // Reject unsorted stageEvents directly in ValidateChartData
    ChartData unsortedData;
    unsortedData.version = 4;
    unsortedData.timingPoints.push_back({ 0, 128.0f, 4, 4 });
    unsortedData.stageEvents.push_back({ 3000, 500, "pulse", 0.6f, 246, 178, 206 });
    unsortedData.stageEvents.push_back({ 1200, 500, "vignette", 0.4f, 246, 178, 206 });
    REQUIRE(!loader.ValidateChartData(unsortedData));

    ChartData negativeData;
    negativeData.version = 4;
    negativeData.timingPoints.push_back({ 0, 128.0f, 4, 4 });
    negativeData.stageEvents.push_back({ -50, 500, "pulse", 0.5f, 246, 178, 206 });
    REQUIRE(!loader.ValidateChartData(negativeData));

    // Preserve v1 / v2 / v3 backward compatibility
    for (int legacyVer : { 1, 2, 3 })
    {
        const auto legacyPath = env.dir / ("legacy_v" + std::to_string(legacyVer) + ".json");
        {
            std::ofstream ofs(legacyPath, std::ios::binary);
            ofs << "{\"version\": " << legacyVer << R"(,
                "timing_points": [{ "time": 0, "bpm": 120.0, "time_signature": [4, 4] }],
                "sv_points": [],
                "keyboard_notes": [{ "time": 800, "lane": 1, "type": "tap" }],
                "mouse_notes": [{ "time": 1200, "x": 0.5, "y": 0.5, "type": "circle" }]
            })";
        }
        auto legacyLoaded = loader.LoadChartData(legacyPath.string());
        REQUIRE(legacyLoaded.has_value());
        REQUIRE(legacyLoaded->version == legacyVer);
        REQUIRE(loader.ValidateChartData(*legacyLoaded));
        REQUIRE(legacyLoaded->stageEvents.empty());
        REQUIRE(legacyLoaded->lyrics.empty());
    }
}

TEST_CASE("EvaluateStageVisuals and EvaluateActiveLyric compute expected envelopes and respect reducedMotion", "[v150][stage][lyrics]")
{
    ChartData chart;
    chart.version = 4;
    chart.timingPoints.push_back({ 0, 128.0f, 4, 4 });
    chart.stageEvents = {
        { 1000,  800,  "pulse",       0.8f, 252, 198, 222 },
        { 3000,  2000, "tint",        0.65f, 120, 210, 250 },
        { 6000,  2000, "vignette",    0.7f, 140, 150, 230 },
        { 10000, 4000, "petal_burst", 1.4f, 255, 190, 218 },
    };
    chart.lyrics = {
        { 2000, 6000, "夜樱初绽", "Night sakura blooms" },
    };

    // Pulse zoom active when reducedMotion == false, strictly 0.0f when reducedMotion == true
    const StageVisualState pulseNormal  = EvaluateStageVisuals(chart, 1100, false);
    const StageVisualState pulseReduced = EvaluateStageVisuals(chart, 1100, true);
    REQUIRE(pulseNormal.pulseZoom > 0.0f);
    REQUIRE(pulseReduced.pulseZoom == 0.0f);

    // Tint active inside [3000, 5000]
    const StageVisualState midTint = EvaluateStageVisuals(chart, 4000, false);
    REQUIRE(midTint.tintStrength > 0.0f);
    REQUIRE(midTint.tintR == 120);
    REQUIRE(midTint.tintG == 210);
    REQUIRE(midTint.tintB == 250);
    REQUIRE(midTint.vignetteBoost == 0.0f);
    REQUIRE_THAT(midTint.petalRateMult, WithinAbs(1.0, 1e-5));

    // Vignette active inside [6000, 8000]
    const StageVisualState midVignette = EvaluateStageVisuals(chart, 7000, false);
    REQUIRE(midVignette.vignetteBoost > 0.0f);
    REQUIRE(midVignette.tintStrength == 0.0f);

    // Petal burst boosts petalRateMult > 1.0f inside [10000, 14000]
    const StageVisualState midBurst = EvaluateStageVisuals(chart, 12000, false);
    REQUIRE(midBurst.petalRateMult > 1.0f);

    // Outside all windows -> neutral state
    for (int outsideMs : { 400, 2400, 9000, 15000 })
    {
        const StageVisualState idle = EvaluateStageVisuals(chart, outsideMs, false);
        REQUIRE(idle.pulseZoom == 0.0f);
        REQUIRE(idle.tintStrength == 0.0f);
        REQUIRE(idle.vignetteBoost == 0.0f);
        REQUIRE_THAT(idle.petalRateMult, WithinAbs(1.0, 1e-5));
    }

    // EvaluateActiveLyric: inactive before, fade-in, full opacity mid-line, fade-out, inactive after
    const ActiveLyricState before = EvaluateActiveLyric(chart, 1800);
    REQUIRE(!before.active);
    REQUIRE(before.opacity == 0.0f);

    const ActiveLyricState fadeIn = EvaluateActiveLyric(chart, 2090);
    REQUIRE(fadeIn.active);
    REQUIRE(fadeIn.opacity > 0.0f);
    REQUIRE(fadeIn.opacity < 1.0f);

    const ActiveLyricState midLine = EvaluateActiveLyric(chart, 4000);
    REQUIRE(midLine.active);
    REQUIRE_THAT(midLine.opacity, WithinAbs(1.0, 1e-5));
    REQUIRE(midLine.text == "夜樱初绽");
    REQUIRE(midLine.subText == "Night sakura blooms");

    const ActiveLyricState fadeOut = EvaluateActiveLyric(chart, 5890);
    REQUIRE(fadeOut.active);
    REQUIRE(fadeOut.opacity > 0.0f);
    REQUIRE(fadeOut.opacity < 1.0f);

    const ActiveLyricState after = EvaluateActiveLyric(chart, 6200);
    REQUIRE(!after.active);
    REQUIRE(after.opacity == 0.0f);
}
