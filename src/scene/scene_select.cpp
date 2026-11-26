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
void SceneSelect::OnEnter() {
    m_font=ResourceManager::GetInstance().GetDefaultFontHandle();
    auto& cfg=Config::GetInstance();
    const auto favorites=cfg.Get<std::vector<std::string>>("library.favorites",{});
    m_favorites={favorites.begin(),favorites.end()};
    m_collections=CollectionManager::LoadFromConfig();
    m_search=std::make_unique<TextInput>(NormRect{0.04f,0.16f,0.28f,0.051f},m_font,0.022f);
    m_search->SetPlaceholder("搜索曲名、曲师、标签  /  Ctrl+F");
    m_search->SetOnChange([this](const std::string&){Filter();});
    m_startInput=std::make_unique<TextInput>(NormRect{0.555f,0.762f,0.105f,0.040f},m_font,0.022f,8);
    m_endInput=std::make_unique<TextInput>(NormRect{0.698f,0.762f,0.105f,0.040f},m_font,0.022f,8);
    m_startInput->SetText("0"); m_endInput->SetText("0");
    m_buttons.push_back(Button({0.326f,0.16f,0.072f,0.051f},"全部",[this]{
        const int totalStates=2+static_cast<int>(m_collections.size());
        m_collectionFilter=(m_collectionFilter+1)%totalStates;
        if(m_collectionFilter>=2) m_activeCollectionIndex=m_collectionFilter-2;
        Filter();
    },false,0.0185f));
    m_buttons.push_back(Button({0.402f,0.16f,0.058f,0.051f},"曲名",[this]{m_sort=(m_sort+1)%3;Filter();},false,0.019f));

    auto dailyBtn=Button({0.355f,0.064f,0.082f,0.044f},"今日挑战",[this]{ActivateDailyChallenge();},false,0.018f);
    VisualStyle::ApplyButton(dailyBtn.get(),ButtonVariant::Accent);
    m_buttons.push_back(std::move(dailyBtn));
    m_buttons.push_back(Button({0.442f,0.064f,0.082f,0.044f},"樱华道场",[this]{m_manager.SwitchScene(std::make_unique<SceneCourse>(m_manager),TransitionType::SilkShutter,0.38f);},false,0.018f));
    m_buttons.push_back(Button({0.529f,0.064f,0.082f,0.044f},"谱面市场",[this]{m_manager.SwitchScene(std::make_unique<SceneMarket>(m_manager),TransitionType::SilkShutter,0.38f);},true,0.018f));
    m_buttons.push_back(Button({0.616f,0.064f,0.081f,0.044f},"导入谱面",[this]{ImportFolder();},false,0.018f));
    m_buttons.push_back(Button({0.702f,0.064f,0.081f,0.044f},"新建谱面",[this]{m_manager.SwitchScene(std::make_unique<SceneChartWizard>(m_manager),TransitionType::SilkShutter,0.38f);},false,0.018f));
    m_buttons.push_back(Button({0.788f,0.064f,0.081f,0.044f},"打开曲库",[]{const std::filesystem::path path=Paths::User("charts");std::error_code ec;std::filesystem::create_directories(path,ec);SDL_OpenURL(("file:///"+path.generic_string()).c_str());},false,0.018f));
    m_buttons.push_back(Button({0.874f,0.064f,0.086f,0.044f},"偏好设置",[this]{m_manager.SwitchScene(std::make_unique<SceneSettings>(m_manager),TransitionType::SilkShutter,0.38f);},false,0.018f));

    m_buttons.push_back(Button({0.04f,0.909f,0.12f,0.057f},"返回主页",[this]{Back();}));
    m_buttons.push_back(Button({0.17f,0.909f,0.09f,0.057f},"刷新 / F5",[this]{Scan();}));
    m_buttons.push_back(Button({0.77f,0.909f,0.19f,0.057f},"开始演奏   →",[this]{Start(m_options.mode);},true));
    m_particles.Clear();
    m_petalEmitter=m_particles.EmitContinuous(0.5f,-0.02f,2.6f,sakura::effects::ParticlePresets::SakuraPetal());
    Scan();
}
void SceneSelect::Scan() {
    m_collections=CollectionManager::LoadFromConfig();
    if(m_collectionFilter>1+static_cast<int>(m_collections.size())) m_collectionFilter=0;
    if(m_activeCollectionIndex>=static_cast<int>(m_collections.size())) m_activeCollectionIndex=0;
    ChartLoader loader;
    m_charts=loader.ScanCharts("resources/charts");
    auto custom=loader.ScanCharts(Paths::User("charts"));
    for(auto& c:custom) {
        auto existing=std::find_if(m_charts.begin(),m_charts.end(),[&](const ChartInfo& other){return other.id==c.id;});
        if(existing==m_charts.end()) m_charts.push_back(std::move(c)); else *existing=std::move(c);
    }
    m_selected=-1; Filter();
    const auto last=Config::GetInstance().Get<std::string>("library.last_chart","");
    for(int i:m_visible) if(m_charts[i].id==last) {Select(i,Config::GetInstance().Get<int>("library.last_difficulty",0));break;}
}
void SceneSelect::Filter() {
    if(m_collectionFilter<0 || m_collectionFilter>1+static_cast<int>(m_collections.size())) m_collectionFilter=0;
    m_visible.clear(); const auto query=Lower(m_search->GetText());
    for(int i=0;i<static_cast<int>(m_charts.size());++i) {
        const auto& c=m_charts[i];
        bool matchCol=true;
        if(m_collectionFilter==1) matchCol=m_favorites.contains(c.id);
        else if(m_collectionFilter>=2) matchCol=CollectionManager::ContainsChart(m_collections[static_cast<size_t>(m_collectionFilter-2)],c.id);
        if(!matchCol) continue;
        std::string hay=c.title+" "+c.artist+" "+c.charter;
        for(const auto& tag:c.tags) hay+=" "+tag;
        if(Lower(hay).find(query)!=std::string::npos) m_visible.push_back(i);
    }
    std::stable_sort(m_visible.begin(),m_visible.end(),[this](int a,int b){
        if(m_sort==1 && m_charts[a].bpm!=m_charts[b].bpm) return m_charts[a].bpm<m_charts[b].bpm;
        if(m_sort==2 && m_charts[a].difficulties.back().level!=m_charts[b].difficulties.back().level) return m_charts[a].difficulties.back().level<m_charts[b].difficulties.back().level;
        return m_charts[a].title<m_charts[b].title;
    });
    m_scroll=0;
    if(std::find(m_visible.begin(),m_visible.end(),m_selected)==m_visible.end()) {
        m_selected=-1; if(!m_visible.empty()) Select(m_visible.front());
    }
    if(m_buttons.size()>=2) {
        if(m_collectionFilter==0) {m_buttons[0]->SetText("全部");m_buttons[0]->SetFontSize(0.019f);}
        else if(m_collectionFilter==1) {m_buttons[0]->SetText("已收藏");m_buttons[0]->SetFontSize(0.0185f);}
        else {m_buttons[0]->SetText(m_collections[static_cast<size_t>(m_collectionFilter-2)].name);m_buttons[0]->SetFontSize(0.016f);}
        VisualStyle::ApplyButton(m_buttons[0].get(),m_collectionFilter>0?ButtonVariant::Primary:ButtonVariant::Secondary);
        m_buttons[1]->SetText(m_sort==0?"曲名":m_sort==1?"BPM":"难度");
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
        sakura::audio::AudioManager::GetInstance().FadeOutMusic(150); m_previewTimer=0; m_previewPlaying=false;
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
    Config::GetInstance().Set("library.last_chart",c.id); Config::GetInstance().Set("library.last_difficulty",m_difficulty);
    ChartLoader loader; auto chart=loader.LoadChartData(c.folderPath+"/"+c.difficulties[m_difficulty].chartFile);
    m_best=chart?sakura::data::Database::GetInstance().GetBestScore(c.id,c.difficulties[m_difficulty].name,Replay::Hash(*chart,c.offset)):std::nullopt;
    m_noteCount=chart?JudgmentCount(*chart):0; m_chartEnd=chart?ChartEndTime(*chart):0;
    if(changed || diffChanged) {
        m_globalEntries.clear();
        m_globalMyRank.reset();
        m_leaderboardStatus.clear();
        m_leaderboardLoading=false;
    }
    if(m_showGlobalLeaderboard || (m_options.ghostMode==GhostMode::OnlineRival && m_globalEntries.empty())) {
        FetchGlobalLeaderboard();
    }
    m_rebuild=true;
    const auto it=std::find(m_visible.begin(),m_visible.end(),index);
    if(it!=m_visible.end()) {const int row=static_cast<int>(it-m_visible.begin());if(row<m_scroll)m_scroll=row;if(row>=m_scroll+6)m_scroll=row-5;}
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
void SceneSelect::FetchGlobalLeaderboard() {
    if(m_selected<0) return;
    const auto& c=m_charts[m_selected];
    const std::string chartId=c.id;
    const std::string diffName=c.difficulties[m_difficulty].name;
    m_leaderboardLoading=true;
    m_leaderboardStatus="正在加载全球排行榜...";
    m_globalEntries.clear();
    m_globalMyRank.reset();
    auto alive=m_alive;
    sakura::net::APIClient::GetInstance().FetchLeaderboardAsync(chartId,diffName,6,[this,alive,chartId,diffName](sakura::net::LeaderboardResult res){
        if(!*alive) return;
        if(m_selected<0 || m_charts[m_selected].id!=chartId || m_charts[m_selected].difficulties[m_difficulty].name!=diffName) return;
        m_leaderboardLoading=false;
        if(!res.ok) {
            m_leaderboardStatus="离线或暂不可用："+res.error;
            m_rebuild=true;
            return;
        }
        m_globalMyRank=res.myRank;
        m_globalEntries=std::move(res.entries);
        m_leaderboardStatus=m_globalEntries.empty()?"暂无上榜记录 · 完成正式演奏抢占榜首":"";
        m_rebuild=true;
    });
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
    m_detailButtons.push_back(Button({0.664f,0.185f,0.082f,0.036f},m_showGlobalLeaderboard?"本地面板":"全球排行",[this]{
        m_showGlobalLeaderboard=!m_showGlobalLeaderboard;
        if(m_showGlobalLeaderboard) FetchGlobalLeaderboard();
        m_rebuild=true;
    },m_showGlobalLeaderboard,0.0165f));
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
                if(m_options.ghostMode==GhostMode::OnlineRival && m_globalEntries.empty() && !m_leaderboardLoading) {
                    FetchGlobalLeaderboard();
                }
                m_rebuild=true;
            },m_options.ghostMode!=GhostMode::Off,0.0165f));
        }
        if(m_options.mode!=PlayMode::Standard) {
            m_detailButtons.push_back(Button({0.815f,0.677f,0.057f,0.037f},"−",[this]{m_options.rate=std::max(0.5f,m_options.rate-0.1f);}));
            m_detailButtons.push_back(Button({0.883f,0.677f,0.057f,0.037f},"+",[this]{m_options.rate=std::min(2.0f,m_options.rate+0.1f);}));
        }
        if(m_options.mode==PlayMode::Practice) m_detailButtons.push_back(Button({0.825f,0.762f,0.116f,0.040f},m_options.loop?"循环：开":"循环：关",[this]{m_options.loop=!m_options.loop;m_rebuild=true;}));
    }
    if(!m_buttons.empty()) {
        const bool asDaily=(m_options.mode==PlayMode::Standard && m_playAsDaily && IsSelectedMatchingDaily());
        m_buttons.back()->SetText(asDaily?"开始今日挑战 →":"开始演奏   →");
    }
    m_detailButtons.push_back(Button({0.515f,0.827f,0.18f,0.040f},"播放最近回放",[this]{Start(PlayMode::Replay);}));
    m_detailButtons.push_back(Button({0.711f,0.827f,0.23f,0.040f},"在工房中编辑",[this]{const auto& c=m_charts[m_selected];m_manager.SwitchScene(std::make_unique<SceneEditor>(m_manager,c.folderPath,c.difficulties[m_difficulty].chartFile),TransitionType::SilkShutter,0.38f);}));
}
void SceneSelect::Favorite() {
    const auto id=m_charts[m_selected].id;
    if(m_favorites.contains(id)) m_favorites.erase(id); else m_favorites.insert(id);
    Config::GetInstance().Set("library.favorites",std::vector<std::string>(m_favorites.begin(),m_favorites.end()));
    m_rebuild=true;Filter();
}
void SceneSelect::Start(PlayMode mode) {
    if(m_selected<0 || m_ghostDownloading) return;
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
    auto* context=new std::shared_ptr<DialogState>(m_dialog);
    SDL_ShowOpenFolderDialog([](void* raw,const char* const* files,int){
        std::unique_ptr<std::shared_ptr<DialogState>> state(static_cast<std::shared_ptr<DialogState>*>(raw));
        std::lock_guard lock((*state)->mutex);(*state)->path=files&&files[0]?files[0]:"";(*state)->ready=true;
    },context,SDL_GetKeyboardFocus(),nullptr,false);
}
void SceneSelect::AcceptImport(const std::string& path) {
    if(path.empty())return;
    const auto target=std::filesystem::path(Paths::User("imports"))/("import-"+std::to_string(SDL_GetTicksNS()));
    try {
        const std::filesystem::path srcPath(path);
        if(std::filesystem::is_regular_file(srcPath)) {
            const auto ext = Lower(srcPath.extension().string());
            if(ext != ".sakura" && ext != ".zip")
                throw std::runtime_error("仅支持导入谱面文件夹或 .sakura / .zip 谱面包");
            if(!sakura::utils::ExtractZipArchive(srcPath, target))
                throw std::runtime_error(".sakura 谱面包校验或解压失败");
        } else if(std::filesystem::is_directory(srcPath)) {
            std::filesystem::create_directories(target);
            for(const auto& entry:std::filesystem::recursive_directory_iterator(srcPath)) {
                if(entry.is_symlink())throw std::runtime_error("谱面目录不能包含符号链接");
                const auto dest=target/std::filesystem::relative(entry.path(),srcPath);
                if(entry.is_directory())std::filesystem::create_directories(dest);
                else if(entry.is_regular_file())std::filesystem::copy_file(entry.path(),dest);
            }
        } else {
            throw std::runtime_error("未找到要导入的谱面路径");
        }
        ChartLoader loader;auto info=loader.LoadChartInfo((target/"info.json").string());
        if(!info)throw std::runtime_error("谱面包缺少有效的 info.json");
        for(const auto& diff:info->difficulties)if(!loader.LoadChartData(info->folderPath+"/"+diff.chartFile))throw std::runtime_error("谱面数据无效："+diff.name);
        if(!std::filesystem::is_regular_file(target/info->musicFile))throw std::runtime_error("没有找到音乐文件");
        const auto destination=std::filesystem::path(Paths::User("charts"))/target.filename();
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::rename(target,destination);
        Scan();ToastManager::Instance().Show("谱面已导入曲库："+info->title,ToastType::Success);
    }catch(const std::exception& e){std::error_code ec;std::filesystem::remove_all(target,ec);ToastManager::Instance().Show(e.what(),ToastType::Error,5);}
}
void SceneSelect::Back(){m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager),TransitionType::SilkShutter,0.36f);}
void SceneSelect::OnExit(){
    *m_alive=false;
    if(m_petalEmitter>=0)m_particles.StopEmitter(m_petalEmitter);
    m_particles.Clear();
    m_bgCover.UnloadImage();
    ResourceManager::GetInstance().UnloadTexture(m_cover);
    m_cover=INVALID_HANDLE;
    sakura::audio::AudioManager::GetInstance().StopMusic();
    m_search->SetFocused(false);m_startInput->SetFocused(false);m_endInput->SetFocused(false);
    Config::GetInstance().Save();
}

