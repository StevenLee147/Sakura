#include "scene_select.h"
#include "scene_menu.h"
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
constexpr Color pink{235,173,194,255}, dim{158,169,192,255}, white{238,233,239,255};
}
std::unique_ptr<Button> SceneSelect::Button(NormRect rect, const std::string& label, std::function<void()> action, bool primary) {
    auto b=std::make_unique<sakura::ui::Button>(rect,label,m_font,0.021f,0.008f);
    b->SetTextAlign(TextAlign::Center); b->SetOnClick(std::move(action));
    VisualStyle::ApplyButton(b.get(),primary?ButtonVariant::Primary:ButtonVariant::Secondary); return b;
}
void SceneSelect::OnEnter() {
    m_font=ResourceManager::GetInstance().GetDefaultFontHandle();
    auto& cfg=Config::GetInstance();
    const auto favorites=cfg.Get<std::vector<std::string>>("library.favorites",{});
    m_favorites={favorites.begin(),favorites.end()};
    m_search=std::make_unique<TextInput>(NormRect{0.04f,0.16f,0.28f,0.051f},m_font,0.022f);
    m_search->SetPlaceholder("搜索曲名、曲师、标签  /  Ctrl+F");
    m_search->SetOnChange([this](const std::string&){Filter();});
    m_startInput=std::make_unique<TextInput>(NormRect{0.555f,0.762f,0.105f,0.040f},m_font,0.022f,8);
    m_endInput=std::make_unique<TextInput>(NormRect{0.698f,0.762f,0.105f,0.040f},m_font,0.022f,8);
    m_startInput->SetText("0"); m_endInput->SetText("0");
    m_buttons.push_back(Button({0.33f,0.16f,0.062f,0.051f},"收藏",[this]{m_onlyFavorites=!m_onlyFavorites;Filter();}));
    m_buttons.push_back(Button({0.398f,0.16f,0.062f,0.051f},"排序",[this]{m_sort=(m_sort+1)%3;Filter();}));
    m_buttons.push_back(Button({0.55f,0.064f,0.09f,0.044f},"导入谱面",[this]{ImportFolder();}));
    m_buttons.push_back(Button({0.65f,0.064f,0.09f,0.044f},"新建谱面",[this]{m_manager.SwitchScene(std::make_unique<SceneChartWizard>(m_manager));}));
    m_buttons.push_back(Button({0.75f,0.064f,0.09f,0.044f},"打开曲库",[]{const std::filesystem::path path=Paths::User("charts");std::error_code ec;std::filesystem::create_directories(path,ec);SDL_OpenURL(("file:///"+path.generic_string()).c_str());}));
    m_buttons.push_back(Button({0.85f,0.064f,0.11f,0.044f},"偏好设置",[this]{m_manager.SwitchScene(std::make_unique<SceneSettings>(m_manager));}));
    m_buttons.push_back(Button({0.04f,0.909f,0.12f,0.057f},"返回主页",[this]{Back();}));
    m_buttons.push_back(Button({0.17f,0.909f,0.09f,0.057f},"刷新 / F5",[this]{Scan();}));
    m_buttons.push_back(Button({0.77f,0.909f,0.19f,0.057f},"开始演奏   →",[this]{Start(m_options.mode);},true));
    Scan();
}
void SceneSelect::Scan() {
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
    m_visible.clear(); const auto query=Lower(m_search->GetText());
    for(int i=0;i<static_cast<int>(m_charts.size());++i) {
        const auto& c=m_charts[i]; std::string hay=c.title+" "+c.artist+" "+c.charter;
        for(const auto& tag:c.tags) hay+=" "+tag;
        if((!m_onlyFavorites || m_favorites.contains(c.id)) && Lower(hay).find(query)!=std::string::npos) m_visible.push_back(i);
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
    m_buttons[0]->SetText(m_onlyFavorites?"已收藏":"收藏");
    m_buttons[1]->SetText(m_sort==0?"曲名":m_sort==1?"BPM":"难度");
    if(m_selected<0) {m_detailButtons.clear();sakura::audio::AudioManager::GetInstance().StopMusic();}
    m_buttons.back()->SetEnabled(m_selected>=0);
}
void SceneSelect::Select(int index,int difficulty) {
    if(index<0 || index>=static_cast<int>(m_charts.size())) return;
    const bool changed=index!=m_selected;
    m_selected=index; const auto& c=m_charts[index];
    m_difficulty=std::clamp(difficulty>=0?difficulty:(changed?0:m_difficulty),0,static_cast<int>(c.difficulties.size())-1);
    if(changed) {
        sakura::audio::AudioManager::GetInstance().FadeOutMusic(150); m_previewTimer=0; m_previewPlaying=false;
        ResourceManager::GetInstance().UnloadTexture(m_cover);
        m_cover=c.coverFile.empty()?INVALID_HANDLE:ResourceManager::GetInstance().LoadTexture(c.folderPath+"/"+c.coverFile).value_or(INVALID_HANDLE);
    }
    Config::GetInstance().Set("library.last_chart",c.id); Config::GetInstance().Set("library.last_difficulty",m_difficulty);
    ChartLoader loader; auto chart=loader.LoadChartData(c.folderPath+"/"+c.difficulties[m_difficulty].chartFile);
    m_best=chart?sakura::data::Database::GetInstance().GetBestScore(c.id,c.difficulties[m_difficulty].name,Replay::Hash(*chart,c.offset)):std::nullopt;
    m_noteCount=chart?JudgmentCount(*chart):0; m_chartEnd=chart?ChartEndTime(*chart):0;
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
void SceneSelect::BuildDetail() {
    m_rebuild=false; m_detailButtons.clear(); if(m_selected<0) return;
    if(m_options.mode!=PlayMode::Practice){m_startInput->SetFocused(false);m_endInput->SetFocused(false);}
    const auto& c=m_charts[m_selected];
    m_detailButtons.push_back(Button({0.883f,0.185f,0.059f,0.037f},m_favorites.contains(c.id)?"已收藏":"收藏",[this]{Favorite();}));
    const int count=static_cast<int>(c.difficulties.size());
    const int first=(m_difficulty/4)*4;
    for(int i=first;i<std::min(first+4,count);++i) {
        const float width=0.425f/std::min(count,4);
        m_detailButtons.push_back(Button({0.515f+(i%4)*width,0.473f,width-0.008f,0.035f},
            c.difficulties[i].name+" "+Number(c.difficulties[i].level,0),[this,i]{Select(m_selected,i);},i==m_difficulty));
    }
    if(count>4){
        m_detailButtons.push_back(Button({0.817f,0.429f,0.055f,0.032f},"‹",[this,count]{Select(m_selected,(m_difficulty+count-1)%count);}));
        m_detailButtons.push_back(Button({0.884f,0.429f,0.055f,0.032f},"›",[this,count]{Select(m_selected,(m_difficulty+1)%count);}));
    }
    const char* modes[]={"正式演奏","自由练习","自动演示"};
    for(int i=0;i<3;++i) m_detailButtons.push_back(Button({0.515f+i*0.145f,0.595f,0.135f,0.042f},modes[i],
        [this,i]{m_options.mode=static_cast<PlayMode>(i);if(i==0)m_options.rate=1;m_rebuild=true;},static_cast<int>(m_options.mode)==i));
    if(m_options.mode!=PlayMode::Standard) {
        m_detailButtons.push_back(Button({0.815f,0.677f,0.057f,0.037f},"−",[this]{m_options.rate=std::max(0.5f,m_options.rate-0.1f);}));
        m_detailButtons.push_back(Button({0.883f,0.677f,0.057f,0.037f},"+",[this]{m_options.rate=std::min(2.0f,m_options.rate+0.1f);}));
    }
    if(m_options.mode==PlayMode::Practice) m_detailButtons.push_back(Button({0.825f,0.762f,0.116f,0.040f},m_options.loop?"循环：开":"循环：关",[this]{m_options.loop=!m_options.loop;m_rebuild=true;}));
    m_detailButtons.push_back(Button({0.515f,0.827f,0.18f,0.040f},"播放最近回放",[this]{Start(PlayMode::Replay);}));
    m_detailButtons.push_back(Button({0.711f,0.827f,0.23f,0.040f},"在工房中编辑",[this]{const auto& c=m_charts[m_selected];m_manager.SwitchScene(std::make_unique<SceneEditor>(m_manager,c.folderPath,c.difficulties[m_difficulty].chartFile));}));
}
void SceneSelect::Favorite() {
    const auto id=m_charts[m_selected].id;
    if(m_favorites.contains(id)) m_favorites.erase(id); else m_favorites.insert(id);
    Config::GetInstance().Set("library.favorites",std::vector<std::string>(m_favorites.begin(),m_favorites.end()));
    m_rebuild=true;Filter();
}
void SceneSelect::Start(PlayMode mode) {
    if(m_selected<0) return; auto options=m_options;options.mode=mode;
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
    if(mode==PlayMode::Standard)options.rate=1;
    m_manager.SwitchScene(std::make_unique<SceneGame>(m_manager,m_charts[m_selected],m_difficulty,options),TransitionType::Fade,0.3f);
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
        ChartLoader loader;auto info=loader.LoadChartInfo((std::filesystem::path(path)/"info.json").string());
        if(!info)throw std::runtime_error("所选目录缺少有效的 info.json");
        for(const auto& diff:info->difficulties)if(!loader.LoadChartData(info->folderPath+"/"+diff.chartFile))throw std::runtime_error("谱面数据无效："+diff.name);
        if(!std::filesystem::is_regular_file(std::filesystem::path(path)/info->musicFile))throw std::runtime_error("没有找到音乐文件");
        std::filesystem::create_directories(target);
        for(const auto& entry:std::filesystem::recursive_directory_iterator(path)) {
            if(entry.is_symlink())throw std::runtime_error("谱面目录不能包含符号链接");
            const auto dest=target/std::filesystem::relative(entry.path(),path);
            if(entry.is_directory())std::filesystem::create_directories(dest);
            else if(entry.is_regular_file())std::filesystem::copy_file(entry.path(),dest);
        }
        const auto destination=std::filesystem::path(Paths::User("charts"))/target.filename();
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::rename(target,destination);
        Scan();ToastManager::Instance().Show("谱面已导入曲库",ToastType::Success);
    }catch(const std::exception& e){std::error_code ec;std::filesystem::remove_all(target,ec);ToastManager::Instance().Show(e.what(),ToastType::Error,5);}
}
void SceneSelect::Back(){m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager),TransitionType::SlideRight,0.3f);}
void SceneSelect::OnExit(){ResourceManager::GetInstance().UnloadTexture(m_cover);m_cover=INVALID_HANDLE;sakura::audio::AudioManager::GetInstance().StopMusic();m_search->SetFocused(false);m_startInput->SetFocused(false);m_endInput->SetFocused(false);Config::GetInstance().Save();}
void SceneSelect::OnUpdate(float dt) {
    m_time+=dt;if(m_rebuild)BuildDetail();
    std::string imported;{std::lock_guard lock(m_dialog->mutex);if(m_dialog->ready){imported=std::move(m_dialog->path);m_dialog->ready=false;}}if(!imported.empty())AcceptImport(imported);
    m_search->Update(dt);m_startInput->Update(dt);m_endInput->Update(dt);
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
    r.DrawText(m_font,"LIBRARY / 曲库",0.04f,0.044f,0.042f,white);
    r.DrawText(m_font,"选一首喜欢的歌，让节奏开始。",0.04f,0.102f,0.020f,dim);
    m_search->Render(r);
    for(int row=0;row<6&&row+m_scroll<static_cast<int>(m_visible.size());++row) {
        const int index=m_visible[row+m_scroll];const auto& c=m_charts[index];const float y=0.24f+row*0.102f;const bool selected=index==m_selected;
        r.DrawRoundedRect({0.04f,y,0.42f,0.090f},0.009f,selected?Color{66,49,68,245}:Color{24,31,48,230});
        if(selected)r.DrawFilledRect({0.04f,y+0.016f,0.0025f,0.058f},pink);
        r.DrawText(m_font,selected?"▶":(m_favorites.contains(c.id)?"♥":"♪"),0.055f,y+0.026f,0.024f,selected?pink:dim);
        VisualStyle::DrawTextFit(r,m_font,c.title,0.086f,y+0.010f,0.029f,0.30f,white);
        VisualStyle::DrawTextFit(r,m_font,c.artist+"  ·  "+Number(c.bpm,0)+" BPM",0.086f,y+0.053f,0.018f,0.32f,dim);
        r.DrawText(m_font,std::to_string(c.difficulties.size())+" 谱面",0.444f,y+0.061f,0.015f,dim,TextAlign::Right);
    }
    if(m_visible.empty()) {r.DrawText(m_font,"没有匹配的曲目",0.25f,0.46f,0.030f,white,TextAlign::Center);r.DrawText(m_font,"清空搜索、关闭收藏筛选，或导入谱面",0.25f,0.52f,0.018f,dim,TextAlign::Center);}
    r.DrawText(m_font,std::to_string(m_visible.size())+" 首曲目   ·   ↑ ↓ 选曲   /   ← → 难度   /   Enter 演奏",0.04f,0.866f,0.017f,dim);
    VisualStyle::DrawPanel(r,{0.49f,0.16f,0.47f,0.72f});
    if(m_selected>=0) {
        const auto& c=m_charts[m_selected];
        if(m_cover!=INVALID_HANDLE)r.DrawSprite(m_cover,{0.515f,0.19f,0.125f,0.221f});
        else {r.DrawGradientRect({0.515f,0.19f,0.125f,0.221f},{108,64,86,255},{42,55,81,255},{26,34,54,255},{64,48,77,255});r.DrawText(m_font,"樱",0.577f,0.23f,0.10f,pink,TextAlign::Center);}
        VisualStyle::DrawTextFit(r,m_font,c.title,0.66f,0.239f,0.037f,0.28f,white);
        VisualStyle::DrawTextFit(r,m_font,c.artist,0.66f,0.298f,0.021f,0.28f,dim);
        VisualStyle::DrawTextFit(r,m_font,"谱师  "+c.charter,0.66f,0.338f,0.018f,0.28f,dim);
        r.DrawText(m_font,Number(c.bpm,0)+" BPM   ·   "+Duration(m_chartEnd)+"   ·   "+std::to_string(m_noteCount)+" 判定",0.66f,0.382f,0.018f,pink);
        r.DrawText(m_font,"DIFFICULTY / 难度",0.515f,0.432f,0.017f,dim);
        r.DrawText(m_font,m_best?"个人最佳  "+std::to_string(m_best->score)+"   /   "+Number(m_best->accuracy,2)+"%":"个人最佳  —  等待第一次演奏",0.515f,0.537f,0.021f,pink);
        r.DrawText(m_font,m_options.mode==PlayMode::Standard?"正式成绩将保存到记录与统计":"辅助模式 · 不计入正式成绩与成就",0.515f,0.66f,0.020f,dim);
        if(m_options.mode!=PlayMode::Standard)r.DrawText(m_font,"播放速度  "+Number(m_options.rate)+"×",0.515f,0.701f,0.022f,white);
        if(m_options.mode==PlayMode::Practice){r.DrawText(m_font,"秒",0.522f,0.77f,0.02f,dim);r.DrawText(m_font,"至",0.674f,0.77f,0.02f,dim);m_startInput->Render(r);m_endInput->Render(r);}
        else r.DrawText(m_font,"A / S / D / F  +  鼠标左键    ·    Esc 暂停",0.515f,0.762f,0.019f,dim);
        for(auto& b:m_detailButtons)b->Render(r);
    }
    for(auto& b:m_buttons)b->Render(r);
}
void SceneSelect::OnEvent(const SDL_Event& e) {
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
    for(auto& b:m_detailButtons)if(b->HandleEvent(e))return;
}
}
