// scene_settings.cpp — 设置场景

#include "scene_settings.h"
#include "scene_calibration.h"
#include "scene_menu.h"
#include "core/config.h"
#include "core/input.h"
#include "core/window.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"
#include "utils/easing.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace sakura::scene
{

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneSettings::SceneSettings(SceneManager& mgr)
    : m_manager(mgr)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneSettings::OnEnter()
{
    LOG_INFO("[SceneSettings] 进入设置");

    auto& rm      = sakura::core::ResourceManager::GetInstance();
    m_font        = rm.GetDefaultFontHandle();
    m_fontSmall   = rm.GetDefaultFontHandle();

    // 创建 TabBar（垂直，左侧）
    m_tabBar = std::make_unique<sakura::ui::TabBar>(
        sakura::core::NormRect{ TAB_X, TAB_Y, TAB_W, TAB_H },
        std::vector<std::string>{ "通用", "音频", "按键", "显示" },
        m_currentTab,
        m_font,
        0.026f,
        sakura::ui::TabBar::Orientation::Vertical);

    m_tabBar->SetOnChange([this](int idx){ m_currentTab = idx; });

    // 初始化各 Tab 控件
    SetupGeneralTab();
    SetupAudioTab();
    SetupKeysTab();
    SetupDisplayTab();
    SetupBackButton();

    // 从 Config 加载当前值
    LoadFromConfig();

    m_listeningKeyIndex = -1;
    m_listenTimer       = 0.0f;
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneSettings::OnExit()
{
    LOG_INFO("[SceneSettings] 退出设置，保存配置");
    sakura::core::Config::GetInstance().Save();
}

// ── SetupGeneralTab ───────────────────────────────────────────────────────────

void SceneSettings::SetupGeneralTab()
{
    auto& cfg = sakura::core::Config::GetInstance();

    // 流速 Slider (0.5~3.0, 步长 0.1)
    m_sliderNoteSpeed = std::make_unique<sakura::ui::Slider>(
        sakura::core::NormRect{ CONTENT_X, SlotY(1), CONTENT_W, 0.07f },
        0.5f, 15.0f,
        cfg.Get<float>(sakura::core::ConfigKeys::kNoteSpeed, 5.0f),
        0.1f, m_font, 0.026f);
    m_sliderNoteSpeed->SetLabel("流速");
    m_sliderNoteSpeed->SetShowValue(true);
    m_sliderNoteSpeed->SetOnChange([](float v)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kNoteSpeed), v);
    });

    // 判定偏移 Slider (-100 ~ +100 ms)
    m_sliderOffset = std::make_unique<sakura::ui::Slider>(
        sakura::core::NormRect{ CONTENT_X, SlotY(2), CONTENT_W, 0.07f },
        -100.0f, 100.0f,
        static_cast<float>(cfg.Get<int>(std::string(sakura::core::ConfigKeys::kAudioOffset), 0)),
        1.0f, m_font, 0.026f);
    m_sliderOffset->SetLabel("判定偏移(ms)");
    m_sliderOffset->SetShowValue(true);
    m_sliderOffset->SetValueFormatter([](float v) -> std::string
    {
        return (v >= 0 ? "+" : "") + std::to_string(static_cast<int>(v)) + "ms";
    });
    m_sliderOffset->SetOnChange([](float v)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kAudioOffset),
            static_cast<int>(v));
    });

    // 延迟校准按钮
    sakura::ui::ButtonColors btnColors;
    btnColors.normal  = { 50, 45, 80, 220 };
    btnColors.hover   = { 80, 70, 120, 235 };
    btnColors.pressed = { 30, 25, 60, 240 };
    btnColors.text    = sakura::core::Color::White;

    m_btnCalibrate = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ CONTENT_X + CONTENT_W * 0.3f, SlotY(3), CONTENT_W * 0.4f, 0.055f },
        "延迟校准", m_font, 0.026f, 0.012f);
    m_btnCalibrate->SetColors(btnColors);
    m_btnCalibrate->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneCalibration>(m_manager),
            TransitionType::Fade, 0.3f);
    });
}

// ── SetupAudioTab ─────────────────────────────────────────────────────────────

