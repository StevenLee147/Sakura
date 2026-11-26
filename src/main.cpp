#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include "core/app.h"
#include "core/config.h"
#include "core/paths.h"
#include "game/chart_loader.h"
#include "scene/scene_menu.h"
#include "scene/scene_market.h"
#include "scene/scene_course.h"
#include "scene/scene_arena.h"
#include "game/course.h"
#include "game/ghost_runner.h"
#include "scene/scene_splash.h"
#include "scene/scene_select.h"
#include "game/achievement_manager.h"
#include "scene/scene_settings.h"
#include "scene/scene_game.h"
#include "scene/scene_result.h"
#include "scene/scene_tutorial.h"
#include "scene/scene_stats.h"
#include "scene/scene_editor.h"
#include "scene/scene_chart_wizard.h"
#include "scene/scene_calibration.h"
#include "scene/scene_pause.h"
#include "scene/record_session.h"
#include "data/database.h"
#include "audio/audio_manager.h"
#include "audio/audio_visualizer.h"
#include <fstream>
#include <cmath>
#include "utils/file_io.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>

namespace
{
// SDL_main passes UTF-8 arguments on Windows; keep non-ASCII script/output paths intact.
std::filesystem::path Utf8ArgumentPath(const char* text)
{
    const std::string value(text);
    return std::filesystem::absolute(std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size())));
}

