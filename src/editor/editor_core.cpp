// editor_core.cpp — 谱面编辑器核心状态实现

#include "editor_core.h"
#include "game/chart_loader.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs   = std::filesystem;
using     json = nlohmann::json;

namespace sakura::editor
{

// ═════════════════════════════════════════════════════════════════════════════
// 辅助
// ═════════════════════════════════════════════════════════════════════════════

const char* EditorCore::NoteTypeToStr(sakura::game::NoteType t)
{
    switch (t)
    {
        case sakura::game::NoteType::Tap:    return "tap";
        case sakura::game::NoteType::Hold:   return "hold";
        case sakura::game::NoteType::Drag:   return "drag";
        case sakura::game::NoteType::Circle: return "circle";
        case sakura::game::NoteType::Slider: return "slider";
        default:                             return "tap";
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// NewChart
// ═════════════════════════════════════════════════════════════════════════════

void EditorCore::NewChart(const std::string& chartId,
                          const std::string& title,
                          float  bpm,
                          int    offsetMs,
                          const std::string& diffName,
                          const std::string& diffFile,
                          const std::string& folderPath)
{
    m_chartInfo            = sakura::game::ChartInfo{};
    m_chartInfo.version    = 2;
    m_chartInfo.id         = chartId;
    m_chartInfo.title      = title;
    m_chartInfo.bpm        = bpm;
    m_chartInfo.offset     = offsetMs;
    m_chartInfo.folderPath = folderPath;

    sakura::game::DifficultyInfo diff;
    diff.name      = diffName;
    diff.level     = 1.0f;
    diff.chartFile = diffFile;
    m_chartInfo.difficulties.push_back(diff);

    m_chartData           = sakura::game::ChartData{};
    m_chartData.version   = 2;

    sakura::game::TimingPoint tp;
    tp.time               = 0;
    tp.bpm                = bpm;
    tp.timeSigNumerator   = 4;
    tp.timeSigDenominator = 4;
    m_chartData.timingPoints.push_back(tp);

    m_folderPath      = folderPath;
    m_diffFile        = diffFile;
    m_dirty           = false;
    m_selectedKbNote  = -1;
    m_currentTimeMs   = 0;
    m_playing         = false;

    LOG_INFO("[EditorCore] 新建谱面: id={} bpm={}", chartId, bpm);
}

// ═════════════════════════════════════════════════════════════════════════════
// LoadChart
// ═════════════════════════════════════════════════════════════════════════════

bool EditorCore::LoadChart(const std::string& folderPath,
                           const std::string& difficultyFile)
{
    std::string infoPath = folderPath + "/info.json";
    std::string dataPath = folderPath + "/" + difficultyFile;

    sakura::game::ChartLoader loader;
    auto infoOpt = loader.LoadChartInfo(infoPath);
    if (!infoOpt)
    {
        LOG_ERROR("[EditorCore] 无法加载 info.json: {}", infoPath);
        return false;
    }

    auto dataOpt = loader.LoadChartData(dataPath);
    if (!dataOpt)
    {
        LOG_ERROR("[EditorCore] 无法加载谱面数据: {}", dataPath);
        return false;
    }

    m_chartInfo           = std::move(*infoOpt);
    m_chartInfo.folderPath = folderPath;
    m_chartData           = std::move(*dataOpt);
    m_folderPath          = folderPath;
    m_diffFile            = difficultyFile;
    m_dirty               = false;
    m_selectedKbNote      = -1;
    m_selectedMouseNote   = -1;
    m_wipSliderActive     = false;
    m_wipSliderIndex      = -1;
    m_currentTimeMs       = 0;
    m_playing             = false;
    m_history.Clear();

    LOG_INFO("[EditorCore] 已加载谱面: {} / {} ({} 个键盘音符)",
             m_chartInfo.title, difficultyFile,
             static_cast<int>(m_chartData.keyboardNotes.size()));
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// SaveChart / SaveChartTo
// ═════════════════════════════════════════════════════════════════════════════

bool EditorCore::SaveChart()
{
    if (m_folderPath.empty() || m_diffFile.empty())
    {
        LOG_WARN("[EditorCore] SaveChart: 未设置路径");
        return false;
    }
    return SaveChartTo(m_folderPath + "/" + m_diffFile);
}

bool EditorCore::SaveChartTo(const std::string& fullPath)
{
    try
    {
        // 确保目录存在
        fs::path p(fullPath);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());

        json j;
        j["version"] = m_chartData.version;

        // timing_points
        json tpArr = json::array();
        for (const auto& tp : m_chartData.timingPoints)
        {
            json t;
            t["time"]           = tp.time;
            t["bpm"]            = tp.bpm;
            t["time_signature"] = { tp.timeSigNumerator, tp.timeSigDenominator };
            tpArr.push_back(std::move(t));
        }
        j["timing_points"] = std::move(tpArr);

        // sv_points
        json svArr = json::array();
        for (const auto& sv : m_chartData.svPoints)
        {
            json s;
            s["time"]   = sv.time;
            s["speed"]  = sv.speed;
            s["easing"] = sv.easing;
            svArr.push_back(std::move(s));
        }
        j["sv_points"] = std::move(svArr);

        // keyboard_notes（按时间升序）
        json kbArr = json::array();
        auto sortedKb = m_chartData.keyboardNotes;
        std::sort(sortedKb.begin(), sortedKb.end(),
                  [](const auto& a, const auto& b){ return a.time < b.time; });
        for (const auto& n : sortedKb)
        {
            json note;
            note["time"]         = n.time;
            note["lane"]         = n.lane;
            note["type"]         = NoteTypeToStr(n.type);
            note["duration"]     = n.duration;
            note["drag_to_lane"] = n.dragToLane;
            kbArr.push_back(std::move(note));
        }
        j["keyboard_notes"] = std::move(kbArr);

        // mouse_notes
        json msArr = json::array();
        auto sortedMs = m_chartData.mouseNotes;
        std::sort(sortedMs.begin(), sortedMs.end(),
                  [](const auto& a, const auto& b){ return a.time < b.time; });
        for (const auto& n : sortedMs)
        {
            json note;
            note["time"] = n.time;
            note["x"]    = n.x;
            note["y"]    = n.y;
            note["type"] = NoteTypeToStr(n.type);
            note["slider_duration"] = n.sliderDuration;
            if (!n.sliderPath.empty())
            {
                json pathArr = json::array();
                for (const auto& pt : n.sliderPath)
                    pathArr.push_back({ pt.first, pt.second });
                note["slider_path"] = std::move(pathArr);
            }
            msArr.push_back(std::move(note));
        }
        j["mouse_notes"] = std::move(msArr);

        std::ofstream ofs(fullPath);
        if (!ofs.is_open())
        {
            LOG_ERROR("[EditorCore] 无法写入文件: {}", fullPath);
            return false;
        }
        ofs << j.dump(4);
        ofs.close();

        // 同步更新 ChartInfo 的 note_count
        int kbSorted   = static_cast<int>(m_chartData.keyboardNotes.size());
        int mouseSorted = static_cast<int>(m_chartData.mouseNotes.size());
        for (auto& diff : m_chartInfo.difficulties)
        {
            if (diff.chartFile == m_diffFile)
            {
                diff.noteCount      = kbSorted;
                diff.mouseNoteCount = mouseSorted;
            }
        }

        m_dirty = false;
        LOG_INFO("[EditorCore] 已保存谱面到: {} (KB={}, Mouse={})",
                 fullPath, kbSorted, mouseSorted);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[EditorCore] 保存失败: {}", e.what());
        return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 时间 / 节拍工具
// ═════════════════════════════════════════════════════════════════════════════

float EditorCore::GetBpmAt(int timeMs) const
{
    float bpm = 120.0f;
    for (const auto& tp : m_chartData.timingPoints)
    {
        if (tp.time <= timeMs)
            bpm = tp.bpm;
        else
            break;
    }
    return bpm > 0.0f ? bpm : 120.0f;
}

float EditorCore::GetBeatIntervalMs(int timeMs) const
{
    float bpm = GetBpmAt(timeMs);
    return 60000.0f / bpm;  // ms per beat
}

int EditorCore::QuantizeTime(int timeMs) const
{
    // 找当前 timing section 的起始时间
    int   sectionStart = 0;
    float sectionBpm   = GetBpmAt(0);

    for (const auto& tp : m_chartData.timingPoints)
    {
        if (tp.time <= timeMs)
        {
            sectionStart = tp.time;
            sectionBpm   = tp.bpm;
        }
        else
            break;
    }

    float beatMs      = 60000.0f / sectionBpm;
    float subdivMs    = beatMs / static_cast<float>(m_beatSnap);
    float offsetFromSection = static_cast<float>(timeMs - sectionStart);
    float snapped       = std::round(offsetFromSection / subdivMs) * subdivMs;
    return sectionStart + static_cast<int>(snapped);
}

void EditorCore::SetBeatSnap(int snap)
{
    // 合法值：1, 2, 4, 8, 16
    if (snap < 1) snap = 1;
    if (snap > 16) snap = 16;
    // 找最近的有效值
    const int valid[] = { 1, 2, 4, 8, 16 };
    int best = 4;
    int bestDist = 999;
    for (int v : valid)
    {
        int dist = std::abs(v - snap);
        if (dist < bestDist) { bestDist = dist; best = v; }
    }
    m_beatSnap = best;
}

// ═════════════════════════════════════════════════════════════════════════════
// 音符 CRUD
// ═════════════════════════════════════════════════════════════════════════════

bool EditorCore::PlaceKeyboardNote(int timeMs, int lane, int durationMs)
{
    if (lane < 0 || lane > 3) return false;

    // 判断是否与现有音符重叠（同 lane、时间差 ≤ 1ms）
    if (FindKeyboardNote(timeMs, lane, 1) != -1)
        return false;

    sakura::game::KeyboardNote note;
    note.time = timeMs;
    note.lane = lane;

    // 根据当前工具设置类型
    switch (m_noteTool)
    {
        case NoteToolType::Tap:
            note.type = sakura::game::NoteType::Tap;
            break;
        case NoteToolType::Hold:
            note.type     = sakura::game::NoteType::Hold;
            // 使用传入的 duration，否则默认 1 拍
            note.duration = (durationMs > 0)
                ? durationMs
                : static_cast<int>(GetBeatIntervalMs(timeMs));
            break;
        case NoteToolType::Drag:
            note.type = sakura::game::NoteType::Drag;
            // dragToLane 默认同轨，用户可通过属性面板修改
            note.dragToLane = lane;
            break;
        default:
            note.type = sakura::game::NoteType::Tap;
    }

    // 通过命令系统执行（支持撤销）
    m_history.Execute(std::make_unique<PlaceNoteCommand>(note), *this);
    m_dirty = true;

    LOG_DEBUG("[EditorCore] 放置音符: time={} lane={}", timeMs, lane);
    return true;
}

bool EditorCore::DeleteKeyboardNote(int index)
{
    if (index < 0 || index >= static_cast<int>(m_chartData.keyboardNotes.size()))
        return false;

    // 保存将被删除的音符数据，通过命令系统执行（支持撤销）
    const auto savedNote = m_chartData.keyboardNotes[index];
    m_history.Execute(std::make_unique<DeleteNoteCommand>(index, savedNote), *this);
    m_dirty = true;

    if (m_selectedKbNote == index)
        m_selectedKbNote = -1;
    else if (m_selectedKbNote > index)
        --m_selectedKbNote;

    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Raw 音符操作（供 EditorCommand 子类调用）
// ═════════════════════════════════════════════════════════════════════════════

int EditorCore::RawAddNote(const sakura::game::KeyboardNote& note)
{
    auto& notes = m_chartData.keyboardNotes;
    // 按时间（次按 lane）升序插入，保持有序
    auto it = std::lower_bound(notes.begin(), notes.end(), note,
        [](const sakura::game::KeyboardNote& a, const sakura::game::KeyboardNote& b) {
            return a.time < b.time || (a.time == b.time && a.lane < b.lane);
        });
    notes.insert(it, note);
    return static_cast<int>(std::distance(notes.begin(), it));
}

void EditorCore::RawInsertNoteAt(int index, const sakura::game::KeyboardNote& note)
{
    auto& notes = m_chartData.keyboardNotes;
    if (index < 0) index = 0;
    if (index > static_cast<int>(notes.size()))
        index = static_cast<int>(notes.size());
    notes.insert(notes.begin() + index, note);
}

void EditorCore::RawRemoveNote(int index)
{
    auto& notes = m_chartData.keyboardNotes;
    if (index < 0 || index >= static_cast<int>(notes.size())) return;
    notes.erase(notes.begin() + index);
}

void EditorCore::RawModifyNote(int index, const sakura::game::KeyboardNote& note)
{
    auto& notes = m_chartData.keyboardNotes;
    if (index < 0 || index >= static_cast<int>(notes.size())) return;
    notes[index] = note;
}

int EditorCore::FindKeyboardNote(int timeMs, int lane, int toleranceMs) const
{
    int bestIdx  = -1;
    int bestDist = toleranceMs + 1;

    for (int i = 0; i < static_cast<int>(m_chartData.keyboardNotes.size()); ++i)
    {
        const auto& n = m_chartData.keyboardNotes[i];
        if (n.lane != lane) continue;
        int dist = std::abs(n.time - timeMs);
        if (dist <= toleranceMs && dist < bestDist)
        {
            bestDist = dist;
            bestIdx  = i;
        }
    }
    return bestIdx;
}

// ═════════════════════════════════════════════════════════════════════════════
// 播放控制
// ═════════════════════════════════════════════════════════════════════════════

int EditorCore::GetTotalDurationMs() const
{
    int maxTime = 0;
    for (const auto& n : m_chartData.keyboardNotes)
        maxTime = std::max(maxTime, n.time + n.duration);
    for (const auto& n : m_chartData.mouseNotes)
        maxTime = std::max(maxTime, n.time + n.sliderDuration);
    return maxTime + 2000;
}

void EditorCore::SetCurrentTimeMs(int ms)
{
    m_currentTimeMs = std::max(0, ms);
}

void EditorCore::TogglePlayback()
{
    if (m_playing)
    {
        m_playing = false;
        sakura::audio::AudioManager::GetInstance().PauseMusic();
        LOG_DEBUG("[EditorCore] 暂停播放 @ {}ms", m_currentTimeMs);
    }
    else
    {
        m_playing = true;
        // 尝试跳转并播放
        sakura::audio::AudioManager::GetInstance().SetMusicPosition(
            static_cast<double>(m_currentTimeMs) / 1000.0);
        sakura::audio::AudioManager::GetInstance().ResumeMusic();
        LOG_DEBUG("[EditorCore] 开始播放 @ {}ms", m_currentTimeMs);
    }
}

void EditorCore::StopPlayback()
{
    if (m_playing)
    {
        m_playing = false;
        sakura::audio::AudioManager::GetInstance().StopMusic();
    }
}

void EditorCore::Update(float dt)
{
    if (!m_playing) return;

    // 从音频管理器同步当前播放时间
    double posSec = sakura::audio::AudioManager::GetInstance().GetMusicPosition();
    if (posSec >= 0.0)
    {
        m_currentTimeMs = static_cast<int>(posSec * 1000.0)
                        + m_chartInfo.offset;
    }
    else
    {
        // 音频管理器不可用，手动推进
        m_currentTimeMs += static_cast<int>(dt * 1000.0f);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 撤销/重做
// ═════════════════════════════════════════════════════════════════════════════

void EditorCore::Undo()
{
    if (!m_history.CanUndo()) return;
    std::string desc = m_history.GetUndoDescription();
    m_history.Undo(*this);
    m_dirty = true;
    m_selectedKbNote = -1;
    LOG_DEBUG("[EditorCore] Undo: {}", desc);
}

void EditorCore::Redo()
{
    if (!m_history.CanRedo()) return;
    std::string desc = m_history.GetRedoDescription();
    m_history.Redo(*this);
    m_dirty = true;
    m_selectedKbNote = -1;
    LOG_DEBUG("[EditorCore] Redo: {}", desc);
}

void EditorCore::ExecuteCommand(std::unique_ptr<EditorCommand> cmd)
{
    if (!cmd) return;
    m_history.Execute(std::move(cmd), *this);
    m_dirty = true;
    LOG_DEBUG("[EditorCore] ExecuteCommand");
}

// ═════════════════════════════════════════════════════════════════════════════
// 鼠标音符 CRUD
// ═════════════════════════════════════════════════════════════════════════════

bool EditorCore::PlaceMouseNote(int timeMs, float nx, float ny, sakura::game::NoteType type)
{
    sakura::game::MouseNote note;
    note.time = timeMs;
    note.x    = std::clamp(nx, 0.0f, 1.0f);
    note.y    = std::clamp(ny, 0.0f, 1.0f);
    note.type = type;

    m_history.Execute(std::make_unique<PlaceMouseNoteCommand>(note), *this);
    m_dirty = true;

    LOG_DEBUG("[EditorCore] 放置鼠标音符: time={} ({:.2f},{:.2f})", timeMs, nx, ny);
    return true;
}

bool EditorCore::DeleteMouseNote(int index)
{
    if (index < 0 || index >= static_cast<int>(m_chartData.mouseNotes.size()))
        return false;

    const auto savedNote = m_chartData.mouseNotes[index];
    m_history.Execute(std::make_unique<DeleteMouseNoteCommand>(index, savedNote), *this);
    m_dirty = true;

    if (m_selectedMouseNote == index)
        m_selectedMouseNote = -1;
    else if (m_selectedMouseNote > index)
        --m_selectedMouseNote;

    return true;
}

int EditorCore::FindMouseNote(int timeMs, float nx, float ny, int toleranceMs, float toleranceXY) const
{
    const auto& notes = m_chartData.mouseNotes;
    int bestIdx  = -1;
    int bestDist = toleranceMs + 1;

    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const auto& n = notes[i];
        int timeDist = std::abs(n.time - timeMs);
        if (timeDist > toleranceMs) continue;

        float dx = n.x - nx;
        float dy = n.y - ny;
        float xyDist = std::sqrt(dx * dx + dy * dy);
        if (xyDist > toleranceXY) continue;

        if (timeDist < bestDist)
        {
            bestDist = timeDist;
            bestIdx  = i;
        }
    }
    return bestIdx;
}

bool EditorCore::StartSlider(int timeMs, float nx, float ny)
{
    if (m_wipSliderActive) return false;  // 已有进行中的 Slider

    m_wipSlider              = sakura::game::MouseNote{};
    m_wipSlider.time         = timeMs;
    m_wipSlider.x            = std::clamp(nx, 0.0f, 1.0f);
    m_wipSlider.y            = std::clamp(ny, 0.0f, 1.0f);
    m_wipSlider.type         = sakura::game::NoteType::Slider;
    m_wipSlider.sliderPath.push_back({ nx, ny });  // 第一个路径点

    m_wipSliderActive = true;
    m_wipSliderIndex  = -1;

    LOG_DEBUG("[EditorCore] 开始构建 Slider @ time={}", timeMs);
    return true;
}

void EditorCore::AddSliderPoint(float nx, float ny)
{
    if (!m_wipSliderActive) return;
    m_wipSlider.sliderPath.push_back({ std::clamp(nx, 0.0f, 1.0f),
                                        std::clamp(ny, 0.0f, 1.0f) });
    if (m_wipSlider.sliderPath.size() >= 2)
    {
        // 计算路径总长来估算 sliderDuration
        m_wipSlider.sliderDuration = static_cast<int>(m_wipSlider.sliderPath.size()) * 200;
    }
    LOG_DEBUG("[EditorCore] 添加 Slider 路径点: ({:.2f},{:.2f})", nx, ny);
}

void EditorCore::FinalizeSlider()
{
    if (!m_wipSliderActive) return;
    if (m_wipSlider.sliderPath.size() >= 2)
    {
        m_history.Execute(std::make_unique<PlaceMouseNoteCommand>(m_wipSlider), *this);
        m_dirty = true;
        LOG_DEBUG("[EditorCore] Slider 完成: {} 个路径点", static_cast<int>(m_wipSlider.sliderPath.size()));
    }
    m_wipSliderActive = false;
    m_wipSliderIndex  = -1;
    m_wipSlider       = {};
}

void EditorCore::CancelSlider()
{
    m_wipSliderActive = false;
    m_wipSliderIndex  = -1;
    m_wipSlider       = {};
    LOG_DEBUG("[EditorCore] 放弃 Slider 构建");
}

const sakura::game::MouseNote* EditorCore::GetWipSlider() const
{
    return m_wipSliderActive ? &m_wipSlider : nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// Raw 鼠标音符操作
// ═════════════════════════════════════════════════════════════════════════════

int EditorCore::RawAddMouseNote(const sakura::game::MouseNote& note)
{
    auto& notes = m_chartData.mouseNotes;
    auto it = std::lower_bound(notes.begin(), notes.end(), note,
        [](const sakura::game::MouseNote& a, const sakura::game::MouseNote& b) {
            return a.time < b.time;
        });
    notes.insert(it, note);
    return static_cast<int>(std::distance(notes.begin(), it));
}

void EditorCore::RawInsertMouseNoteAt(int index, const sakura::game::MouseNote& note)
{
    auto& notes = m_chartData.mouseNotes;
    if (index < 0) index = 0;
    if (index > static_cast<int>(notes.size())) index = static_cast<int>(notes.size());
    notes.insert(notes.begin() + index, note);
}

void EditorCore::RawRemoveMouseNote(int index)
{
    auto& notes = m_chartData.mouseNotes;
    if (index < 0 || index >= static_cast<int>(notes.size())) return;
    notes.erase(notes.begin() + index);
}

void EditorCore::RawModifyMouseNote(int index, const sakura::game::MouseNote& note)
{
    auto& notes = m_chartData.mouseNotes;
    if (index < 0 || index >= static_cast<int>(notes.size())) return;
    notes[index] = note;
}
} // namespace sakura::editor