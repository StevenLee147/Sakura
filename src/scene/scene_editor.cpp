// scene_editor.cpp — 谱面编辑器场景实现

#include "scene_editor.h"
#include "core/paths.h"
#include "core/theme.h"
#include "ui/visual_style.h"
#include "scene_menu.h"
#include "scene_chart_wizard.h"
#include "scene_game.h"
#include "game/chart_loader.h"
#include "core/resource_manager.h"
#include "audio/audio_manager.h"
#include "ui/toast.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace sakura::scene
{

namespace
{
class EditPropertiesCommand final : public sakura::editor::EditorCommand {
    sakura::game::ChartData beforeData,afterData;
    sakura::game::ChartInfo beforeInfo,afterInfo;
public:
    EditPropertiesCommand(sakura::editor::EditorCore& core,sakura::game::ChartData data,sakura::game::ChartInfo info)
        :beforeData(core.GetChartData()),afterData(std::move(data)),beforeInfo(core.GetChartInfo()),afterInfo(std::move(info)){}
    void Execute(sakura::editor::EditorCore& core)override{core.GetChartData()=afterData;core.GetChartInfo()=afterInfo;core.ClearSelection();}
    void Undo(sakura::editor::EditorCore& core)override{core.GetChartData()=beforeData;core.GetChartInfo()=beforeInfo;core.ClearSelection();}
    std::string GetDescription()const override{return "编辑属性";}
};
constexpr float OVERVIEW_X = 0.77f;
constexpr float OVERVIEW_Y = 0.06f;
constexpr float OVERVIEW_W = 0.21f;
constexpr float OVERVIEW_H = 0.94f;
}

// ── 构造 ──────────────────────────────────────────────────────────────────────

SceneEditor::SceneEditor(SceneManager& mgr,
                         const std::string& folderPath,
                         const std::string& difficultyFile,
                         int difficultyIndex)
    : m_manager(mgr)
    , m_timeline(m_core)
    , m_mouseArea(m_core)
    , m_preview(m_core)
    , m_initFolderPath(folderPath)
    , m_initDiffFile(difficultyFile)
    , m_initDiffIndex(difficultyIndex)
{
}

// ── OnEnter ───────────────────────────────────────────────────────────────────

void SceneEditor::OnEnter()
{
    LOG_INFO("[SceneEditor] 进入编辑器场景");

    auto& rm      = sakura::core::ResourceManager::GetInstance();
    m_fontUI      = rm.GetDefaultFontHandle();
    m_fontSmall   = rm.GetDefaultFontHandle();

    m_ctrlHeld    = false;
    m_shiftHeld   = false;

    if(!m_initFolderPath.empty()) {
        try {
            namespace fs=std::filesystem;
            const auto source=fs::weakly_canonical(m_initFolderPath);
            const auto builtin=fs::weakly_canonical("resources/charts");
            const auto relative=source.lexically_relative(builtin);
            if(!relative.empty() && *relative.begin()!="..") {
                const auto target=fs::path(sakura::core::Paths::User("charts"))/source.filename();
                if(!fs::exists(target)) {fs::create_directories(target);fs::copy(source,target,fs::copy_options::recursive);}
                m_initFolderPath=target.string();
            }
        }catch(const std::exception& e){
            sakura::ui::ToastManager::Instance().Show(std::string("无法创建可编辑副本：")+e.what(),sakura::ui::ToastType::Error);
            m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager));return;
        }
    }
    // 加载或新建谱面
    if (!m_initFolderPath.empty())
    {
        if (!m_core.LoadChart(m_initFolderPath, m_initDiffFile))
        {
            sakura::ui::ToastManager::Instance().Show("谱面加载失败，请检查文件",sakura::ui::ToastType::Error);
            m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager));return;
        }
        // 调用方显式给出难度索引时（离线录制），属性面板与试玩使用对应难度
        if (m_initDiffIndex >= 0 && m_initDiffIndex < static_cast<int>(m_core.GetChartInfo().difficulties.size()))
            m_currentDiffIndex = m_initDiffIndex;
    }
    else
    {
        m_manager.SwitchScene(std::make_unique<SceneChartWizard>(m_manager));return;
    }

    // 音频停止（回到编辑器时停止游戏音乐）
    sakura::audio::AudioManager::GetInstance().StopMusic();

    // 初始化时间轴字体
    m_timeline.SetFont(m_fontSmall);
    m_mouseArea.SetFont(m_fontSmall);
    m_preview.SetFont(m_fontSmall);

    // 加载音频波形（若谱面目录中有音乐文件）
    {
        const auto& info = m_core.GetChartInfo();
        if (!info.musicFile.empty() && !info.folderPath.empty())
        {
            std::string musicPath = info.folderPath + "/" + info.musicFile;
            m_timeline.LoadWaveform(musicPath);
        }
    }

    // 初始滚动：让 t=0 在时间轴靠下位置
    m_timeline.CenterOnTime(0);

    SetupToolbar();
    m_btnProperties=std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.50f,0.84f,0.17f,0.034f},"编辑属性",m_fontUI,0.018f);
    m_btnProperties->SetTextAlign(sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::ApplyButton(m_btnProperties.get(),sakura::ui::ButtonVariant::Secondary);
    m_btnProperties->SetOnClick([this]{OpenProperties();});
    UpdateToolButtons();
}

// ── OnExit ────────────────────────────────────────────────────────────────────

void SceneEditor::OnExit()
{
    LOG_INFO("[SceneEditor] 退出编辑器场景");
    m_core.StopPlayback();
}

// ── SetupToolbar ──────────────────────────────────────────────────────────────

