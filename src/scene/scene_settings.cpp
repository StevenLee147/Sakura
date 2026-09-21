#include "scene_settings.h"
#include "scene_menu.h"
#include "scene_select.h"
#include "scene_calibration.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/paths.h"
#include "core/theme.h"
#include "data/database.h"
#include "ui/visual_style.h"
#include "ui/slider.h"
#include "ui/toggle.h"
#include "ui/dropdown.h"
#include "ui/toast.h"
#include "utils/file_io.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <cmath>
#include <sstream>

namespace sakura::scene
{
using namespace sakura::core;
using namespace sakura::ui;
namespace
{
const char* titles[] = {"游玩", "声音", "操作", "显示", "视觉", "数据"};
const char* subtitles[] = {"找到属于你的节奏", "让每一次击打准确回应", "左手节奏，右手旋律", "让画面跟上你的反应", "樱花与光，恰到好处", "每一次进步，都值得保留"};
const char* keyNames[] = {"input.key_lane_0", "input.key_lane_1", "input.key_lane_2", "input.key_lane_3", "input.key_pause", "input.key_retry"};
constexpr int defaultKeys[] = {4, 22, 7, 9, 41, 21};
NormRect ControlBounds(size_t row) { return {0.685f, 0.235f + static_cast<float>(row) * 0.079f, 0.235f, 0.042f}; }
std::unique_ptr<Button> MakeButton(NormRect bounds, const std::string& text, FontHandle font, std::function<void()> action)
{
    auto button = std::make_unique<Button>(bounds, text, font, 0.023f, 0.009f);
    button->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(button.get(), ButtonVariant::Secondary);
    button->SetOnClick(std::move(action));
    return button;
}
}

void SceneSettings::OnEnter()
{
    m_font = ResourceManager::GetInstance().GetDefaultFontHandle();
    m_saved = Config::GetInstance().GetRoot();
    m_dirty = false;
    m_modal = 0;
    for (int i = 0; i < 6; ++i)
        m_tabs[i] = MakeButton({0.04f, 0.225f + i * 0.093f, 0.15f, 0.066f}, titles[i], m_font, [this, i]
        {
            m_page = i; m_listening = -1;
            for (auto& page : m_pages) for (auto& row : page)
                if (auto* dropdown = dynamic_cast<Dropdown*>(row.control.get())) dropdown->Close();
        });
    m_footer.clear();
    m_footer.push_back(MakeButton({0.04f, 0.914f, 0.15f, 0.052f}, "返回", m_font, [this] { Back(); }));
    m_footer.push_back(MakeButton({0.68f, 0.914f, 0.12f, 0.052f}, "恢复默认", m_font, [this] { ShowModal(2); }));
    m_footer.push_back(MakeButton({0.82f, 0.914f, 0.14f, 0.052f}, "保存设置", m_font, [this] { Save(); }));
    VisualStyle::ApplyButton(m_footer.back().get(), ButtonVariant::Primary);
    BuildPages();
}

void SceneSettings::AddSlider(int page, const std::string& title, const std::string& hint,
    const std::string& key, float low, float high, float step, bool integral)
{
    auto& cfg = Config::GetInstance();
    auto slider = std::make_unique<Slider>(ControlBounds(m_pages[page].size()), low, high,
        cfg.Get<float>(key, low), step, m_font, 0.020f);
    slider->SetValueFormatter([high, integral](float value)
    {
        if (high == 1.0f) return std::to_string(static_cast<int>(std::round(value * 100))) + "%";
        if (integral) return std::to_string(static_cast<int>(value));
        std::ostringstream text; text << std::fixed << std::setprecision(1) << value;
        return text.str();
    });
    slider->SetOnChange([this, key, integral](float value)
    {
        if (integral) Config::GetInstance().Set(key, static_cast<int>(std::round(value)));
        else Config::GetInstance().Set(key, value);
        m_dirty = true;
        if (key.starts_with("audio.")) SyncAudio();
    });
    m_pages[page].push_back({title, hint, std::move(slider)});
}

void SceneSettings::AddToggle(int page, const std::string& title, const std::string& hint, const std::string& key)
{
    auto bounds = ControlBounds(m_pages[page].size());
    bounds.x = 0.864f; bounds.width = 0.057f;
    auto toggle = std::make_unique<Toggle>(bounds, Config::GetInstance().Get<bool>(key, true), m_font, 0.022f);
    toggle->SetOnChange([this, key](bool value) { Config::GetInstance().Set(key, value); m_dirty = true; });
    m_pages[page].push_back({title, hint, std::move(toggle)});
}

void SceneSettings::AddChoice(int page, const std::string& title, const std::string& hint, const std::string& key,
    const std::vector<std::string>& labels, const std::vector<nlohmann::json>& values)
{
    auto choiceLabels=labels;auto choiceValues=values;
    int selected = 0;
    const auto current = key=="display.resolution" ? nlohmann::json::array({Config::GetInstance().Get<int>("display.window_width",1600),Config::GetInstance().Get<int>("display.window_height",900)}) : Config::GetInstance().Get<nlohmann::json>(key);
    for (int i = 0; i < static_cast<int>(values.size()); ++i) if (values[i] == current) selected = i;
    if(key=="display.resolution" && std::find(choiceValues.begin(),choiceValues.end(),current)==choiceValues.end()){
        selected=static_cast<int>(choiceValues.size());choiceValues.push_back(current);
        choiceLabels.push_back(std::to_string(current[0].get<int>())+" × "+std::to_string(current[1].get<int>()));
    }
    auto dropdown = std::make_unique<Dropdown>(ControlBounds(m_pages[page].size()), choiceLabels, selected, m_font, 0.022f);
    dropdown->SetOnChange([this, key, values=std::move(choiceValues)](int index, const std::string&)
    {
        Config::GetInstance().Set(key, values[index]);
        if (key == "display.resolution")
        {
            const auto& size = values[index];
            Config::GetInstance().Set("display.window_width", size[0].get<int>());
            Config::GetInstance().Set("display.window_height", size[1].get<int>());
        }
        if (key == "audio.hitsound")
        {
            sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(values[index].get<std::string>());
            sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::Tap);
        }
        m_dirty = true;
    });
    m_pages[page].push_back({title, hint, std::move(dropdown)});
}