void SceneSettings::SetupAudioTab()
{
    auto& cfg = sakura::core::Config::GetInstance();
    auto& audio = sakura::audio::AudioManager::GetInstance();

    sakura::ui::ButtonColors btnColors;
    btnColors.normal  = { 50, 45, 80, 220 };
    btnColors.hover   = { 80, 70, 120, 235 };
    btnColors.pressed = { 30, 25, 60, 240 };
    btnColors.text    = sakura::core::Color::White;
    (void)btnColors;

    // 主音量
    m_sliderMaster = std::make_unique<sakura::ui::Slider>(
        sakura::core::NormRect{ CONTENT_X, SlotY(1), CONTENT_W, 0.07f },
        0.0f, 1.0f,
        cfg.Get<float>(std::string(sakura::core::ConfigKeys::kMasterVolume), 1.0f),
        0.01f, m_font, 0.026f);
    m_sliderMaster->SetLabel("主音量");
    m_sliderMaster->SetValueFormatter([](float v) -> std::string
    {
        return std::to_string(static_cast<int>(v * 100.0f)) + "%";
    });
    m_sliderMaster->SetOnChange([](float v)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kMasterVolume), v);
        sakura::audio::AudioManager::GetInstance().SetMasterVolume(v);
    });

    // 音乐音量
    m_sliderMusic = std::make_unique<sakura::ui::Slider>(
        sakura::core::NormRect{ CONTENT_X, SlotY(2), CONTENT_W, 0.07f },
        0.0f, 1.0f,
        cfg.Get<float>(std::string(sakura::core::ConfigKeys::kMusicVolume), 0.8f),
        0.01f, m_font, 0.026f);
    m_sliderMusic->SetLabel("音乐音量");
    m_sliderMusic->SetValueFormatter([](float v) -> std::string
    {
        return std::to_string(static_cast<int>(v * 100.0f)) + "%";
    });
    m_sliderMusic->SetOnChange([](float v)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kMusicVolume), v);
        sakura::audio::AudioManager::GetInstance().SetMusicVolume(v);
    });

    // 音效音量
    m_sliderSFX = std::make_unique<sakura::ui::Slider>(
        sakura::core::NormRect{ CONTENT_X, SlotY(3), CONTENT_W, 0.07f },
        0.0f, 1.0f,
        cfg.Get<float>(std::string(sakura::core::ConfigKeys::kSfxVolume), 0.8f),
        0.01f, m_font, 0.026f);
    m_sliderSFX->SetLabel("音效音量");
    m_sliderSFX->SetValueFormatter([](float v) -> std::string
    {
        return std::to_string(static_cast<int>(v * 100.0f)) + "%";
    });
    m_sliderSFX->SetOnChange([](float v)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kSfxVolume), v);
        sakura::audio::AudioManager::GetInstance().SetSFXVolume(v);
    });

    (void)audio;

    // Hitsound 下拉框
    m_dropHitsound = std::make_unique<sakura::ui::Dropdown>(
        sakura::core::NormRect{ CONTENT_X + CONTENT_W * 0.30f, SlotY(4), CONTENT_W * 0.40f, 0.055f },
        std::vector<std::string>{ "default", "soft", "drum" },
        0,
        m_font, 0.026f);
    {
        std::string cur = cfg.Get<std::string>("audio.hitsound", "default");
        std::vector<std::string> opts = { "default", "soft", "drum" };
        for (int i = 0; i < static_cast<int>(opts.size()); ++i)
        {
            if (opts[i] == cur) { m_dropHitsound->SetSelectedIndex(i); break; }
        }
    }
    m_dropHitsound->SetOnChange([](int, const std::string& val)
    {
        sakura::core::Config::GetInstance().Set(
            std::string("audio.hitsound"), val);
    });
}

// ── SetupKeysTab ──────────────────────────────────────────────────────────────