void SceneEditor::SetupToolbar()
{
    const char* toolLabels[TOOL_COUNT] = { "Tap", "Hold", "Circle", "Slider" };

    for (int i = 0; i < TOOL_COUNT; ++i)
    {
        float x = 0.01f + i * 0.075f;
        m_toolBtns[i] = std::make_unique<sakura::ui::Button>(
            sakura::core::NormRect{ x, 0.005f, 0.068f, 0.048f },
            toolLabels[i], m_fontUI, 0.020f, 0.008f);
        sakura::ui::VisualStyle::ApplyButton(m_toolBtns[i].get(), sakura::ui::ButtonVariant::Secondary);
        m_toolBtns[i]->SetTextAlign(sakura::core::TextAlign::Center);
        m_toolBtns[i]->SetTextPadding(0.0f);

        int toolIdx = i;
        m_toolBtns[i]->SetOnClick([this, toolIdx]()
        {
            if (m_core.HasWipSlider())
                m_core.CancelSlider();
            m_core.SetNoteTool(static_cast<sakura::editor::NoteToolType>(toolIdx));
            UpdateToolButtons();
        });
    }

    // 播放/暂停按钮
    m_btnPlay = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.40f, 0.005f, 0.08f, 0.048f },
        "▶ 播放", m_fontUI, 0.020f, 0.008f);
    m_btnPlay->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnPlay->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnPlay.get(), sakura::ui::ButtonVariant::Primary);
    m_btnPlay->SetOnClick([this]()
    {
        TogglePlayback();
    });

    // 撤销按钮
    m_btnUndo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.635f, 0.005f, 0.072f, 0.048f },
        "↩ 撤销", m_fontUI, 0.020f, 0.008f);
    m_btnUndo->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnUndo->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnUndo.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnUndo->SetOnClick([this]()
    {
        m_core.Undo();
        UpdateUndoRedoButtons();
        sakura::ui::ToastManager::Instance().Show(
            "撤销: " + (m_core.CanRedo() ? m_core.GetRedoDescription() : ""),
            sakura::ui::ToastType::Info);
    });

    // 重做按钮
    m_btnRedo = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.712f, 0.005f, 0.072f, 0.048f },
        "重做", m_fontUI, 0.020f, 0.008f);
    m_btnRedo->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnRedo->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnRedo.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnRedo->SetOnClick([this]()
    {
        m_core.Redo();
        UpdateUndoRedoButtons();
        sakura::ui::ToastManager::Instance().Show(
            "重做: " + (m_core.CanUndo() ? m_core.GetUndoDescription() : ""),
            sakura::ui::ToastType::Info);
    });

    // 保存按钮
    m_btnSave = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.82f, 0.005f, 0.08f, 0.048f },
        "保存", m_fontUI, 0.020f, 0.008f);
    m_btnSave->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnSave->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnSave.get(), sakura::ui::ButtonVariant::Accent);
    m_btnSave->SetOnClick([this]() { DoSave(); });

    // 退出
    m_btnBack = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.91f, 0.005f, 0.08f, 0.048f },
        "← 退出", m_fontUI, 0.020f, 0.008f);
    m_btnBack->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnBack->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnBack.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnBack->SetOnClick([this]() { RequestExit(); });

    // BeatSnap 调整 ↑↓
    m_btnSnapDec = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.54f, 0.005f, 0.040f, 0.048f },
        "1/↓", m_fontUI, 0.018f, 0.006f);
    m_btnSnapDec->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnSnapDec->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnSnapDec.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnSnapDec->SetOnClick([this]()
    {
        int cur = m_core.GetBeatSnap();
        const int vals[] = { 1, 2, 4, 8, 16 };
        for (int k = static_cast<int>(std::size(vals)) - 1; k >= 0; --k)
        {
            if (vals[k] < cur) { m_core.SetBeatSnap(vals[k]); break; }
        }
    });

    m_btnSnapInc = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.585f, 0.005f, 0.040f, 0.048f },
        "1/↑", m_fontUI, 0.018f, 0.006f);
    m_btnSnapInc->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnSnapInc->SetTextPadding(0.0f);
    sakura::ui::VisualStyle::ApplyButton(m_btnSnapInc.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnSnapInc->SetOnClick([this]()
    {
        int cur = m_core.GetBeatSnap();
        const int vals[] = { 1, 2, 4, 8, 16 };
        for (int v : vals)
        {
            if (v > cur) { m_core.SetBeatSnap(v); break; }
        }
    });

    // 难度管理按钮（在属性面板区域内）
    m_btnDiffPrev = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.425f, 0.696f, 0.048f, 0.034f },
        "◀", m_fontUI, 0.018f, 0.005f);
    sakura::ui::VisualStyle::ApplyButton(m_btnDiffPrev.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnDiffPrev->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnDiffPrev->SetTextPadding(0.0f);
    m_btnDiffPrev->SetOnClick([this]()
    {
        int cnt = static_cast<int>(m_core.GetChartInfo().difficulties.size());
        if (cnt > 1)
            SwitchDifficulty((m_currentDiffIndex + cnt - 1) % cnt);
    });

    m_btnDiffNext = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.697f, 0.696f, 0.048f, 0.034f },
        "▶", m_fontUI, 0.018f, 0.005f);
    sakura::ui::VisualStyle::ApplyButton(m_btnDiffNext.get(), sakura::ui::ButtonVariant::Secondary);
    m_btnDiffNext->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnDiffNext->SetTextPadding(0.0f);
    m_btnDiffNext->SetOnClick([this]()
    {
        int cnt = static_cast<int>(m_core.GetChartInfo().difficulties.size());
        if (cnt > 1)
            SwitchDifficulty((m_currentDiffIndex + 1) % cnt);
    });

    m_btnDiffAdd = std::make_unique<sakura::ui::Button>(
        sakura::core::NormRect{ 0.597f, 0.696f, 0.048f, 0.034f },
        "+ 难度", m_fontUI, 0.015f, 0.005f);
    sakura::ui::VisualStyle::ApplyButton(m_btnDiffAdd.get(), sakura::ui::ButtonVariant::Accent);
    m_btnDiffAdd->SetTextAlign(sakura::core::TextAlign::Center);
    m_btnDiffAdd->SetTextPadding(0.0f);
    m_btnDiffAdd->SetOnClick([this]() { AddNewDifficulty(); });
}

// ── UpdateToolButtons ─────────────────────────────────────────────────────────

void SceneEditor::UpdateToolButtons()
{
    int current = static_cast<int>(m_core.GetNoteTool());
    for (int i = 0; i < TOOL_COUNT; ++i)
    {
        if (!m_toolBtns[i]) continue;
        sakura::ui::ButtonColors c;
        if (i == current)
        {
            c = sakura::ui::VisualStyle::ButtonColorsFor(sakura::ui::ButtonVariant::Primary);
        }
        else
        {
            c = sakura::ui::VisualStyle::ButtonColorsFor(sakura::ui::ButtonVariant::Secondary);
        }
        m_toolBtns[i]->SetColors(c);
    }
}

// ── UpdateUndoRedoButtons ─────────────────────────────────────────────────────
void SceneEditor::UpdateUndoRedoButtons()
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    if (m_btnUndo)
    {
        sakura::ui::ButtonColors c = sakura::ui::VisualStyle::ButtonColorsFor(sakura::ui::ButtonVariant::Secondary);
        if (!m_core.CanUndo())
        {
            c.normal  = { 16, 18, 32, 110 };
            c.hover   = { 20, 22, 38, 120 };
            c.pressed = { 14, 16, 28, 110 };
            c.border  = { 90, 95, 130, 55 };
            c.text    = palette.textMuted;
        }
        m_btnUndo->SetColors(c);

        const int cnt = m_core.GetUndoCount();
        m_btnUndo->SetText(cnt > 0 ? ("↩ 撤销(" + std::to_string(cnt) + ")") : "↩ 撤销");
    }

    if (m_btnRedo)
    {
        sakura::ui::ButtonColors c = sakura::ui::VisualStyle::ButtonColorsFor(sakura::ui::ButtonVariant::Secondary);
        if (!m_core.CanRedo())
        {
            c.normal  = { 16, 18, 32, 110 };
            c.hover   = { 20, 22, 38, 120 };
            c.pressed = { 14, 16, 28, 110 };
            c.border  = { 90, 95, 130, 55 };
            c.text    = palette.textMuted;
        }
        m_btnRedo->SetColors(c);

        const int cnt = m_core.GetRedoCount();
        m_btnRedo->SetText(cnt > 0 ? ("↪ 重做(" + std::to_string(cnt) + ")") : "↪ 重做");
    }
}