// Reproducible screenshots exercise the real SDL renderer, scenes, assets and
// shutdown sequence. A separate user directory keeps validation out of player data.
class CaptureApp final : public sakura::core::App
{
public:
    explicit CaptureApp(std::filesystem::path directory, bool verify) : m_directory(std::move(directory)) {
        if(verify)for(const auto* name:{"verify_live","verify_replay","verify_practice","verify_album","verify_missing_audio"})m_scenes.emplace_back(name);
    }
    bool succeeded = true;
private:
    std::filesystem::path m_directory;
    std::vector<std::string> m_scenes = {"first_run", "first_run_skip", "menu", "select", "select_practice", "market", "course", "arena", "settings", "settings_audio", "settings_controls", "settings_display", "settings_visual", "settings_data", "tutorial", "stats",
        "editor", "editor_properties", "editor_preview", "editor_return", "create", "create_complete", "calibration", "game", "pause", "game_bga_f3", "result", "result_detail"};
    size_t m_stage = 0;
    bool m_started = false;
    bool m_capture = false;
    float m_time = 0;
    std::vector<float> m_frameTimes;
    std::vector<double> m_cpuTimes;
    nlohmann::json m_manifest = nlohmann::json::array();
    sakura::game::ChartInfo m_chart, m_fixture;
    bool m_action=false, m_retried=false, m_paused=false, m_resuming=false, m_reheld=false,m_resized=false;
    float m_resultWait=0, m_pauseWait=0;
    size_t m_eventCursor=0;
    sakura::game::GameResult m_liveResult;
    std::vector<sakura::game::SessionInput> m_events;
    long long m_initialPlays=0;
    bool m_semantic=true;
    void Key(SDL_Scancode code, SDL_Keymod mod=SDL_KMOD_NONE) { SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=code;e.key.mod=mod;GetSceneManager().HandleEvent(e); }
    void Type(const std::string& value) { SDL_Event e{};e.type=SDL_EVENT_TEXT_INPUT;e.text.text=value.c_str();GetSceneManager().HandleEvent(e); }
    void Click(float x,float y) {
        int w=0,h=0;SDL_GetWindowSize(GetWindow().GetSDLWindow(),&w,&h);
        SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=x*w;e.motion.y=y*h;SDL_PushEvent(&e);
        e={};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=x*w;e.button.y=y*h;SDL_PushEvent(&e);
        e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);
    }
    void Fixture() {
        using namespace sakura::game;
        const auto folder=m_directory/"fixture";std::filesystem::create_directories(folder);
        const nlohmann::json data={{"version",3},{"timing_points",{{{"time",0},{"bpm",120}}}},
            {"keyboard_notes",{{{"time",500},{"lane",0},{"type","tap"}},{{"time",1000},{"lane",1},{"type","hold"},{"duration",1000}},{{"time",2100},{"lane",2},{"type","tap"}},{{"time",2450},{"lane",4},{"type","tap"},{"link_id",1}}}},
            {"mouse_notes",{{{"time",800},{"x",0.5},{"y",0.5},{"type","circle"}},{{"time",1400},{"x",0.35},{"y",0.5},{"type","slider"},{"slider_duration",800},{"slider_path",{{0.65,0.5}}}},{{"time",2450},{"x",0.5},{"y",0.5},{"type","circle"},{"link_id",1},{"require_lane",4}}}}};
        sakura::utils::AtomicWrite(folder/"normal.json",data.dump());
        constexpr int samples=44100*3;std::ofstream wav(folder/"music.wav",std::ios::binary);
        auto u16=[&](uint16_t v){wav.put(static_cast<char>(v));wav.put(static_cast<char>(v>>8));};
        auto u32=[&](uint32_t v){u16(static_cast<uint16_t>(v));u16(static_cast<uint16_t>(v>>16));};
        wav.write("RIFF",4);u32(36+samples*2);wav.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(44100);u32(88200);u16(2);u16(16);wav.write("data",4);u32(samples*2);
        for(int i=0;i<samples;++i)u16(static_cast<uint16_t>(static_cast<int16_t>(std::sin(i*6.2831853*220/44100)*1000)));
        m_fixture.id="qa_fixture";m_fixture.title="Input & Replay Validation";m_fixture.folderPath=folder.string();m_fixture.musicFile="music.wav";
        DifficultyInfo diff;diff.name="Normal";diff.chartFile="normal.json";m_fixture.difficulties={diff};
        m_events={{500,InputKind::LaneDown,0},{540,InputKind::LaneUp,0},{800,InputKind::MouseDown,0,0.5f,0.5f},{840,InputKind::MouseUp},
            {1000,InputKind::LaneDown,1},{1400,InputKind::MouseDown,0,0.35f,0.5f},{2010,InputKind::LaneUp,1},
            {2100,InputKind::LaneDown,2},{2150,InputKind::LaneUp,2},{2185,InputKind::Pointer,0,0.65f,0.5f},{2260,InputKind::MouseUp},
            {2450,InputKind::LaneDown,4},{2452,InputKind::MouseDown,0,0.5f,0.5f},{2490,InputKind::MouseUp},{2495,InputKind::LaneUp,4}};
    }
    void Input(const sakura::game::SessionInput& input) {
        using namespace sakura::game;SDL_Event event{};event.common.timestamp=SDL_GetTicksNS();
        constexpr SDL_Scancode keys[]={SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_D,SDL_SCANCODE_F,SDL_SCANCODE_SPACE};
        if(input.kind==InputKind::LaneDown||input.kind==InputKind::LaneUp){event.type=input.kind==InputKind::LaneDown?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;event.key.scancode=keys[input.lane];}
        else {
            int w=0,h=0;SDL_GetWindowSize(GetWindow().GetSDLWindow(),&w,&h);
            if(input.kind==InputKind::MouseDown||input.kind==InputKind::Pointer){SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.x=(0.45f+input.x*0.5f)*w;motion.motion.y=(0.16f+input.y*0.66f)*h;SDL_PushEvent(&motion);}
            if(input.kind==InputKind::Pointer)return;
            event.type=input.kind==InputKind::MouseDown?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;event.button.button=SDL_BUTTON_LEFT;
            event.button.x=(0.45f+input.x*0.5f)*w;event.button.y=(0.16f+input.y*0.66f)*h;
        }
        SDL_PushEvent(&event);
    }


    void OnUpdate(float dt) override
    {
        auto& manager = GetSceneManager();
        if (!m_started)
        {
            using namespace sakura::scene;
            if (m_chart.id.empty())
            {
                sakura::game::ChartLoader loader;
                auto charts = loader.ScanCharts("resources/charts");
                if (charts.empty()) { succeeded = false; RequestQuit(); return; }
                m_chart = charts.front();
                for (const auto& c : charts) if (c.id == "sakura_awakening") { m_chart = c; break; }
            }
            std::unique_ptr<Scene> scene;
            const auto& name = m_scenes[m_stage];
            if (name == "first_run") {
                sakura::core::Config::GetInstance().Set("tutorial.prompt_shown",false);
                scene=std::make_unique<SceneSplash>(manager);
            }
            if (name == "menu") scene = std::make_unique<SceneMenu>(manager);
            if (name == "select" || name=="select_practice") scene = std::make_unique<SceneSelect>(manager);
            if (name == "market") scene = std::make_unique<SceneMarket>(manager);
            if (name == "course") scene = std::make_unique<SceneCourse>(manager);
            if (name == "arena") scene = std::make_unique<SceneArena>(manager);
            if (name.starts_with("settings")) scene = std::make_unique<SceneSettings>(manager);
            if (name == "tutorial") scene = std::make_unique<SceneTutorial>(manager);
            if (name == "stats") scene = std::make_unique<SceneStats>(manager);
            if (name.starts_with("editor") && name != "editor_return") scene = std::make_unique<SceneEditor>(manager, m_chart.folderPath, m_chart.difficulties[0].chartFile);
            if (name == "create" || name == "create_complete") scene = std::make_unique<SceneChartWizard>(manager);
            if (name == "calibration") scene = std::make_unique<SceneCalibration>(manager);
            if (name == "game")
            {
                sakura::game::PlayOptions options;
                options.mode = sakura::game::PlayMode::AutoPlay;
                scene = std::make_unique<SceneGame>(manager, m_chart, 0, options);
            }
            if (name == "game_bga_f3")
            {
                sakura::game::ChartLoader loader;
                auto charts = loader.ScanCharts("resources/charts");
                sakura::game::ChartInfo bgaChart = m_chart;
                for (const auto& c : charts) if (c.id == "sakura_breath" || !c.videoFile.empty()) { bgaChart = c; break; }
                sakura::game::PlayOptions options;
                options.mode = sakura::game::PlayMode::AutoPlay;
                scene = std::make_unique<SceneGame>(manager, bgaChart, 0, options);
            }
            if (name == "pause")
            {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = SDL_SCANCODE_ESCAPE;
                manager.HandleEvent(event);
            }
            if (name == "result" || name == "result_detail")
            {
                sakura::game::GameResult result;
                result.chartId = m_chart.id; result.chartTitle = m_chart.title;
                result.difficulty = m_chart.difficulties[0].name;
                result.difficultyLevel = m_chart.difficulties[0].level;
                result.score = 982430; result.accuracy = 98.42f; result.grade = sakura::game::Grade::S;
                result.perfectCount = 187; result.greatCount = 10; result.goodCount = 2; result.missCount = 1;
                result.maxCombo = 124; result.playTimeSeconds = 75.0; result.assisted = true;
                for (int i = 0; i < 150; ++i) {
                    const int err = (i * 17 % 67) - 27;
                    result.hitErrors.push_back(err);
                    const auto jr = (i == 92) ? sakura::game::JudgeResult::Miss : sakura::game::JudgeResult::Perfect;
                    const int cb = (i < 92) ? (i + 1) : (i == 92 ? 0 : (i - 92));
                    result.samples.push_back({ i * 500, err, jr, (i % 4 != 3), i % 5, (i % 7 == 0), cb, true });
                }
                scene = std::make_unique<SceneResult>(manager, result, m_chart);
            }
            if(name.starts_with("verify_")){
                sakura::game::PlayOptions options;
                if(m_fixture.id.empty()){Fixture();m_initialPlays=sakura::data::Database::GetInstance().GetTotalPlayCount();}
                if(name=="verify_replay"){options.mode=sakura::game::PlayMode::Replay;options.replayFile=m_liveResult.replayFile;}
                if(name=="verify_practice"){options.mode=sakura::game::PlayMode::Practice;options.startMs=900;options.endMs=2050;options.rate=0.75f;}
                if(name=="verify_album"){options.mode=sakura::game::PlayMode::AutoPlay;options.rate=2;}
                auto chart=name=="verify_album"?m_chart:m_fixture;
                if(name=="verify_missing_audio")chart.musicFile="missing.wav";
                scene=std::make_unique<SceneGame>(manager,chart,0,options);
                m_resultWait=0;m_eventCursor=0;
            }
            if (scene) manager.SwitchScene(std::move(scene), TransitionType::None);
            m_started = true;
            m_time = 0; m_action=false; m_semantic=true;
            m_frameTimes.clear();
            m_cpuTimes.clear();
        }
        m_time += dt;
        if (m_time > 0.5f) {m_frameTimes.push_back(dt * 1000.0f);m_cpuTimes.push_back(GetCpuFrameMs());}
        const auto& name=m_scenes[m_stage];
        if(!m_action && m_time>0.3f && !manager.IsTransitioning()){
            m_action=true;
            if(name.starts_with("settings_")){
                int tabs=name=="settings_audio"?1:name=="settings_controls"?2:name=="settings_display"?3:name=="settings_visual"?4:5;
                for(int i=0;i<tabs;++i)Key(SDL_SCANCODE_TAB);
            }
            if(name=="select_practice")Click(0.73f,0.615f);
            if(name=="editor_properties")Click(0.585f,0.852f);
            if(name=="editor_preview")Key(SDL_SCANCODE_F5);
            if(name=="result_detail")Key(SDL_SCANCODE_TAB);
            if(name=="game_bga_f3"){SDL_Event f3{};f3.type=SDL_EVENT_KEY_DOWN;f3.key.scancode=SDL_SCANCODE_F3;SDL_PushEvent(&f3);}
            if(name=="editor_return" || name=="first_run_skip")Key(SDL_SCANCODE_ESCAPE);
            if(name=="create_complete"){
                const std::array<std::string,9> fields={"新建验证","Sakura QA","123","25","普通",
                    std::filesystem::absolute(std::filesystem::path(m_chart.folderPath)/m_chart.musicFile).string(),"","",
                    sakura::core::Paths::User("charts/新建验证")};
                for(size_t i=0;i<fields.size();++i){Key(SDL_SCANCODE_A,SDL_KMOD_CTRL);Type(fields[i]);if(i+1<fields.size())Key(SDL_SCANCODE_TAB);}
                Key(SDL_SCANCODE_RETURN);
            }
        }
        if(name.starts_with("verify_")) {
            using namespace sakura::game;using namespace sakura::scene;
            if(auto* game=dynamic_cast<SceneGame*>(manager.GetCurrentScene());game && !manager.IsTransitioning()){
                if(name=="verify_live"){
                    if(!m_retried && m_time>0.6f){Key(SDL_SCANCODE_R);m_retried=true;return;}
                    if(game->State().IsPlaying()){
                        const int now=game->State().GetCurrentTime();
                        if(!m_resized && now>=1700){SDL_SetWindowSize(GetWindow().GetSDLWindow(),1280,800);m_resized=true;}
                        if(!m_paused&&now>=1600){Key(SDL_SCANCODE_ESCAPE);m_paused=true;return;}
                        if(m_resuming&&!m_reheld){Input({now,InputKind::LaneDown,1});Input({now,InputKind::MouseDown,0,0.35f,0.5f});m_reheld=true;}
                        while(m_eventCursor<m_events.size()&&m_events[m_eventCursor].time<=now)Input(m_events[m_eventCursor++]);
                    }
                }
            }
            if(name=="verify_live"&&dynamic_cast<ScenePause*>(manager.GetCurrentScene())){
                m_pauseWait+=dt;if(m_pauseWait>0.5f&&!manager.IsTransitioning()){Key(SDL_SCANCODE_ESCAPE);m_resuming=true;}
            }
            if(auto* result=dynamic_cast<SceneResult*>(manager.GetCurrentScene());result&&!manager.IsTransitioning()){
                m_resultWait+=dt;
                if(m_resultWait>1.6f){
                    const auto& r=result->Result();
                    if(name=="verify_live"){m_liveResult=r;m_semantic=r.isFullCombo&&r.missCount==0&&r.score>=900000&&r.totalJudgments==9&&r.synergyCount>=1&&r.thumbHitCount>=1&&!r.replayFile.empty()&&m_reheld;}
                    if(name=="verify_replay")m_semantic=r.score==m_liveResult.score&&r.hitErrors==m_liveResult.hitErrors&&r.assisted;
                    if(name=="verify_practice")m_semantic=r.totalJudgments==2&&r.assisted; // only complete hold in this range
                    if(name=="verify_album")m_semantic=r.score==1000000&&r.missCount==0&&r.assisted;
                    m_semantic &= sakura::data::Database::GetInstance().GetTotalPlayCount()==m_initialPlays+1;
                    m_capture=true;
                }
            }
            if(name=="verify_missing_audio" && m_time>2.0f){m_semantic=dynamic_cast<SceneSelect*>(manager.GetCurrentScene())!=nullptr;m_capture=true;}
            if(m_time>160){m_semantic=false;m_capture=true;}
        } else {
            const float duration = name=="game" ? 9.6f : name=="game_bga_f3" ? 4.6f : name=="first_run"?6.5f:name=="result"?1.8f:1.3f;
            if(m_time>duration&&!manager.IsTransitioning())m_capture=true;
        }
    }

    void OnRender() override
    {
        if (!m_capture) return;
        m_capture = false;
        auto* surface = SDL_RenderReadPixels(GetRenderer().GetSDLRenderer(), nullptr);
        const auto file = m_directory / (m_scenes[m_stage] + ".png");
        const bool saved = surface && IMG_SavePNG(surface, file.string().c_str());
        if (surface) SDL_DestroySurface(surface);
        const auto& name=m_scenes[m_stage];
        auto* current=GetSceneManager().GetCurrentScene();
        using namespace sakura::scene;
        if(name=="first_run")m_semantic=dynamic_cast<SceneTutorial*>(current)!=nullptr;
        if(name=="first_run_skip")m_semantic=dynamic_cast<SceneMenu*>(current)&&sakura::core::Config::GetInstance().Get<bool>("tutorial.prompt_shown",false);
        if(name=="menu")m_semantic=dynamic_cast<SceneMenu*>(current)!=nullptr;
        if(name.starts_with("select")) {const auto* select=dynamic_cast<SceneSelect*>(current);m_semantic=select && (name!="select_practice" || select->CurrentMode()==sakura::game::PlayMode::Practice);}
        if(name=="market")m_semantic=dynamic_cast<SceneMarket*>(current)!=nullptr;
        if(name=="course")m_semantic=dynamic_cast<SceneCourse*>(current)!=nullptr;
        if(name=="arena")m_semantic=dynamic_cast<SceneArena*>(current)!=nullptr;
        if(name.starts_with("settings")){const auto* settings=dynamic_cast<SceneSettings*>(current);int page=name=="settings"?0:name=="settings_audio"?1:name=="settings_controls"?2:name=="settings_display"?3:name=="settings_visual"?4:5;m_semantic=settings&&settings->CurrentPage()==page;}
        if(name=="game"){const auto* game=dynamic_cast<SceneGame*>(current);m_semantic=game&&game->State().IsPlaying()&&game->Session().Score().GetJudgedCount()>0;}
        if(name=="game_bga_f3"){const auto* game=dynamic_cast<SceneGame*>(current);m_semantic=game&&game->State().IsPlaying()&&IsDiagnosticsOverlayVisible();SetDiagnosticsOverlayVisible(false);}
        if(name=="pause")m_semantic=dynamic_cast<ScenePause*>(current)!=nullptr;
        if(name=="editor_properties"){const auto* editor=dynamic_cast<SceneEditor*>(current);m_semantic=editor&&editor->PropertyDialogOpen();}
        if(name=="editor_preview"){const auto* game=dynamic_cast<SceneGame*>(current);m_semantic=game&&game->State().GetOptions().returnToEditor;}
        if(name=="editor_return")m_semantic=dynamic_cast<SceneEditor*>(current)!=nullptr;
        if(name=="create_complete"){
            sakura::game::ChartLoader loader;
            const auto charts=loader.ScanCharts(sakura::core::Paths::User("charts"));
            m_semantic=dynamic_cast<SceneEditor*>(current)&&std::any_of(charts.begin(),charts.end(),[](const auto& chart){
                return chart.title=="新建验证" && chart.id=="新建验证" && chart.bpm==123 && chart.offset==25 &&
                    chart.coverFile.empty() && chart.backgroundFile.empty() &&
                    std::filesystem::exists(std::filesystem::path(chart.folderPath)/chart.musicFile);
            });
        }
        if(name=="result")m_semantic=dynamic_cast<SceneResult*>(current)!=nullptr;
        succeeded &= saved && m_semantic;
        std::sort(m_frameTimes.begin(), m_frameTimes.end());
        std::sort(m_cpuTimes.begin(),m_cpuTimes.end());
        const double cpu95=m_cpuTimes.empty()?0:m_cpuTimes[static_cast<size_t>((m_cpuTimes.size()-1)*0.95)];
        const float p95 = m_frameTimes.empty() ? 0 : m_frameTimes[static_cast<size_t>((m_frameTimes.size() - 1) * 0.95)];
        m_manifest.push_back({{"scene", m_scenes[m_stage]}, {"saved", saved}, {"verified",m_semantic}, {"frame_ms_p95", p95},
            {"width", GetRenderer().GetScreenWidth()}, {"height", GetRenderer().GetScreenHeight()}, {"cpu_submission_ms_p95",cpu95},
            {"renderer", SDL_GetRendererName(GetRenderer().GetSDLRenderer())}});
        if (++m_stage >= m_scenes.size())
        {
            succeeded &= sakura::utils::AtomicWrite(m_directory / "manifest.json", m_manifest.dump(2));
            RequestQuit();
        }
        else m_started = false;
    }
};
}