void SceneSettings::AddAction(int page, const std::string& title, const std::string& hint,
    const std::string& label, std::function<void()> action)
{
    m_pages[page].push_back({title, hint, MakeButton(ControlBounds(m_pages[page].size()), label, m_font, std::move(action))});
}

void SceneSettings::BuildPages()
{
    for (auto& page : m_pages) page.clear();
    auto& cfg = Config::GetInstance();
    AddSlider(0, "下落速度", "只改变阅读速度，不影响歌曲速度与判定", "gameplay.note_speed", 0.5f, 15, 0.1f);
    AddSlider(0, "鼠标预读时间 / ms", "接近圈从出现到命中的时间", "gameplay.mouse_approach_ms", 400, 2000, 50, true);
    AddSlider(0, "背景压暗", "让谱面始终清晰可读", "gameplay.background_dim", 0, 1, 0.05f);
    AddSlider(0, "轨道不透明度", "调整键盘区域与背景的对比", "gameplay.lane_opacity", 0.2f, 1, 0.05f);
    AddToggle(0, "判定偏差", "显示 EARLY / LATE 和实时偏差条", "gameplay.show_hit_error");
    AddToggle(0, "连击数字", "在舞台中显示当前连击", "gameplay.show_combo");
    AddAction(0, "练习与自动演示", "在曲库选择模式、速度与练习区间", "前往曲库", [this]
    { if (Save()) m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager), TransitionType::Fade, 0.25f); });

    AddSlider(1, "主音量", "音乐与音效的总音量", "audio.master_volume", 0, 1, 0.01f);
    AddSlider(1, "音乐音量", "歌曲、选曲试听与校准节拍", "audio.music_volume", 0, 1, 0.01f);
    AddSlider(1, "音效音量", "打击与界面提示音", "audio.sfx_volume", 0, 1, 0.01f);
    AddSlider(1, "全局音频偏移 / ms", "正值延后判定；建议先进行节拍校准", "audio.global_offset_ms", -500, 500, 1, true);
    AddToggle(1, "打击音效", "界面提示音仍由音效音量控制", "audio.hitsounds_enabled");
    AddAction(1, "节拍校准", "跟随节拍按空格，测量设备与操作延迟", "开始校准", [this]
    { if (Save()) m_manager.SwitchScene(std::make_unique<SceneCalibration>(m_manager), TransitionType::Fade, 0.25f); });
    AddChoice(1, "打击音色", "选择后播放试听", "audio.hitsound", {"清脆 / Default", "轻柔 / Soft", "鼓点 / Drum"}, {"default", "soft", "drum"});
    AddToggle(1, "判定提示音", "额外播放 Perfect / Great 等判定音色", "audio.judgment_sounds");

    for (int i = 0; i < 6; ++i)
    {
        m_keys[i] = cfg.Get<int>(keyNames[i], defaultKeys[i]);
        AddAction(2, i < 4 ? "轨道 " + std::to_string(i + 1) : (i == 4 ? "暂停 / 继续" : "快速重试"),
            i < 4 ? "左手操作，默认 A / S / D / F" : (i == 4 ? "Esc 始终可用；失去焦点时自动暂停" : "重新进入同一歌曲、难度和模式"),
            SDL_GetScancodeName(static_cast<SDL_Scancode>(m_keys[i])), [this, i] { m_listening = i; m_listenTime = 0; });
    }
    AddAction(2, "恢复默认键位", "A / S / D / F · Esc 暂停 · R 重试", "重置键位", [this]
    {
        for (int i = 0; i < 6; ++i) { m_keys[i] = defaultKeys[i]; Config::GetInstance().Set(keyNames[i], defaultKeys[i]); }
        m_dirty = true;
    });

    AddToggle(3, "全屏显示", "F11 随时切换，设置会保持同步", "display.fullscreen");
    AddToggle(3, "垂直同步", "减少撕裂；关闭可降低输入延迟", "display.vsync");
    AddToggle(3, "显示帧率", "在右上角显示实时 FPS", "graphics.show_fps");
    AddChoice(3, "窗口分辨率", "全屏时使用显示器分辨率", "display.resolution",
        {"1280 × 720", "1600 × 900", "1920 × 1080", "2560 × 1440"},
        {nlohmann::json::array({1280,720}), nlohmann::json::array({1600,900}), nlohmann::json::array({1920,1080}), nlohmann::json::array({2560,1440})});
    AddChoice(3, "帧率上限", "垂直同步启用时受显示器刷新率限制", "display.fps_limit",
        {"60 FPS", "120 FPS", "144 FPS", "165 FPS", "240 FPS", "360 FPS", "480 FPS", "不限制"}, {60,120,144,165,240,360,480,0});

    AddToggle(4, "樱花与命中粒子", "飘落花瓣、击打碎光与连击庆祝", "graphics.particles");
    AddToggle(4, "柔光", "音符、轨道和界面的辉光", "graphics.glow");
    AddToggle(4, "屏幕震动", "仅在连击中断时提供轻微反馈", "graphics.shake");
    AddToggle(4, "暗角", "为舞台边缘添加柔和阴影", "graphics.vignette");
    AddToggle(4, "减少动态效果", "静止背景并关闭震动与转场动画", "graphics.reduced_motion");
    AddSlider(4, "特效强度", "降低亮度和粒子数量，保持节奏清晰", "graphics.effect_intensity", 0, 1, 0.05f);
    AddToggle(4, "鼠标轨迹", "在鼠标区域显示短暂的光点轨迹", "gameplay.cursor_trail");
    AddToggle(4, "背景柔化", "柔化自定义歌曲背景，让音符更清晰", "graphics.background_blur");

    AddToggle(5, "保存回放", "正常模式结束后保留输入记录，可在结算观看", "gameplay.save_replays");
    AddAction(5, "用户数据", "设置、成绩、自制谱面和回放均保存在这里", "打开数据目录", []
    { SDL_OpenURL(("file:///" + Paths::User()).c_str()); });
    AddAction(5, "存档备份", "包含成绩、设置、回放与自制谱面；创建独立快照", "创建备份", [this] { CreateBackup(); });
    AddAction(5, "恢复存档", "选择备份文件夹；恢复前会备份当前存档", "选择备份", [this]
    {
        auto* request = new std::shared_ptr<DialogState>(m_dialog);
        SDL_ShowOpenFolderDialog([](void* data, const char* const* files, int)
        {
            std::unique_ptr<std::shared_ptr<DialogState>> state(static_cast<std::shared_ptr<DialogState>*>(data));
            if (files && files[0])
            { std::lock_guard lock((*state)->mutex); (*state)->directory = files[0]; }
        }, request, nullptr, nullptr, false);
    });
    AddAction(5, "备份记录", "每个备份包含可独立恢复的 SQLite 快照", "打开备份目录", []
    {
        std::filesystem::create_directories(Paths::User("backups"));
        SDL_OpenURL(("file:///" + Paths::User("backups")).c_str());
    });
}