void SceneSettings::SetupKeysTab()
{
    auto& cfg = sakura::core::Config::GetInstance();

    // 默认键
    m_keyCodes[0] = static_cast<SDL_Scancode>(cfg.Get<int>("input.key_lane_0", SDL_SCANCODE_A));
    m_keyCodes[1] = static_cast<SDL_Scancode>(cfg.Get<int>("input.key_lane_1", SDL_SCANCODE_S));
    m_keyCodes[2] = static_cast<SDL_Scancode>(cfg.Get<int>("input.key_lane_2", SDL_SCANCODE_D));
    m_keyCodes[3] = static_cast<SDL_Scancode>(cfg.Get<int>("input.key_lane_3", SDL_SCANCODE_F));
    m_keyCodes[4] = static_cast<SDL_Scancode>(cfg.Get<int>(std::string(sakura::core::ConfigKeys::kKeyPause), SDL_SCANCODE_ESCAPE));
    m_keyCodes[5] = static_cast<SDL_Scancode>(cfg.Get<int>(std::string(sakura::core::ConfigKeys::kKeyRetry), SDL_SCANCODE_R));

    sakura::ui::ButtonColors keyBtnColors;
    keyBtnColors.normal  = { 45, 45, 70, 220 };
    keyBtnColors.hover   = { 75, 65, 110, 235 };
    keyBtnColors.pressed = { 25, 25, 50,  240 };
    keyBtnColors.text    = sakura::core::Color::White;

    sakura::ui::ButtonColors listeningColors;
    listeningColors.normal  = { 100, 50, 150, 230 };
    listeningColors.hover   = { 120, 70, 180, 240 };
    listeningColors.pressed = { 80,  30, 120, 240 };
    listeningColors.text    = sakura::core::Color::White;
    (void)listeningColors;

    for (int i = 0; i < KEY_BIND_COUNT; ++i)
    {
        float y = SlotY(i + 1);
        m_keyButtons[i] = std::make_unique<sakura::ui::Button>(
            sakura::core::NormRect{ CONTENT_X + CONTENT_W * 0.35f, y - 0.025f,
                                   CONTENT_W * 0.3f, 0.05f },
            "", m_font, 0.026f, 0.01f);
        m_keyButtons[i]->SetColors(keyBtnColors);

        int idx = i;
        m_keyButtons[i]->SetOnClick([this, idx]()
        {
            m_listeningKeyIndex = idx;
            m_listenTimer       = 0.0f;
            LOG_INFO("[SceneSettings] 等待按键绑定: 索引 {}", idx);
        });
    }

    UpdateKeyButtonLabels();

    // 恢复默认按钮
    sakura::ui::ButtonColors resetColors;
    resetColors.normal  = { 80, 40, 40, 220 };
    resetColors.hover   = { 110, 60, 60, 235 };
    resetColors.pressed = { 55,  25, 25, 240 };
    resetColors.text    = sakura::core::Color::White;

    m_btnResetKeys = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ CONTENT_X + CONTENT_W * 0.3f, SlotY(KEY_BIND_COUNT + 1), CONTENT_W * 0.4f, 0.05f },
        "恢复默认", m_font, 0.026f, 0.012f);
    m_btnResetKeys->SetColors(resetColors);
    m_btnResetKeys->SetOnClick([this]()
    {
        m_keyCodes[0] = SDL_SCANCODE_A;
        m_keyCodes[1] = SDL_SCANCODE_S;
        m_keyCodes[2] = SDL_SCANCODE_D;
        m_keyCodes[3] = SDL_SCANCODE_F;
        m_keyCodes[4] = SDL_SCANCODE_ESCAPE;
        m_keyCodes[5] = SDL_SCANCODE_R;
        SaveKeyBindings();
        UpdateKeyButtonLabels();
        sakura::ui::ToastManager::Instance().Show("按键已恢复默认", sakura::ui::ToastType::Success);
    });
}

// ── SetupDisplayTab ───────────────────────────────────────────────────────────

