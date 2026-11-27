#include "scene_settings.h"
#include "scene_menu.h"
#include "scene_select.h"
#include "scene_calibration.h"
#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/paths.h"
#include "core/skin_manager.h"
#include "core/theme.h"
#include "data/database.h"
#include "ui/visual_style.h"
#include "ui/slider.h"
#include "ui/toggle.h"
#include "ui/dropdown.h"
#include "ui/toast.h"
#include "utils/file_io.h"

#include <algorithm>
#include <atomic>
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
const char* keyNames[] = {"input.key_lane_0", "input.key_lane_1", "input.key_lane_2", "input.key_lane_3", "input.key_thumb", "input.key_pause", "input.key_retry"};
constexpr int defaultKeys[] = {4, 22, 7, 9, 44, 41, 21};
NormRect ControlBounds(size_t row) { return {0.685f, 0.235f + static_cast<float>(row) * 0.079f, 0.235f, 0.042f}; }
std::unique_ptr<Button> MakeButton(NormRect bounds, const std::string& text, FontHandle font, std::function<void()> action)
{
    auto button = std::make_unique<Button>(bounds, text, font, 0.023f, 0.009f);
    button->SetTextAlign(TextAlign::Center);
    VisualStyle::ApplyButton(button.get(), ButtonVariant::Secondary);
    button->SetOnClick(std::move(action));
    return button;
}

nlohmann::json StripLegacyOnlineCredentials(nlohmann::json root)
{
    if (root.is_object() && root.contains("online") && root["online"].is_object())
    {
        auto& online = root["online"];
        for (const char* key : {
            "auth_token", "refresh_token", "access_token",
            "user_id", "username", "is_guest",
            "total_pp", "sakura_rating", "dan_badge", "expires_at" })
        {
            online.erase(key);
        }
    }
    return root;
}

void PurgeLegacyOnlineKeysFromLiveConfig()
{
    auto& cfg = Config::GetInstance();
    for (const char* key : {
        "online.auth_token", "online.refresh_token", "online.access_token",
        "online.user_id", "online.username", "online.is_guest",
        "online.total_pp", "online.sakura_rating", "online.dan_badge", "online.expires_at" })
    {
        if (cfg.Has(key))
            cfg.Remove(key);
    }
}

void PreserveActiveAuthEndpointInConfig()
{
    PurgeLegacyOnlineKeysFromLiveConfig();
    auto& api = sakura::net::APIClient::GetInstance();
    if (api.GetAuthState() != sakura::net::AuthState::Unauthenticated || api.IsAuthenticated())
    {
        const std::string activeUrl = api.GetServerUrl();
        if (!activeUrl.empty())
            Config::GetInstance().Set("online.server_url", activeUrl);
    }
}

void RestoreConfigPreservingAuth(const nlohmann::json& snapshot)
{
    // 仅恢复常规设置，绝不触碰 AuthStore / auth_session.json 与 APIClient 登录态
    Config::GetInstance().Restore(StripLegacyOnlineCredentials(snapshot));
    PreserveActiveAuthEndpointInConfig();
}

void ResetConfigToDefaultsPreservingAuth()
{
    // 重置常规设置默认值，完整保留 APIClient / AuthStore 登录会话与服务端地址
    Config::GetInstance().ResetToDefaults();
    PreserveActiveAuthEndpointInConfig();
}
}

void SceneSettings::OnEnter()
{
    m_font = ResourceManager::GetInstance().GetDefaultFontHandle();
    m_topBar = std::make_unique<TopBar>("PREFERENCES  ·  偏好设置", m_font, [this] { Back(); });
    m_saved = StripLegacyOnlineCredentials(Config::GetInstance().GetRoot());
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
        if (high <= 1.0f && !integral) return std::to_string(static_cast<int>(std::round(value * 100))) + "%";
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
    toggle->SetOnChange([this, key](bool value)
    {
        Config::GetInstance().Set(key, value);
        if (key == "audio.menu_bgm_enabled")
            sakura::audio::AudioManager::GetInstance().EnsureMenuBgm();
        m_dirty = true;
    });
    m_pages[page].push_back({title, hint, std::move(toggle)});
}