void SceneSettings::SyncAudio()
{
    auto& cfg = Config::GetInstance();
    auto& audio = sakura::audio::AudioManager::GetInstance();
    audio.SetMasterVolume(cfg.Get<float>("audio.master_volume", 1));
    audio.SetMusicVolume(cfg.Get<float>("audio.music_volume", 0.8f));
    audio.SetSFXVolume(cfg.Get<float>("audio.sfx_volume", 0.8f));
}

bool SceneSettings::Save()
{
    if (!Config::GetInstance().Save())
    { ToastManager::Instance().Show("设置保存失败，请检查目录权限与剩余空间", ToastType::Error); return false; }
    m_saved = Config::GetInstance().GetRoot();
    m_dirty = false;
    ToastManager::Instance().Show("设置已保存", ToastType::Success);
    return true;
}

void SceneSettings::OnExit()
{
    if (m_dirty)
    {
        Config::GetInstance().Restore(m_saved);
        SyncAudio();
        sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(Config::GetInstance().Get<std::string>("audio.hitsound", "default"));
    }
}

void SceneSettings::Back()
{
    if (m_dirty) { ShowModal(1); return; }
    m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::SlideRight, 0.25f);
}

bool SceneSettings::CanClose(){
    if(!m_dirty)return true;
    m_closeRequested=true;ShowModal(1);return false;
}