#include "utils/archive.h"
#include "net/api_client.h"
#include "core/updater.h"
#include "game/replay.h"

namespace
{
bool RunVerifyOnline(const std::string& baseUrl)
{
    using namespace sakura::game;
    namespace fs = std::filesystem;

    std::error_code tmpEc;
    const fs::path e2eDir = fs::temp_directory_path(tmpEc) / "sakura_e2e_v110";
    fs::remove_all(e2eDir, tmpEc);
    fs::create_directories(e2eDir, tmpEc);
    sakura::core::Config::GetInstance().Load((e2eDir / "settings.json").string());
    sakura::net::APIClient api(nullptr, (e2eDir / "queue.json").string());
    api.LoadFromConfig();
    api.SetServerUrl(baseUrl);

    if (!api.CheckHealth()) return false;

    const auto upd = sakura::core::Updater::GetInstance().CheckNow("v0.9.1-beta", nullptr, baseUrl + "/api/v1/version");
    if (!upd.ok || !upd.hasUpdate) return false;

    const auto guest = api.LoginGuest("SakuraGuest");
    if (!guest.ok) return false;

    const std::string uname = "E2E_" + std::to_string(SDL_GetTicksNS() % 100000000ULL);
    const auto reg = api.Register(uname, "SakuraPass123!");
    if (!reg.ok) return false;
    const auto login = api.Login(uname, "SakuraPass123!");
    if (!login.ok || !api.IsAuthenticated()) return false;
    const auto me = api.RefreshProfile();
    if (!me.ok || me.user.username != uname) return false;

    // Construct deterministic Chart v3 fixture and drive PlaySession with real human inputs
    ChartData chart;
    chart.version = 3;
    chart.timingPoints = { { 0, 120.0f, 4, 4 } };
    KeyboardNote k0; k0.time = 500;  k0.lane = 0; k0.type = NoteType::Tap;
    KeyboardNote k1; k1.time = 1000; k1.lane = 1; k1.type = NoteType::Hold; k1.duration = 1000;
    KeyboardNote k2; k2.time = 2100; k2.lane = 2; k2.type = NoteType::Tap;
    KeyboardNote k3; k3.time = 2450; k3.lane = 4; k3.type = NoteType::Tap; k3.linkId = 1;
    chart.keyboardNotes = { k0, k1, k2, k3 };

    MouseNote m0; m0.time = 800;  m0.x = 0.5f;  m0.y = 0.5f; m0.type = NoteType::Circle;
    MouseNote m1; m1.time = 1400; m1.x = 0.35f; m1.y = 0.5f; m1.type = NoteType::Slider;
    m1.sliderDuration = 800; m1.sliderPath = { { 0.65f, 0.5f } };
    MouseNote m2; m2.time = 2450; m2.x = 0.5f;  m2.y = 0.5f; m2.type = NoteType::Circle;
    m2.linkId = 1; m2.requireLane = 4;
    chart.mouseNotes = { m0, m1, m2 };

    const std::string chartHash = Replay::Hash(chart, 0);
    const std::vector<SessionInput> script = {
        { 500, InputKind::LaneDown, 0 }, { 540, InputKind::LaneUp, 0 },
        { 800, InputKind::MouseDown, 0, 0.5f, 0.5f }, { 840, InputKind::MouseUp },
        { 1000, InputKind::LaneDown, 1 }, { 1400, InputKind::MouseDown, 0, 0.35f, 0.5f },
        { 2005, InputKind::Tick }, { 2010, InputKind::LaneUp, 1 },
        { 2100, InputKind::LaneDown, 2 }, { 2150, InputKind::LaneUp, 2 },
        { 2185, InputKind::Pointer, 0, 0.65f, 0.5f }, { 2205, InputKind::Tick },
        { 2260, InputKind::MouseUp }, { 2450, InputKind::LaneDown, 4 },
        { 2452, InputKind::MouseDown, 0, 0.5f, 0.5f }, { 2490, InputKind::MouseUp },
        { 2495, InputKind::LaneUp, 4 }
    };

    ChartData liveChart = chart;
    PlaySession liveSession;
    liveSession.Initialize(liveChart, false, 0, 0);
    for (const auto& ev : script) liveSession.Submit(ev);
    liveSession.Finish();

    GameResult liveResult = liveSession.Score().GetResult("sakura_breath", "Sakura Breath", "Expert", 2, 10.5f, 3.0);
    liveResult.chartHash      = chartHash;
    liveResult.totalJudgments = liveSession.Score().GetJudgedCount();
    liveResult.assisted       = false;

    const fs::path replayOut = fs::path(sakura::core::Paths::User("replays")) / "e2e_live.skr";
    std::error_code ec;
    fs::create_directories(replayOut.parent_path(), ec);
    Replay recorded;
    recorded.chartId   = "sakura_breath";
    recorded.chartHash = chartHash;
    recorded.difficulty = 2;
    recorded.rate      = 1.0f;
    recorded.startMs   = 0;
    recorded.inputs    = liveSession.Recording();
    if (!recorded.Save(replayOut.string())) return false;
    liveResult.replayFile = replayOut.string();

    // Assisted score must be rejected by server
    GameResult assistedResult = liveResult;
    assistedResult.assisted = true;
    // Direct server check via unassisted vs tampered
    GameResult tampered = liveResult;
    tampered.score = 1200000;
    if (api.SubmitScore(tampered).ok) return false;

    // Valid unassisted score + .skr upload
    const auto submitted = api.SubmitScore(liveResult);
    if (!submitted.ok || submitted.rank != 1 || submitted.pp <= 0.0 || submitted.scoreId.empty())
        return false;

    // Fetch leaderboard and verify #1 entry & myRank
    const auto lb = api.FetchLeaderboard("sakura_breath", "Expert", 10);
    if (!lb.ok || !lb.myRank.has_value() || *lb.myRank != 1 || lb.entries.empty())
        return false;
    if (lb.entries[0].username != uname || lb.entries[0].score != liveResult.score || !lb.entries[0].hasReplay)
        return false;

    // Download replay and verify deterministic frame-accurate playback parity
    const fs::path dlPath = fs::path(sakura::core::Paths::User("replays")) / "e2e_downloaded.skr";
    if (!api.DownloadReplay(submitted.scoreId, dlPath.string()))
        return false;
    const auto dlReplay = Replay::Load(dlPath.string());
    if (!dlReplay || dlReplay->chartHash != chartHash)
        return false;

    ChartData replayChart = chart;
    PlaySession replaySession;
    replaySession.Initialize(replayChart, false, 0, 0);
    for (const auto& ev : dlReplay->inputs) replaySession.Submit(ev);
    replaySession.Finish();

    if (replaySession.Score().GetScore() != liveResult.score ||
        replaySession.Score().GetMaxCombo() != liveResult.maxCombo ||
        replaySession.Score().GetHitErrors() != liveResult.hitErrors)
    {
        return false;
    }

    // v1.2.0 E2E: Export & publish .sakura -> Market search & rate -> Fresh user install -> Play & rank -> Cloud Sync restore
    const std::string mktSlug = "e2e_bloom_" + std::to_string(SDL_GetTicksNS() % 100000000ULL);
    const fs::path srcPkgDir  = e2eDir / "workshop_chart";
    const fs::path pkgArchive = e2eDir / (mktSlug + ".sakura");
    fs::remove_all(srcPkgDir, ec);
    fs::create_directories(srcPkgDir, ec);

    const nlohmann::json pkgInfo = {
        { "version", 3 },
        { "id", mktSlug },
        { "title", "E2E Blossom " + mktSlug },
        { "artist", "Sakura Atelier" },
        { "charter", uname },
        { "music_file", "music.wav" },
        { "bpm", 120.0 },
        { "tags", { "e2e", "synergy" } },
        { "difficulties", nlohmann::json::array({
            { { "name", "Expert" }, { "level", 10.8 }, { "chart_file", "expert.json" } }
        }) }
    };
    const nlohmann::json pkgDiff = {
        { "version", 3 },
        { "timing_points", nlohmann::json::array({ { { "time", 0 }, { "bpm", 120.0 } } }) },
        { "keyboard_notes", nlohmann::json::array({
            { { "time", 500 }, { "lane", 0 }, { "type", "tap" } },
            { { "time", 1000 }, { "lane", 1 }, { "type", "hold" }, { "duration", 1000 } },
            { { "time", 2100 }, { "lane", 2 }, { "type", "tap" } },
            { { "time", 2450 }, { "lane", 4 }, { "type", "tap" }, { "link_id", 1 } }
        }) },
        { "mouse_notes", nlohmann::json::array({
            { { "time", 800 }, { "x", 0.5 }, { "y", 0.5 }, { "type", "circle" } },
            { { "time", 1400 }, { "x", 0.35 }, { "y", 0.5 }, { "type", "slider" }, { "slider_duration", 800 }, { "slider_path", nlohmann::json::array({ nlohmann::json::array({ 0.65, 0.5 }) }) } },
            { { "time", 2450 }, { "x", 0.5 }, { "y", 0.5 }, { "type", "circle" }, { "link_id", 1 }, { "require_lane", 4 } }
        }) }
    };
    if (!sakura::utils::AtomicWrite(srcPkgDir / "info.json", pkgInfo.dump(2))) return false;
    if (!sakura::utils::AtomicWrite(srcPkgDir / "expert.json", pkgDiff.dump(2))) return false;
    {
        constexpr int samples = 44100;
        std::ofstream wav(srcPkgDir / "music.wav", std::ios::binary);
        auto u16 = [&](uint16_t v) { wav.put(static_cast<char>(v & 0xFF)); wav.put(static_cast<char>((v >> 8) & 0xFF)); };
        auto u32 = [&](uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>((v >> 16) & 0xFFFF)); };
        wav.write("RIFF", 4); u32(36 + samples * 2); wav.write("WAVEfmt ", 8);
        u32(16); u16(1); u16(1); u32(44100); u32(88200); u16(2); u16(16);
        wav.write("data", 4); u32(samples * 2);
        for (int i = 0; i < samples; ++i) u16(0);
    }
    if (!sakura::utils::CreateZipArchive(srcPkgDir, pkgArchive)) return false;