// ── TogglePlayback ───────────────────────────────────────────────────────────

void SceneEditor::TogglePlayback()
{
    if (!m_core.IsPlaying())
    {
        const auto& info = m_core.GetChartInfo();
        if (!info.musicFile.empty() && !info.folderPath.empty())
        {
            auto& audio = sakura::audio::AudioManager::GetInstance();
            if (!audio.IsPlaying() && !audio.IsPaused())
            {
                std::string path = info.folderPath + "/" + info.musicFile;
                audio.PlayMusic(path, 0);
            }
        }
    }

    m_core.TogglePlayback();

    if (m_btnPlay)
        m_btnPlay->SetText(m_core.IsPlaying() ? "⏸ 暂停" : "▶ 播放");
}

// ── DoSave ────────────────────────────────────────────────────────────────────

void SceneEditor::DoSave()
{
    bool ok = m_core.SaveChart();
    if (ok)
    {
        sakura::ui::ToastManager::Instance().Show(
            "谱面已保存", sakura::ui::ToastType::Success);
    }
    else
    {
        sakura::ui::ToastManager::Instance().Show(
            "保存失败，请检查路径", sakura::ui::ToastType::Error);
    }
}

// ── SwitchDifficulty ──────────────────────────────────────────────────────────

void SceneEditor::SwitchDifficulty(int index)
{
    const auto& diffs = m_core.GetChartInfo().difficulties;
    if (diffs.empty() || index < 0 || index >= static_cast<int>(diffs.size())) return;
    if (index == m_currentDiffIndex) return;

    // 先保存当前难度
    if (m_core.IsDirty()) { DoSave(); if(m_core.IsDirty()) return; }

    m_currentDiffIndex = index;
    const std::string& folderPath = m_core.GetFolderPath();
    const std::string& diffFile   = diffs[index].chartFile;

    if (!m_core.LoadChart(folderPath, diffFile))
    {
        sakura::ui::ToastManager::Instance().Show(
            "难度加载失败: " + diffFile, sakura::ui::ToastType::Error);
        return;
    }

    // 重新加载波形
    const auto& info = m_core.GetChartInfo();
    if (!info.musicFile.empty() && !info.folderPath.empty())
        m_timeline.LoadWaveform(info.folderPath + "/" + info.musicFile);

    m_timeline.CenterOnTime(0);
    sakura::ui::ToastManager::Instance().Show(
        "已切换到难度: " + diffs[index].name, sakura::ui::ToastType::Info);
}

// ── AddNewDifficulty ──────────────────────────────────────────────────────────

