#pragma once

// chart.h — 谱面相关数据结构（元信息 + 谱面数据）

#include "note.h"
#include <string>
#include <vector>

namespace sakura::game
{

// ── 时间点 ─────────────────────────────────────────────────────────────────────

// TimingPoint — BPM/拍号变化点
struct TimingPoint
{
    int   time              = 0;    // 时间（毫秒）
    float bpm               = 120.0f;
    int   timeSigNumerator  = 4;    // 拍号分子（如 4/4 中的 4）
    int   timeSigDenominator = 4;   // 拍号分母（如 4/4 中的 4）
};

// SVPoint — 滚动速度变化点（Scroll Velocity）
struct SVPoint
{
    int         time   = 0;        // 时间（毫秒）
    float       speed  = 1.0f;     // 速度倍率（相对于 NoteSpeed 配置）
    std::string easing = "linear"; // 缓动方式（linear/ease_in/ease_out 等）
};

// ── 难度信息 ──────────────────────────────────────────────────────────────────

struct DifficultyInfo
{
    std::string name           = "Normal"; // 难度名称
    float       level          = 5.0f;     // 难度等级（1.0~15.0，支持小数）
    std::string chartFile;                 // 谱面数据文件名（相对于谱面目录）
    int         noteCount      = 0;        // 键盘音符总数
    int         holdCount      = 0;        // Hold 音符总数
    int         mouseNoteCount = 0;        // 鼠标音符总数
};

// ── 谱面元信息 ────────────────────────────────────────────────────────────────

struct ChartInfo
{
    int         version          = 2;
    std::string id;                        // 唯一标识符（与文件夹名一致）
    std::string title;                     // 曲目名称
    std::string artist;                    // 曲师/歌手
    std::string charter;                   // 谱面作者
    std::string source;                    // 来源信息（可选）
    std::vector<std::string> tags;         // 标签列表

    std::string musicFile;                 // 音乐文件名
    std::string coverFile;                 // 封面图片文件名
    std::string backgroundFile;            // 背景图片文件名
    int         previewTime      = 0;      // 预览起始时间（毫秒）

    float       bpm              = 120.0f; // 基础 BPM
    int         offset           = 0;      // 音频偏移（毫秒）

    std::string folderPath;                // 谱面文件夹完整路径（运行时填充）
    std::vector<DifficultyInfo> difficulties;
};

// ── 谱面完整数据 ──────────────────────────────────────────────────────────────

struct ChartData
{
    int         version        = 2;
    std::vector<TimingPoint>  timingPoints;   // 时间变化点列表（按 time 升序）
    std::vector<SVPoint>      svPoints;       // SV 变化点列表（按 time 升序）
    std::vector<KeyboardNote> keyboardNotes;  // 键盘端音符（按 time 升序）
    std::vector<MouseNote>    mouseNotes;     // 鼠标端音符（按 time 升序）
};

// ── 游戏结果 ──────────────────────────────────────────────────────────────────

// GameResult — 一局游戏的完整结果，用于传递给结算界面和数据库
struct GameResult
{
    std::string chartId;            // 谱面 ID
    std::string chartTitle;         // 曲目名称（显示用）
    std::string difficulty;         // 难度名称
    float       difficultyLevel = 0.0f;

    int     score       = 0;        // 最终分数（0~1,000,000+）
    float   accuracy    = 0.0f;     // 准确率（0.0~100.0%）
    int     maxCombo    = 0;        // 最大连击
    Grade   grade       = Grade::D; // 评级

    int     perfectCount = 0;
    int     greatCount   = 0;
    int     goodCount    = 0;
    int     badCount     = 0;
    int     missCount    = 0;

    bool    isFullCombo  = false;   // 全连击（0 Miss + 0 Bad）
    bool    isAllPerfect = false;   // 全完美

    long long playedAt   = 0;       // Unix 时间戳（秒）
    std::vector<int> hitErrors;     // 每个音符的判定偏差（毫秒）
};

} // namespace sakura::game