    auto failStep = [](const char* step, const std::string& detail = {}) {
        std::ofstream f(sakura::core::Paths::User("verify-online-fail.txt"), std::ios::app);
        f << "[verify-online] FAIL at " << step << ": " << detail << "\n";
        return false;
    };

    const auto pubRes = api.PublishChartPackage(pkgArchive.string(), "Automated v1.2.0 E2E package");
    if (!pubRes.ok || pubRes.chart.id.empty()) return failStep("PublishChartPackage", pubRes.error);

    sakura::net::MarketQuery mq;
    mq.q        = mktSlug;
    mq.minLevel = 9.0f;
    mq.maxLevel = 15.0f;
    const auto mktList = api.FetchMarketCharts(mq);
    if (!mktList.ok || mktList.charts.empty() || mktList.charts[0].id != pubRes.chart.id)
        return failStep("FetchMarketCharts", mktList.error);

    const auto rateRes = api.RateMarketChart(pubRes.chart.id, 5, "E2E verified 5 stars");
    if (!rateRes.ok || rateRes.myRating != 5) return failStep("RateMarketChart", rateRes.error);

    const fs::path freshChartsDir = e2eDir / "fresh_user_charts";
    fs::remove_all(freshChartsDir, ec);
    float lastProgress = 0.0f;
    const auto dlRes = api.DownloadAndInstallChart(mktList.charts[0], freshChartsDir.string(),
        [&](float p) { lastProgress = std::max(lastProgress, p); });
    if (!dlRes.ok || lastProgress < 0.99f)
        return failStep("DownloadAndInstallChart", dlRes.error + " prog=" + std::to_string(lastProgress));
    if (sakura::net::APIClient::GetInstalledMarketState(mktList.charts[0], freshChartsDir.string()) != sakura::net::MarketInstallState::Installed)
        return failStep("GetInstalledMarketState");

