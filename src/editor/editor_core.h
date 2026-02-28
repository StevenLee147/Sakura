#pragma once

// editor_core.h — 谱面编辑器核心状态管理
// 持有谱面数据、播放状态、BeatSnap、音符 CRUD 操作

#include "game/chart.h"
#include "game/note.h"

#include <string>
#include <optional>

namespace sakura::editor
{

// ── 音符放置工具类型 ──────────────────────────────────────────────────────────
enum class NoteToolType
{
    Tap    = 0,   // 键盘端单击（快捷键 1）
    Hold   = 1,   // 键盘端长按（快捷键 2）
    Drag   = 2,   // 键盘端滑动（快捷键 3）
    Circle = 3,   // 鼠标端圆圈（快捷键 4）
    Slider = 4    // 鼠标端滑条（快捷键 5）
};

// ── EditorCore ────────────────────────────────────────────────────────────────
// 单一谱面的编辑状态，不涉及渲染。
// SceneEditor 持有一个 EditorCore 实例，EditorTimeline 持有引用。
class EditorCore
{
public:
    EditorCore()  = default;
    ~EditorCore() = default;

    EditorCore(const EditorCore&)            = delete;
    EditorCore& operator=(const EditorCore&) = delete;

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    // 创建一个空白谱面（仅包含一个 TimingPoint）
    void NewChart(const std::string& chartId = "new-chart",
                  const std::string& title   = "新谱面",
                  float  bpm       = 120.0f,
                  int    offsetMs  = 0,
                  const std::string& diffName  = "Normal",
                  const std::string& diffFile  = "normal.json",
                  const std::string& folderPath = "");

    // 从 JSON 文件载入已有谱面数据（info.json + 难度文件）
    // folderPath：谱面根目录；difficultyFile：难度文件名（如 "normal.json"）
    bool LoadChart(const std::string& folderPath,
                   const std::string& difficultyFile = "normal.json");

    // 保存到当前路径（原来的 diffFile）
    bool SaveChart();

    // 保存到指定路径（另存为）
    bool SaveChartTo(const std::string& fullPath);

    // ── 谱面数据访问 ──────────────────────────────────────────────────────────

    const sakura::game::ChartInfo& GetChartInfo() const { return m_chartInfo; }
          sakura::game::ChartInfo& GetChartInfo()       { return m_chartInfo; }
    const sakura::game::ChartData& GetChartData() const { return m_chartData; }
          sakura::game::ChartData& GetChartData()       { return m_chartData; }

    bool        IsDirty()       const { return m_dirty; }
    void        ClearDirty()          { m_dirty = false; }
    std::string GetFolderPath() const { return m_folderPath; }
    std::string GetDiffFile()   const { return m_diffFile; }

    // ── 时间/节拍工具 ─────────────────────────────────────────────────────────

    // 返回 timeMs 处的 BPM
    float GetBpmAt(int timeMs = 0) const;

    // 一拍的时长（毫秒）
    float GetBeatIntervalMs(int timeMs = 0) const;

    // 将 timeMs 量化到最近的 BeatSnap 网格点
    int QuantizeTime(int timeMs) const;

    // BeatSnap 表示每拍的细分数（1/n 拍，n=1,2,4,8,16）
    // snap=4 → 每拍 4 个放置点 (1/4 拍)
    void SetBeatSnap(int snap);
    int  GetBeatSnap() const { return m_beatSnap; }

    // ── 音符工具 ──────────────────────────────────────────────────────────────

    void         SetNoteTool(NoteToolType t) { m_noteTool = t; }
    NoteToolType GetNoteTool()         const { return m_noteTool; }

    // 放置一个键盘音符（timeMs 已量化）；若在相同位置(±1ms)的同 lane 有音符则忽略
    // 返回 true = 成功放置；false = 重复或越界
    bool PlaceKeyboardNote(int timeMs, int lane);

    // 删除指定索引的键盘音符；成功返回 true
    bool DeleteKeyboardNote(int index);

    // 查找最近的键盘音符：在 lane 上离 timeMs 最近的、偏差 ≤ toleranceMs 的音符索引
    // 未找到返回 -1
    int FindKeyboardNote(int timeMs, int lane, int toleranceMs = 80) const;

    // 选中/取消选中键盘音符
    void SelectKeyboardNote(int index) { m_selectedKbNote = index; }
    void ClearSelection()              { m_selectedKbNote = -1; }
    int  GetSelectedKbNote()    const  { return m_selectedKbNote; }

    // ── 播放控制 ──────────────────────────────────────────────────────────────

    void SetCurrentTimeMs(int ms);
    int  GetCurrentTimeMs()   const { return m_currentTimeMs; }
    int  GetTotalDurationMs() const;   // 最后一个音符后 2000ms

    bool IsPlaying() const { return m_playing; }

    // Update 在编辑器每帧调用：若正在播放，则推进 m_currentTimeMs
    void Update(float dt);

    // 切换播放/暂停
    void TogglePlayback();
    void StopPlayback();

private:
    sakura::game::ChartInfo m_chartInfo;
    sakura::game::ChartData m_chartData;

    std::string m_folderPath;
    std::string m_diffFile = "normal.json";
    bool        m_dirty    = false;

    int          m_beatSnap       = 4;
    NoteToolType m_noteTool       = NoteToolType::Tap;
    int          m_selectedKbNote = -1;

    int  m_currentTimeMs = 0;
    bool m_playing       = false;

    // NoteType 枚举 → 序列化字符串
    static const char* NoteTypeToStr(sakura::game::NoteType t);
};

} // namespace sakura::editor