void SceneEditor::AddNewDifficulty()
{
    auto& info = m_core.GetChartInfo();
    int newIdx = static_cast<int>(info.difficulties.size());

    // 新难度名
    std::string newName  = "Hard";
    if (newIdx == 1)      newName = "Hard";
    else if (newIdx == 2) newName = "Expert";
    else if (newIdx == 3) newName = "Master";
    else                  newName = "Extra" + std::to_string(newIdx);

    std::string newFile = newName;
    std::transform(newFile.begin(), newFile.end(), newFile.begin(), ::tolower);
    newFile += ".json";

    // 先保存当前
    if (m_core.IsDirty()) { DoSave(); if(m_core.IsDirty()) return; }

    // 从当前难度复制到新文件
    bool ok = m_core.SaveChartTo(info.folderPath + "/" + newFile);
    if (!ok)
    {
        sakura::ui::ToastManager::Instance().Show(
            "新建难度失败", sakura::ui::ToastType::Error);
        return;
    }

    m_core.LoadChart(m_core.GetFolderPath(),newFile);
    m_currentDiffIndex = newIdx;
    m_timeline.CenterOnTime(0);

    sakura::ui::ToastManager::Instance().Show(
        "已添加并切换到难度: " + newName, sakura::ui::ToastType::Info);
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────

void SceneEditor::StartFullPreview(int fromMs){
    m_core.StopPlayback();
    if(m_core.IsDirty()){DoSave();if(m_core.IsDirty())return;}
    if(m_core.GetChartData().keyboardNotes.empty()&&m_core.GetChartData().mouseNotes.empty()){
        sakura::ui::ToastManager::Instance().Show("先放置一些音符，再开始试玩",sakura::ui::ToastType::Info);return;
    }
    sakura::game::PlayOptions options;options.mode=sakura::game::PlayMode::Practice;options.startMs=std::max(0,fromMs);options.returnToEditor=true;
    m_manager.PushScene(std::make_unique<SceneGame>(m_manager,m_core.GetChartInfo(),m_currentDiffIndex,options),TransitionType::Fade,0.25f);
}

void SceneEditor::OpenProperties(){
    m_preview.Stop();m_core.StopPlayback();m_propertyEditing=true;
    m_propertyKeyboard=m_core.GetSelectedKbNote();m_propertyMouse=m_core.GetSelectedMouseNote();
    std::vector<std::string> values;
    const auto& data=m_core.GetChartData();
    if(m_propertyKeyboard>=0&&m_propertyKeyboard<static_cast<int>(data.keyboardNotes.size())){
        const auto& n=data.keyboardNotes[m_propertyKeyboard];
        m_propertyLabels={"时间 / ms","轨道 / 1–4","持续时间 / ms（Tap 为 0）"};
        values={std::to_string(n.time),std::to_string(n.lane+1),std::to_string(n.duration)};
    }else if(m_propertyMouse>=0&&m_propertyMouse<static_cast<int>(data.mouseNotes.size())){
        const auto& n=data.mouseNotes[m_propertyMouse];
        m_propertyLabels={"时间 / ms","横向位置 / 0–1","纵向位置 / 0–1","持续时间 / ms（Circle 为 0）"};
        values={std::to_string(n.time),std::to_string(n.x),std::to_string(n.y),std::to_string(n.sliderDuration)};
    }else{
        m_propertyKeyboard=m_propertyMouse=-1;const auto& info=m_core.GetChartInfo();const auto& d=info.difficulties[m_currentDiffIndex];
        m_propertyLabels={"初始 BPM（后续变速点保留）","谱面偏移 / ms","难度名称","难度等级 / 1–20"};
        values={std::to_string(info.bpm),std::to_string(info.offset),d.name,std::to_string(d.level)};
    }
    m_propertyInputs.clear();
    for(size_t i=0;i<values.size();++i){
        auto input=std::make_unique<sakura::ui::TextInput>(sakura::core::NormRect{0.53f,0.32f+i*0.088f,0.20f,0.047f},m_fontUI,0.023f,48);
        input->SetText(values[i]);input->SetOnSubmit([this](const std::string&){ApplyProperties();});m_propertyInputs.push_back(std::move(input));
    }
    m_propertyApply=std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.53f,0.73f,0.20f,0.053f},"应用修改",m_fontUI,0.024f);
    m_propertyCancel=std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.27f,0.73f,0.20f,0.053f},"取消",m_fontUI,0.024f);
    for(auto* b:{m_propertyApply.get(),m_propertyCancel.get()})b->SetTextAlign(sakura::core::TextAlign::Center);
    sakura::ui::VisualStyle::ApplyButton(m_propertyApply.get(),sakura::ui::ButtonVariant::Primary);
    sakura::ui::VisualStyle::ApplyButton(m_propertyCancel.get(),sakura::ui::ButtonVariant::Secondary);
    m_propertyApply->SetOnClick([this]{ApplyProperties();});m_propertyCancel->SetOnClick([this]{CloseProperties();});
}
void SceneEditor::CloseProperties(){m_propertyEditing=false;for(auto& i:m_propertyInputs)i->SetFocused(false);}
void SceneEditor::ApplyProperties(){
    try{
        auto number=[this](int i,double low,double high){size_t used=0;const auto& text=m_propertyInputs[i]->GetText();const double value=std::stod(text,&used);
            if(used!=text.size()||!std::isfinite(value)||value<low||value>high)throw std::runtime_error("请输入范围内的有效数值");return value;};
        auto data=m_core.GetChartData();auto info=m_core.GetChartInfo();
        if(m_propertyKeyboard>=0){auto& n=data.keyboardNotes[m_propertyKeyboard];n.time=static_cast<int>(number(0,0,3600000));n.lane=static_cast<int>(number(1,1,4))-1;
            n.duration=static_cast<int>(number(2,0,600000));n.type=n.duration>0?sakura::game::NoteType::Hold:sakura::game::NoteType::Tap;
        }else if(m_propertyMouse>=0){auto& n=data.mouseNotes[m_propertyMouse];n.time=static_cast<int>(number(0,0,3600000));n.x=static_cast<float>(number(1,0,1));n.y=static_cast<float>(number(2,0,1));
            n.sliderDuration=static_cast<int>(number(3,n.type==sakura::game::NoteType::Slider?1:0,n.type==sakura::game::NoteType::Slider?600000:0));
        }else{
            info.bpm=static_cast<float>(number(0,20,1000));info.offset=static_cast<int>(number(1,-10000,10000));
            auto& d=info.difficulties[m_currentDiffIndex];d.name=m_propertyInputs[2]->GetText();if(d.name.empty())throw std::runtime_error("难度名称不能为空");d.level=static_cast<float>(number(3,1,20));
            for(size_t i=0;i<info.difficulties.size();++i)if(i!=m_currentDiffIndex&&info.difficulties[i].name==d.name)throw std::runtime_error("难度名称已经存在");
            if(!data.timingPoints.empty())data.timingPoints.front().bpm=info.bpm;
        }
        std::stable_sort(data.keyboardNotes.begin(),data.keyboardNotes.end(),[](const auto& a,const auto& b){return a.time<b.time;});
        std::stable_sort(data.mouseNotes.begin(),data.mouseNotes.end(),[](const auto& a,const auto& b){return a.time<b.time;});
        if(!sakura::game::ChartLoader{}.ValidateChartData(data))throw std::runtime_error("修改后的谱面数据无效");
        m_core.ExecuteCommand(std::make_unique<EditPropertiesCommand>(m_core,std::move(data),std::move(info)));
        CloseProperties();sakura::ui::ToastManager::Instance().Show("属性已更新 · Ctrl+Z 可撤销",sakura::ui::ToastType::Success);
    }catch(const std::exception& e){sakura::ui::ToastManager::Instance().Show(e.what(),sakura::ui::ToastType::Warning);}
}
void SceneEditor::RenderProperties(sakura::core::Renderer& r){
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawScrim(r, 0.66f);
    sakura::ui::VisualStyle::DrawPanel(r, {0.23f, 0.20f, 0.54f, 0.64f}, true, true);
    r.DrawText(m_fontUI,
        m_propertyKeyboard >= 0 ? "键盘音符属性" : (m_propertyMouse >= 0 ? "鼠标音符属性" : "谱面元数据与节奏属性"),
        0.27f, 0.232f, 0.033f, palette.text);
    sakura::ui::VisualStyle::DrawGildedDivider(r, 0.27f, 0.73f, 0.285f, 0.72f);
    for (size_t i = 0; i < m_propertyInputs.size(); ++i)
    {
        const float rowY = 0.320f + static_cast<float>(i) * 0.088f;
        if (m_propertyInputs[i]->IsFocused())
        {
            r.DrawRoundedGradientRect({0.26f, rowY - 0.008f, 0.48f, 0.063f}, 0.009f,
                {42, 32, 64, 95}, {26, 24, 48, 55}, {42, 32, 64, 95}, {26, 24, 48, 55});
        }
        r.DrawText(m_fontSmall, m_propertyLabels[i], 0.275f, rowY + 0.010f, 0.020f,
            m_propertyInputs[i]->IsFocused() ? palette.accent : palette.textDim);
        m_propertyInputs[i]->Render(r);
    }
    r.DrawText(m_fontSmall, "Enter 应用修改  ·  Tab 切换输入框  ·  ESC 取消", 0.27f, 0.682f, 0.0175f, palette.textMuted);
    m_propertyApply->Render(r);
    m_propertyCancel->Render(r);
}

bool SceneEditor::CanClose() {
    if(m_allowClose||!m_core.IsDirty())return true;
    RequestExit(true);return false;
}
void SceneEditor::FinishExit(){
    m_exitDialog=false;
    if(m_closeWindow){m_allowClose=true;SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}
    else m_manager.SwitchScene(std::make_unique<SceneMenu>(m_manager),TransitionType::SlideRight,0.3f);
}
void SceneEditor::RequestExit(bool window){
    if(m_exitDialog){m_closeWindow|=window;return;}
    CloseProperties();m_closeWindow=window;
    if(!m_core.IsDirty()){FinishExit();return;}
    m_preview.Stop();m_core.StopPlayback();
    m_exitDialog=true;m_exitButtons.clear();
    const char* labels[]={"继续编辑","放弃修改","保存并退出"};
    for(int i=0;i<3;++i){
        auto button=std::make_unique<sakura::ui::Button>(sakura::core::NormRect{0.28f+i*0.15f,0.57f,0.14f,0.055f},labels[i],m_fontUI,0.023f);
        button->SetTextAlign(sakura::core::TextAlign::Center);
        sakura::ui::VisualStyle::ApplyButton(button.get(),i==2?sakura::ui::ButtonVariant::Primary:sakura::ui::ButtonVariant::Secondary);
        button->SetOnClick([this,i]{if(i==0){m_exitDialog=false;return;}if(i==2){DoSave();if(m_core.IsDirty())return;}FinishExit();});
        m_exitButtons.push_back(std::move(button));
    }
}