    ChartLoader mktLoader;
    const auto installedCharts = mktLoader.ScanCharts(freshChartsDir.string());
    if (installedCharts.size() != 1 || installedCharts[0].id != mktSlug)
        return failStep("ScanCharts", "count=" + std::to_string(installedCharts.size()));
    auto loadedDiff = mktLoader.LoadChartData(installedCharts[0].folderPath + "/" + installedCharts[0].difficulties[0].chartFile);
    if (!loadedDiff) return failStep("LoadChartData");

    PlaySession mktSession;
    mktSession.Initialize(*loadedDiff, false, 0, 0);
    for (const auto& ev : script) mktSession.Submit(ev);
    mktSession.Finish();

    GameResult mktResult = mktSession.Score().GetResult(mktSlug, installedCharts[0].title, "Expert", 0, 10.8f, 3.0);
    mktResult.chartHash      = Replay::Hash(*loadedDiff, 0);
    mktResult.totalJudgments = mktSession.Score().GetJudgedCount();
    mktResult.assisted       = false;
    const auto mktSub = api.SubmitScore(mktResult);
    if (!mktSub.ok || mktSub.rank != 1) return failStep("SubmitMarketScore", mktSub.error);

    // Multi-device Cloud Sync push on Device A -> pull restore on clean Device B
    auto& db  = sakura::data::Database::GetInstance();
    auto& cfg = sakura::core::Config::GetInstance();
    db.Shutdown();
    const fs::path dbA = e2eDir / "device_a.db";
    const fs::path dbB = e2eDir / "device_b.db";
    fs::remove(dbA, ec); fs::remove(dbB, ec);
    if (!db.Initialize(dbA.string())) return failStep("InitDbA");
    if (!db.SaveScore(mktResult)) return failStep("SaveScoreDbA");
    if (!db.SaveAchievement("first_fc")) return failStep("SaveAchDbA");
    cfg.Set("gameplay.note_speed", 7.6f);
    cfg.Set("audio.global_offset_ms", -15);
    cfg.Set("online.settings_updated_at", static_cast<long long>(std::time(nullptr)) + 10LL);
    const auto syncPush = api.SyncCloudSave();
    if (!syncPush.ok) return failStep("SyncPush", syncPush.error);