void SceneSettings::ShowModal(int kind)
{
    m_modal = kind;
    m_modalButtons.clear();
    m_modalButtons.push_back(MakeButton({0.29f,0.59f,0.13f,0.05f}, "取消", m_font, [this] { m_modal = 0; m_closeRequested=false; }));
    if (kind == 1)
        m_modalButtons.push_back(MakeButton({0.44f,0.59f,0.13f,0.05f}, "放弃修改", m_font, [this]
        { if(m_closeRequested){Config::GetInstance().Restore(m_saved);m_dirty=false;SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}else m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::Fade, 0.25f); }));
    m_modalButtons.push_back(MakeButton({0.59f,0.59f,0.13f,0.05f}, kind == 1 ? "保存并返回" : "确认", m_font, [this, kind]
    {
        if (kind == 1) { if (Save()) { m_modal = 0; if(m_closeRequested){SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}else Back(); } }
        if (kind == 2)
        {
            Config::GetInstance().ResetToDefaults(); m_dirty = true; m_modal = 0;
            SyncAudio(); BuildPages();
        }
        if (kind == 3) { RestoreBackup(); m_modal = 0; }
    }));
    VisualStyle::ApplyButton(m_modalButtons.back().get(), ButtonVariant::Primary);
}

bool SceneSettings::CreateBackup()
{
    try
    {
        const std::filesystem::path folder = Paths::User("backups/" + std::to_string(std::time(nullptr)) + "-" + std::to_string(SDL_GetTicks()));
        std::filesystem::create_directories(folder);
        const bool ok = sakura::data::Database::GetInstance().BackupTo((folder / "sakura.db").string()) &&
            sakura::utils::AtomicWrite(folder / "settings.json", Config::GetInstance().GetRoot().dump(2));
        if (std::filesystem::exists(Paths::User("replays")))
            std::filesystem::copy(Paths::User("replays"), folder / "replays", std::filesystem::copy_options::recursive);
        if (std::filesystem::exists(Paths::User("charts")))
            std::filesystem::copy(Paths::User("charts"), folder / "charts", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks);
        if(ok)m_lastBackupDirectory=folder.string();
        ToastManager::Instance().Show(ok ? "存档备份已创建" : "备份失败", ok ? ToastType::Success : ToastType::Error);
        return ok;
    }
    catch (const std::exception&) { ToastManager::Instance().Show("备份失败，请检查存储空间", ToastType::Error); return false; }
}