void SceneEditor::OnUpdate(float dt)
{
    if(m_exitDialog){for(auto& button:m_exitButtons)button->Update(dt);return;}
    if(m_propertyEditing){for(auto& i:m_propertyInputs)i->Update(dt);m_propertyApply->Update(dt);m_propertyCancel->Update(dt);return;}
    if(m_btnProperties)m_btnProperties->Update(dt);
    m_recoveryTimer+=dt;
    if(m_recoveryTimer>=45){
        m_recoveryTimer=0;
        if(m_core.IsDirty()&&!m_core.GetChartInfo().folderPath.empty()){
            const auto path=std::filesystem::path(m_core.GetChartInfo().folderPath)/("recovery_"+m_core.GetDiffFile());
            if(!m_core.SaveChartTo(path.string(),false))sakura::ui::ToastManager::Instance().Show("恢复副本写入失败，请及时保存",sakura::ui::ToastType::Warning);
        }
    }
    // 预览模式下只更新预览
    if (m_preview.IsActive())
    {
        m_preview.Update(dt);
        // 保持时间轴跟随预览时间位置
        m_timeline.CenterOnTime(m_preview.GetCurrentMs());
        return;
    }

    m_core.Update(dt);
    m_timeline.Update(dt);

    // 同步播放按钮标签
    if (m_btnPlay)
        m_btnPlay->SetText(m_core.IsPlaying() ? "⏸ 暂停" : "▶ 播放");

    // 同步撤销/重做按钮状态（每帧更新文本和颜色）
    UpdateUndoRedoButtons();
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void SceneEditor::OnRender(sakura::core::Renderer& renderer)
{
    sakura::ui::VisualStyle::DrawSceneBackground(renderer);

    // 1. 工具栏（预览中也显示，但用红色边框区分状态）
    RenderToolbar(renderer);

    // 2. 键盘时间轴（EditorTimeline）
    m_timeline.Render(renderer);

    // 3. 右侧区域：预览模式时渲染预览，否则渲染鼠标编辑区
    if (m_preview.IsActive())
        m_preview.Render(renderer);
    else
        m_mouseArea.Render(renderer);

    // 4. 音符属性与谱面信息
    RenderPropertyPanel(renderer);

    // 5. 底部全曲缩略轴
    RenderOverviewAxis(renderer);

    if (m_propertyEditing) RenderProperties(renderer);
    if (m_exitDialog)
    {
        const auto& palette = sakura::core::Theme::GetInstance().Colors();
        sakura::ui::VisualStyle::DrawScrim(renderer, 0.66f);
        sakura::ui::VisualStyle::DrawPanel(renderer, {0.24f, 0.34f, 0.52f, 0.34f}, true, true);
        renderer.DrawText(m_fontUI, "保存谱面的修改？", 0.5f, 0.382f, 0.036f, palette.text, sakura::core::TextAlign::Center);
        sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.31f, 0.69f, 0.440f, 0.72f);
        renderer.DrawText(m_fontSmall, "保存后可在选歌曲库中立即演奏这张谱面。", 0.5f, 0.472f, 0.021f, palette.textDim, sakura::core::TextAlign::Center);
        for (auto& button : m_exitButtons) button->Render(renderer);
    }
}

// ── RenderToolbar ─────────────────────────────────────────────────────────────
void SceneEditor::RenderToolbar(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    // 琉璃顶栏 + 鎏金底边细线
    renderer.DrawRoundedGradientRect({ 0.0f, 0.0f, 1.0f, 0.06f }, 0.0f,
        { 22, 26, 48, 242 }, { 26, 22, 48, 242 },
        { 14, 16, 30, 248 }, { 14, 16, 30, 248 });
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.0f, 1.0f, 0.059f, 0.85f);

    // 工具按钮（激活态叠加底部流光指示条）
    const int activeTool = static_cast<int>(m_core.GetNoteTool());
    for (int i = 0; i < TOOL_COUNT; ++i)
    {
        if (!m_toolBtns[i]) continue;
        m_toolBtns[i]->Render(renderer);
        if (i == activeTool)
        {
            const auto tb = m_toolBtns[i]->GetBounds();
            renderer.DrawRoundedGradientRect(
                { tb.x + 0.010f, tb.y + tb.height - 0.004f, tb.width - 0.020f, 0.003f }, 0.0015f,
                palette.accent, palette.primary, palette.accent, palette.primary);
        }
    }

    if (m_btnPlay) m_btnPlay->Render(renderer);
    if (m_btnUndo) m_btnUndo->Render(renderer);
    if (m_btnRedo) m_btnRedo->Render(renderer);

    // BeatSnap 胶囊徽章与曲目标题
    if (m_fontUI != sakura::core::INVALID_HANDLE)
    {
        const std::string snapStr = "拍分 1/" + std::to_string(m_core.GetBeatSnap());
        sakura::ui::VisualStyle::DrawBadgePill(renderer,
            { 0.484f, 0.009f, 0.052f, 0.040f },
            snapStr, m_fontUI, 0.0145f,
            { 28, 32, 56, 215 }, palette.gildedHairline, palette.accent);

        std::string titleStr = m_core.GetChartInfo().title;
        if (m_core.IsDirty()) titleStr += " ●";
        sakura::ui::VisualStyle::DrawTextFit(renderer, m_fontUI, titleStr,
            0.312f, 0.018f, 0.0185f, 0.082f,
            m_core.IsDirty() ? palette.accent : palette.text);
    }

    if (m_btnSnapDec) m_btnSnapDec->Render(renderer);
    if (m_btnSnapInc) m_btnSnapInc->Render(renderer);
    if (m_btnSave)    m_btnSave->Render(renderer);
    if (m_btnBack)    m_btnBack->Render(renderer);
}

// ── RenderMouseArea ───────────────────────────────────────────────────────────

void SceneEditor::RenderMouseArea(sakura::core::Renderer& renderer)
{
    // 鼠标编辑区域：(0.42, 0.06, 0.33, 0.60)
    sakura::ui::VisualStyle::DrawPanel(renderer, { 0.42f, 0.06f, 0.33f, 0.60f });
    renderer.DrawLine(0.42f, 0.06f, 0.75f, 0.06f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.42f, 0.06f, 0.42f, 0.66f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.42f, 0.66f, 0.75f, 0.66f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);
    renderer.DrawLine(0.75f, 0.06f, 0.75f, 0.66f,
        sakura::core::Color{ 60, 50, 100, 120 }, 0.001f);

    if (m_fontUI != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_fontUI, "鼠标编辑区",
            0.585f, 0.36f, 0.025f,
            sakura::core::Color{ 100, 90, 130, 150 },
            sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall, "(Step 2.7 完善)",
            0.585f, 0.40f, 0.018f,
            sakura::core::Color{ 80, 70, 110, 120 },
            sakura::core::TextAlign::Center);
    }
}