    db.Shutdown();
    if (!db.Initialize(dbB.string())) return failStep("InitDbB");
    cfg.Set("gameplay.note_speed", 5.0f);
    cfg.Set("audio.global_offset_ms", 0);
    cfg.Set("online.settings_updated_at", 0LL);
    const auto syncPull = api.SyncCloudSave();
    if (!syncPull.ok || syncPull.scoresPulled < 1 || syncPull.achievementsPulled < 1)
        return failStep("SyncPull", syncPull.error + " sp=" + std::to_string(syncPull.scoresPulled) + " ap=" + std::to_string(syncPull.achievementsPulled));
    const auto restoredBest = db.GetBestScore(mktSlug, "Expert", mktResult.chartHash);
    if (!restoredBest || restoredBest->score != mktResult.score || !db.IsAchievementUnlocked("first_fc"))
        return failStep("VerifyRestoredDbB");
    if (std::abs(cfg.Get<float>("gameplay.note_speed", 5.0f) - 7.6f) > 0.05f ||
        cfg.Get<int>("audio.global_offset_ms", 0) != -15)
    {
        return failStep("VerifyRestoredSettings");
    }
    db.Shutdown();

    // v1.4.0 E2E: Daily Challenge parity + daily leaderboard rank + Course Dan badge & Collections sync
    const auto dailyInfo    = api.FetchDailyChallenge("2026-11-26", 10);
    const auto offlineDaily = ComputeOfflineDailyChallenge("2026-11-26");
    if (!dailyInfo.ok || dailyInfo.offlineFallback || dailyInfo.spec.chartId != offlineDaily.chartId)
        return failStep("FetchDailyChallenge", dailyInfo.error + " chart=" + dailyInfo.spec.chartId);

    GameResult validDailyGameResult;
    validDailyGameResult.chartId         = dailyInfo.spec.chartId;
    validDailyGameResult.chartTitle      = dailyInfo.spec.chartTitle.empty() ? dailyInfo.spec.chartId : dailyInfo.spec.chartTitle;
    validDailyGameResult.chartHash       = "e2e_daily_hash";
    validDailyGameResult.difficulty      = dailyInfo.spec.difficultyName;
    validDailyGameResult.difficultyLevel = dailyInfo.spec.difficultyLevel;
    validDailyGameResult.score           = 988000;
    validDailyGameResult.accuracy        = 99.2f;
    validDailyGameResult.maxCombo        = 100;
    validDailyGameResult.grade           = Grade::S;
    validDailyGameResult.perfectCount    = 99;
    validDailyGameResult.greatCount      = 1;
    validDailyGameResult.goodCount       = 0;
    validDailyGameResult.badCount        = 0;
    validDailyGameResult.missCount       = 0;
    validDailyGameResult.totalJudgments  = 100;
    validDailyGameResult.isFullCombo     = true;
    validDailyGameResult.isAllPerfect    = false;
    validDailyGameResult.assisted        = false;
    validDailyGameResult.playbackRate    = 1.0f;
    const auto dailySub = api.SubmitDailyScore("2026-11-26", validDailyGameResult);
    if (!dailySub.ok || dailySub.rank != 1)
        return failStep("SubmitDailyScore", dailySub.error + " rank=" + std::to_string(dailySub.rank));

