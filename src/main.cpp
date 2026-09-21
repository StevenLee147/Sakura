#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include "core/app.h"
#include "core/config.h"
#include "core/paths.h"
#include "game/chart_loader.h"
#include "scene/scene_menu.h"
#include "scene/scene_splash.h"
#include "scene/scene_select.h"
#include "scene/scene_settings.h"
#include "scene/scene_game.h"
#include "scene/scene_result.h"
#include "scene/scene_tutorial.h"
#include "scene/scene_stats.h"
#include "scene/scene_editor.h"
#include "scene/scene_chart_wizard.h"
#include "scene/scene_calibration.h"
#include "scene/scene_pause.h"
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
    std::vector<std::string> m_scenes = {"first_run", "first_run_skip", "menu", "select", "select_practice", "settings", "settings_audio", "settings_controls", "settings_display", "settings_visual", "settings_data", "tutorial", "stats",
        "editor", "editor_properties", "editor_preview", "editor_return", "create", "create_complete", "calibration", "game", "pause", "result"};
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
        const nlohmann::json data={{"version",2},{"timing_points",{{{"time",0},{"bpm",120}}}},
            {"keyboard_notes",{{{"time",500},{"lane",0},{"type","tap"}},{{"time",1000},{"lane",1},{"type","hold"},{"duration",1000}},{{"time",2100},{"lane",2},{"type","tap"}}}},
            {"mouse_notes",{{{"time",800},{"x",0.5},{"y",0.5},{"type","circle"}},{{"time",1400},{"x",0.35},{"y",0.5},{"type","slider"},{"slider_duration",800},{"slider_path",{{0.65,0.5}}}}}}};
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
            {2100,InputKind::LaneDown,2},{2150,InputKind::LaneUp,2},{2185,InputKind::Pointer,0,0.65f,0.5f},{2260,InputKind::MouseUp}};
    }
    void Input(const sakura::game::SessionInput& input) {
        using namespace sakura::game;SDL_Event event{};event.common.timestamp=SDL_GetTicksNS();
        constexpr SDL_Scancode keys[]={SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_D,SDL_SCANCODE_F};
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
            }
            std::unique_ptr<Scene> scene;
            const auto& name = m_scenes[m_stage];
            if (name == "first_run") {
                sakura::core::Config::GetInstance().Set("tutorial.prompt_shown",false);
                scene=std::make_unique<SceneSplash>(manager);
            }
            if (name == "menu") scene = std::make_unique<SceneMenu>(manager);
            if (name == "select" || name=="select_practice") scene = std::make_unique<SceneSelect>(manager);
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
            if (name == "pause")
            {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = SDL_SCANCODE_ESCAPE;
                manager.HandleEvent(event);
            }
            if (name == "result")
            {
                sakura::game::GameResult result;
                result.chartId = m_chart.id; result.chartTitle = m_chart.title;
                result.difficulty = m_chart.difficulties[0].name;
                result.difficultyLevel = m_chart.difficulties[0].level;
                result.score = 982430; result.accuracy = 98.42f; result.grade = sakura::game::Grade::S;
                result.perfectCount = 187; result.greatCount = 10; result.goodCount = 2; result.missCount = 1;
                result.maxCombo = 124; result.assisted = true;
                for (int i = 0; i < 150; ++i) result.hitErrors.push_back((i * 17 % 67) - 27);
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
                    if(name=="verify_live"){m_liveResult=r;m_semantic=r.score==1000000&&r.totalJudgments==7&&!r.replayFile.empty()&&m_reheld;}
                    if(name=="verify_replay")m_semantic=r.score==m_liveResult.score&&r.hitErrors==m_liveResult.hitErrors&&r.assisted;
                    if(name=="verify_practice")m_semantic=r.totalJudgments==2&&r.assisted; // only complete hold in this range
                    if(name=="verify_album")m_semantic=r.score==1000000&&r.missCount==0&&r.assisted;
                    m_semantic &= sakura::data::Database::GetInstance().GetTotalPlayCount()==m_initialPlays+1;
                    m_capture=true;
                }
            }
            if(name=="verify_missing_audio" && m_time>2.0f){m_semantic=dynamic_cast<SceneSelect*>(manager.GetCurrentScene())!=nullptr;m_capture=true;}
            if(m_time>110){m_semantic=false;m_capture=true;}
        } else {
            const float duration = name=="game" ? 9.6f : name=="first_run"?6.5f:name=="result"?1.8f:1.3f;
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
        if(name.starts_with("settings")){const auto* settings=dynamic_cast<SceneSettings*>(current);int page=name=="settings"?0:name=="settings_audio"?1:name=="settings_controls"?2:name=="settings_display"?3:name=="settings_visual"?4:5;m_semantic=settings&&settings->CurrentPage()==page;}
        if(name=="game"){const auto* game=dynamic_cast<SceneGame*>(current);m_semantic=game&&game->State().IsPlaying()&&game->Session().Score().GetJudgedCount()>0;}
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

int main(int argc, char* argv[])
{
    try
    {
        namespace fs = std::filesystem;
        fs::path capture;
        fs::path verifyAudio;
        int width = 1600, height = 900;bool verify=false,verifyImages=false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--smoke-test" && i + 1 < argc) capture = fs::absolute(argv[++i]);
            else if (arg == "--verify-play") verify=true;
            else if (arg == "--verify-audio" && i+1<argc)verifyAudio=fs::absolute(argv[++i]);
            else if (arg == "--verify-media" && i+1<argc){verifyAudio=fs::absolute(argv[++i]);verifyImages=true;}
            else if (arg == "--size" && i + 2 < argc) { width = std::stoi(argv[++i]); height = std::stoi(argv[++i]); }
            else return 2;
        }
        const fs::path original = fs::current_path();
        if(!verifyAudio.empty()&&capture.empty())capture=verifyAudio/"results";
        const fs::path base = SDL_GetBasePath();
        if (fs::exists(base / "resources/fonts")) fs::current_path(base);
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
