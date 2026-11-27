#include "scene_select.h"
#include "scene_menu.h"
#include "scene_course.h"
#include "scene_market.h"
#include "scene_game.h"
#include "scene_settings.h"
#include "scene_editor.h"
#include "scene_chart_wizard.h"
#include "core/input.h"
#include "core/config.h"
#include "core/paths.h"
#include "core/theme.h"
#include "audio/audio_manager.h"
#include "audio/audio_visualizer.h"
#include "game/chart_converter.h"
#include "game/chart_loader.h"
#include "game/chart_utils.h"
#include "game/replay.h"
#include "data/database.h"
#include "ui/visual_style.h"
#include "ui/toast.h"
#include "utils/archive.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace sakura::scene {
using namespace sakura::core;
using namespace sakura::ui;
using namespace sakura::game;
namespace {
std::string Number(float v, int precision = 1) { std::ostringstream s; s << std::fixed << std::setprecision(precision) << v; return s.str(); }
std::string Lower(std::string s) { for(auto& c:s) if(static_cast<unsigned char>(c)<128) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::filesystem::path ResolveInputPath(const std::string& rawPath) {
    if(rawPath.empty()) return {};
    std::error_code ec;
    const auto* u8=reinterpret_cast<const char8_t*>(rawPath.data());
    std::filesystem::path u8Path{std::u8string(u8,u8+rawPath.size())};
    if(std::filesystem::exists(u8Path,ec) && !ec) return u8Path;
    ec.clear();
    std::filesystem::path localPath(rawPath);
    if(std::filesystem::exists(localPath,ec) && !ec) return localPath;
    return u8Path;
}
std::string PathExtLower(const std::filesystem::path& p) {
    const auto u8=p.extension().u8string();
    return Lower(std::string(reinterpret_cast<const char*>(u8.data()),u8.size()));
}
std::string Duration(int ms) { const int sec=ms/1000; return std::to_string(sec/60)+":"+(sec%60<10?"0":"")+std::to_string(sec%60); }
bool ReplayFileExists(const std::string& path) {
    if(path.empty()) return false;
    std::error_code ec;
    if(std::filesystem::is_regular_file(path,ec) && !ec) return true;
    ec.clear();
    const auto* u8=reinterpret_cast<const char8_t*>(path.data());
    const std::filesystem::path u8Path{std::u8string(u8,u8+path.size())};
    return std::filesystem::is_regular_file(u8Path,ec) && !ec;
}
const char* GhostModeLabel(GhostMode mode) {
    switch(mode) {
        case GhostMode::PersonalBest: return "幽灵竞速：个人最佳 PB";
        case GhostMode::OnlineRival:  return "幽灵竞速：线上对手";
        case GhostMode::Off:
        default:                      return "幽灵竞速：关";
    }
}
constexpr Color pink{246,178,206,255}, gold{250,216,157,255}, dim{168,178,204,255}, white{248,242,248,255};
}
std::unique_ptr<Button> SceneSelect::Button(NormRect rect, const std::string& label, std::function<void()> action, bool primary, float fontSize) {
    auto b=std::make_unique<sakura::ui::Button>(rect,label,m_font,fontSize,0.008f);
    b->SetTextAlign(TextAlign::Center); b->SetOnClick(std::move(action));
    VisualStyle::ApplyButton(b.get(),primary?ButtonVariant::Primary:ButtonVariant::Secondary); return b;
}
void SceneSelect::PersistSelectContext(bool flushToDisk) {
    if(m_restoringContext) return;
    SelectMemoryState st=SelectContextManager::Load(Config::GetInstance());
    if(m_selected>=0 && m_selected<static_cast<int>(m_charts.size())) {
        const auto& c=m_charts[m_selected];
        st.chartId=c.id;
        st.difficultyIndex=std::clamp(m_difficulty,0,std::max(0,static_cast<int>(c.difficulties.size())-1));
        if(!c.difficulties.empty()) st.difficultyName=c.difficulties[st.difficultyIndex].name;
    }
    st.collectionFilter=m_collectionFilter;
    st.sortMode=m_sort;
    st.scrollPos=m_scrollTarget;
    st.minLevel=m_filterMinLevel;
    st.maxLevel=m_filterMaxLevel;
    st.lampFilter=m_lampFilter;
    SelectContextManager::Save(Config::GetInstance(),st,flushToDisk);
}
void SceneSelect::RefreshChartLamps() {
    m_chartLamps.clear();
    const auto allBest=sakura::data::Database::GetInstance().GetAllBestScores();
    for(const auto& rec:allBest) {
        const SelectLamp lamp=ComputeChartLamp(rec);
        auto it=m_chartLamps.find(rec.chartId);
        if(it==m_chartLamps.end() || static_cast<int>(lamp)>static_cast<int>(it->second))
            m_chartLamps[rec.chartId]=lamp;
    }
}
void SceneSelect::TriggerRandomRoulette() {
    if(m_visible.empty()) return;
    sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
    const int picked=PickRandomVisibleChart(m_visible,m_selected,SDL_GetTicksNS());
    if(picked<0) return;
    if(m_visible.size()<=1) { Select(picked); return; }
    m_rouletteActive=true;
    m_rouletteTimer=0.0f;
    m_rouletteStepAccum=0.0f;
    m_rouletteFinalChart=picked;
}
void SceneSelect::OnEnter() {
    *m_alive=true;
    m_font=ResourceManager::GetInstance().GetDefaultFontHandle();
    m_topBar=std::make_unique<TopBar>("SONG SELECT  ·  曲库选歌",m_font,[this]{Back();});
    m_profileModal=std::make_unique<ProfileCardModal>(m_font);
    m_profileModal->SetOnRivalChanged([this]{
        if(m_selected>=0 && m_showGlobalLeaderboard) FetchGlobalLeaderboard();
        m_rebuild=true;
    });
    m_profileModal->SetOnChallengeGhost([this](const sakura::net::UserProfileScoreItem& item, const std::string& rivalName){
        for(int i=0;i<static_cast<int>(m_charts.size());++i) {
            if(m_charts[i].id==item.chartId) {
                const int dIdx=ResolveMatchingDifficultyIndex(m_charts[i],item.difficulty,item.difficultyLevel);
                Select(i,dIdx);
                const std::string dest=Paths::User("replays/rivals/rival_"+item.scoreId+".skr");
                std::error_code ec; std::filesystem::create_directories(std::filesystem::path(dest).parent_path(),ec);
                ToastManager::Instance().Show("正在下载劲敌 "+rivalName+" 的幽灵回放...",ToastType::Info,2.0f);
                auto alive=m_alive; const ChartInfo chartCopy=m_charts[i];
                sakura::net::APIClient::GetInstance().DownloadReplayAsync(item.scoreId,dest,[this,alive,chartCopy,dIdx,rivalName](bool ok,std::string localPath){
                    if(!*alive || !ok) return;
                    PlayOptions opts; opts.mode=PlayMode::Standard; opts.ghostMode=GhostMode::OnlineRival;
                    opts.ghostReplayFile=localPath; opts.ghostRivalName=rivalName;
                    PersistSelectContext(true);
                    m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager,chartCopy,dIdx,opts),TransitionType::SakuraSweep,0.42f);
                });
                return;
            }
        }
        ToastManager::Instance().Show("本地尚未安装该谱面："+item.chartTitle,ToastType::Warning,3.0f);
    });
    auto& cfg=Config::GetInstance();
    const auto favorites=cfg.Get<std::vector<std::string>>("library.favorites",{});
    m_favorites={favorites.begin(),favorites.end()};
    m_collections=CollectionManager::LoadFromConfig();
    m_buttons.clear(); m_detailButtons.clear();
    m_search=std::make_unique<TextInput>(NormRect{0.04f,0.16f,0.202f,0.051f},m_font,0.020f);
    m_search->SetPlaceholder("搜索曲名、曲师、标签 / Ctrl+F");
    m_search->SetOnChange([this](const std::string&){Filter();PersistSelectContext(false);});
    m_startInput=std::make_unique<TextInput>(NormRect{0.555f,0.762f,0.105f,0.040f},m_font,0.022f,8);
    m_endInput=std::make_unique<TextInput>(NormRect{0.698f,0.762f,0.105f,0.040f},m_font,0.022f,8);
    m_startInput->SetText("0"); m_endInput->SetText("0");
    m_buttons.push_back(Button({0.247f,0.16f,0.062f,0.051f},"全部",[this]{
        const int totalStates=2+static_cast<int>(m_collections.size());
        m_collectionFilter=(m_collectionFilter+1)%totalStates;
        if(m_collectionFilter>=2) m_activeCollectionIndex=m_collectionFilter-2;
        Filter(); PersistSelectContext(false);
    },false,0.017f));
    m_buttons.push_back(Button({0.313f,0.16f,0.048f,0.051f},"曲名",[this]{m_sort=(m_sort+1)%3;Filter();PersistSelectContext(false);},false,0.017f));
    m_buttons.push_back(Button({0.365f,0.16f,0.050f,0.051f},"全灯",[this]{
        m_lampFilter=static_cast<SelectLampFilter>((static_cast<int>(m_lampFilter)+1)%5);
        Filter(); PersistSelectContext(false);
    },false,0.016f));
    m_buttons.push_back(Button({0.419f,0.16f,0.052f,0.051f},"全定数",[this]{
        static constexpr std::pair<float,float> kPresets[5]={{1.0f,15.0f},{1.0f,6.5f},{7.0f,10.0f},{10.5f,13.0f},{13.0f,15.0f}};
        m_levelPresetIdx=(m_levelPresetIdx+1)%5;
        m_filterMinLevel=kPresets[m_levelPresetIdx].first;
        m_filterMaxLevel=kPresets[m_levelPresetIdx].second;
        Filter(); PersistSelectContext(false);
    },false,0.0155f));

    auto dailyBtn=Button({0.355f,0.064f,0.082f,0.044f},"今日挑战",[this]{ActivateDailyChallenge();},false,0.018f);
    VisualStyle::ApplyButton(dailyBtn.get(),ButtonVariant::Accent);
    m_buttons.push_back(std::move(dailyBtn));
    m_buttons.push_back(Button({0.442f,0.064f,0.082f,0.044f},"樱华道场",[this]{PersistSelectContext(true);m_manager.SwitchScene(std::make_unique<SceneCourse>(m_manager),TransitionType::SilkShutter,0.38f);},false,0.018f));
    m_buttons.push_back(Button({0.529f,0.064f,0.082f,0.044f},"谱面市场",[this]{PersistSelectContext(true);m_manager.SwitchScene(std::make_unique<SceneMarket>(m_manager),TransitionType::SilkShutter,0.38f);},true,0.018f));
    m_buttons.push_back(Button({0.616f,0.064f,0.081f,0.044f},"导入谱面",[this]{ImportFolder();},false,0.018f));
    m_buttons.push_back(Button({0.702f,0.064f,0.081f,0.044f},"新建谱面",[this]{PersistSelectContext(true);m_manager.SwitchScene(std::make_unique<SceneChartWizard>(m_manager),TransitionType::SilkShutter,0.38f);},false,0.018f));
    m_buttons.push_back(Button({0.788f,0.064f,0.081f,0.044f},"打开曲库",[]{const std::filesystem::path path=Paths::User("charts");std::error_code ec;std::filesystem::create_directories(path,ec);SDL_OpenURL(("file:///"+path.generic_string()).c_str());},false,0.018f));
    m_buttons.push_back(Button({0.874f,0.064f,0.086f,0.044f},"偏好设置",[this]{PersistSelectContext(true);m_manager.SwitchScene(std::make_unique<SceneSettings>(m_manager),TransitionType::SilkShutter,0.38f);},false,0.018f));

    m_buttons.push_back(Button({0.04f,0.909f,0.12f,0.057f},"返回主页",[this]{Back();}));
    m_buttons.push_back(Button({0.17f,0.909f,0.09f,0.057f},"刷新 / F5",[this]{Scan();}));
    m_buttons.push_back(Button({0.27f,0.909f,0.096f,0.057f},"随机 / F2",[this]{TriggerRandomRoulette();}));
    m_buttons.push_back(Button({0.77f,0.909f,0.19f,0.057f},"开始演奏   →",[this]{Start(m_options.mode);},true));
    m_particles.Clear();
    m_petalEmitter=m_particles.EmitContinuous(0.5f,-0.02f,2.6f,sakura::effects::ParticlePresets::SakuraPetal());
    Scan();
}
void SceneSelect::Scan() {
    // Snapshot remembered chart & view state BEFORE Filter/Select can touch Config!
    const SelectMemoryState desired=SelectContextManager::Load(Config::GetInstance());
    m_restoringContext=true;
    m_collections=CollectionManager::LoadFromConfig();
    m_collectionFilter=desired.collectionFilter;
    m_sort=desired.sortMode;
    m_filterMinLevel=desired.minLevel;
    m_filterMaxLevel=desired.maxLevel;
    m_lampFilter=desired.lampFilter;
    if(m_filterMinLevel<=1.01f && m_filterMaxLevel>=14.99f) m_levelPresetIdx=0;
    if(m_collectionFilter>1+static_cast<int>(m_collections.size())) m_collectionFilter=0;
    if(m_collectionFilter>=2) m_activeCollectionIndex=m_collectionFilter-2;
    if(m_activeCollectionIndex>=static_cast<int>(m_collections.size())) m_activeCollectionIndex=0;

    ChartLoader loader;
    m_charts=loader.ScanCharts("resources/charts");
    auto custom=loader.ScanCharts(Paths::User("charts"));
    for(auto& c:custom) {
        auto existing=std::find_if(m_charts.begin(),m_charts.end(),[&](const ChartInfo& other){return other.id==c.id;});
        if(existing==m_charts.end()) m_charts.push_back(std::move(c)); else *existing=std::move(c);
    }
    RefreshChartLamps();
    m_selected=-1;
    Filter(false);

    auto target=SelectContextManager::ResolveRestoredSelection(m_charts,m_visible,desired);
    if(target.clearedObscuringFilters) {
        m_collectionFilter=0;
        m_filterMinLevel=1.0f;
        m_filterMaxLevel=15.0f;
        m_levelPresetIdx=0;
        m_lampFilter=SelectLampFilter::All;
        if(m_search) m_search->SetText("");
        Filter(false);
        target=SelectContextManager::ResolveRestoredSelection(m_charts,m_visible,desired);
    }
    if(target.chartIndex>=0) {
        Select(target.chartIndex,target.difficultyIndex);
        const float maxScroll=static_cast<float>(std::max(0,static_cast<int>(m_visible.size())-6));
        if(desired.scrollPos>0.01f) {
            const auto visIt=std::find(m_visible.begin(),m_visible.end(),m_selected);
            if(visIt!=m_visible.end()) {
                const float row=static_cast<float>(visIt-m_visible.begin());
                if(desired.scrollPos<=row && row<desired.scrollPos+6.0f)
                    m_scrollTarget=std::clamp(desired.scrollPos,0.0f,maxScroll);
            }
        }
        m_scrollPos=m_scrollTarget;
        m_scroll=static_cast<int>(std::floor(m_scrollPos));
    }
    m_restoringContext=false;
    PersistSelectContext(false);
}
void SceneSelect::Filter(bool allowFallbackSelect) {
    if(m_collectionFilter<0 || m_collectionFilter>1+static_cast<int>(m_collections.size())) m_collectionFilter=0;
    m_visible.clear(); const auto query=Lower(m_search->GetText());
    for(int i=0;i<static_cast<int>(m_charts.size());++i) {
        const auto& c=m_charts[i];
        bool matchCol=true;
        if(m_collectionFilter==1) matchCol=m_favorites.contains(c.id);
        else if(m_collectionFilter>=2) matchCol=CollectionManager::ContainsChart(m_collections[static_cast<size_t>(m_collectionFilter-2)],c.id);
        if(!matchCol) continue;
        const auto lampIt=m_chartLamps.find(c.id);
        const SelectLamp lamp=(lampIt!=m_chartLamps.end())?lampIt->second:SelectLamp::Unplayed;
        if(!MatchesLampFilter(lamp,m_lampFilter)) continue;
        if(!ChartMatchesLevelRange(c,-1,m_filterMinLevel,m_filterMaxLevel)) continue;
        std::string hay=c.title+" "+c.artist+" "+c.charter;
        for(const auto& tag:c.tags) hay+=" "+tag;
        if(Lower(hay).find(query)!=std::string::npos) m_visible.push_back(i);
    }
    std::stable_sort(m_visible.begin(),m_visible.end(),[this](int a,int b){
        if(m_sort==1 && m_charts[a].bpm!=m_charts[b].bpm) return m_charts[a].bpm<m_charts[b].bpm;
        if(m_sort==2 && m_charts[a].difficulties.back().level!=m_charts[b].difficulties.back().level) return m_charts[a].difficulties.back().level<m_charts[b].difficulties.back().level;
        return m_charts[a].title<m_charts[b].title;
    });
    const float maxScroll=static_cast<float>(std::max(0,static_cast<int>(m_visible.size())-6));
    m_scrollTarget=std::clamp(m_scrollTarget,0.0f,maxScroll);
    m_scrollPos=std::clamp(m_scrollPos,0.0f,maxScroll);
    m_scroll=static_cast<int>(std::floor(m_scrollTarget));
    if(std::find(m_visible.begin(),m_visible.end(),m_selected)==m_visible.end()) {
        m_selected=-1;
        if(allowFallbackSelect && !m_visible.empty()) Select(m_visible.front());
    } else {
        const auto visIt=std::find(m_visible.begin(),m_visible.end(),m_selected);
        const float row=static_cast<float>(visIt-m_visible.begin());
        if(row<m_scrollTarget) m_scrollTarget=row;
        if(row>=m_scrollTarget+6.0f) m_scrollTarget=row-5.0f;
        m_scrollTarget=std::clamp(m_scrollTarget,0.0f,maxScroll);
    }
    if(m_buttons.size()>=4) {
        if(m_collectionFilter==0) {m_buttons[0]->SetText("全部");m_buttons[0]->SetFontSize(0.017f);}
        else if(m_collectionFilter==1) {m_buttons[0]->SetText("收藏");m_buttons[0]->SetFontSize(0.017f);}
        else {m_buttons[0]->SetText(m_collections[static_cast<size_t>(m_collectionFilter-2)].name);m_buttons[0]->SetFontSize(0.0145f);}
        VisualStyle::ApplyButton(m_buttons[0].get(),m_collectionFilter>0?ButtonVariant::Primary:ButtonVariant::Secondary);
        m_buttons[1]->SetText(m_sort==0?"曲名":m_sort==1?"BPM":"难度");
        static constexpr const char* kLampLabels[5]={"全灯","未游","CLR+","FC+","AP"};
        m_buttons[2]->SetText(kLampLabels[static_cast<int>(m_lampFilter)]);
        VisualStyle::ApplyButton(m_buttons[2].get(),m_lampFilter!=SelectLampFilter::All?ButtonVariant::Accent:ButtonVariant::Secondary);
        const bool fullRange=(m_filterMinLevel<=1.01f && m_filterMaxLevel>=14.99f);
        m_buttons[3]->SetText(fullRange?std::string("全定数"):("Lv."+FormatDifficultyLevel(m_filterMinLevel)+"~"+FormatDifficultyLevel(m_filterMaxLevel)));
        VisualStyle::ApplyButton(m_buttons[3].get(),!fullRange?ButtonVariant::Primary:ButtonVariant::Secondary);
    }
    if(m_selected<0) {
        m_rebuild=true;
        sakura::audio::AudioManager::GetInstance().StopMusic();
        if(!m_buttons.empty()) m_buttons.back()->SetText("开始演奏   →");
    }
    if(!m_buttons.empty()) m_buttons.back()->SetEnabled(m_selected>=0);
}
void SceneSelect::Select(int index,int difficulty) {
    if(index<0 || index>=static_cast<int>(m_charts.size())) return;
    const bool changed=index!=m_selected;
    std::string prevDiffName;
    float prevDiffLevel = 0.0f;
    const bool hadPrev = (m_selected >= 0 && m_selected < static_cast<int>(m_charts.size()) &&
                          m_difficulty >= 0 && m_difficulty < static_cast<int>(m_charts[m_selected].difficulties.size()));
    if (hadPrev) {
        prevDiffName  = m_charts[m_selected].difficulties[m_difficulty].name;
        prevDiffLevel = m_charts[m_selected].difficulties[m_difficulty].level;
    }
    const int prevDifficulty = m_difficulty;
    m_selected=index; const auto& c=m_charts[index];
    int targetDiff = difficulty;
    if (targetDiff < 0) {
        if (!changed) targetDiff = m_difficulty;
        else if (hadPrev) targetDiff = ResolveMatchingDifficultyIndex(c, prevDiffName, prevDiffLevel);
        else targetDiff = 0;
    }
    m_difficulty=std::clamp(targetDiff,0,static_cast<int>(c.difficulties.size())-1);
    const bool diffChanged = (m_difficulty != prevDifficulty);
    if(changed) {
        m_previewTimer=0.30f; m_previewPlaying=false;
        ResourceManager::GetInstance().UnloadTexture(m_cover);
        const std::string coverPath=c.coverFile.empty()?"":(c.folderPath+"/"+c.coverFile);
        m_cover=coverPath.empty()?INVALID_HANDLE:ResourceManager::GetInstance().LoadTexture(coverPath).value_or(INVALID_HANDLE);
        m_bgCover.UnloadImage();
        if(!coverPath.empty() && m_bgCover.LoadImage(coverPath)){
            m_bgCover.SetDimming(0.68f);
            m_bgCover.SetBlurEnabled(Config::GetInstance().Get<bool>("graphics.background_blur",true));
            m_bgCover.SetBlurStrength(0.84f);
        }
    }
    ChartLoader loader; auto chart=loader.LoadChartData(c.folderPath+"/"+c.difficulties[m_difficulty].chartFile);
    m_best=chart?sakura::data::Database::GetInstance().GetBestScore(c.id,c.difficulties[m_difficulty].name,Replay::Hash(*chart,c.offset)):std::nullopt;
    m_noteCount=chart?JudgmentCount(*chart):0; m_chartEnd=chart?ChartEndTime(*chart):0;
    m_npsProfile=chart?ComputeDualZoneNpsCurve(*chart,48):DualZoneNpsProfile{};
    if(changed || diffChanged) {
        m_globalEntries.clear();
        m_globalMyRank.reset();
        m_leaderboardStatus.clear();
        m_leaderboardLoading=false;
        m_leaderboardPendingFetch=false;
        m_leaderboardDebounce=0.0f;
    }
    if(m_showGlobalLeaderboard || (m_options.ghostMode==GhostMode::OnlineRival && (changed || diffChanged || m_globalEntries.empty()))) {
        const std::string& chartId=c.id;
        const std::string& diffName=c.difficulties[m_difficulty].name;
        if(!ApplyCachedLeaderboard(chartId,diffName)) {
            m_leaderboardStatus="正在加载全球排行榜...";
        }
        m_leaderboardPendingFetch=true;
        m_leaderboardDebounce=0.18f;
    }
    m_rebuild=true;
    const auto it=std::find(m_visible.begin(),m_visible.end(),index);
    if(it!=m_visible.end()) {
        const float row=static_cast<float>(it-m_visible.begin());
        const float maxScroll=static_cast<float>(std::max(0,static_cast<int>(m_visible.size())-6));
        if(row<m_scrollTarget) m_scrollTarget=row;
        if(row>=m_scrollTarget+6.0f) m_scrollTarget=row-5.0f;
        m_scrollTarget=std::clamp(m_scrollTarget,0.0f,maxScroll);
        m_scroll=static_cast<int>(std::floor(m_scrollTarget));
    }
    if(!m_restoringContext) PersistSelectContext(false);
}
void SceneSelect::SelectRelative(int delta) {
    if(m_visible.empty()) return;
    const auto it=std::find(m_visible.begin(),m_visible.end(),m_selected);
    const int row=it==m_visible.end()?0:static_cast<int>(it-m_visible.begin());
    Select(m_visible[std::clamp(row+delta,0,static_cast<int>(m_visible.size())-1)]);
}
bool SceneSelect::SelectChart(const std::string& id,int difficulty) {
    for(int i:m_visible) if(m_charts[i].id==id) {Select(i,difficulty);return true;}
    return false;
}
bool SceneSelect::ApplyCachedLeaderboard(const std::string& chartId, const std::string& diffName) {
    const std::string_view scope=(m_leaderboardViewMode==2)?"rivals":"global";
    auto cached=sakura::net::APIClient::GetInstance().GetCachedLeaderboard(chartId,diffName,scope);
    if(!cached.has_value()) return false;
    m_globalEntries=std::move(cached->entries);
    m_globalMyRank=cached->myRank;
    m_leaderboardLoading=false;
    m_leaderboardStatus=m_globalEntries.empty()?((m_leaderboardViewMode==2)?"劲敌圈暂无记录 · 点击全球榜选手可加为劲敌":"暂无上榜记录 · 完成正式演奏抢占榜首"):"";
    return true;
}
void SceneSelect::FetchGlobalLeaderboard() {
    if(m_selected<0) return;
    const auto& c=m_charts[m_selected];
    const std::string chartId=c.id;
    const std::string diffName=c.difficulties[m_difficulty].name;
    const std::string scope=(m_leaderboardViewMode==2)?"rivals":"global";
    m_leaderboardPendingFetch=false;
    m_leaderboardDebounce=0.0f;
    if(m_globalEntries.empty()) {
        ApplyCachedLeaderboard(chartId,diffName);
    }
    const bool hadCachedEntries=!m_globalEntries.empty();
    m_leaderboardLoading=true;
    if(!hadCachedEntries) {
        m_leaderboardStatus=(scope=="rivals")?"正在同步劲敌圈排行榜...":"正在加载全球排行榜...";
    }
    auto alive=m_alive;
    const int modeSnap=m_leaderboardViewMode;
    sakura::net::APIClient::GetInstance().FetchLeaderboardAsync(chartId,diffName,8,[this,alive,chartId,diffName,modeSnap](sakura::net::LeaderboardResult res){
        if(!*alive) return;
        if(m_selected<0 || m_charts[m_selected].id!=chartId || m_charts[m_selected].difficulties[m_difficulty].name!=diffName || m_leaderboardViewMode!=modeSnap) return;
        m_leaderboardLoading=false;
        if(!res.ok) {
            if(m_globalEntries.empty()) {
                m_leaderboardStatus=res.error;
            }
            m_rebuild=true;
            return;
        }
        m_globalMyRank=res.myRank;
        m_globalEntries=std::move(res.entries);
        m_leaderboardStatus=m_globalEntries.empty()?((modeSnap==2)?"劲敌圈暂无成绩 · 点击任意选手可加为劲敌":"暂无上榜记录 · 完成正式演奏抢占榜首"):"";
        m_rebuild=true;
    },scope);
}
void SceneSelect::WatchOnlineReplay(const sakura::net::LeaderboardRow& row) {
    if(m_selected<0 || !row.hasReplay || row.scoreId.empty()) return;
    const std::string dest=Paths::User("replays/online_"+row.scoreId+".skr");
    ToastManager::Instance().Show("正在下载 "+row.username+" 的线上回放...",ToastType::Info,2.0f);
    auto alive=m_alive;
    const int selIdx=m_selected, diffIdx=m_difficulty;
    sakura::net::APIClient::GetInstance().DownloadReplayAsync(row.scoreId,dest,[this,alive,selIdx,diffIdx](bool ok,std::string localPath){
        if(!*alive) return;
        if(!ok || selIdx<0 || selIdx>=static_cast<int>(m_charts.size())) {
            ToastManager::Instance().Show("下载线上回放失败",ToastType::Error,3.0f);
            return;
        }
        PlayOptions opts;
        opts.mode=PlayMode::Replay;
        opts.replayFile=localPath;
        m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager,m_charts[selIdx],diffIdx,opts),TransitionType::SakuraSweep,0.42f);
    });
}
void SceneSelect::CreatePresetCollection() {
    static constexpr const char* kPresets[]={"热身曲单","右手特训","高难冲分"};
    auto exists=[this](std::string_view name){
        return std::any_of(m_collections.begin(),m_collections.end(),[&](const SongCollection& col){return col.name==name;});
    };
    std::string chosen;
    for(const char* preset:kPresets) {
        if(!exists(preset)) {chosen=preset; break;}
    }
    if(chosen.empty()) {
        int n=static_cast<int>(m_collections.size())+1;
        chosen="精选曲单 #"+std::to_string(n);
        while(exists(chosen)) {++n; chosen="精选曲单 #"+std::to_string(n);}
    }
    const auto created=CollectionManager::CreateCollection(m_collections,chosen);
    CollectionManager::SaveToConfig(m_collections);
    m_activeCollectionIndex=static_cast<int>(m_collections.size())-1;
    ToastManager::Instance().Show("已新建曲单「"+created.name+"」· 点「入单」可收入当前曲目",ToastType::Success,2.8f);
    m_rebuild=true;
}
void SceneSelect::ToggleCurrentInCollection() {
    if(m_selected<0 || m_selected>=static_cast<int>(m_charts.size())) return;
    const auto& c=m_charts[m_selected];
    if(m_collections.empty()) {
        CollectionManager::CreateCollection(m_collections,"精选曲单 1");
        m_activeCollectionIndex=0;
    }
    int targetIdx=0;
    if(m_collectionFilter>=2 && m_collectionFilter-2<static_cast<int>(m_collections.size())) targetIdx=m_collectionFilter-2;
    else if(m_activeCollectionIndex>=0 && m_activeCollectionIndex<static_cast<int>(m_collections.size())) targetIdx=m_activeCollectionIndex;
    const std::string colId=m_collections[static_cast<size_t>(targetIdx)].id;
    const std::string colName=m_collections[static_cast<size_t>(targetIdx)].name;
    CollectionManager::ToggleChartInCollection(m_collections,colId,c.id);
    CollectionManager::SaveToConfig(m_collections);
    const bool inCol=CollectionManager::ContainsChart(m_collections[static_cast<size_t>(targetIdx)],c.id);
    ToastManager::Instance().Show(
        inCol?("已加入曲单「"+colName+"」"):("已从曲单「"+colName+"」移除"),
        inCol?ToastType::Success:ToastType::Info,2.4f);
    m_rebuild=true;
    if(m_collectionFilter>=2) Filter();
}
bool SceneSelect::IsSelectedMatchingDaily() const {
    if(!m_dailyActiveSpec.has_value() || m_selected<0 || m_selected>=static_cast<int>(m_charts.size())) return false;
    const auto& c=m_charts[m_selected];
    const std::string& wantId=m_dailyResolvedChartId.empty()?m_dailyActiveSpec->chartId:m_dailyResolvedChartId;
    if(c.id!=wantId) return false;
    const int expectedDiff=ResolveMatchingDifficultyIndex(c,m_dailyActiveSpec->difficultyName,m_dailyActiveSpec->difficultyLevel);
    return m_difficulty==expectedDiff;
}
void SceneSelect::ActivateDailyChallenge() {
    if(m_charts.empty()) {
        ToastManager::Instance().Show("当前曲库暂无谱面，无法开启今日挑战",ToastType::Warning,3.0f);
        return;
    }
    const auto todaySpec=ComputeOfflineDailyChallenge(GetCurrentDateUtc8());
    auto resolveChartIndex=[this](const DailyChallengeSpec& spec)->int {
        for(int i=0;i<static_cast<int>(m_charts.size());++i) if(m_charts[i].id==spec.chartId) return i;
        if(!spec.chartTitle.empty()) {
            const std::string want=Lower(spec.chartTitle);
            for(int i=0;i<static_cast<int>(m_charts.size());++i) if(Lower(m_charts[i].title)==want) return i;
        }
        return 0;
    };
    const int chartIdx=resolveChartIndex(todaySpec);
    if(std::find(m_visible.begin(),m_visible.end(),chartIdx)==m_visible.end()) {
        m_search->SetText("");
        m_collectionFilter=0;
        Filter();
    }
    const int diffIdx=ResolveMatchingDifficultyIndex(m_charts[chartIdx],todaySpec.difficultyName,todaySpec.difficultyLevel);
    m_dailyActiveSpec=todaySpec;
    m_dailyResolvedChartId=m_charts[chartIdx].id;
    m_dailyMyRank.reset();
    m_playAsDaily=true;
    m_options.mode=PlayMode::Standard;
    m_options.rate=1.0f;
    m_showGlobalLeaderboard=false;
    Select(chartIdx,diffIdx);

    auto alive=m_alive;
    sakura::net::APIClient::GetInstance().FetchDailyChallengeAsync(
        todaySpec.dateUtc8,10,[this,alive,todaySpec](sakura::net::DailyInfoResult res){
            if(!*alive) return;
            DailyChallengeSpec spec=todaySpec;
            if(res.ok && !res.spec.dateUtc8.empty()) {
                spec=res.spec;
                m_dailyActiveSpec=spec;
                for(int i=0;i<static_cast<int>(m_charts.size());++i) {
                    if(m_charts[i].id==spec.chartId) {
                        m_dailyResolvedChartId=m_charts[i].id;
                        if(m_playAsDaily && m_selected!=i) {
                            if(std::find(m_visible.begin(),m_visible.end(),i)==m_visible.end()) {
                                m_search->SetText("");
                                m_collectionFilter=0;
                                Filter();
                            }
                            Select(i,ResolveMatchingDifficultyIndex(m_charts[i],spec.difficultyName,spec.difficultyLevel));
                        }
                        break;
                    }
                }
            }
            if(res.ok && !res.offlineFallback) m_dailyMyRank=res.leaderboard.myRank;
            const std::string goalStr=!spec.goalText.empty()?spec.goalText:("目标分数 "+std::to_string(spec.targetScore));
            std::string rankStr=" · 离线轮换挑战";
            if(res.ok && !res.offlineFallback) {
                rankStr=res.leaderboard.myRank.has_value()?(" · 线上排名 #"+std::to_string(*res.leaderboard.myRank)):" · 线上暂未上榜";
            }
            ToastManager::Instance().Show("今日挑战 ["+spec.dateUtc8+"]："+goalStr+rankStr,ToastType::Info,4.0f);
            m_rebuild=true;
        });
}
void SceneSelect::BuildDetail() {
    m_rebuild=false; m_detailButtons.clear();
    if(m_selected<0) {
        if(!m_buttons.empty()) m_buttons.back()->SetText("开始演奏   →");
        return;
    }
    if(m_options.mode!=PlayMode::Practice || m_showGlobalLeaderboard){m_startInput->SetFocused(false);m_endInput->SetFocused(false);}
    const auto& c=m_charts[m_selected];
    bool inTargetCol=false;
    if(!m_collections.empty()) {
        int targetIdx=0;
        if(m_collectionFilter>=2 && m_collectionFilter-2<static_cast<int>(m_collections.size())) targetIdx=m_collectionFilter-2;
        else if(m_activeCollectionIndex>=0 && m_activeCollectionIndex<static_cast<int>(m_collections.size())) targetIdx=m_activeCollectionIndex;
        inTargetCol=CollectionManager::ContainsChart(m_collections[static_cast<size_t>(targetIdx)],c.id);
    }
    static constexpr const char* kLbModeLabels[3]={"本地面板","全球排行","劲敌排行"};
    m_detailButtons.push_back(Button({0.664f,0.185f,0.082f,0.036f},kLbModeLabels[m_leaderboardViewMode],[this]{
        m_leaderboardViewMode=(m_leaderboardViewMode+1)%3;
        m_showGlobalLeaderboard=(m_leaderboardViewMode>0);
        m_globalEntries.clear(); m_globalMyRank.reset();
        if(m_showGlobalLeaderboard && m_selected>=0) {
            ApplyCachedLeaderboard(m_charts[m_selected].id,m_charts[m_selected].difficulties[m_difficulty].name);
            FetchGlobalLeaderboard();
        }
        m_rebuild=true;
    },m_showGlobalLeaderboard,0.0160f));
    m_detailButtons.push_back(Button({0.752f,0.185f,0.060f,0.036f},m_favorites.contains(c.id)?"已收藏":"收藏",[this]{Favorite();},m_favorites.contains(c.id),0.0165f));
    m_detailButtons.push_back(Button({0.818f,0.185f,0.060f,0.036f},"+曲单",[this]{CreatePresetCollection();},false,0.0165f));
    m_detailButtons.push_back(Button({0.884f,0.185f,0.060f,0.036f},inTargetCol?"已入单":"入单",[this]{ToggleCurrentInCollection();},inTargetCol,0.0165f));
    const int count=static_cast<int>(c.difficulties.size());
    const int first=(m_difficulty/4)*4;
    for(int i=first;i<std::min(first+4,count);++i) {
        const float width=0.425f/std::min(count,4);
        m_detailButtons.push_back(Button({0.515f+(i%4)*width,0.473f,width-0.008f,0.035f},
            c.difficulties[i].name+" Lv."+FormatDifficultyLevel(c.difficulties[i].level),[this,i]{Select(m_selected,i);},i==m_difficulty));
    }
    if(count>4){
        m_detailButtons.push_back(Button({0.817f,0.429f,0.055f,0.032f},"‹",[this,count]{Select(m_selected,(m_difficulty+count-1)%count);}));
        m_detailButtons.push_back(Button({0.884f,0.429f,0.055f,0.032f},"›",[this,count]{Select(m_selected,(m_difficulty+1)%count);}));
    }
    if(m_showGlobalLeaderboard) {
        m_detailButtons.push_back(Button({0.848f,0.528f,0.090f,0.034f},"刷新榜单",[this]{FetchGlobalLeaderboard();}));
        const int rows=std::min(5,static_cast<int>(m_globalEntries.size()));
        for(int i=0;i<rows;++i) {
            if(m_globalEntries[i].hasReplay) {
                const auto rowCopy=m_globalEntries[i];
                m_detailButtons.push_back(Button({0.872f,0.574f+i*0.046f,0.066f,0.036f},"回放",[this,rowCopy]{WatchOnlineReplay(rowCopy);}));
            }
        }
    } else {
        const char* modes[]={"正式演奏","自由练习","自动演示"};
        for(int i=0;i<3;++i) m_detailButtons.push_back(Button({0.515f+i*0.145f,0.595f,0.135f,0.042f},modes[i],
            [this,i]{m_options.mode=static_cast<PlayMode>(i);if(i==0)m_options.rate=1;m_rebuild=true;},static_cast<int>(m_options.mode)==i));
        if(m_options.mode==PlayMode::Standard && IsSelectedMatchingDaily()) {
            m_detailButtons.push_back(Button({0.848f,0.657f,0.090f,0.034f},m_playAsDaily?"挑战：开":"挑战：关",[this]{
                m_playAsDaily=!m_playAsDaily;
                m_rebuild=true;
            },m_playAsDaily,0.016f));
        }
        if(m_options.mode==PlayMode::Standard) {
            m_detailButtons.push_back(Button({0.515f,0.712f,0.235f,0.036f},GhostModeLabel(m_options.ghostMode),[this]{
                const int next=(static_cast<int>(m_options.ghostMode)+1)%3;
                m_options.ghostMode=static_cast<GhostMode>(next);
                if(m_options.ghostMode==GhostMode::OnlineRival && m_selected>=0 && m_globalEntries.empty() && !m_leaderboardLoading) {
                    ApplyCachedLeaderboard(m_charts[m_selected].id,m_charts[m_selected].difficulties[m_difficulty].name);
                    m_leaderboardPendingFetch=true;
                    m_leaderboardDebounce=0.18f;
                }
                m_rebuild=true;
            },m_options.ghostMode!=GhostMode::Off,0.0165f));
        }
        if(m_options.mode!=PlayMode::Standard) {
            m_detailButtons.push_back(Button({0.815f,0.648f,0.057f,0.035f},"−",[this]{m_options.rate=std::max(0.5f,m_options.rate-0.1f);}));
            m_detailButtons.push_back(Button({0.883f,0.648f,0.057f,0.035f},"+",[this]{m_options.rate=std::min(2.0f,m_options.rate+0.1f);}));
        }
        if(m_options.mode==PlayMode::Practice) m_detailButtons.push_back(Button({0.825f,0.762f,0.116f,0.040f},m_options.loop?"循环：开":"循环：关",[this]{m_options.loop=!m_options.loop;m_rebuild=true;}));
    }
    if(!m_buttons.empty()) {
        const bool asDaily=(m_options.mode==PlayMode::Standard && m_playAsDaily && IsSelectedMatchingDaily());
        m_buttons.back()->SetText(asDaily?"开始今日挑战 →":"开始演奏   →");
    }
    m_detailButtons.push_back(Button({0.515f,0.827f,0.18f,0.040f},"播放最近回放",[this]{Start(PlayMode::Replay);}));
    m_detailButtons.push_back(Button({0.711f,0.827f,0.23f,0.040f},"在工房中编辑",[this]{PersistSelectContext(true);const auto& c=m_charts[m_selected];m_manager.SwitchScene(std::make_unique<SceneEditor>(m_manager,c.folderPath,c.difficulties[m_difficulty].chartFile),TransitionType::SilkShutter,0.38f);}));
}
void SceneSelect::Favorite() {
    const auto id=m_charts[m_selected].id;
    if(m_favorites.contains(id)) m_favorites.erase(id); else m_favorites.insert(id);
    Config::GetInstance().Set("library.favorites",std::vector<std::string>(m_favorites.begin(),m_favorites.end()));
    m_rebuild=true;Filter();
}
void SceneSelect::Start(PlayMode mode) {
    if(m_selected<0 || m_ghostDownloading) return;
    PersistSelectContext(true);
    auto options=m_options; options.mode=mode;
    options.ghostReplayFile.clear();
    options.ghostRivalName.clear();
    if(mode==PlayMode::Standard && m_playAsDaily && m_dailyActiveSpec.has_value() && IsSelectedMatchingDaily()) {
        options.isDailyChallenge=true;
        options.dailyDate=m_dailyActiveSpec->dateUtc8;
    } else {
        options.isDailyChallenge=false;
        options.dailyDate.clear();
    }
    if(mode==PlayMode::Replay) {
        const auto key="library.replay_"+m_charts[m_selected].id+"_"+std::to_string(m_difficulty);
        options.replayFile=Config::GetInstance().Get<std::string>(key,"");
        if(options.replayFile.empty()) {ToastManager::Instance().Show("这张谱面还没有回放，完成一次正式演奏后即可保存",ToastType::Info);return;}
    }
    if(mode==PlayMode::Practice) {
        try {
            size_t consumedA=0,consumedB=0;
            const double a=std::stod(m_startInput->GetText(),&consumedA), b=std::stod(m_endInput->GetText(),&consumedB);
            if(consumedA!=m_startInput->GetText().size() || consumedB!=m_endInput->GetText().size() || !std::isfinite(a)||!std::isfinite(b)||a<0||b<0||a*1000>=m_chartEnd || (b>0&&(b<=a || b*1000>m_chartEnd+1000))) throw std::invalid_argument("range");
            options.startMs=static_cast<int>(a*1000);options.endMs=static_cast<int>(b*1000);
        } catch(...) {ToastManager::Instance().Show("请输入有效练习区间：起点 < 终点，终点 0 表示曲终",ToastType::Warning);return;}
    } else {options.startMs=options.endMs=0;options.loop=false;}
    if(mode==PlayMode::Standard) {
        options.rate=1.0f;
        const auto& c=m_charts[m_selected];
        const std::string pbKey="library.replay_"+c.id+"_"+std::to_string(m_difficulty);
        const std::string pbPath=Config::GetInstance().Get<std::string>(pbKey,"");
        const bool hasPB=ReplayFileExists(pbPath);
        if(options.ghostMode==GhostMode::PersonalBest) {
            if(hasPB) {
                options.ghostReplayFile=pbPath;
                options.ghostRivalName="PB 个人最佳";
            } else {
                options.ghostMode=GhostMode::Off;
                ToastManager::Instance().Show("暂无个人最佳回放，本次将以无幽灵模式开局",ToastType::Info,2.5f);
            }
        } else if(options.ghostMode==GhostMode::OnlineRival) {
            if(!m_globalEntries.empty() && m_globalEntries[0].hasReplay && !m_globalEntries[0].scoreId.empty()) {
                const auto& top=m_globalEntries[0];
                const std::string onlinePath=Paths::User("replays/online_"+top.scoreId+".skr");
                const std::string rivalName="#1 "+top.username;
                if(ReplayFileExists(onlinePath)) {
                    options.ghostReplayFile=onlinePath;
                    options.ghostRivalName=rivalName;
                } else {
                    std::error_code dirEc;
                    std::filesystem::create_directories(std::filesystem::path(onlinePath).parent_path(),dirEc);
                    m_ghostDownloading=true;
                    ToastManager::Instance().Show("正在同步 "+rivalName+" 的幽灵回放...",ToastType::Info,2.0f);
                    auto alive=m_alive;
                    const ChartInfo chartCopy=c;
                    const int diffIdx=m_difficulty;
                    PlayOptions launchOpts=options;
                    const std::string fallbackPb=hasPB?pbPath:std::string{};
                    sakura::net::APIClient::GetInstance().DownloadReplayAsync(
                        top.scoreId,onlinePath,
                        [this,alive,chartCopy,diffIdx,launchOpts,rivalName,fallbackPb](bool ok,std::string localPath) mutable {
                            if(!*alive) return;
                            m_ghostDownloading=false;
                            if(ok && ReplayFileExists(localPath)) {
                                launchOpts.ghostMode=GhostMode::OnlineRival;
                                launchOpts.ghostReplayFile=localPath;
                                launchOpts.ghostRivalName=rivalName;
                            } else if(!fallbackPb.empty()) {
                                launchOpts.ghostMode=GhostMode::PersonalBest;
                                launchOpts.ghostReplayFile=fallbackPb;
                                launchOpts.ghostRivalName="PB 个人最佳";
                                ToastManager::Instance().Show("线上幽灵下载失败，已回退至个人最佳 PB",ToastType::Info,2.5f);
                            } else {
                                launchOpts.ghostMode=GhostMode::Off;
                                launchOpts.ghostReplayFile.clear();
                                launchOpts.ghostRivalName.clear();
                                ToastManager::Instance().Show("线上幽灵暂不可用，本次将以无幽灵模式开局",ToastType::Info,2.5f);
                            }
                            m_manager.SwitchScene(
                                std::make_unique<SceneGame>(m_manager,chartCopy,diffIdx,launchOpts),
                                TransitionType::SakuraSweep,0.42f);
                        });
                    return;
                }
            } else if(hasPB) {
                options.ghostMode=GhostMode::PersonalBest;
                options.ghostReplayFile=pbPath;
                options.ghostRivalName="PB 个人最佳";
                ToastManager::Instance().Show("暂无线上对手回放，已回退至个人最佳 PB",ToastType::Info,2.5f);
            } else {
                options.ghostMode=GhostMode::Off;
                ToastManager::Instance().Show("暂无可用幽灵回放，本次将以无幽灵模式开局",ToastType::Info,2.5f);
            }
        }
    } else {
        options.ghostMode=GhostMode::Off;
    }
    m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager,m_charts[m_selected],m_difficulty,options),TransitionType::SakuraSweep,0.42f);
}
void SceneSelect::ImportFolder() {
    static const SDL_DialogFileFilter kFilters[] = {
        { "谱面包 (.sakura / Phira .pez / osu! .osz / .sm)", "sakura;pez;osz;sm;osu;zip" }
    };
    auto* context=new std::shared_ptr<DialogState>(m_dialog);
    SDL_ShowOpenFileDialog([](void* raw,const char* const* files,int){
        std::unique_ptr<std::shared_ptr<DialogState>> state(static_cast<std::shared_ptr<DialogState>*>(raw));
        std::lock_guard lock((*state)->mutex);(*state)->path=files&&files[0]?files[0]:"";(*state)->ready=true;
    },context,SDL_GetKeyboardFocus(),kFilters,1,nullptr,false);
}
void SceneSelect::AcceptImport(const std::string& path) {
    if(path.empty())return;
    const auto target=std::filesystem::path(Paths::User("imports"))/("import-"+std::to_string(SDL_GetTicksNS()));
    auto finishConvertedImport=[this](const sakura::game::ConversionResult& convRes){
        Scan();
        if(!SelectChart(convRes.chartInfo.id,0)) {
            if(m_search) m_search->SetText("");
            m_collectionFilter=0;
            Filter();
            SelectChart(convRes.chartInfo.id,0);
        }
        ToastManager::Instance().Show(
            "已智能转换并导入双区谱面："+convRes.chartInfo.title+" ("+std::to_string(convRes.convertedDifficulties)+" 难度)",
            ToastType::Success,4.0f);
    };
    try {
        const std::filesystem::path srcPath=ResolveInputPath(path);
        std::error_code fsEc;
        if(!std::filesystem::exists(srcPath,fsEc) || fsEc)
            throw std::runtime_error("未找到要导入的谱面路径");

        const std::filesystem::path destChartsRoot(Paths::User("charts"));
        const auto detectedFmt=sakura::game::ChartConverter::DetectFormat(srcPath);
        if(detectedFmt==sakura::game::ExternalChartFormat::PhiraPez ||
           detectedFmt==sakura::game::ExternalChartFormat::PhiraJson ||
           detectedFmt==sakura::game::ExternalChartFormat::OsuArchive ||
           detectedFmt==sakura::game::ExternalChartFormat::OsuFile ||
           detectedFmt==sakura::game::ExternalChartFormat::StepManiaSm) {
            std::error_code mkEc;
            std::filesystem::create_directories(destChartsRoot,mkEc);
            const auto convRes=sakura::game::ChartConverter::ImportExternalPackage(srcPath,destChartsRoot);
            if(!convRes.ok)
                throw std::runtime_error(convRes.error.empty()?"外部谱面包转换导入失败":convRes.error);
            finishConvertedImport(convRes);
            return;
        }

        if(std::filesystem::is_regular_file(srcPath,fsEc) && !fsEc) {
            const auto ext = PathExtLower(srcPath);
            if(ext != ".sakura" && ext != ".zip")
                throw std::runtime_error("仅支持导入谱面文件夹或 .sakura / .pez / .osz / .sm / .osu / .zip 谱面包");
            if(!sakura::utils::ExtractZipArchive(srcPath, target))
                throw std::runtime_error(".sakura 谱面包校验或解压失败");
            if(ext == ".zip") {
                std::error_code infoEc;
                ChartLoader zipCheckLoader;
                const bool hasNativeRootInfo = std::filesystem::is_regular_file(target / "info.json", infoEc) &&
                                               !infoEc &&
                                               zipCheckLoader.LoadChartInfo((target / "info.json").string()).has_value();
                if(!hasNativeRootInfo) {
                    std::error_code mkEc;
                    std::filesystem::create_directories(destChartsRoot,mkEc);
                    const auto convRes = sakura::game::ChartConverter::ImportExternalPackage(srcPath, destChartsRoot);
                    if(convRes.ok) {
                        std::error_code rmEc;
                        std::filesystem::remove_all(target, rmEc);
                        finishConvertedImport(convRes);
                        return;
                    }
                }
            }
        } else if(std::filesystem::is_directory(srcPath,fsEc) && !fsEc) {
            std::filesystem::create_directories(target);
            for(const auto& entry:std::filesystem::recursive_directory_iterator(srcPath)) {
                if(entry.is_symlink())throw std::runtime_error("谱面目录不能包含符号链接");
                const auto dest=target/std::filesystem::relative(entry.path(),srcPath);
                if(entry.is_directory())std::filesystem::create_directories(dest);
                else if(entry.is_regular_file())std::filesystem::copy_file(entry.path(),dest);
            }
            std::error_code infoEc;
            if(!std::filesystem::is_regular_file(target / "info.json", infoEc) || infoEc) {
                std::error_code mkEc;
                std::filesystem::create_directories(destChartsRoot,mkEc);
                const auto convRes = sakura::game::ChartConverter::ImportExternalPackage(srcPath, destChartsRoot);
                if(convRes.ok) {
                    std::error_code rmEc;
                    std::filesystem::remove_all(target, rmEc);
                    finishConvertedImport(convRes);
                    return;
                }
            }
        } else {
            throw std::runtime_error("未找到要导入的谱面路径");
        }
        ChartLoader loader;auto info=loader.LoadChartInfo((target/"info.json").string());
        if(!info)throw std::runtime_error("谱面包缺少有效的 info.json");
        for(const auto& diff:info->difficulties)if(!loader.LoadChartData(info->folderPath+"/"+diff.chartFile))throw std::runtime_error("谱面数据无效："+diff.name);
        if(!std::filesystem::is_regular_file(target/info->musicFile))throw std::runtime_error("没有找到音乐文件");
        const auto destination=destChartsRoot/target.filename();
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::rename(target,destination);
        Scan();ToastManager::Instance().Show("谱面已导入曲库："+info->title,ToastType::Success);
    }catch(const std::exception& e){std::error_code ec;std::filesystem::remove_all(target,ec);ToastManager::Instance().Show(e.what(),ToastType::Error,5);}
}
void SceneSelect::Back(){PersistSelectContext(true);m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager),TransitionType::SilkShutter,0.36f);}
void SceneSelect::OnExit(){
    PersistSelectContext(true);
    *m_alive=false;
    if(m_petalEmitter>=0)m_particles.StopEmitter(m_petalEmitter);
    m_particles.Clear();
    m_bgCover.UnloadImage();
    ResourceManager::GetInstance().UnloadTexture(m_cover);
    m_cover=INVALID_HANDLE;
    sakura::audio::AudioManager::GetInstance().StopMusic();
    if(m_search) m_search->SetFocused(false);
    if(m_startInput) m_startInput->SetFocused(false);
    if(m_endInput) m_endInput->SetFocused(false);
    m_profileModal.reset();
    m_topBar.reset();
    Config::GetInstance().Save();
}