    const fs::path dbV140 = e2eDir / "device_v140.db";
    fs::remove(dbV140, ec);
    if (!db.Initialize(dbV140.string())) return failStep("InitDbV140");
    const CourseRecord kaidenRec{ "dan_kaiden", "皆传", 10, 3920000, 99.1f, 84.0f, true, 1795600000LL };
    if (!db.SaveCourseRecord(kaidenRec)) return failStep("SaveCourseRecordV140");

    std::vector<SongCollection> e2eCols = CollectionManager::LoadFromConfig();
    const SongCollection e2eCol = CollectionManager::CreateCollection(e2eCols, "E2E_Col", 1795600000LL);
    CollectionManager::ToggleChartInCollection(e2eCols, e2eCol.id, "sakura_breath", 1795600001LL);
    CollectionManager::SaveToConfig(e2eCols);

    const auto syncV140 = api.SyncCloudSave();
    if (!syncV140.ok) return failStep("SyncCloudSaveV140", syncV140.error);
    const auto profV140 = api.RefreshProfile();
    if (!profV140.ok || profV140.user.danBadge != "皆传")
        return failStep("RefreshProfileDanBadge", profV140.error + " badge=" + profV140.user.danBadge);
    db.Shutdown();

    // v1.6.0 E2E: GhostRunner deterministic parity + 2-player Arena room match
    GhostRunner ghost;
    if (!ghost.InitializeFromFile(chart, dlPath.string(), "OnlineRival"))
        return failStep("GhostInitializeFromFile");
    GhostSnapshot ghostSnap;
    for (int t = 0; t <= 2600; t += 20)
        ghostSnap = ghost.AdvanceTo(t, liveResult.score);
    if (!ghostSnap.active || ghostSnap.ghostScore != liveResult.score || ghostSnap.deltaScore != 0)
        return failStep("GhostAdvanceTo", "ghost=" + std::to_string(ghostSnap.ghostScore) + " live=" + std::to_string(liveResult.score));

    sakura::net::APIClient apiB(nullptr, (e2eDir / "queue_rival.json").string());
    apiB.SetServerUrl(baseUrl);
    const std::string rivalName = uname + "_Rival";
    const auto regB = apiB.Register(rivalName, "SakuraPass123!");
    if (!regB.ok) return failStep("ArenaRegisterRival", regB.error);
    const auto loginB = apiB.Login(rivalName, "SakuraPass123!");
    if (!loginB.ok || !apiB.IsAuthenticated()) return failStep("ArenaLoginRival", loginB.error);

    const auto createdRoom = api.CreateArenaRoom("E2E Arena Room", "sakura_awakening", "樱光初醒", "Hard", 7.0f, chartHash);
    if (!createdRoom.ok || createdRoom.room.code.empty())
        return failStep("CreateArenaRoom", createdRoom.error);
    const std::string roomCode = createdRoom.room.code;

    const auto listedRooms = apiB.ListArenaRooms();
    const bool foundRoom = listedRooms.ok && std::any_of(
        listedRooms.rooms.begin(), listedRooms.rooms.end(),
        [&](const sakura::net::ArenaRoomState& r) { return r.code == roomCode; });
    if (!foundRoom)
        return failStep("ListArenaRooms", listedRooms.error);

    const auto joinedRoom = apiB.JoinArenaRoom(roomCode);
    if (!joinedRoom.ok || joinedRoom.room.players.size() < 2)
        return failStep("JoinArenaRoom", joinedRoom.error);

    const auto unreadyRes = apiB.SetArenaReady(roomCode, false, false);
    if (!unreadyRes.ok)
        return failStep("SetArenaReadyFalse", unreadyRes.error);
    const auto readyRes = apiB.SetArenaReady(roomCode, true, false);
    if (!readyRes.ok)
        return failStep("SetArenaReadyTrue", readyRes.error);

    const auto startRes = api.SetArenaReady(roomCode, true, true);
    if (!startRes.ok || startRes.room.status != "playing")
        return failStep("StartArenaMatch", startRes.error + " status=" + startRes.room.status);

    const auto progHost = api.ReportArenaProgress(roomCode, 992500, 99.4f, 186, 186, "SS", true);
    if (!progHost.ok)
        return failStep("ReportArenaProgressHost", progHost.error);
    const auto progRival = apiB.ReportArenaProgress(roomCode, 964000, 97.8f, 142, 150, "S", true);
    if (!progRival.ok)
        return failStep("ReportArenaProgressRival", progRival.error);

    const auto arenaFinal = api.FetchArenaRoomState(roomCode);
    if (!arenaFinal.ok || arenaFinal.room.status != "finished" || arenaFinal.room.players.size() != 2)
        return failStep("FetchArenaRoomState", arenaFinal.error + " status=" + arenaFinal.room.status);

    int hostPlacement  = 0;
    int rivalPlacement = 0;
    for (const auto& p : arenaFinal.room.players)
    {
        if (p.username == uname && p.finished && p.score == 992500)
            hostPlacement = p.placement;
        if (p.username == rivalName && p.finished && p.score == 964000)
            rivalPlacement = p.placement;
    }
    if (hostPlacement != 1 || rivalPlacement != 2)
        return failStep("VerifyArenaPlacements", "host=" + std::to_string(hostPlacement) + " rival=" + std::to_string(rivalPlacement));
    apiB.Shutdown();

    const nlohmann::json report = {
        { "verified", true },
        { "username", uname },
        { "score_id", submitted.scoreId },
        { "market_chart_id", pubRes.chart.id },
        { "score", liveResult.score },
        { "rank", submitted.rank },
        { "pp", submitted.pp },
        { "replay_events", dlReplay->inputs.size() },
        { "cloud_restored_scores", syncPull.scoresPulled },
        { "cloud_restored_achievements", syncPull.achievementsPulled },
        { "daily_chart_id", dailyInfo.spec.chartId },
        { "dan_badge", profV140.user.danBadge },
        { "arena_room_code", roomCode }
    };
    sakura::utils::AtomicWrite(fs::path(sakura::core::Paths::User("verify-online.json")), report.dump(2));
    api.Shutdown();
    fs::remove_all(e2eDir, tmpEc);
    return true;
}
}