void SceneSettings::RestoreBackup()
{
    const auto before=Config::GetInstance().GetRoot();
    bool changedDatabase=false;
    const std::filesystem::path staging=Paths::User("restore-staging-"+std::to_string(SDL_GetTicksNS()));
    std::vector<std::string> swapped;
    try
    {
        const std::filesystem::path folder = m_restoreDirectory;
        std::ifstream file(folder / "settings.json");
        auto settings = nlohmann::json::parse(file);
        if (!settings.is_object())throw std::runtime_error("设置格式无效");
        if(!CreateBackup())return;
        if (!sakura::data::Database::GetInstance().RestoreFrom((folder / "sakura.db").string()))
            throw std::runtime_error("备份数据库无效");
        changedDatabase=true;
        std::filesystem::create_directories(staging);
        // Stage complete folders before replacing any active files. Earlier backups
        // may not contain charts; preserve that directory when it is absent.
        for(const std::string name:{"replays","charts"})if(std::filesystem::is_directory(folder/name)){
            std::filesystem::copy(folder/name,staging/name,std::filesystem::copy_options::recursive|std::filesystem::copy_options::skip_symlinks);
            const auto live=std::filesystem::path(Paths::User(name));
            if(std::filesystem::exists(live))std::filesystem::rename(live,staging/(name+".old"));
            swapped.push_back(name);
            std::filesystem::rename(staging/name,live);
        }
        auto relocate=[&](auto&& self,nlohmann::json& value)->void{
            if(value.is_object())for(auto& item:value.items())self(self,item.value());
            else if(value.is_string()){
                const std::filesystem::path old=value.get<std::string>();
                if(old.extension()==".skr") {const auto local=std::filesystem::path(Paths::User("replays"))/old.filename();if(std::filesystem::exists(local))value=local.string();}
            }
        };
        relocate(relocate,settings);
        Config::GetInstance().Restore(settings);
        if(!Config::GetInstance().SaveForce())throw std::runtime_error("设置无法写入");
        m_saved=Config::GetInstance().GetRoot();m_dirty=false;
        SyncAudio(); BuildPages();
        sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(Config::GetInstance().Get<std::string>("audio.hitsound","default"));
        std::error_code cleanup;std::filesystem::remove_all(staging,cleanup);
        ToastManager::Instance().Show("存档已恢复，原存档已备份", ToastType::Success);
    }
    catch (const std::exception& error) {
        bool filesRecovered=true;
        for(auto it=swapped.rbegin();it!=swapped.rend();++it){
            std::error_code ec;const auto live=std::filesystem::path(Paths::User(*it));
            if(std::filesystem::exists(live))std::filesystem::remove_all(live,ec);
            if(!ec && std::filesystem::exists(staging/(*it+".old")))std::filesystem::rename(staging/(*it+".old"),live,ec);
            filesRecovered &= !ec;
        }
        const bool recovered=!changedDatabase || sakura::data::Database::GetInstance().RestoreFrom((std::filesystem::path(m_lastBackupDirectory)/"sakura.db").string());
        Config::GetInstance().Restore(before);Config::GetInstance().SaveForce();SyncAudio();
        if(filesRecovered){std::error_code ec;std::filesystem::remove_all(staging,ec);}
        ToastManager::Instance().Show(recovered && filesRecovered ? std::string("恢复未完成，原记录已保留：")+error.what() : "恢复失败，请从备份目录恢复原存档",ToastType::Error,6);
    }
}

void SceneSettings::OnUpdate(float dt)
{
    {
        std::lock_guard lock(m_dialog->mutex);
        if (!m_dialog->directory.empty())
        { m_restoreDirectory = std::move(m_dialog->directory); m_dialog->directory.clear(); ShowModal(3); }
    }
    if (m_listening >= 0 && (m_listenTime += dt) > 6) m_listening = -1;
    for (int i = 0; i < 6; ++i)
    {
        VisualStyle::ApplyButton(m_tabs[i].get(), i == m_page ? ButtonVariant::Primary : ButtonVariant::Secondary);
        m_tabs[i]->Update(dt);
    }
    static_cast<Toggle*>(m_pages[3][0].control.get())->SetOn(Config::GetInstance().Get<bool>("display.fullscreen",false));
    for (auto& row : m_pages[m_page]) row.control->Update(dt);
    if (m_page == 2)
        for (int i = 0; i < 6; ++i)
            static_cast<Button*>(m_pages[2][i].control.get())->SetText(m_listening == i ? "请按键…" : SDL_GetScancodeName(static_cast<SDL_Scancode>(m_keys[i])));
    for (auto& button : m_footer) button->Update(dt);
    for (auto& button : m_modalButtons) button->Update(dt);
}