void SceneSelect::OnUpdate(float dt) {
    m_time+=dt;
    if(m_topBar) m_topBar->Update(dt);
    if(m_profileModal) m_profileModal->Update(dt);
    const float maxScroll=static_cast<float>(std::max(0,static_cast<int>(m_visible.size())-6));
    m_scrollTarget=std::clamp(m_scrollTarget,0.0f,maxScroll);
    m_scrollPos+=(m_scrollTarget-m_scrollPos)*(1.0f-std::exp(-18.0f*dt));
    if(std::abs(m_scrollTarget-m_scrollPos)<0.002f) m_scrollPos=m_scrollTarget;
    m_scroll=static_cast<int>(std::floor(m_scrollPos));

    if(m_rouletteActive && !m_visible.empty()) {
        m_rouletteTimer+=dt;
        const float progress=std::clamp(m_rouletteTimer/0.55f,0.0f,1.0f);
        const float interval=0.032f+0.110f*progress*progress;
        m_rouletteStepAccum+=dt;
        while(m_rouletteStepAccum>=interval && progress<1.0f) {
            m_rouletteStepAccum-=interval;
            const auto it=std::find(m_visible.begin(),m_visible.end(),m_selected);
            const int nextRow=(it==m_visible.end())?0:((static_cast<int>(it-m_visible.begin())+1)%static_cast<int>(m_visible.size()));
            Select(m_visible[nextRow]);
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonHover);
        }
        if(progress>=1.0f) {
            m_rouletteActive=false;
            if(m_rouletteFinalChart>=0) Select(m_rouletteFinalChart);
            sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
        }
    }
    if(m_leaderboardPendingFetch) {
        m_leaderboardDebounce-=dt;
        if(m_leaderboardDebounce<=0.0f) {
            m_leaderboardDebounce=0.0f;
            m_leaderboardPendingFetch=false;
            if(m_selected>=0 && (m_showGlobalLeaderboard || m_options.ghostMode==GhostMode::OnlineRival)) {
                FetchGlobalLeaderboard();
            }
        }
    }
    if(m_rebuild)BuildDetail();
    std::string imported;{std::lock_guard lock(m_dialog->mutex);if(m_dialog->ready){imported=std::move(m_dialog->path);m_dialog->ready=false;}}if(!imported.empty())AcceptImport(imported);
    m_search->Update(dt);m_startInput->Update(dt);m_endInput->Update(dt);
    m_bgCover.Update(dt);
    m_particles.Update(dt);
    for(auto& b:m_buttons)b->Update(dt);for(auto& b:m_detailButtons)b->Update(dt);
    auto& audio=sakura::audio::AudioManager::GetInstance();
    if(m_selected>=0) {
        m_previewTimer+=dt;
        if(!m_previewPlaying&&m_previewTimer>0.40f) {
            const auto& c=m_charts[m_selected];
            audio.SetPlaybackSpeed(1.0f);
            m_previewPlaying=true;
            const double totalDurSec=std::max(5.0,static_cast<double>(m_chartEnd)/1000.0);
            const double startSec=(c.previewTime>0)?(static_cast<double>(c.previewTime)/1000.0):(totalDurSec*0.25);
            const double loopEndSec=startSec+22.0;
            if(!audio.CrossfadeMusic(c.folderPath+"/"+c.musicFile,startSec,startSec,loopEndSec,280))
                ToastManager::Instance().Show("音乐预览不可用，请检查音频文件",ToastType::Warning);
        }
    }
}