void SceneSettings::AddChoice(int page, const std::string& title, const std::string& hint, const std::string& key,
    const std::vector<std::string>& labels, const std::vector<nlohmann::json>& values)
{
    auto choiceLabels=labels;auto choiceValues=values;
    int selected = 0;
    nlohmann::json current;
    if (key == "display.resolution")
    {
        current = nlohmann::json::array({Config::GetInstance().Get<int>("display.window_width",1600),Config::GetInstance().Get<int>("display.window_height",900)});
    }
    else if (key == "graphics.skin_path")
    {
        std::string curSkin = Config::GetInstance().Get<std::string>(key, "moonlit_sakura");
        if (curSkin.empty()) curSkin = "moonlit_sakura";
        if (const auto* matched = SkinManager::GetInstance().FindSkin(curSkin))
            curSkin = matched->id;
        current = curSkin;
    }
    else
    {
        current = Config::GetInstance().Get<nlohmann::json>(key);
    }
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
        if (key == "audio.latency_mode")
        {
            sakura::audio::AudioManager::GetInstance().SetLatencyMode(values[index].get<std::string>());
        }
        if (key == "audio.output_device")
        {
            sakura::audio::AudioManager::GetInstance().SetOutputDevice(values[index].get<std::string>());
        }
        if (key == "graphics.skin_path")
        {
            SkinManager::GetInstance().SetActiveSkin(values[index].get<std::string>());
            const std::string& hitsoundSet = SkinManager::GetInstance().GetActiveSkin().hitsoundSet;
            Config::GetInstance().Set("audio.hitsound", hitsoundSet);
            sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(hitsoundSet);
            sakura::audio::AudioManager::GetInstance().PlayHitsound(sakura::audio::HitsoundType::Tap);
            static constexpr std::string_view kHitsoundSets[] = {"default", "cyber", "pure", "ukiyo", "soft", "drum"};
            for (auto& row : m_pages[1])
            {
                if (row.title == "打击音色")
                {
                    if (auto* hsDropdown = dynamic_cast<Dropdown*>(row.control.get()))
                    {
                        for (int hsIdx = 0; hsIdx < 6; ++hsIdx)
                        {
                            if (hitsoundSet == kHitsoundSets[hsIdx])
                            {
                                hsDropdown->SetSelectedIndex(hsIdx);
                                break;
                            }
                        }
                    }
                    break;
                }
            }
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
    AddToggle(0, "双语同步歌词", "在舞台底部展示 Chart v4 时间轴歌词", "gameplay.show_lyrics");
    AddToggle(0, "变速锁定音高", "练习与回放变速时采用 WSOLA 保持原调", "audio.pitch_lock");
    AddAction(0, "练习与自动演示", "在曲库选择模式、速度与练习区间", "前往曲库", [this]
    { if (Save()) m_manager.SwitchScene(std::make_unique<SceneSelect>(m_manager), TransitionType::Fade, 0.25f); });

    AddSlider(1, "主音量", "音乐与音效的总音量", "audio.master_volume", 0, 1, 0.01f);
    AddSlider(1, "音乐音量", "歌曲、选曲交叉试听与夜樱菜单环境音", "audio.music_volume", 0, 1, 0.01f);
    AddSlider(1, "打击音独立音量", "双区 10 类音符打击音效增益", "audio.hitsound_volume", 0, 1, 0.01f);
    AddSlider(1, "界面音独立音量", "按钮悬停、点击与转场反馈音增益", "audio.ui_sfx_volume", 0, 1, 0.01f);
    AddSlider(1, "全局音频偏移 / ms", "正值延后判定；建议先进行节拍校准", "audio.global_offset_ms", -500, 500, 1, true);
    {
        auto& am = sakura::audio::AudioManager::GetInstance();
        std::ostringstream latHint;
        latHint << "当前管线：" << am.GetActualSampleRate() << " Hz · " << am.GetPeriodFrames()
                << " frames (~" << std::fixed << std::setprecision(1) << am.GetEstimatedOutputLatencyMs() << " ms)";
        AddChoice(1, "音频缓冲延迟档位", latHint.str(), "audio.latency_mode",
            {"超低延迟 (128f)", "均衡推荐 (256f)", "安全防爆 (512f)"},
            {"ultra_low", "balanced", "safe"});
        std::vector<std::string> devLabels;
        std::vector<nlohmann::json> devValues;
        for (const auto& d : am.GetPlaybackDevices())
        {
            devLabels.push_back(d.name);
            devValues.push_back(d.id);
        }
        AddChoice(1, "音频输出设备", "支持耳机与蓝牙设备热插拔无感自动重连", "audio.output_device", devLabels, devValues);
    }
    AddChoice(1, "打击音色", "6 套双区 12 复音防爆音包，选择后试听", "audio.hitsound",
        {"墨樱晶石 / Default", "霓虹电音 / Cyber", "竞技极简 / Pure", "浮世竹韵 / Ukiyo", "轻柔 / Soft", "鼓点 / Drum"},
        {"default", "cyber", "pure", "ukiyo", "soft", "drum"});
    AddToggle(1, "打击音效与微调", "启用双区打击音及 ±1.5% 防疲劳人性化微变调", "audio.hitsounds_enabled");
    AddToggle(1, "大厅夜樱环境音", "在主菜单、统计、道场与市场无缝播放夜樱背景乐", "audio.menu_bgm_enabled");
    AddAction(1, "节拍校准", "跟随节拍按空格，测量设备与操作延迟", "开始校准", [this]
    { if (Save()) m_manager.SwitchScene(std::make_unique<SceneCalibration>(m_manager), TransitionType::Fade, 0.25f); });

    for (int i = 0; i < 7; ++i)
    {
        m_keys[i] = cfg.Get<int>(keyNames[i], defaultKeys[i]);
        const std::string rowTitle = i < 4 ? ("轨道 " + std::to_string(i + 1))
            : (i == 4 ? "樱枝横梁" : (i == 5 ? "暂停 / 继续" : "快速重试"));
        const std::string rowHint = i < 4 ? "左手操作，默认 A / S / D / F"
            : (i == 4 ? "左手大拇指，默认 Space；用于横梁与组合技"
            : (i == 5 ? "Esc 始终可用；失去焦点时自动暂停" : "重新进入同一歌曲、难度和模式"));
        AddAction(2, rowTitle, rowHint,
            SDL_GetScancodeName(static_cast<SDL_Scancode>(m_keys[i])), [this, i] { m_listening = i; m_listenTime = 0; });
    }
    AddAction(2, "恢复默认键位", "A / S / D / F / Space · Esc 暂停 · R 重试", "重置键位", [this]
    {
        for (int i = 0; i < 7; ++i) { m_keys[i] = defaultKeys[i]; Config::GetInstance().Set(keyNames[i], defaultKeys[i]); }
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

    {
        auto& skinMgr = SkinManager::GetInstance();
        skinMgr.ScanSkins();
        std::vector<std::string> skinLabels;
        std::vector<nlohmann::json> skinValues;
        for (const auto& skin : skinMgr.GetAvailableSkins())
        {
            skinLabels.push_back(skin.name.empty() ? skin.id : skin.name);
            skinValues.push_back(skin.id);
        }
        if (skinLabels.empty())
        {
            for (const auto& skin : SkinManager::GetBuiltinPresets())
            {
                skinLabels.push_back(skin.name.empty() ? skin.id : skin.name);
                skinValues.push_back(skin.id);
            }
        }
        AddChoice(4, "舞台皮肤主题", "实时切换音符晶石、滑条配色与专属打击音包", "graphics.skin_path", skinLabels, skinValues);
    }
    AddToggle(4, "樱花与命中粒子", "飘落花瓣、击打碎光与连击庆祝", "graphics.particles");
    AddToggle(4, "柔光", "音符、轨道和界面的辉光", "graphics.glow");
    AddToggle(4, "屏幕震动", "仅在连击中断时提供轻微反馈", "graphics.shake");
    AddToggle(4, "暗角", "为舞台边缘添加柔和阴影", "graphics.vignette");
    AddToggle(4, "减少动态效果", "静止背景并关闭震动与转场动画", "graphics.reduced_motion");
    AddSlider(4, "特效强度", "降低亮度和粒子数量，保持节奏清晰", "graphics.effect_intensity", 0, 1, 0.05f);
    AddToggle(4, "鼠标轨迹", "在鼠标区域显示短暂的光点轨迹", "gameplay.cursor_trail");
    AddToggle(4, "背景柔化", "柔化自定义歌曲背景，让音符更清晰", "graphics.background_blur");
    AddToggle(4, "背景视频舞台", "启用谱面原生硬件加速 MP4 / WebM 动态舞台", "visual.bga_enabled");
    AddSlider(4, "舞台暗化度", "动态视频与夜樱舞台压暗比例 (20% ~ 90%)", "visual.bg_dim", 0.20f, 0.90f, 0.05f);

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
    AddToggle(5, "自动云端同步", "启动与正式结算后自动幂等合并云端成绩与成就", "online.auto_cloud_sync");
    {
        const long long lastTs = cfg.Get<long long>("online.last_cloud_sync_ts", 0LL);
        std::string syncHint = "合并多设备最高成绩、最早解锁成就与核心键位流速设置";
        if (lastTs > 0)
        {
            const std::time_t tt = static_cast<std::time_t>(lastTs);
            std::tm tmBuf{};
#ifdef _WIN32
            localtime_s(&tmBuf, &tt);
#else
            localtime_r(&tt, &tmBuf);
#endif
            std::ostringstream oss;
            oss << "最近云同步：" << std::put_time(&tmBuf, "%Y-%m-%d %H:%M");
            syncHint = oss.str();
        }
        AddAction(5, "多端云存档", syncHint, "立即云端同步", []
        {
            auto& api = sakura::net::APIClient::GetInstance();
            if (api.GetAuthState() == sakura::net::AuthState::SessionExpired)
            {
                ToastManager::Instance().Show("登录凭据已过期，请先在主菜单右上角验证密码重登", ToastType::Warning, 3.6f);
                return;
            }
            if (!api.IsAuthenticated())
            {
                ToastManager::Instance().Show("请先在主菜单右上角登录在线账号后再进行云同步", ToastType::Info, 3.5f);
                return;
            }
            ToastManager::Instance().Show("正在与云端合并存档...", ToastType::Info, 2.0f);
            api.SyncCloudSaveAsync([](sakura::net::CloudSyncResult res)
            {
                if (!res.ok)
                {
                    ToastManager::Instance().Show("云同步暂不可用：" + res.error, ToastType::Warning, 3.8f);
                    return;
                }
                ToastManager::Instance().Show(
                    "云端同步完成 · 上传更新 " + std::to_string(res.scoresPushed)
                    + " 项 / 拉取恢复 " + std::to_string(res.scoresPulled + res.achievementsPulled) + " 项",
                    ToastType::Success, 4.0f);
            });
        });
    }
    AddToggle(5, "自动检查更新", "启动时后台静默检测 GitHub Releases 新版本", "online.auto_check_updates");
    AddAction(5, "版本更新", std::string("当前版本 v") + SAKURA_VERSION_STRING + " · 后台核验 SHA-256 并执行原子热更新", "检查并热更新", []
    {
        static std::atomic<bool> s_updateBusy{ false };
        if (s_updateBusy.exchange(true))
        {
            ToastManager::Instance().Show("更新任务正在后台执行中，请稍候...", ToastType::Info, 2.5f);
            return;
        }
        ToastManager::Instance().Show("正在后台检查最新版本...", ToastType::Info, 2.2f);
        Updater::GetInstance().CheckAsync(SAKURA_VERSION_STRING, [](const UpdateCheckResult& res)
        {
            if (!res.ok)
            {
                s_updateBusy.store(false);
                ToastManager::Instance().Show("检查更新不可用：" + res.error, ToastType::Warning, 3.8f);
                return;
            }
            if (!res.hasUpdate)
            {
                s_updateBusy.store(false);
                ToastManager::Instance().Show(std::string("当前已是最新版本 (v") + SAKURA_VERSION_STRING + ")", ToastType::Success, 3.2f);
                return;
            }

            ToastManager::Instance().Show(
                "发现新版本 " + res.latestVer + " · 正在后台流式下载并核验 SHA-256...",
                ToastType::Info, 4.5f);

            const char* basePath = SDL_GetBasePath();
            const std::filesystem::path appDir = (basePath && *basePath)
                ? std::filesystem::path(basePath)
                : std::filesystem::current_path();
            const auto installMode = Updater::DetectInstallMode(appDir);
            const std::filesystem::path stagingDir = std::filesystem::path(Paths::User("updates")) / res.latestVer;

            auto progressNotified = std::make_shared<bool>(false);
            auto onProgress = [progressNotified, ver = res.latestVer](float ratio)
            {
                if (!*progressNotified && ratio >= 0.5f && ratio < 1.0f)
                {
                    *progressNotified = true;
                    ToastManager::Instance().Show(
                        "正在下载更新包 " + ver + " (50%+)...",
                        ToastType::Info, 2.2f);
                }
            };

            auto onComplete = [res](StagedUpdateResult staged)
            {
                s_updateBusy.store(false);
                if (!staged.ok)
                {
                    ToastManager::Instance().Show(
                        "自动热更新未完成（" + staged.error + "），正在打开发布页",
                        ToastType::Warning, 4.5f);
                    SDL_OpenURL(res.releaseUrl.c_str());
                    return;
                }

                ToastManager::Instance().Show(
                    "校验通过，正在原子热替换并重启 Sakura...",
                    ToastType::Success, 4.0f);
                if (Updater::LaunchHotSwapScript(staged.scriptPath))
                {
                    SDL_Event q{};
                    q.type = SDL_EVENT_QUIT;
                    SDL_PushEvent(&q);
                }
                else
                {
                    SDL_OpenURL(res.releaseUrl.c_str());
                }
            };

            Updater::GetInstance().DownloadAndStageUpdateAsync(
                res, installMode, appDir, stagingDir, std::move(onProgress), std::move(onComplete));
        });
    });

    for (auto& page : m_pages)
    {
        if (page.size() <= 8) continue;
        const float rowStep = 0.630f / static_cast<float>(page.size());
        for (size_t r = 0; r < page.size(); ++r)
        {
            NormRect b = page[r].control->GetBounds();
            b.y = 0.232f + static_cast<float>(r) * rowStep + 0.004f;
            b.height = std::min(b.height, rowStep - 0.016f);
            page[r].control->SetBounds(b);
        }
    }
}

void SceneSettings::SyncAudio()
{
    auto& cfg = Config::GetInstance();
    auto& audio = sakura::audio::AudioManager::GetInstance();
    audio.SetMasterVolume(cfg.Get<float>("audio.master_volume", 1.0f));
    audio.SetMusicVolume(cfg.Get<float>("audio.music_volume", 0.8f));
    audio.SetSFXVolume(cfg.Get<float>("audio.sfx_volume", 0.8f));
    audio.SetHitsoundVolume(cfg.Get<float>("audio.hitsound_volume", 0.85f));
    audio.SetUISFXVolume(cfg.Get<float>("audio.ui_sfx_volume", 0.75f));
}

bool SceneSettings::Save()
{
    PreserveActiveAuthEndpointInConfig();
    Config::GetInstance().Set("online.settings_updated_at", static_cast<long long>(std::time(nullptr)));
    if (!Config::GetInstance().Save())
    { ToastManager::Instance().Show("设置保存失败，请检查目录权限与剩余空间", ToastType::Error); return false; }
    m_saved = StripLegacyOnlineCredentials(Config::GetInstance().GetRoot());
    m_dirty = false;
    if (Config::GetInstance().Get<bool>("online.auto_cloud_sync", true) &&
        sakura::net::APIClient::GetInstance().IsAuthenticated())
    {
        sakura::net::APIClient::GetInstance().SyncCloudSaveAsync();
    }
    ToastManager::Instance().Show("设置已保存", ToastType::Success);
    return true;
}

void SceneSettings::OnExit()
{
    if (m_dirty)
    {
        RestoreConfigPreservingAuth(m_saved);
        std::string savedSkin = Config::GetInstance().Get<std::string>("graphics.skin_path", "moonlit_sakura");
        if (savedSkin.empty()) savedSkin = "moonlit_sakura";
        SkinManager::GetInstance().SetActiveSkin(savedSkin);
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
        { if(m_closeRequested){RestoreConfigPreservingAuth(m_saved);std::string savedSkin=Config::GetInstance().Get<std::string>("graphics.skin_path","moonlit_sakura");if(savedSkin.empty())savedSkin="moonlit_sakura";SkinManager::GetInstance().SetActiveSkin(savedSkin);m_dirty=false;SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}else m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager), TransitionType::Fade, 0.25f); }));
    m_modalButtons.push_back(MakeButton({0.59f,0.59f,0.13f,0.05f}, kind == 1 ? "保存并返回" : "确认", m_font, [this, kind]
    {
        if (kind == 1) { if (Save()) { m_modal = 0; if(m_closeRequested){SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}else Back(); } }
        if (kind == 2)
        {
            ResetConfigToDefaultsPreservingAuth(); m_dirty = true; m_modal = 0;
            SkinManager::GetInstance().SetActiveSkin(Config::GetInstance().Get<std::string>("graphics.skin_path", "moonlit_sakura"));
            SyncAudio();
            sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(Config::GetInstance().Get<std::string>("audio.hitsound", "default"));
            BuildPages();
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
        const nlohmann::json sanitizedSettings = StripLegacyOnlineCredentials(Config::GetInstance().GetRoot());
        const bool ok = sakura::data::Database::GetInstance().BackupTo((folder / "sakura.db").string()) &&
            sakura::utils::AtomicWrite(folder / "settings.json", sanitizedSettings.dump(2));
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
    const auto before = StripLegacyOnlineCredentials(Config::GetInstance().GetRoot());
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
        RestoreConfigPreservingAuth(settings);
        if(!Config::GetInstance().SaveForce())throw std::runtime_error("设置无法写入");
        m_saved = StripLegacyOnlineCredentials(Config::GetInstance().GetRoot()); m_dirty = false;
        std::string restoredSkin = Config::GetInstance().Get<std::string>("graphics.skin_path", "moonlit_sakura");
        if (restoredSkin.empty()) restoredSkin = "moonlit_sakura";
        SkinManager::GetInstance().SetActiveSkin(restoredSkin);
        SyncAudio(); BuildPages();
        sakura::audio::AudioManager::GetInstance().LoadHitsoundSet(Config::GetInstance().Get<std::string>("audio.hitsound","default"));
        std::error_code cleanup;std::filesystem::remove_all(staging,cleanup);
        ToastManager::Instance().Show("存档已恢复，原存档与在线登录态已保留", ToastType::Success);
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
        RestoreConfigPreservingAuth(before);Config::GetInstance().SaveForce();SyncAudio();
        if(filesRecovered){std::error_code ec;std::filesystem::remove_all(staging,ec);}
        ToastManager::Instance().Show(recovered && filesRecovered ? std::string("恢复未完成，原记录已保留：")+error.what() : "恢复失败，请从备份目录恢复原存档",ToastType::Error,6);
    }
}

void SceneSettings::OnUpdate(float dt)
{
    if (m_topBar) m_topBar->Update(dt);
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
        for (int i = 0; i < 7; ++i)
            static_cast<Button*>(m_pages[2][i].control.get())->SetText(m_listening == i ? "请按键…" : SDL_GetScancodeName(static_cast<SDL_Scancode>(m_keys[i])));
    for (auto& button : m_footer) button->Update(dt);
    for (auto& button : m_modalButtons) button->Update(dt);
}

void SceneSettings::OnEvent(const SDL_Event& event)
{
    if (!m_modal && m_listening < 0 && m_topBar && m_topBar->HandleEvent(event))
        return;
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
            if(code==SDL_SCANCODE_ESCAPE && m_listening!=5){m_listening=-1;return;}
            const bool conflict = code == SDL_SCANCODE_F11 || code == SDL_SCANCODE_UNKNOWN ||
                (code == SDL_SCANCODE_ESCAPE && m_listening != 5) ||
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

    VisualStyle::DrawBadgePill(renderer, {0.04f, 0.036f, 0.12f, 0.028f}, "SYSTEM CONFIG",
        m_font, 0.014f, {36, 28, 56, 195}, palette.gildedHairline, palette.accent);
    renderer.DrawText(m_font, "设置 · PREFERENCES", 0.04f, 0.070f, 0.038f, palette.text);
    renderer.DrawText(m_font, "调整手感，让节奏成为本能。", 0.04f, 0.116f, 0.020f, palette.textDim);

    // 左侧分类导航底座与选中流光指示
    VisualStyle::DrawGlassCard(renderer, {0.030f, 0.205f, 0.170f, 0.585f},
        {20, 24, 42, 168}, {12, 15, 28, 192}, palette.surfaceBorder, {110, 120, 160, 36}, 0.014f, 0.32f);
    for (int t = 0; t < 6; ++t)
    {
        if (!m_tabs[t]) continue;
        if (t == m_page)
        {
            const NormRect tb = m_tabs[t]->GetBounds();
            renderer.DrawSoftShadow(tb, 0.010f, 0.010f, {palette.primary.r, palette.primary.g, palette.primary.b, 58}, 0.0f);
            m_tabs[t]->Render(renderer);
            renderer.DrawRoundedGradientRect(
                {tb.x - 0.006f, tb.y + 0.012f, 0.0045f, tb.height - 0.024f}, 0.002f,
                palette.accent, palette.primary, palette.accent, palette.primary);
        }
        else
        {
            m_tabs[t]->Render(renderer);
        }
    }

    // 右侧主琉璃面板
    VisualStyle::DrawPanel(renderer, {0.22f, 0.155f, 0.74f, 0.725f}, false, true);
    renderer.DrawText(m_font, titles[m_page], 0.253f, 0.174f, 0.030f, palette.text);
    renderer.DrawText(m_font, subtitles[m_page], 0.927f, 0.183f, 0.018f, palette.accent, TextAlign::Right);
    VisualStyle::DrawGildedDivider(renderer, 0.248f, 0.932f, 0.218f, 0.78f);

    const size_t pageRows = m_pages[m_page].size();
    const bool compactRows = pageRows > 8;
    const float rowStep = compactRows ? (0.630f / static_cast<float>(pageRows)) : 0.079f;
    const float rowStartY = compactRows ? 0.232f : 0.234f;
    const float cardH = compactRows ? (rowStep - 0.007f) : 0.068f;
    const float titleSz = compactRows ? 0.0205f : 0.023f;
    const float hintSz  = compactRows ? 0.0148f : 0.0165f;
    const float hintOff = compactRows ? 0.027f : 0.033f;

    size_t i = 0;
    for (auto& row : m_pages[m_page])
    {
        const size_t rowIdx = i++;
        const float y = rowStartY + static_cast<float>(rowIdx) * rowStep;
        const bool isListeningRow = (m_page == 2 && m_listening >= 0 && static_cast<size_t>(m_listening) == rowIdx);
        const Color rowTop = isListeningRow
            ? Color{64, 38, 72, 135}
            : ((rowIdx % 2 == 0) ? Color{28, 33, 56, 76} : Color{20, 24, 42, 42});
        const Color rowBot = isListeningRow
            ? Color{46, 28, 56, 115}
            : ((rowIdx % 2 == 0) ? Color{22, 26, 46, 58} : Color{16, 20, 36, 28});
        renderer.DrawRoundedGradientRect({0.242f, y - 0.005f, 0.696f, cardH}, 0.009f, rowTop, rowTop, rowBot, rowBot);
        if (isListeningRow)
        {
            renderer.DrawRoundedRectOutlineGradient({0.242f, y - 0.005f, 0.696f, cardH}, 0.009f,
                palette.accent, palette.primary, 0.0014f);
        }

        renderer.DrawText(m_font, row.title, 0.256f, y, titleSz, palette.text);
        renderer.DrawText(m_font, row.hint, 0.256f, y + hintOff, hintSz, palette.textDim);
        if (i < pageRows)
            renderer.DrawLine(0.256f, y + cardH, 0.924f, y + cardH, {160, 152, 196, 28}, 0.001f);
        if (auto* dropdown = dynamic_cast<Dropdown*>(row.control.get()); !dropdown || !dropdown->IsOpen())
            row.control->Render(renderer);
    }
    for (auto& row : m_pages[m_page])
        if (auto* dropdown = dynamic_cast<Dropdown*>(row.control.get()); dropdown && dropdown->IsOpen())
            dropdown->Render(renderer);

    for (auto& button : m_footer) button->Render(renderer);
    VisualStyle::DrawBadgePill(renderer, {0.22f, 0.919f, 0.34f, 0.042f},
        m_dirty ? "● 有未保存的修改 · Ctrl+S 立即保存" : "✓ 设置已同步 · Tab 切换分类 · Ctrl+S 保存",
        m_font, 0.0175f,
        m_dirty ? Color{58, 30, 52, 195} : Color{22, 28, 48, 175},
        m_dirty ? palette.primary : palette.surfaceBorder,
        m_dirty ? palette.accent : palette.textDim);

    if (m_modal)
    {
        VisualStyle::DrawScrim(renderer, 0.64f);
        VisualStyle::DrawPanel(renderer, {0.25f, 0.32f, 0.50f, 0.37f}, true, true);
        renderer.DrawText(m_font,
            m_modal == 1 ? "保存本次调整？" : (m_modal == 2 ? "恢复全部默认设置？" : "恢复选中的存档？"),
            0.5f, 0.372f, 0.033f, palette.text, TextAlign::Center);
        VisualStyle::DrawGildedDivider(renderer, 0.31f, 0.69f, 0.428f, 0.72f);
        renderer.DrawText(m_font, m_modal == 1 ? "放弃修改会恢复进入此页面时的设置。" :
            (m_modal == 2 ? "成绩和谱面会保留，设置恢复后仍可取消。" : "当前存档会先备份，再替换成绩与设置。"),
            0.5f, 0.456f, 0.021f, palette.textDim, TextAlign::Center);
        for (auto& button : m_modalButtons) button->Render(renderer);
    }
    if (m_topBar)
    {
        m_topBar->Render(renderer);
        m_topBar->RenderDrawer(renderer);
    }
}
} // namespace sakura::scene