void SceneSettings::OnEvent(const SDL_Event& event)
{
    if (m_modal)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {m_modal = 0;m_closeRequested=false;}
        else for (auto& button : m_modalButtons) if (button->HandleEvent(event)) break;
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const int code = event.key.scancode;
        if (m_listening >= 0)
        {
            if(code==SDL_SCANCODE_ESCAPE && m_listening!=4){m_listening=-1;return;}
            const bool conflict = code == SDL_SCANCODE_F11 || code == SDL_SCANCODE_UNKNOWN ||
                (code == SDL_SCANCODE_ESCAPE && m_listening != 4) ||
                std::any_of(m_keys.begin(), m_keys.end(), [this, code](int value) { return value == code && value != m_keys[m_listening]; });
            if (conflict) ToastManager::Instance().Show("此按键已使用或为系统保留，请换一个", ToastType::Warning);
            else { m_keys[m_listening] = code; Config::GetInstance().Set(keyNames[m_listening], code); m_dirty = true; m_listening = -1; }
            return;
        }
        if (code == SDL_SCANCODE_ESCAPE) { Back(); return; }
        if (code == SDL_SCANCODE_S && (event.key.mod & SDL_KMOD_CTRL)) { Save(); return; }
        if (code == SDL_SCANCODE_TAB) { m_page = (m_page + 1) % 6; return; }
    }
    for (auto& row : m_pages[m_page])
        if (auto* dropdown = dynamic_cast<Dropdown*>(row.control.get()); dropdown && dropdown->IsOpen())
        { dropdown->HandleEvent(event); return; }
    for (auto& button : m_tabs) if (button->HandleEvent(event)) return;
    for (auto& button : m_footer) if (button->HandleEvent(event)) return;
    for (auto& row : m_pages[m_page]) if (row.control->HandleEvent(event)) return;
}

void SceneSettings::OnRender(Renderer& renderer)
{
    const auto& palette = Theme::GetInstance().Colors();
    VisualStyle::DrawSceneBackground(renderer);
    renderer.DrawText(m_font, "PREFERENCES / 设置", 0.04f, 0.05f, 0.037f, palette.text);
    renderer.DrawText(m_font, "调整手感，让节奏成为本能。", 0.04f, 0.108f, 0.021f, palette.textDim);
    VisualStyle::DrawPanel(renderer, {0.22f, 0.155f, 0.74f, 0.725f});
    renderer.DrawText(m_font, titles[m_page], 0.25f, 0.176f, 0.029f, palette.text);
    renderer.DrawText(m_font, subtitles[m_page], 0.925f, 0.184f, 0.018f, palette.textDim, TextAlign::Right);
    for (auto& tab : m_tabs) tab->Render(renderer);
    size_t i = 0;
    for (auto& row : m_pages[m_page])
    {
        const float y = 0.234f + static_cast<float>(i++) * 0.079f;
        renderer.DrawText(m_font, row.title, 0.253f, y, 0.024f, palette.text);
        renderer.DrawText(m_font, row.hint, 0.253f, y + 0.034f, 0.017f, palette.textDim);
        if (i < m_pages[m_page].size()) renderer.DrawLine(0.253f, y + 0.070f, 0.927f, y + 0.070f, {125, 121, 156, 35}, 0.001f);
        if (auto* dropdown = dynamic_cast<Dropdown*>(row.control.get()); !dropdown || !dropdown->IsOpen()) row.control->Render(renderer);
    }
    for (auto& row : m_pages[m_page])
        if (auto* dropdown = dynamic_cast<Dropdown*>(row.control.get()); dropdown && dropdown->IsOpen()) dropdown->Render(renderer);
    for (auto& button : m_footer) button->Render(renderer);
    renderer.DrawText(m_font, m_dirty ? "● 有未保存的修改" : "设置已保存 · Tab 切换分类 · Ctrl+S 保存",
        0.24f, 0.929f, 0.019f, m_dirty ? palette.primary : palette.textDim);
    if (m_modal)
    {
        VisualStyle::DrawScrim(renderer);
        VisualStyle::DrawPanel(renderer, {0.25f,0.32f,0.50f,0.37f}, true, true);
        renderer.DrawText(m_font, m_modal == 1 ? "保存本次调整？" : (m_modal == 2 ? "恢复全部默认设置？" : "恢复选中的存档？"),
            0.5f,0.38f,0.033f,palette.text,TextAlign::Center);
        renderer.DrawText(m_font, m_modal == 1 ? "放弃修改会恢复进入此页面时的设置。" :
            (m_modal == 2 ? "成绩和谱面会保留，设置恢复后仍可取消。" : "当前存档会先备份，再替换成绩与设置。"),
            0.5f,0.46f,0.022f,palette.textDim,TextAlign::Center);
        for (auto& button : m_modalButtons) button->Render(renderer);
    }
}
} // namespace sakura::scene