// ── RenderPropertyPanel ───────────────────────────────────────────────────────
void SceneEditor::RenderPropertyPanel(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawGlassCard(renderer, { 0.42f, 0.68f, 0.33f, 0.312f },
        { 18, 22, 40, 218 }, { 12, 15, 28, 238 },
        palette.gildedHairline, palette.surfaceBorder, 0.010f, 0.36f);

    if (m_fontSmall == sakura::core::INVALID_HANDLE) return;

    constexpr float px = 0.585f;

    renderer.DrawText(m_fontSmall, "检视器 · INSPECTOR",
        0.536f, 0.684f, 0.0145f,
        palette.accent,
        sakura::core::TextAlign::Center);

    if (m_btnDiffPrev) m_btnDiffPrev->Render(renderer);
    if (m_btnDiffNext) m_btnDiffNext->Render(renderer);
    if (m_btnDiffAdd)  m_btnDiffAdd->Render(renderer);

    {
        std::string diffName = "—";
        const auto& diffs = m_core.GetChartInfo().difficulties;
        if (m_currentDiffIndex >= 0 &&
            m_currentDiffIndex < static_cast<int>(diffs.size()))
        {
            diffName = diffs[m_currentDiffIndex].name;
        }
        sakura::ui::VisualStyle::DrawBadgePill(renderer,
            { 0.478f, 0.700f, 0.114f, 0.028f },
            diffName, m_fontSmall, 0.015f,
            { 28, 32, 56, 205 }, palette.primary, palette.text);
    }
    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.435f, 0.735f, 0.734f, 0.65f);

    const int selIdx    = m_core.GetSelectedKbNote();
    const int selMouse  = m_core.GetSelectedMouseNote();
    if (selIdx >= 0 && selIdx < static_cast<int>(m_core.GetChartData().keyboardNotes.size()))
    {
        const auto& n = m_core.GetChartData().keyboardNotes[selIdx];
        renderer.DrawText(m_fontSmall,
            "已选键盘音符 #" + std::to_string(selIdx),
            px, 0.742f, 0.0175f, palette.accent, sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            "时间: " + std::to_string(n.time) + " ms   ·   轨道: " + std::to_string(n.lane + 1),
            px, 0.768f, 0.017f, palette.text, sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            "持续时长: " + std::to_string(n.duration) + " ms",
            px, 0.794f, 0.017f, palette.textDim, sakura::core::TextAlign::Center);
    }
    else if (selMouse >= 0 && selMouse < static_cast<int>(m_core.GetChartData().mouseNotes.size()))
    {
        const auto& mn = m_core.GetChartData().mouseNotes[selMouse];
        renderer.DrawText(m_fontSmall,
            "已选鼠标音符 #" + std::to_string(selMouse),
            px, 0.742f, 0.0175f, palette.accent, sakura::core::TextAlign::Center);
        std::ostringstream posSS;
        posSS << "时间: " << mn.time << " ms   ·   坐标 ("
              << std::fixed << std::setprecision(2) << mn.x << ", " << mn.y << ")";
        renderer.DrawText(m_fontSmall, posSS.str(),
            px, 0.768f, 0.017f, palette.text, sakura::core::TextAlign::Center);
        renderer.DrawText(m_fontSmall,
            mn.type == sakura::game::NoteType::Slider
                ? ("Slider 时长: " + std::to_string(mn.sliderDuration) + " ms")
                : "类型: Hit Circle",
            px, 0.794f, 0.017f, palette.textDim, sakura::core::TextAlign::Center);
    }
    else
    {
        const auto& info = m_core.GetChartInfo();
        sakura::ui::VisualStyle::DrawTextFit(renderer, m_fontSmall,
            "曲名: " + info.title, 0.440f, 0.742f, 0.017f, 0.290f, palette.text);

        const int kbCount    = static_cast<int>(m_core.GetChartData().keyboardNotes.size());
        const int mouseCount = static_cast<int>(m_core.GetChartData().mouseNotes.size());
        renderer.DrawText(m_fontSmall,
            "BPM " + std::to_string(static_cast<int>(info.bpm))
            + "   ·   KB " + std::to_string(kbCount)
            + " / Mouse " + std::to_string(mouseCount),
            px, 0.768f, 0.017f, palette.textDim, sakura::core::TextAlign::Center);

        const int curMs = m_core.GetCurrentTimeMs();
        const float bpm = m_core.GetBpmAt(curMs);
        std::ostringstream timeSS;
        const float sec = static_cast<float>(curMs) / 1000.0f;
        timeSS << (curMs < 0 ? "-" : "")
               << static_cast<int>(std::abs(sec)) << "."
               << std::setw(3) << std::setfill('0') << (std::abs(curMs) % 1000)
               << "s   ·   当前 BPM " << static_cast<int>(bpm);
        renderer.DrawText(m_fontSmall, timeSS.str(),
            px, 0.795f, 0.017f, palette.accent, sakura::core::TextAlign::Center);
    }
    if (m_btnProperties) m_btnProperties->Render(renderer);

    sakura::ui::VisualStyle::DrawGildedDivider(renderer, 0.435f, 0.735f, 0.870f, 0.45f);
    renderer.DrawText(m_fontSmall, "1-4 工具  ·  Space 播放  ·  Del 删除",
        px, 0.878f, 0.015f, palette.textMuted, sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall, "Ctrl+Z/Y 撤销重做  ·  Ctrl+S 保存  ·  Ctrl+M 镜像",
        px, 0.901f, 0.015f, palette.textMuted, sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall, "F5/F6 完整试玩  ·  F8 内嵌预览  ·  ESC 退出",
        px, 0.924f, 0.015f, palette.textMuted, sakura::core::TextAlign::Center);
    renderer.DrawText(m_fontSmall, "点击左侧时间尺或右侧全曲轴可快速跳转",
        px, 0.947f, 0.015f, palette.textMuted, sakura::core::TextAlign::Center);
}

// ── HandleOverviewAxisEvent ─────────────────────────────────────────────────

bool SceneEditor::HandleOverviewAxisEvent(const SDL_Event& event)
{
    auto updateFromPointer = [this](float nx, float ny)
    {
        if (nx < OVERVIEW_X || nx > OVERVIEW_X + OVERVIEW_W
         || ny < OVERVIEW_Y || ny > OVERVIEW_Y + OVERVIEW_H)
        {
            return false;
        }

        int totalMs = std::max(m_core.GetTotalDurationMs(), 1000);
        float progress = (OVERVIEW_Y + OVERVIEW_H - ny) / OVERVIEW_H;
        progress = std::clamp(progress, 0.0f, 1.0f);

        int targetMs = static_cast<int>(std::round(progress * totalMs));
        m_core.SetCurrentTimeMs(targetMs);
        m_timeline.CenterOnTime(targetMs);
        return true;
    };

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
     && event.button.button == SDL_BUTTON_LEFT)
    {
        SDL_Window* win = SDL_GetWindowFromID(event.button.windowID);
        if (!win) return false;

        int ww = 0;
        int wh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return false;

        float nx = event.button.x / static_cast<float>(ww);
        float ny = event.button.y / static_cast<float>(wh);
        return updateFromPointer(nx, ny);
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION
     && (event.motion.state & SDL_BUTTON_LMASK) != 0)
    {
        SDL_Window* win = SDL_GetWindowFromID(event.motion.windowID);
        if (!win) return false;

        int ww = 0;
        int wh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return false;

        float nx = event.motion.x / static_cast<float>(ww);
        float ny = event.motion.y / static_cast<float>(wh);
        return updateFromPointer(nx, ny);
    }

    return false;
}