void SceneSettings::SetupDisplayTab()
{
    auto& cfg = sakura::core::Config::GetInstance();

    // 全屏 Toggle
    m_toggleFullscreen = std::make_unique<sakura::ui::Toggle>(
        sakura::core::NormRect{ CONTENT_X, SlotY(1), CONTENT_W * 0.5f, 0.06f },
        cfg.Get<bool>(std::string(sakura::core::ConfigKeys::kFullscreen), false),
        m_font, 0.026f);
    m_toggleFullscreen->SetLabel("全屏模式");
    m_toggleFullscreen->SetOnChange([](bool on)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kFullscreen), on);
        // 实际切换由 App/Window 在下帧检测并处理
    });

    // 帧率 Dropdown
    m_dropFpsLimit = std::make_unique<sakura::ui::Dropdown>(
        sakura::core::NormRect{ CONTENT_X + CONTENT_W * 0.3f, SlotY(2), CONTENT_W * 0.4f, 0.055f },
        std::vector<std::string>{ "60", "120", "144", "240", "无限制" },
        2,  // 默认 144
        m_font, 0.026f);
    {
        int fps = cfg.Get<int>(std::string(sakura::core::ConfigKeys::kFpsLimit), 144);
        std::vector<int> fpsOpts = { 60, 120, 144, 240, 0 };
        for (int i = 0; i < static_cast<int>(fpsOpts.size()); ++i)
        {
            if (fpsOpts[i] == fps) { m_dropFpsLimit->SetSelectedIndex(i); break; }
        }
    }
    m_dropFpsLimit->SetOnChange([](int idx, const std::string&)
    {
        std::vector<int> fpsOpts = { 60, 120, 144, 240, 0 };
        if (idx >= 0 && idx < static_cast<int>(fpsOpts.size()))
        {
            sakura::core::Config::GetInstance().Set(
                std::string(sakura::core::ConfigKeys::kFpsLimit), fpsOpts[idx]);
        }
    });

    // VSync Toggle
    m_toggleVSync = std::make_unique<sakura::ui::Toggle>(
        sakura::core::NormRect{ CONTENT_X, SlotY(3), CONTENT_W * 0.5f, 0.06f },
        cfg.Get<bool>(std::string(sakura::core::ConfigKeys::kVSync), true),
        m_font, 0.026f);
    m_toggleVSync->SetLabel("垂直同步");
    m_toggleVSync->SetOnChange([](bool on)
    {
        sakura::core::Config::GetInstance().Set(
            std::string(sakura::core::ConfigKeys::kVSync), on);
    });
}

// ── SetupBackButton ───────────────────────────────────────────────────────────

void SceneSettings::SetupBackButton()
{
    sakura::ui::ButtonColors backColors;
    backColors.normal  = { 45, 45, 70, 220 };
    backColors.hover   = { 70, 65, 105, 235 };
    backColors.pressed = { 25, 25, 50, 240 };
    backColors.text    = sakura::core::Color::White;

    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.39f, 0.93f - 0.027f, 0.22f, 0.055f },
        "返回", m_font, 0.026f, 0.012f);
    m_btnBack->SetColors(backColors);
    m_btnBack->SetOnClick([this]()
    {
        m_manager.SwitchScene(
            std::make_unique<SceneMenu>(m_manager),
            TransitionType::SlideRight, 0.4f);
    });
}

// ── LoadFromConfig ────────────────────────────────────────────────────────────

void SceneSettings::LoadFromConfig()
{
    // 控件值在各 Setup 函数中已从 Config 读取，此处无需重复操作
}

// ── SaveKeyBindings ───────────────────────────────────────────────────────────

void SceneSettings::SaveKeyBindings()
{
    auto& cfg = sakura::core::Config::GetInstance();
    cfg.Set(std::string("input.key_lane_0"), static_cast<int>(m_keyCodes[0]));
    cfg.Set(std::string("input.key_lane_1"), static_cast<int>(m_keyCodes[1]));
    cfg.Set(std::string("input.key_lane_2"), static_cast<int>(m_keyCodes[2]));
    cfg.Set(std::string("input.key_lane_3"), static_cast<int>(m_keyCodes[3]));
    cfg.Set(std::string(sakura::core::ConfigKeys::kKeyPause), static_cast<int>(m_keyCodes[4]));
    cfg.Set(std::string(sakura::core::ConfigKeys::kKeyRetry),  static_cast<int>(m_keyCodes[5]));
}

// ── UpdateKeyButtonLabels ─────────────────────────────────────────────────────

void SceneSettings::UpdateKeyButtonLabels()
{
    for (int i = 0; i < KEY_BIND_COUNT; ++i)
    {
        if (m_listeningKeyIndex == i)
        {
            m_keyButtons[i]->SetText("按下任意键...");
        }
        else
        {
            const char* keyName = SDL_GetScancodeName(m_keyCodes[i]);
            m_keyButtons[i]->SetText(keyName ? keyName : "?");
        }
    }
}

// ── HasKeyConflict ────────────────────────────────────────────────────────────

bool SceneSettings::HasKeyConflict(int index, SDL_Scancode code) const
{
    for (int i = 0; i < KEY_BIND_COUNT; ++i)
    {
        if (i != index && m_keyCodes[i] == code)
            return true;
    }
    return false;
}

// ── GetKeyBindName ────────────────────────────────────────────────────────────