void SceneSelect::OnRender(Renderer& r) {
    VisualStyle::DrawSceneBackground(r);
    if(m_bgCover.HasImage()) {
        m_bgCover.Render(r);
        VisualStyle::DrawSakuraLandscape(r,m_time,0.28f);
    } else {
        VisualStyle::DrawSakuraLandscape(r,m_time,0.72f);
    }
    r.DrawGradientRect({0.0f,0.0f,1.0f,1.0f},{8,12,26,125},{12,16,34,95},{8,12,26,175},{10,14,30,155});

    const float aspect=static_cast<float>(r.GetScreenHeight())/std::max(1,r.GetScreenWidth());
    VisualStyle::DrawBadgePill(r,{0.04f,0.032f,0.135f,0.026f},"SONG ARCHIVE",m_font,0.013f,{48,36,70,200},{250,216,157,150},gold);
    r.DrawText(m_font,"LIBRARY / 曲库",0.04f,0.062f,0.039f,white);
    r.DrawText(m_font,"选一首喜欢的歌，让月光与节奏共鸣。",0.04f,0.114f,0.019f,dim);
    m_search->Render(r);

    const auto [mx,my]=Input::GetMousePosition();
    {
        SDL_Renderer* raw=r.GetSDLRenderer();
        SDL_Rect prevClip{}; const bool wasClipped=SDL_RenderClipEnabled(raw); SDL_GetRenderClipRect(raw,&prevClip);
        const int sw=std::max(1,r.GetScreenWidth()), sh=std::max(1,r.GetScreenHeight());
        SDL_Rect listClip{static_cast<int>(0.028f*sw),static_cast<int>(0.232f*sh),static_cast<int>(0.442f*sw),static_cast<int>(0.624f*sh)};
        SDL_SetRenderClipRect(raw,&listClip);
        const int baseRow=std::max(0,static_cast<int>(std::floor(m_scrollPos)));
        const float frac=m_scrollPos-static_cast<float>(baseRow);
        for(int slot=0;slot<=6 && (baseRow+slot)<static_cast<int>(m_visible.size());++slot) {
            const int index=m_visible[baseRow+slot];const auto& c=m_charts[index];
            const float y=0.24f+(static_cast<float>(slot)-frac)*0.102f;
            if(y>0.855f || y+0.090f<0.232f) continue;
            const bool selected=index==m_selected;
            const bool hovered=(mx>=0.04f&&mx<=0.458f&&my>=std::max(0.236f,y)&&my<std::min(0.852f,y+0.090f));
            const float indent=selected?0.008f:(hovered?0.004f:0.0f);
            const NormRect cardRect{0.04f+indent,y,0.418f-indent,0.090f};

            if(selected) {
                VisualStyle::DrawGlassCard(r,cardRect,
                    {82,52,88,238},{44,34,68,242},
                    {250,216,157,225},{246,178,206,135},0.012f,0.55f);
                r.DrawRoundedGradientRect({cardRect.x+0.005f,y+0.015f,0.0035f,0.060f},0.0017f,gold,pink,gold,pink,6,true);
            } else {
                VisualStyle::DrawGlassCard(r,cardRect,
                    hovered?Color{36,44,74,224}:Color{22,28,52,208},
                    hovered?Color{24,30,54,232}:Color{14,18,36,220},
                    hovered?Color{246,178,206,135}:Color{166,176,214,68},
                    {112,122,168,36},0.011f,0.34f);
            }

            const bool fav=m_favorites.contains(c.id);
            r.DrawText(m_font,selected?"▶":(fav?"♥":"♪"),cardRect.x+0.016f,y+0.026f,0.023f,selected?gold:(fav?pink:dim));
            VisualStyle::DrawTextFit(r,m_font,c.title,cardRect.x+0.046f,y+0.011f,0.027f,0.250f,white);
            VisualStyle::DrawTextFit(r,m_font,c.artist+"  ·  "+Number(c.bpm,0)+" BPM",cardRect.x+0.046f,y+0.052f,0.0165f,0.235f,dim);

            int dispDiffIdx = static_cast<int>(c.difficulties.size()) - 1;
            if (selected) {
                dispDiffIdx = std::clamp(m_difficulty, 0, static_cast<int>(c.difficulties.size()) - 1);
            } else if (m_selected >= 0 && m_selected < static_cast<int>(m_charts.size())) {
                const auto& curChart = m_charts[m_selected];
                const int curDiffIdx = std::clamp(m_difficulty, 0, static_cast<int>(curChart.difficulties.size()) - 1);
                dispDiffIdx = ResolveMatchingDifficultyIndex(
                    c, curChart.difficulties[curDiffIdx].name, curChart.difficulties[curDiffIdx].level);
            }
            const auto& cardDiff = c.difficulties[dispDiffIdx];
            VisualStyle::DrawBadgePill(r,{cardRect.x+cardRect.width-0.122f,y+0.013f,0.108f,0.026f},
                cardDiff.name+" Lv."+FormatDifficultyLevel(cardDiff.level),m_font,0.0130f,
                selected?Color{104,64,98,220}:Color{36,44,74,195},
                selected?gold:Color{174,184,218,95},selected?gold:pink);

            const auto lampIt=m_chartLamps.find(c.id);
            const SelectLamp lamp=(lampIt!=m_chartLamps.end())?lampIt->second:SelectLamp::Unplayed;
            if(lamp!=SelectLamp::Unplayed) {
                const char* lampTxt=(lamp==SelectLamp::AllPerfect?"✦ AP":(lamp==SelectLamp::FullCombo?"★ FC":"● CLR"));
                const Color lampCol=(lamp==SelectLamp::AllPerfect?Color{142,232,255,255}:(lamp==SelectLamp::FullCombo?gold:Color{152,236,194,255}));
                VisualStyle::DrawBadgePill(r,{cardRect.x+cardRect.width-0.068f,y+0.050f,0.054f,0.024f},
                    lampTxt,m_font,0.012f,{28,34,58,215},lampCol,lampCol);
            } else {
                r.DrawText(m_font,std::to_string(c.difficulties.size())+" 谱面",cardRect.x+cardRect.width-0.016f,y+0.055f,0.0145f,dim,TextAlign::Right);
            }
        }
        r.Flush();
        SDL_SetRenderClipRect(raw,wasClipped?&prevClip:nullptr);
    }
    if(m_visible.size()>6) {
        constexpr NormRect trackRect{0.464f,0.242f,0.006f,0.600f};
        r.DrawRoundedRect(trackRect,0.003f,{22,28,52,185},true);
        const float maxScroll=static_cast<float>(std::max(1,static_cast<int>(m_visible.size())-6));
        const float thumbH=std::max(0.056f,trackRect.height*(6.0f/static_cast<float>(m_visible.size())));
        const float thumbY=trackRect.y+(trackRect.height-thumbH)*std::clamp(m_scrollPos/maxScroll,0.0f,1.0f);
        const bool sbHover=m_draggingScrollbar || (mx>=trackRect.x-0.008f&&mx<=trackRect.x+trackRect.width+0.008f&&my>=trackRect.y&&my<=trackRect.y+trackRect.height);
        r.DrawRoundedGradientRect({trackRect.x,thumbY,trackRect.width,thumbH},0.003f,sbHover?gold:pink,sbHover?gold:pink,pink,{156,128,210,225});
    }
    if(m_visible.empty()) {
        VisualStyle::DrawGlassCard(r,{0.06f,0.42f,0.38f,0.16f},{26,32,58,210},{16,20,40,224},{250,216,157,120},{140,150,192,48},0.014f,0.40f);
        r.DrawText(m_font,"没有匹配的曲目",0.25f,0.455f,0.029f,white,TextAlign::Center);
        r.DrawText(m_font,"清空搜索、切换灯态/定数筛选，或导入谱面",0.25f,0.515f,0.018f,dim,TextAlign::Center);
    }
    r.DrawText(m_font,std::to_string(m_visible.size())+" 首曲目  ·  ↑↓ 选曲  /  ←→ 难度  /  F2 随机  /  Enter 演奏",0.04f,0.866f,0.0165f,dim);

    VisualStyle::DrawGlassCard(r,{0.49f,0.16f,0.47f,0.72f},
        {24,30,56,216},{14,18,36,232},{250,216,157,170},{168,158,214,68},0.018f,0.56f);

    if(m_selected>=0) {
        const auto& c=m_charts[m_selected];
        const float discCX=0.585f, discCY=0.298f, coverH=0.205f, coverW=coverH*aspect;
        const float spinDeg=m_previewPlaying?(m_time*24.0f):(m_time*4.0f);

        if(m_previewPlaying) {
            sakura::audio::AudioVisualizer::GetInstance().RenderCircle(r,discCX,discCY,0.106f,{246,178,206,215},0.85f);
        }
        r.DrawRingGradient(discCX,discCY,0.094f,0.114f,{250,216,157,95},{246,178,206,0},56);
        r.DrawCircleFilled(discCX+0.022f*aspect,discCY,0.096f,{16,18,30,240},56);
        r.DrawCircleOutline(discCX+0.022f*aspect,discCY,0.096f,{250,216,157,130},0.0015f,56);
        r.DrawArc(discCX+0.022f*aspect,discCY,0.076f,spinDeg,spinDeg+110.0f,{246,178,206,115},0.0015f,36);

        const NormRect coverRect{0.515f,0.194f,coverW,coverH};
        r.DrawSoftShadow(coverRect,0.012f,0.016f,{4,6,14,175},0.005f,10);
        const auto activeCover=(m_cover!=INVALID_HANDLE)?m_cover:VisualStyle::GetDefaultCoverTexture();
        if(activeCover!=INVALID_HANDLE) {
            r.DrawSprite(activeCover,coverRect,0.0f,Color::White,1.0f);
        } else {
            r.DrawRoundedGradientRect(coverRect,0.012f,{108,64,86,255},{42,55,81,255},{26,34,54,255},{64,48,77,255},10,true);
            r.DrawText(m_font,"樱",coverRect.x+coverRect.width*0.5f,coverRect.y+0.04f,0.095f,pink,TextAlign::Center);
        }
        r.DrawRoundedRectOutlineGradient(coverRect,0.008f,{250,216,157,210},{246,178,206,115},0.0020f,10);

        const float metaX=std::max(0.66f,coverRect.x+coverRect.width+0.026f);
        const float metaW=0.94f-metaX;
        VisualStyle::DrawTextFit(r,m_font,c.title,metaX,0.228f,0.036f,metaW,white);
        VisualStyle::DrawTextFit(r,m_font,c.artist,metaX,0.288f,0.021f,metaW,dim);
        VisualStyle::DrawTextFit(r,m_font,"谱师  "+c.charter,metaX,0.328f,0.018f,metaW,dim);
        r.DrawText(m_font,Number(c.bpm,0)+" BPM   ·   "+Duration(m_chartEnd)+"   ·   "+std::to_string(m_noteCount)+" 判定",metaX,0.372f,0.018f,gold);

        VisualStyle::DrawGildedDivider(r,0.515f,0.938f,0.416f,0.68f);
        r.DrawText(m_font,"DIFFICULTY / 难度选择",0.515f,0.430f,0.016f,dim);

        if(m_best && !m_globalEntries.empty()) {
            const std::string targetRivalId=Config::GetInstance().Get<std::string>(ConfigKeys::kTargetRivalId,"");
            const auto me=sakura::net::APIClient::GetInstance().GetCurrentUser();
            const sakura::net::LeaderboardRow* rivalRow=nullptr;
            for(const auto& er:m_globalEntries) {
                if(!targetRivalId.empty() && er.userId==targetRivalId) { rivalRow=&er; break; }
            }
            if(!rivalRow && m_leaderboardViewMode==2) {
                for(const auto& er:m_globalEntries) if(er.userId!=me.id) { rivalRow=&er; break; }
            }
            if(rivalRow) {
                const std::string deltaTxt=sakura::net::APIClient::FormatScoreDeltaText(rivalRow->username,m_best->score,rivalRow->score);
                const bool ahead=(m_best->score>=rivalRow->score);
                VisualStyle::DrawBadgePill(r,{0.752f,0.428f,0.188f,0.030f},deltaTxt,m_font,0.0132f,
                    ahead?Color{28,58,52,225}:Color{74,34,54,225},ahead?Color{152,236,194,215}:pink,ahead?Color{152,236,194,255}:pink);
            }
        }

        if(m_showGlobalLeaderboard) {
            const std::string scopePrefix=(m_leaderboardViewMode==2)?"劲敌圈对决  ·  ":"全球高分榜  ·  ";
            const std::string hdr=m_globalMyRank.has_value()?(scopePrefix+"我的名次 #"+std::to_string(*m_globalMyRank)):(scopePrefix+"点击选手查看名片");
            r.DrawText(m_font,hdr,0.515f,0.533f,0.019f,gold);
            const int rows=std::min(5,static_cast<int>(m_globalEntries.size()));
            for(int i=0;i<rows;++i) {
                const auto& row=m_globalEntries[i];
                const float ry=0.572f+i*0.046f;
                r.DrawRoundedRect({0.515f,ry,0.350f,0.039f},0.007f,i==0?Color{66,44,76,195}:Color{26,32,58,185},true,6);
                r.DrawText(m_font,"#"+std::to_string(row.rank),0.524f,ry+0.008f,0.017f,i==0?gold:pink);
                const std::string userLabel=row.danBadge.empty()?row.username:("["+row.danBadge+"] "+row.username);
                VisualStyle::DrawTextFit(r,m_font,userLabel,0.558f,ry+0.008f,0.017f,0.165f,white);
                r.DrawText(m_font,std::to_string(row.score)+"  "+Number(row.accuracy,1)+"%",0.855f,ry+0.008f,0.016f,gold,TextAlign::Right);
            }
            if(m_globalEntries.empty()) {
                r.DrawRoundedRect({0.515f,0.590f,0.425f,0.120f},0.010f,{22,28,52,185},true,8);
                VisualStyle::DrawTextFit(r,m_font,m_leaderboardStatus.empty()?"暂无全球排行数据":m_leaderboardStatus,0.535f,0.638f,0.018f,0.385f,dim);
            }
        } else {
            r.DrawRoundedGradientRect({0.515f,0.525f,0.425f,0.052f},0.009f,{34,40,70,185},{24,30,54,185},{28,34,60,195},{20,24,46,195},8,true);
            r.DrawRoundedRectOutlineGradient({0.515f,0.525f,0.425f,0.052f},0.009f,{250,216,157,110},{164,174,214,45},0.0012f,8);
            if(m_best) {
                r.DrawText(m_font,"个人最佳  "+std::to_string(m_best->score)+"   ·   "+Number(m_best->accuracy,2)+"%",0.530f,0.538f,0.021f,gold);
                const char* gStr=(m_best->grade==Grade::SS?"SS":m_best->grade==Grade::S?"S":m_best->grade==Grade::A?"A":m_best->grade==Grade::B?"B":m_best->grade==Grade::C?"C":"D");
                VisualStyle::DrawBadgePill(r,{0.872f,0.535f,0.056f,0.032f},gStr,m_font,0.017f,{72,48,84,225},gold,gold);
            } else {
                r.DrawText(m_font,"个人最佳  —  等待第一次演奏",0.530f,0.538f,0.020f,pink);
            }

            if(m_options.mode==PlayMode::Standard && m_dailyActiveSpec.has_value() && IsSelectedMatchingDaily()) {
                r.DrawRoundedGradientRect({0.515f,0.652f,0.425f,0.044f},0.008f,{68,44,78,205},{36,30,62,205},{52,36,68,215},{28,24,50,215},8,true);
                r.DrawRoundedRectOutlineGradient({0.515f,0.652f,0.425f,0.044f},0.008f,{250,216,157,175},{246,178,206,95},0.0013f,8);
                const std::string goalStr=!m_dailyActiveSpec->goalText.empty()?m_dailyActiveSpec->goalText:("目标 "+std::to_string(m_dailyActiveSpec->targetScore));
                const std::string rankTag=m_dailyMyRank.has_value()?(" · #"+std::to_string(*m_dailyMyRank)):"";
                VisualStyle::DrawTextFit(r,m_font,"今日挑战 ["+m_dailyActiveSpec->dateUtc8+"] · "+goalStr+rankTag,0.528f,0.663f,0.016f,0.312f,m_playAsDaily?gold:dim);
            } else {
                r.DrawText(m_font,m_options.mode==PlayMode::Standard?"正式成绩将保存到记录与统计":"辅助模式 · 不计入正式成绩与成就",0.515f,0.656f,0.019f,dim);
            }
            if(m_options.mode==PlayMode::Standard && m_options.ghostMode!=GhostMode::Off) {
                std::string ghostHint;
                Color ghostColor=pink;
                if(m_options.ghostMode==GhostMode::PersonalBest) {
                    ghostHint=m_best?("目标  "+std::to_string(m_best->score)):"暂无本地 PB 回放";
                    ghostColor=m_best?gold:dim;
                } else if(m_options.ghostMode==GhostMode::OnlineRival) {
                    if(!m_globalEntries.empty() && m_globalEntries[0].hasReplay && !m_globalEntries[0].scoreId.empty()) {
                        ghostHint="对手 #1 "+m_globalEntries[0].username;
                        ghostColor=gold;
                    } else if(m_leaderboardLoading || m_leaderboardPendingFetch) {
                        ghostHint="正在同步榜首...";
                        ghostColor=dim;
                    } else {
                        ghostHint="无线上回放 · 自动回退";
                        ghostColor=dim;
                    }
                }
                if(!ghostHint.empty()) {
                    VisualStyle::DrawTextFit(r,m_font,ghostHint,0.760f,0.720f,0.0155f,0.178f,ghostColor);
                }
            }
            if(m_options.mode!=PlayMode::Standard) r.DrawText(m_font,"播放速度  "+Number(m_options.rate)+"×",0.515f,0.654f,0.021f,gold);

            // Dual-Zone NPS Density Curve (Gold = Keyboard 4K/Thumb, Pink = Mouse Circle/Slider)
            const NormRect npsBox=(m_options.mode==PlayMode::Practice)
                ? NormRect{0.515f,0.692f,0.425f,0.058f}
                : NormRect{0.515f,0.754f,0.425f,0.062f};
            r.DrawRoundedRect(npsBox,0.008f,{16,20,38,200},true);
            r.DrawRoundedRectOutlineGradient(npsBox,0.008f,{250,216,157,95},{140,150,196,42},0.0010f);
            r.DrawText(m_font,"NPS DENSITY · 键(金) / 鼠(粉)",npsBox.x+0.010f,npsBox.y+0.005f,0.012f,dim);
            r.DrawText(m_font,"Peak "+Number(m_npsProfile.peakTotalNps,1)+" NPS",npsBox.x+npsBox.width-0.010f,npsBox.y+0.005f,0.012f,gold,TextAlign::Right);
            if(!m_npsProfile.keyboardNps.empty() && m_npsProfile.peakTotalNps>0.01f) {
                const int nB=static_cast<int>(m_npsProfile.keyboardNps.size());
                const float plotX=npsBox.x+0.008f, plotW=npsBox.width-0.016f;
                const float plotBot=npsBox.y+npsBox.height-0.006f, plotH=npsBox.height-0.024f;
                const float peak=std::max(1.0f,m_npsProfile.peakTotalNps);
                for(int bi=0;bi+1<nB;++bi) {
                    const float x1=plotX+(static_cast<float>(bi)/static_cast<float>(nB-1))*plotW;
                    const float x2=plotX+(static_cast<float>(bi+1)/static_cast<float>(nB-1))*plotW;
                    const float kb1=std::clamp(m_npsProfile.keyboardNps[bi]/peak,0.0f,1.0f);
                    const float kb2=std::clamp(m_npsProfile.keyboardNps[bi+1]/peak,0.0f,1.0f);
                    const float tot1=std::clamp((m_npsProfile.keyboardNps[bi]+m_npsProfile.mouseNps[bi])/peak,0.0f,1.0f);
                    const float tot2=std::clamp((m_npsProfile.keyboardNps[bi+1]+m_npsProfile.mouseNps[bi+1])/peak,0.0f,1.0f);
                    r.DrawLine(x1,plotBot-tot1*plotH,x2,plotBot-tot2*plotH,pink,0.0018f);
                    r.DrawLine(x1,plotBot-kb1*plotH,x2,plotBot-kb2*plotH,gold,0.0018f);
                }
            }
            if(m_options.mode==PlayMode::Practice) {
                const float durSec=std::max(1.0f,static_cast<float>(m_chartEnd)/1000.0f);
                float sSec=0.0f, eSec=durSec;
                try { sSec=std::clamp(std::stof(m_startInput->GetText()),0.0f,durSec); } catch(...) {}
                try { const float rawE=std::stof(m_endInput->GetText()); eSec=(rawE<=0.0f)?durSec:std::clamp(rawE,sSec,durSec); } catch(...) {}
                const float hx1=npsBox.x+0.008f+(sSec/durSec)*(npsBox.width-0.016f);
                const float hx2=npsBox.x+0.008f+(eSec/durSec)*(npsBox.width-0.016f);
                r.DrawFilledRect({hx1,npsBox.y+0.018f,std::max(0.002f,hx2-hx1),npsBox.height-0.022f},{250,216,157,42});
                r.DrawLine(hx1,npsBox.y+0.014f,hx1,npsBox.y+npsBox.height-0.003f,gold,0.0024f);
                r.DrawLine(hx2,npsBox.y+0.014f,hx2,npsBox.y+npsBox.height-0.003f,pink,0.0024f);
                r.DrawText(m_font,"秒",0.522f,0.77f,0.02f,dim);r.DrawText(m_font,"至",0.674f,0.77f,0.02f,dim);m_startInput->Render(r);m_endInput->Render(r);
            }
        }

        if(m_previewPlaying && m_options.mode!=PlayMode::Practice && m_showGlobalLeaderboard) {
            sakura::audio::AudioVisualizer::GetInstance().RenderWave(r,{0.515f,0.792f,0.425f,0.024f},{246,178,206,150},0.65f);
        }
        for(auto& b:m_detailButtons)b->Render(r);
    }
    for(auto& b:m_buttons)b->Render(r);
    m_particles.Render(r);
    if(m_profileModal) m_profileModal->Render(r);
    if(m_topBar) {
        m_topBar->Render(r);
        m_topBar->RenderDrawer(r);
    }
}
void SceneSelect::OnEvent(const SDL_Event& e) {
    if(m_topBar && m_topBar->HandleEvent(e)) return;
    if(m_profileModal && m_profileModal->HandleEvent(e)) return;
    if(m_showGlobalLeaderboard && e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) {
        const auto [mx,my]=Input::GetMousePosition();
        const int rows=std::min(5,static_cast<int>(m_globalEntries.size()));
        for(int i=0;i<rows;++i) {
            const float ry=0.572f+i*0.046f;
            if(mx>=0.515f && mx<=0.865f && my>=ry && my<=ry+0.039f && !m_globalEntries[i].userId.empty()) {
                sakura::audio::AudioManager::GetInstance().PlayUISFX(sakura::audio::UISFXType::ButtonClick);
                m_profileModal->Open(m_globalEntries[i].userId,m_globalEntries[i].username);
                return;
            }
        }
    }
    if(e.type==SDL_EVENT_DROP_FILE && e.drop.data){AcceptImport(e.drop.data);return;}
    if(e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) {
        m_draggingScrollbar=false;
        m_draggingPracticeHandle=0;
    }
    auto mouse=Input::GetMousePosition();
    if(e.type==SDL_EVENT_MOUSE_MOTION) {
        if(m_draggingScrollbar && m_visible.size()>6) {
            const float maxScroll=static_cast<float>(std::max(1,static_cast<int>(m_visible.size())-6));
            const float ratio=std::clamp((mouse.y-0.242f)/0.600f,0.0f,1.0f);
            m_scrollTarget=ratio*maxScroll;
            m_scrollPos=m_scrollTarget;
            return;
        }
        if(m_draggingPracticeHandle!=0 && m_options.mode==PlayMode::Practice && m_chartEnd>0) {
            const float durSec=std::max(1.0f,static_cast<float>(m_chartEnd)/1000.0f);
            const float frac=std::clamp((mouse.x-0.523f)/0.409f,0.0f,1.0f);
            const int sec=static_cast<int>(std::lround(frac*durSec));
            if(m_draggingPracticeHandle==1) m_startInput->SetText(std::to_string(std::max(0,sec)));
            else m_endInput->SetText(std::to_string(std::max(1,sec)));
            return;
        }
    }
    bool consumed=m_search->HandleEvent(e);
    if(m_options.mode==PlayMode::Practice){consumed=m_startInput->HandleEvent(e)||consumed;consumed=m_endInput->HandleEvent(e)||consumed;}
    if(consumed && (e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_TEXT_INPUT || e.type==SDL_EVENT_TEXT_EDITING))return;
    const bool typing=m_search->IsFocused()||m_startInput->IsFocused()||m_endInput->IsFocused();
    if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat&&!typing) {
        if(e.key.scancode==SDL_SCANCODE_ESCAPE){Back();return;}
        if(e.key.scancode==SDL_SCANCODE_F5){Scan();return;}
        if(e.key.scancode==SDL_SCANCODE_F2){TriggerRandomRoulette();return;}
        if(e.key.scancode==SDL_SCANCODE_F&&(e.key.mod&SDL_KMOD_CTRL)){m_search->SetFocused(true);return;}
        if(e.key.scancode==SDL_SCANCODE_UP){SelectRelative(-1);return;}
        if(e.key.scancode==SDL_SCANCODE_DOWN){SelectRelative(1);return;}
        if(e.key.scancode==SDL_SCANCODE_PAGEUP){SelectRelative(-6);return;}
        if(e.key.scancode==SDL_SCANCODE_PAGEDOWN){SelectRelative(6);return;}
        if(e.key.scancode==SDL_SCANCODE_HOME && !m_visible.empty()){Select(m_visible.front());return;}
        if(e.key.scancode==SDL_SCANCODE_END && !m_visible.empty()){Select(m_visible.back());return;}
        if(e.key.scancode==SDL_SCANCODE_RETURN){Start(m_options.mode);return;}
        if(m_selected>=0&&(e.key.scancode==SDL_SCANCODE_LEFT||e.key.scancode==SDL_SCANCODE_RIGHT)){Select(m_selected,m_difficulty+(e.key.scancode==SDL_SCANCODE_LEFT?-1:1));return;}
    }
    if(e.type==SDL_EVENT_MOUSE_WHEEL&&mouse.x<0.475f){
        const float maxScroll=static_cast<float>(std::max(0,static_cast<int>(m_visible.size())-6));
        m_scrollTarget=std::clamp(m_scrollTarget-e.wheel.y*0.85f,0.0f,maxScroll);
        PersistSelectContext(false);
        return;
    }
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button==SDL_BUTTON_LEFT) {
        if(m_visible.size()>6 && mouse.x>=0.458f && mouse.x<=0.478f && mouse.y>=0.240f && mouse.y<=0.848f) {
            m_draggingScrollbar=true;
            const float maxScroll=static_cast<float>(std::max(1,static_cast<int>(m_visible.size())-6));
            m_scrollTarget=std::clamp((mouse.y-0.242f)/0.600f,0.0f,1.0f)*maxScroll;
            m_scrollPos=m_scrollTarget;
            return;
        }
        if(m_selected>=0 && !m_showGlobalLeaderboard && m_options.mode==PlayMode::Practice &&
           mouse.x>=0.515f && mouse.x<=0.940f && mouse.y>=0.692f && mouse.y<=0.750f && m_chartEnd>0) {
            const float durSec=std::max(1.0f,static_cast<float>(m_chartEnd)/1000.0f);
            float sSec=0.0f, eSec=durSec;
            try { sSec=std::clamp(std::stof(m_startInput->GetText()),0.0f,durSec); } catch(...) {}
            try { const float rawE=std::stof(m_endInput->GetText()); eSec=(rawE<=0.0f)?durSec:std::clamp(rawE,sSec,durSec); } catch(...) {}
            const float hx1=0.523f+(sSec/durSec)*0.409f;
            const float hx2=0.523f+(eSec/durSec)*0.409f;
            m_draggingPracticeHandle=(std::abs(mouse.x-hx1)<=std::abs(mouse.x-hx2))?1:2;
            const int sec=static_cast<int>(std::lround(std::clamp((mouse.x-0.523f)/0.409f,0.0f,1.0f)*durSec));
            if(m_draggingPracticeHandle==1) m_startInput->SetText(std::to_string(std::max(0,sec)));
            else m_endInput->SetText(std::to_string(std::max(1,sec)));
            return;
        }
        if(mouse.x>=0.04f&&mouse.x<0.458f&&mouse.y>=0.24f&&mouse.y<0.852f){
            const int row=static_cast<int>(std::floor(((mouse.y-0.24f)/0.102f)+m_scrollPos));
            if(row>=0 && row<static_cast<int>(m_visible.size())){Select(m_visible[row]);if(e.button.clicks==2)Start(m_options.mode);return;}
        }
    }
    for(auto& b:m_buttons)if(b->HandleEvent(e))return;
    if(m_selected>=0) for(auto& b:m_detailButtons)if(b->HandleEvent(e))return;
}
}