void SceneSelect::OnUpdate(float dt) {
    m_time+=dt;if(m_rebuild)BuildDetail();
    std::string imported;{std::lock_guard lock(m_dialog->mutex);if(m_dialog->ready){imported=std::move(m_dialog->path);m_dialog->ready=false;}}if(!imported.empty())AcceptImport(imported);
    m_search->Update(dt);m_startInput->Update(dt);m_endInput->Update(dt);
    m_bgCover.Update(dt);
    m_particles.Update(dt);
    for(auto& b:m_buttons)b->Update(dt);for(auto& b:m_detailButtons)b->Update(dt);
    auto& audio=sakura::audio::AudioManager::GetInstance();
    if(m_selected>=0) {
        m_previewTimer+=dt;
        if(!m_previewPlaying&&m_previewTimer>0.45f) {
            const auto& c=m_charts[m_selected];audio.SetPlaybackSpeed(1);m_previewPlaying=true;
            if(!audio.PlayMusic(c.folderPath+"/"+c.musicFile,0,c.previewTime/1000.0))ToastManager::Instance().Show("音乐预览不可用，请检查音频文件",ToastType::Warning);
        }
        if(m_previewPlaying&&m_previewTimer>16){audio.FadeOutMusic(400);m_previewPlaying=false;m_previewTimer=-0.5f;}
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
    for(int row=0;row<6&&row+m_scroll<static_cast<int>(m_visible.size());++row) {
        const int index=m_visible[row+m_scroll];const auto& c=m_charts[index];const float y=0.24f+row*0.102f;const bool selected=index==m_selected;
        const bool hovered=(mx>=0.04f&&mx<=0.46f&&my>=y&&my<y+0.090f);
        const float indent=selected?0.008f:(hovered?0.004f:0.0f);
        const NormRect cardRect{0.04f+indent,y,0.42f-indent,0.090f};

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
        VisualStyle::DrawTextFit(r,m_font,c.title,cardRect.x+0.046f,y+0.011f,0.028f,0.275f,white);
        VisualStyle::DrawTextFit(r,m_font,c.artist+"  ·  "+Number(c.bpm,0)+" BPM",cardRect.x+0.046f,y+0.052f,0.017f,0.275f,dim);

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
        VisualStyle::DrawBadgePill(r,{cardRect.x+cardRect.width-0.124f,y+0.014f,0.110f,0.026f},
            cardDiff.name+" Lv."+FormatDifficultyLevel(cardDiff.level),m_font,0.0132f,
            selected?Color{104,64,98,220}:Color{36,44,74,195},
            selected?gold:Color{174,184,218,95},selected?gold:pink);
        r.DrawText(m_font,std::to_string(c.difficulties.size())+" 谱面",cardRect.x+cardRect.width-0.016f,y+0.056f,0.015f,dim,TextAlign::Right);
    }
    if(m_visible.empty()) {
        VisualStyle::DrawGlassCard(r,{0.06f,0.42f,0.38f,0.16f},{26,32,58,210},{16,20,40,224},{250,216,157,120},{140,150,192,48},0.014f,0.40f);
        r.DrawText(m_font,"没有匹配的曲目",0.25f,0.455f,0.029f,white,TextAlign::Center);
        r.DrawText(m_font,"清空搜索、切换曲单筛选，或导入谱面",0.25f,0.515f,0.018f,dim,TextAlign::Center);
    }
    r.DrawText(m_font,std::to_string(m_visible.size())+" 首曲目   ·   ↑ ↓ 选曲   /   ← → 难度   /   Enter 演奏",0.04f,0.866f,0.017f,dim);

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

        if(m_showGlobalLeaderboard) {
            const std::string hdr=m_globalMyRank.has_value()?("全球高分榜  ·  我的名次 #"+std::to_string(*m_globalMyRank)):"全球高分榜  ·  GLOBAL TOP";
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
                    } else if(m_leaderboardLoading) {
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
            if(m_options.mode!=PlayMode::Standard)r.DrawText(m_font,"播放速度  "+Number(m_options.rate)+"×",0.515f,0.698f,0.022f,gold);
            if(m_options.mode==PlayMode::Practice){r.DrawText(m_font,"秒",0.522f,0.77f,0.02f,dim);r.DrawText(m_font,"至",0.674f,0.77f,0.02f,dim);m_startInput->Render(r);m_endInput->Render(r);}
            else r.DrawText(m_font,"A / S / D / F / Space  +  鼠标    ·    Esc 暂停",0.515f,0.760f,0.019f,dim);
        }

        if(m_previewPlaying) {
            sakura::audio::AudioVisualizer::GetInstance().RenderWave(r,{0.515f,0.792f,0.425f,0.024f},{246,178,206,150},0.65f);
        }
        for(auto& b:m_detailButtons)b->Render(r);
    }
    for(auto& b:m_buttons)b->Render(r);
    m_particles.Render(r);
}
void SceneSelect::OnEvent(const SDL_Event& e) {
    if(e.type==SDL_EVENT_DROP_FILE && e.drop.data){AcceptImport(e.drop.data);return;}
    bool consumed=m_search->HandleEvent(e);
    if(m_options.mode==PlayMode::Practice){consumed=m_startInput->HandleEvent(e)||consumed;consumed=m_endInput->HandleEvent(e)||consumed;}
    if(consumed && (e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_TEXT_INPUT))return;
    const bool typing=m_search->IsFocused()||m_startInput->IsFocused()||m_endInput->IsFocused();
    if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat&&!typing) {
        if(e.key.scancode==SDL_SCANCODE_ESCAPE){Back();return;}
        if(e.key.scancode==SDL_SCANCODE_F5){Scan();return;}
        if(e.key.scancode==SDL_SCANCODE_F&&(e.key.mod&SDL_KMOD_CTRL)){m_search->SetFocused(true);return;}
        if(e.key.scancode==SDL_SCANCODE_UP){SelectRelative(-1);return;}
        if(e.key.scancode==SDL_SCANCODE_DOWN){SelectRelative(1);return;}
        if(e.key.scancode==SDL_SCANCODE_RETURN){Start(m_options.mode);return;}
        if(m_selected>=0&&(e.key.scancode==SDL_SCANCODE_LEFT||e.key.scancode==SDL_SCANCODE_RIGHT)){Select(m_selected,m_difficulty+(e.key.scancode==SDL_SCANCODE_LEFT?-1:1));return;}
    }
    auto mouse=Input::GetMousePosition();
    if(e.type==SDL_EVENT_MOUSE_WHEEL&&mouse.x<0.47f){m_scroll=std::clamp(m_scroll-static_cast<int>(e.wheel.y),0,std::max(0,static_cast<int>(m_visible.size())-6));}
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button==SDL_BUTTON_LEFT&&mouse.x>=0.04f&&mouse.x<=0.46f&&mouse.y>=0.24f&&mouse.y<0.852f){
        const int row=static_cast<int>((mouse.y-0.24f)/0.102f)+m_scroll;
        if(row<static_cast<int>(m_visible.size())){Select(m_visible[row]);if(e.button.clicks==2)Start(m_options.mode);return;}
    }
    for(auto& b:m_buttons)if(b->HandleEvent(e))return;
    if(m_selected>=0) for(auto& b:m_detailButtons)if(b->HandleEvent(e))return;
}
}