const char* SceneSettings::GetKeyBindName(int index) const
{
    static const char* names[KEY_BIND_COUNT] = {
        "轨道 1", "轨道 2", "轨道 3", "轨道 4", "暂停键", "快速重试"
    };
    if (index >= 0 && index < KEY_BIND_COUNT) return names[index];
    return "";
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneSettings::OnUpdate(float dt)
{
    // 按键监听超时
    if (m_listeningKeyIndex >= 0)
    {
        m_listenTimer += dt;
        if (m_listenTimer >= LISTEN_TIMEOUT)
        {
            m_listeningKeyIndex = -1;
            UpdateKeyButtonLabels();
        }
    }

    // 更新 TabBar
    m_tabBar->Update(dt);

    // 更新各 Tab 控件
    m_sliderNoteSpeed->Update(dt);
    m_sliderOffset->Update(dt);
    m_btnCalibrate->Update(dt);

    m_sliderMaster->Update(dt);
    m_sliderMusic->Update(dt);
    m_sliderSFX->Update(dt);
    m_dropHitsound->Update(dt);

    for (auto& btn : m_keyButtons) if (btn) btn->Update(dt);
    m_btnResetKeys->Update(dt);

    m_toggleFullscreen->Update(dt);
    m_dropFpsLimit->Update(dt);
    m_toggleVSync->Update(dt);

    m_btnBack->Update(dt);

    // 更新 Toast
    sakura::ui::ToastManager::Instance().Update(dt);
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneSettings::OnEvent(const SDL_Event& event)
{
    // 按键绑定监听模式
    if (m_listeningKeyIndex >= 0)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            SDL_Scancode code = event.key.scancode;

            // ESC 取消监听
            if (code == SDL_SCANCODE_ESCAPE)
            {
                m_listeningKeyIndex = -1;
                UpdateKeyButtonLabels();
                return;
            }

            if (HasKeyConflict(m_listeningKeyIndex, code))
            {
                sakura::ui::ToastManager::Instance().Show(
                    std::string("按键冲突：") + SDL_GetScancodeName(code),
                    sakura::ui::ToastType::Warning);
            }
            else
            {
                m_keyCodes[m_listeningKeyIndex] = code;
                SaveKeyBindings();
                sakura::ui::ToastManager::Instance().Show(
                    std::string(GetKeyBindName(m_listeningKeyIndex)) +
                    std::string(" 已绑定为 ") + SDL_GetScancodeName(code),
                    sakura::ui::ToastType::Success);
            }
            m_listeningKeyIndex = -1;
            UpdateKeyButtonLabels();
            return;
        }
        return;
    }

    // 返回键
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            m_manager.SwitchScene(
                std::make_unique<SceneMenu>(m_manager),
                TransitionType::SlideRight, 0.4f);
            return;
        }
    }

    // TabBar
    if (m_tabBar->HandleEvent(event)) return;

    // 各 Tab 控件事件
    switch (m_currentTab)
    {
        case TAB_GENERAL:
            m_sliderNoteSpeed->HandleEvent(event);
            m_sliderOffset->HandleEvent(event);
            m_btnCalibrate->HandleEvent(event);
            break;
        case TAB_AUDIO:
            m_sliderMaster->HandleEvent(event);
            m_sliderMusic->HandleEvent(event);
            m_sliderSFX->HandleEvent(event);
            m_dropHitsound->HandleEvent(event);
            break;
        case TAB_KEYS:
            for (auto& btn : m_keyButtons) if (btn) btn->HandleEvent(event);
            m_btnResetKeys->HandleEvent(event);
            break;
        case TAB_DISPLAY:
            m_toggleFullscreen->HandleEvent(event);
            m_dropFpsLimit->HandleEvent(event);
            m_toggleVSync->HandleEvent(event);
            break;
    }

    m_btnBack->HandleEvent(event);
}

// ── DrawSectionTitle ──────────────────────────────────────────────────────────

void SceneSettings::DrawSectionTitle(sakura::core::Renderer& renderer,
                                      const char* title, float y)
{
    renderer.DrawText(m_font, title,
                      CONTENT_X, y, 0.030f,
                      { 200, 180, 240, 230 },
                      sakura::core::TextAlign::Left);
    // 分隔线
    renderer.DrawLine(CONTENT_X, y + 0.035f,
                      CONTENT_X + CONTENT_W, y + 0.035f,
                      { 120, 100, 160, 100 }, 0.001f);
}

// ── RenderGeneralTab ──────────────────────────────────────────────────────────