// ── RenderOverviewAxis ────────────────────────────────────────────────────────
void SceneEditor::RenderOverviewAxis(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawGlassCard(renderer,
        { OVERVIEW_X, OVERVIEW_Y, OVERVIEW_W, OVERVIEW_H - 0.008f },
        { 14, 18, 34, 222 }, { 9, 12, 24, 240 },
        palette.gildedHairline, palette.surfaceBorder, 0.010f, 0.36f);

    const float kbRegionW = OVERVIEW_W * 0.62f;
    const float msRegionX = OVERVIEW_X + kbRegionW;
    const float msRegionW = OVERVIEW_W - kbRegionW;
    renderer.DrawLine(msRegionX, OVERVIEW_Y + 0.042f, msRegionX, OVERVIEW_Y + OVERVIEW_H - 0.014f,
        { 150, 145, 195, 36 }, 0.001f);

    int totalMs = m_core.GetTotalDurationMs();
    if (totalMs <= 0) totalMs = 10000;

    // 4K 键盘音符缩略点（左侧 62% 宽度）
    const auto& notes = m_core.GetChartData().keyboardNotes;
    for (const auto& n : notes)
    {
        const float progress = static_cast<float>(n.time) / static_cast<float>(totalMs);
        const float noteY    = OVERVIEW_Y + OVERVIEW_H - progress * OVERVIEW_H;
        if (noteY < OVERVIEW_Y + 0.006f || noteY > OVERVIEW_Y + OVERVIEW_H - 0.010f) continue;

        const float noteX = OVERVIEW_X + 0.006f + (static_cast<float>(n.lane) + 0.5f) / 4.0f * (kbRegionW - 0.012f);
        const sakura::core::Color tickCol = (n.type == sakura::game::NoteType::Hold)
            ? palette.holdColor
            : palette.laneColors[std::clamp(n.lane, 0, 3)];
        renderer.DrawFilledRect(
            { noteX - 0.0045f, noteY - 0.0016f, 0.009f, 0.0032f },
            { tickCol.r, tickCol.g, tickCol.b, 205 });
    }

    // 鼠标音符缩略点（右侧 38% 宽度）
    const auto& mouseNotes = m_core.GetChartData().mouseNotes;
    for (const auto& mn : mouseNotes)
    {
        const float progress = static_cast<float>(mn.time) / static_cast<float>(totalMs);
        const float noteY    = OVERVIEW_Y + OVERVIEW_H - progress * OVERVIEW_H;
        if (noteY < OVERVIEW_Y + 0.006f || noteY > OVERVIEW_Y + OVERVIEW_H - 0.010f) continue;

        const float noteX = msRegionX + 0.008f + std::clamp(mn.x, 0.0f, 1.0f) * (msRegionW - 0.016f);
        const sakura::core::Color dotCol = (mn.type == sakura::game::NoteType::Slider)
            ? palette.accent
            : palette.circleColor;
        renderer.DrawCircleFilled(noteX, noteY, 0.0026f, { dotCol.r, dotCol.g, dotCol.b, 210 }, 12);
    }

    // 琉璃视窗透镜框
    if (totalMs > 0)
    {
        const float scrollProgress = static_cast<float>(m_timeline.GetScrollTimeMs())
                                   / static_cast<float>(totalMs);
        const float viewProgress   = static_cast<float>(
            m_timeline.GetScrollTimeMs() + 4000) / static_cast<float>(totalMs);

        const float viewTop    = OVERVIEW_Y + OVERVIEW_H - std::clamp(viewProgress,   0.0f, 1.0f) * OVERVIEW_H;
        const float viewBottom = OVERVIEW_Y + OVERVIEW_H - std::clamp(scrollProgress, 0.0f, 1.0f) * OVERVIEW_H;
        const float viewHeight = std::max(0.012f, viewBottom - viewTop);
        const sakura::core::NormRect lensRect{ OVERVIEW_X + 0.004f, viewTop, OVERVIEW_W - 0.008f, viewHeight };
        renderer.DrawRoundedGradientRect(lensRect, 0.006f,
            { 255, 195, 222, 42 }, { 195, 225, 255, 36 },
            { 255, 195, 222, 26 }, { 195, 225, 255, 22 });
        renderer.DrawRoundedRectOutlineGradient(lensRect, 0.006f,
            palette.accent, palette.primary, 0.0014f);
    }

    // 播放头鎏金发光线
    const int curMs = m_core.GetCurrentTimeMs();
    if (curMs >= 0)
    {
        const float progress = std::clamp(static_cast<float>(curMs) / static_cast<float>(totalMs), 0.0f, 1.0f);
        const float headY = OVERVIEW_Y + OVERVIEW_H - progress * OVERVIEW_H;
        renderer.DrawLine(OVERVIEW_X + 0.003f, headY, OVERVIEW_X + OVERVIEW_W - 0.003f, headY,
            palette.accent, 0.0024f);
    }

    if (m_fontSmall != sakura::core::INVALID_HANDLE)
    {
        sakura::ui::VisualStyle::DrawBadgePill(renderer,
            { OVERVIEW_X + 0.035f, OVERVIEW_Y + 0.010f, OVERVIEW_W - 0.070f, 0.026f },
            "全曲缩略轴", m_fontSmall, 0.0135f,
            { 22, 26, 46, 200 }, palette.surfaceBorder, palette.accent);
    }
}

// ── OnEvent ───────────────────────────────────────────────────────────────────