int main(int argc, char* argv[])
{
    try
    {
        namespace fs = std::filesystem;
        fs::path capture;
        fs::path verifyAudio;
        fs::path recordScript, recordOutput;
        fs::path openPackage;
        std::string verifyOnlineUrl;
        int width = 1600, height = 900;bool verify=false,verifyImages=false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--smoke-test" && i + 1 < argc) capture = fs::absolute(argv[++i]);
            else if (arg == "--verify-play") verify=true;
            else if (arg == "--verify-online" && i + 1 < argc) verifyOnlineUrl = argv[++i];
            else if (arg == "--verify-audio" && i+1<argc)verifyAudio=fs::absolute(argv[++i]);
            else if (arg == "--verify-media" && i+1<argc){verifyAudio=fs::absolute(argv[++i]);verifyImages=true;}
            else if (arg == "--size" && i + 2 < argc) { width = std::stoi(argv[++i]); height = std::stoi(argv[++i]); }
            else if (arg == "--record" && i + 1 < argc) recordScript = Utf8ArgumentPath(argv[++i]);
            else if (arg == "--record-out" && i + 1 < argc) recordOutput = Utf8ArgumentPath(argv[++i]);
            else if (!arg.starts_with("--") && openPackage.empty()) openPackage = Utf8ArgumentPath(argv[i]);
            else return 2;
        }
        // Offline recording owns its own isolated user directory and cannot be combined with QA modes.
        if (!recordOutput.empty() && recordScript.empty()) return 2;
        if (!recordScript.empty() && (!capture.empty() || !verifyAudio.empty() || verify)) return 2;
        const fs::path original = fs::current_path();
        if(!verifyAudio.empty()&&capture.empty())capture=verifyAudio/"results";
        const fs::path base = SDL_GetBasePath();
        if (fs::exists(base / "resources/fonts")) fs::current_path(base);
        if (!recordScript.empty()) return sakura::scene::RunRecordMode(recordScript, recordOutput);
        fs::path userRoot;
        if (!capture.empty()) userRoot = capture / "userdata";
        else if (const char* path = SDL_getenv("SAKURA_USER_DIR"); path && *path) userRoot = fs::absolute(path);
        else if (fs::exists(base / "portable.txt")) userRoot = base / "userdata";
        else
        {
            char* preference = SDL_GetPrefPath("Sakura", "Sakura");
            if (!preference) return 1;
            userRoot = preference;
            SDL_free(preference);
        }
        fs::create_directories(userRoot);
        sakura::core::Paths::SetUserRoot(userRoot);
        if (!verifyOnlineUrl.empty())
            return RunVerifyOnline(verifyOnlineUrl) ? 0 : 1;
        if (!openPackage.empty() && capture.empty())
        {
            std::error_code pkgEc;
            if (fs::is_regular_file(openPackage, pkgEc))
            {
                const fs::path stage = fs::path(sakura::core::Paths::User("imports")) / ("assoc-" + std::to_string(SDL_GetTicksNS()));
                if (sakura::utils::ExtractZipArchive(openPackage, stage))
                {
                    sakura::game::ChartLoader loader;
                    if (auto info = loader.LoadChartInfo((stage / "info.json").string()))
                    {
                        bool valid = fs::is_regular_file(stage / info->musicFile, pkgEc);
                        for (const auto& d : info->difficulties)
                            valid &= loader.LoadChartData(info->folderPath + "/" + d.chartFile).has_value();
                        if (valid)
                        {
                            const fs::path dest = fs::path(sakura::core::Paths::User("charts")) / stage.filename();
                            fs::create_directories(dest.parent_path(), pkgEc);
                            fs::rename(stage, dest, pkgEc);
                        }
                    }
                }
                fs::remove_all(stage, pkgEc);
            }
        }
        // Preserve installations created before the per-user storage migration.
        if (capture.empty())
            for (const char* relative : {"config/settings.json", "data/sakura.db"})
            {
                if (!fs::exists(userRoot / relative) && fs::exists(original / relative))
                {
                    fs::create_directories((userRoot / relative).parent_path());
                    fs::copy_file(original / relative, userRoot / relative);
                }
            }
        if (!capture.empty())
        {
            auto& config = sakura::core::Config::GetInstance();
            config.Load(sakura::core::Paths::User("config/settings.json"));
            config.Set("display.window_width", std::clamp(width, 960, 7680));
            config.Set("display.window_height", std::clamp(height, 540, 4320));
            config.Set("audio.master_volume", 0.0f);
            config.Set("display.fullscreen", false);
            config.Set("online.auto_check_updates", false);
            config.Save();
            if(!verifyAudio.empty()){
                sakura::core::App app;if(!app.Initialize())return 1;
                auto& audio=sakura::audio::AudioManager::GetInstance();
                nlohmann::json checks=nlohmann::json::array();bool passed=true;
                for(const auto* extension:{"wav","mp3","flac","ogg"}){
                    const bool loaded=audio.PlayMusic((verifyAudio/(std::string("fixture.")+extension)).string(),0,0,true);
                    const double duration=audio.GetMusicDuration();
                    const bool seek=loaded&&audio.SetMusicPosition(0.5);
                    audio.ResumeMusic();SDL_Delay(35);audio.PauseMusic();
                    const double position=audio.GetMusicPosition();
                    auto& visualizer=sakura::audio::AudioVisualizer::GetInstance();
                    visualizer.Update(0.04f,0.5,true);
                    const auto& bands=visualizer.GetBands();
                    const bool analyzed=*std::max_element(bands.begin(),bands.end())>0.001f;
                    const bool ok=loaded&&seek&&duration>1.3&&position>=0.49&&position<0.7&&analyzed;
                    checks.push_back({{"format",extension},{"loaded",loaded},{"duration",duration},{"position_after_seek",position},{"spectrum",analyzed},{"verified",ok}});passed&=ok;
                    audio.StopMusic();
                }
                passed&=sakura::utils::AtomicWrite(capture/"audio-formats.json",checks.dump(2));
                if(verifyImages){
                    nlohmann::json images=nlohmann::json::array();
                    for(const auto* extension:{"png","jpg","webp","bmp"}){
                        auto* surface=IMG_Load((verifyAudio/(std::string("fixture.")+extension)).string().c_str());
                        const bool ok=surface&&IMG_SavePNG(surface,(capture/(std::string(extension)+"-converted.png")).string().c_str());
                        images.push_back({{"format",extension},{"verified",ok}});passed&=ok;
                        if(surface)SDL_DestroySurface(surface);
                    }
                    passed&=sakura::utils::AtomicWrite(capture/"image-formats.json",images.dump(2));
                }
                app.Shutdown();return passed?0:1;
            }
            CaptureApp app(capture,verify);
            if (!app.Initialize()) return 1;
            app.Run();
            app.Shutdown();
            return app.succeeded ? 0 : 1;
        }
        sakura::core::App app;
        if (!app.Initialize())
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Sakura", "启动失败，请查看用户数据目录内 logs/sakura.log。", nullptr);
            return 1;
        }
        app.Run();
        app.Shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Sakura", error.what(), nullptr);
        return 1;
    }
}