void SceneSettings::RenderGeneralTab(sakura::core::Renderer& renderer)
{
    DrawSectionTitle(renderer, "游戏设置", CONTENT_Y);

    // 行标签
    renderer.DrawText(m_font, "下落速度",
        CONTENT_X, SlotY(1), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "判定偏移",
        CONTENT_X, SlotY(2), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "延迟校准",
        CONTENT_X, SlotY(3), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);

    m_sliderNoteSpeed->Render(renderer);
    m_sliderOffset->Render(renderer);
    m_btnCalibrate->Render(renderer);
}

// ── RenderAudioTab ────────────────────────────────────────────────────────────

void SceneSettings::RenderAudioTab(sakura::core::Renderer& renderer)
{
    DrawSectionTitle(renderer, "音频", CONTENT_Y);

    renderer.DrawText(m_font, "主音量",
        CONTENT_X, SlotY(1), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "音乐音量",
        CONTENT_X, SlotY(2), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "音效音量",
        CONTENT_X, SlotY(3), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "打击音效",
        CONTENT_X, SlotY(4), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);

    m_sliderMaster->Render(renderer);
    m_sliderMusic->Render(renderer);
    m_sliderSFX->Render(renderer);
    m_dropHitsound->Render(renderer);
}

// ── RenderKeysTab ─────────────────────────────────────────────────────────────

void SceneSettings::RenderKeysTab(sakura::core::Renderer& renderer)
{
    DrawSectionTitle(renderer, "按键绑定", CONTENT_Y);

    for (int i = 0; i < KEY_BIND_COUNT; ++i)
    {
        float y = SlotY(i + 1);
        // 名称
        renderer.DrawText(m_font, GetKeyBindName(i),
            CONTENT_X, y, 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);

        // 监听提示
        if (m_listeningKeyIndex == i)
        {
            float t = std::fmod(m_listenTimer, 0.8f) / 0.8f;
            uint8_t alpha = static_cast<uint8_t>(128 + 127 * std::sin(t * 3.14159f));
            renderer.DrawText(m_font, "按下任意键...",
                CONTENT_X + CONTENT_W * 0.35f, y, 0.026f,
                { 200, 160, 255, alpha }, sakura::core::TextAlign::Left);
        }

        if (m_keyButtons[i]) m_keyButtons[i]->Render(renderer);
    }

    m_btnResetKeys->Render(renderer);
}

// ── RenderDisplayTab ──────────────────────────────────────────────────────────

void SceneSettings::RenderDisplayTab(sakura::core::Renderer& renderer)
{
    DrawSectionTitle(renderer, "显示", CONTENT_Y);

    renderer.DrawText(m_font, "全屏模式",
        CONTENT_X, SlotY(1), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "帧率上限",
        CONTENT_X, SlotY(2), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);
    renderer.DrawText(m_font, "垂直同步",
        CONTENT_X, SlotY(3), 0.026f, { 200, 200, 210, 200 }, sakura::core::TextAlign::Left);

    m_toggleFullscreen->Render(renderer);
    m_dropFpsLimit->Render(renderer);
    m_toggleVSync->Render(renderer);
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneSettings::OnRender(sakura::core::Renderer& renderer)
{
    renderer.Clear(sakura::core::Color::DarkBlue);

    // 背景面板
    renderer.DrawRoundedRect(
        { 0.01f, 0.08f, 0.98f, 0.86f }, 0.015f,
        { 20, 18, 35, 200 }, true);

    // 标题
    renderer.DrawText(m_font, "设置",
        0.5f, 0.05f, 0.045f, { 220, 200, 255, 230 }, sakura::core::TextAlign::Center);

    // TabBar
    m_tabBar->Render(renderer);

    // 内容区背景
    renderer.DrawRoundedRect(
        { CONTENT_X - 0.01f, CONTENT_Y - 0.01f, CONTENT_W + 0.02f, CONTENT_H + 0.02f },
        0.012f, { 30, 25, 50, 180 }, true);

    // 当前 Tab 内容
    switch (m_currentTab)
    {
        case TAB_GENERAL: RenderGeneralTab(renderer); break;
        case TAB_AUDIO:   RenderAudioTab(renderer);   break;
        case TAB_KEYS:    RenderKeysTab(renderer);    break;
        case TAB_DISPLAY: RenderDisplayTab(renderer); break;
    }

    // 返回按钮
    m_btnBack->Render(renderer);

    // Toast 通知（最顶层）
    sakura::ui::ToastManager::Instance().Render(renderer, m_font, 0.024f);
}

} // namespace sakura::scene