void SceneEditor::OnEvent(const SDL_Event& event)
{
    if(m_propertyEditing){
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.scancode==SDL_SCANCODE_ESCAPE){CloseProperties();return;}
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.scancode==SDL_SCANCODE_TAB){
            size_t next=0;for(size_t i=0;i<m_propertyInputs.size();++i)if(m_propertyInputs[i]->IsFocused())next=(i+1)%m_propertyInputs.size();
            for(auto& i:m_propertyInputs)i->SetFocused(false);m_propertyInputs[next]->SetFocused(true);return;
        }
        for(auto& i:m_propertyInputs)i->HandleEvent(event);
        if(m_propertyEditing){m_propertyApply->HandleEvent(event);m_propertyCancel->HandleEvent(event);}return;
    }

    if(m_exitDialog){
        if(event.type==SDL_EVENT_KEY_DOWN&&event.key.scancode==SDL_SCANCODE_ESCAPE)m_exitDialog=false;
        else for(auto& button:m_exitButtons)if(button->HandleEvent(event))break;
        return;
    }
    // 追踪 Ctrl/Shift 键状态
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
        if (event.key.scancode == SDL_SCANCODE_LCTRL
         || event.key.scancode == SDL_SCANCODE_RCTRL)
        {
            m_ctrlHeld = pressed;
        }
        if (event.key.scancode == SDL_SCANCODE_LSHIFT
         || event.key.scancode == SDL_SCANCODE_RSHIFT)
        {
            m_shiftHeld = pressed;
        }
    }

    // ── 预览模式事件（优先处理） ─────────────────────────────────────────────
    if (m_preview.IsActive())
    {
        m_preview.HandleEvent(event);
        return;  // 预览中不处理编辑器事件
    }

    // ── 键盘快捷键 ───────────────────────────────────────────────────────────
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const SDL_Scancode sc = event.key.scancode;

        // F5 → 从当前时间开始试玩
        if (sc == SDL_SCANCODE_F5)
        {
            m_core.StopPlayback();  // 停止编辑器播放
            StartFullPreview(m_core.GetCurrentTimeMs());
            sakura::ui::ToastManager::Instance().Show(
                "▶ 开始试玩 (ESC 退出)", sakura::ui::ToastType::Info);
            return;
        }

        // F6 → 从选中音符 -2s 处开始试玩
        if (sc == SDL_SCANCODE_F6)
        {
            int startMs = m_core.GetCurrentTimeMs();
            int sel = m_core.GetSelectedKbNote();
            if (sel >= 0 && sel < static_cast<int>(
                    m_core.GetChartData().keyboardNotes.size()))
            {
                startMs = m_core.GetChartData().keyboardNotes[sel].time - 2000;
            }
            startMs = std::max(0, startMs);
            m_core.StopPlayback();
            StartFullPreview(startMs);
            sakura::ui::ToastManager::Instance().Show(
                "▶ 试玩 (从 -2s 处) (ESC 退出)", sakura::ui::ToastType::Info);
            return;
        }

        if(sc==SDL_SCANCODE_F8){m_core.StopPlayback();m_preview.Start(m_core.GetCurrentTimeMs());return;}

        // ESC → 返回主菜单
        if (sc == SDL_SCANCODE_ESCAPE)
        {
            RequestExit();
            return;
        }

        // 空格 → 播放/暂停
        if (sc == SDL_SCANCODE_SPACE)
        {
            TogglePlayback();
            return;
        }

        // Ctrl+S → 保存
        if (m_ctrlHeld && !m_shiftHeld && sc == SDL_SCANCODE_S)
        {
            DoSave();
            return;
        }

        // Ctrl+Shift+S → 另存为（保存到当前难度的副本）
        if (m_ctrlHeld && m_shiftHeld && sc == SDL_SCANCODE_S)
        {
            const auto& info    = m_core.GetChartInfo();
            std::string newFile = info.folderPath + "/backup_" + m_core.GetDiffFile();
            bool ok = m_core.SaveChartTo(newFile, false);
            if (ok)
                sakura::ui::ToastManager::Instance().Show(
                    "备份已保存到: backup_" + m_core.GetDiffFile(),
                    sakura::ui::ToastType::Success);
            else
                sakura::ui::ToastManager::Instance().Show(
                    "备份保存失败", sakura::ui::ToastType::Error);
            return;
        }

        // Ctrl+Z → 撤销
        if (m_ctrlHeld && sc == SDL_SCANCODE_Z)
        {
            m_core.Undo();
            UpdateUndoRedoButtons();
            return;
        }

        // Ctrl+Y → 重做
        if (m_ctrlHeld && sc == SDL_SCANCODE_Y)
        {
            m_core.Redo();
            UpdateUndoRedoButtons();
            return;
        }

        // Ctrl+A → 全选键盘音符（显示数量）
        if (m_ctrlHeld && sc == SDL_SCANCODE_A)
        {
            int cnt = static_cast<int>(m_core.GetChartData().keyboardNotes.size());
            sakura::ui::ToastManager::Instance().Show(
                "已选中 " + std::to_string(cnt) + " 个键盘音符",
                sakura::ui::ToastType::Info);
            return;
        }

        // Ctrl+M → 水平镜像所有键盘音符（道 0↔3，1↔2）
        if (m_ctrlHeld && sc == SDL_SCANCODE_M)
        {
            const auto& notes = m_core.GetChartData().keyboardNotes;
            if (!notes.empty())
            {
                auto batch = std::make_unique<sakura::editor::BatchCommand>("镜像音符");
                for (int i = 0; i < static_cast<int>(notes.size()); ++i)
                {
                    auto newNote = notes[i];
                    newNote.lane = 3 - newNote.lane;
                    batch->Add(std::make_unique<sakura::editor::ModifyNoteCommand>(
                        i, notes[i], newNote));
                }
                m_core.ExecuteCommand(std::move(batch));
                sakura::ui::ToastManager::Instance().Show(
                    "所有键盘音符已镜像", sakura::ui::ToastType::Info);
            }
            return;
        }

        // Enter → 完成当前进行中的 Slider
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
        {
            if (m_core.HasWipSlider())
            {
                m_core.FinalizeSlider();
                sakura::ui::ToastManager::Instance().Show(
                    "Slider 已完成", sakura::ui::ToastType::Success);
            }
            return;
        }

        // 1-4 → 切换音符工具（自动取消进行中的 Slider）
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_4)
        {
            if (m_core.HasWipSlider())
                m_core.CancelSlider();
            int toolIdx = sc - SDL_SCANCODE_1;
            m_core.SetNoteTool(
                static_cast<sakura::editor::NoteToolType>(toolIdx));
            UpdateToolButtons();
            return;
        }

        // Delete → 删除选中音符
        if (sc == SDL_SCANCODE_DELETE)
        {
            int sel = m_core.GetSelectedKbNote();
            if (sel >= 0)
            {
                m_core.DeleteKeyboardNote(sel);
                m_core.ClearSelection();
                sakura::ui::ToastManager::Instance().Show(
                    "已删除选中音符", sakura::ui::ToastType::Info);
                return;
            }
            int msel = m_core.GetSelectedMouseNote();
            if (msel >= 0)
            {
                m_core.DeleteMouseNote(msel);
                m_core.ClearSelection();
                sakura::ui::ToastManager::Instance().Show(
                    "已删除鼠标音符", sakura::ui::ToastType::Info);
            }
            return;
        }

        // Ctrl+滚轮由 timeline 处理，但 Ctrl 单独按下时不做其他事
    }

    if(m_btnProperties && m_btnProperties->HandleEvent(event))return;
    if (HandleOverviewAxisEvent(event))
        return;

    // ── 工具栏事件 ───────────────────────────────────────────────────────────
    for (auto& btn : m_toolBtns) if (btn) btn->HandleEvent(event);
    if (m_btnPlay)     m_btnPlay->HandleEvent(event);
    if (m_btnUndo)     m_btnUndo->HandleEvent(event);
    if (m_btnRedo)     m_btnRedo->HandleEvent(event);
    if (m_btnSave)     m_btnSave->HandleEvent(event);
    if (m_btnBack)     m_btnBack->HandleEvent(event);
    if (m_btnSnapDec)  m_btnSnapDec->HandleEvent(event);
    if (m_btnSnapInc)  m_btnSnapInc->HandleEvent(event);
    if (m_btnDiffPrev) m_btnDiffPrev->HandleEvent(event);
    if (m_btnDiffNext) m_btnDiffNext->HandleEvent(event);
    if (m_btnDiffAdd)  m_btnDiffAdd->HandleEvent(event);

    // ── 时间轴事件（可能消费事件） ───────────────────────────────────────────
    m_mouseArea.HandleEvent(event);
    m_timeline.HandleEvent(event);
}

} // namespace sakura::scene
