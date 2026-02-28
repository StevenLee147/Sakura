// editor_command.cpp — EditorCommand 系列实现 + CommandHistory

#include "editor_command.h"
#include "editor_core.h"
#include "utils/logger.h"

#include <algorithm>

namespace sakura::editor
{

// ═════════════════════════════════════════════════════════════════════════════
// PlaceNoteCommand
// ═════════════════════════════════════════════════════════════════════════════

PlaceNoteCommand::PlaceNoteCommand(const sakura::game::KeyboardNote& note)
    : m_note(note)
{
}

void PlaceNoteCommand::Execute(EditorCore& core)
{
    m_insertedIndex = core.RawAddNote(m_note);
    LOG_TRACE("[Cmd] PlaceNote Execute: idx={} time={} lane={}",
              m_insertedIndex, m_note.time, m_note.lane);
}

void PlaceNoteCommand::Undo(EditorCore& core)
{
    if (m_insertedIndex >= 0)
    {
        core.RawRemoveNote(m_insertedIndex);
        LOG_TRACE("[Cmd] PlaceNote Undo: removed idx={}", m_insertedIndex);
        m_insertedIndex = -1;
    }
}

std::string PlaceNoteCommand::GetDescription() const
{
    return "放置音符 (lane=" + std::to_string(m_note.lane)
         + ", time=" + std::to_string(m_note.time) + "ms)";
}

// ═════════════════════════════════════════════════════════════════════════════
// DeleteNoteCommand
// ═════════════════════════════════════════════════════════════════════════════

DeleteNoteCommand::DeleteNoteCommand(int index,
                                     const sakura::game::KeyboardNote& savedNote)
    : m_originalIndex(index)
    , m_savedNote(savedNote)
{
}

void DeleteNoteCommand::Execute(EditorCore& core)
{
    core.RawRemoveNote(m_originalIndex);
    LOG_TRACE("[Cmd] DeleteNote Execute: idx={} time={} lane={}",
              m_originalIndex, m_savedNote.time, m_savedNote.lane);
}

void DeleteNoteCommand::Undo(EditorCore& core)
{
    core.RawInsertNoteAt(m_originalIndex, m_savedNote);
    LOG_TRACE("[Cmd] DeleteNote Undo: restored idx={}", m_originalIndex);
}

std::string DeleteNoteCommand::GetDescription() const
{
    return "删除音符 (lane=" + std::to_string(m_savedNote.lane)
         + ", time=" + std::to_string(m_savedNote.time) + "ms)";
}

// ═════════════════════════════════════════════════════════════════════════════
// MoveNoteCommand
// ═════════════════════════════════════════════════════════════════════════════

MoveNoteCommand::MoveNoteCommand(int oldIndex,
                                 const sakura::game::KeyboardNote& oldNote,
                                 int newTimeMs, int newLane)
    : m_oldIndex(oldIndex)
    , m_oldNote(oldNote)
    , m_newNote(oldNote)
{
    m_newNote.time = newTimeMs;
    m_newNote.lane = newLane;
}

void MoveNoteCommand::Execute(EditorCore& core)
{
    core.RawRemoveNote(m_oldIndex);
    m_newIndex = core.RawAddNote(m_newNote);
    LOG_TRACE("[Cmd] MoveNote Execute: old=({},{}) → new=({},{})",
              m_oldNote.lane, m_oldNote.time, m_newNote.lane, m_newNote.time);
}

void MoveNoteCommand::Undo(EditorCore& core)
{
    if (m_newIndex >= 0)
    {
        core.RawRemoveNote(m_newIndex);
        m_newIndex = -1;
    }
    m_oldIndex = core.RawAddNote(m_oldNote);
    LOG_TRACE("[Cmd] MoveNote Undo: restored to lane={} time={}",
              m_oldNote.lane, m_oldNote.time);
}

std::string MoveNoteCommand::GetDescription() const
{
    return "移动音符 → lane=" + std::to_string(m_newNote.lane)
         + ", time=" + std::to_string(m_newNote.time) + "ms";
}

// ═════════════════════════════════════════════════════════════════════════════
// ModifyNoteCommand
// ═════════════════════════════════════════════════════════════════════════════

ModifyNoteCommand::ModifyNoteCommand(int index,
                                     const sakura::game::KeyboardNote& oldNote,
                                     const sakura::game::KeyboardNote& newNote)
    : m_index(index)
    , m_oldNote(oldNote)
    , m_newNote(newNote)
{
}

void ModifyNoteCommand::Execute(EditorCore& core)
{
    core.RawModifyNote(m_index, m_newNote);
    LOG_TRACE("[Cmd] ModifyNote Execute: idx={}", m_index);
}

void ModifyNoteCommand::Undo(EditorCore& core)
{
    core.RawModifyNote(m_index, m_oldNote);
    LOG_TRACE("[Cmd] ModifyNote Undo: idx={}", m_index);
}

std::string ModifyNoteCommand::GetDescription() const
{
    return "修改音符 #" + std::to_string(m_index);
}

// ═════════════════════════════════════════════════════════════════════════════
// BatchCommand
// ═════════════════════════════════════════════════════════════════════════════

BatchCommand::BatchCommand(std::string description)
    : m_description(std::move(description))
{
}

void BatchCommand::Add(std::unique_ptr<EditorCommand> cmd)
{
    m_commands.push_back(std::move(cmd));
}

void BatchCommand::Execute(EditorCore& core)
{
    for (auto& cmd : m_commands)
        cmd->Execute(core);
}

void BatchCommand::Undo(EditorCore& core)
{
    // 逆序撤销
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
        (*it)->Undo(core);
}

std::string BatchCommand::GetDescription() const
{
    return m_description;
}

// ═════════════════════════════════════════════════════════════════════════════
// CommandHistory
// ═════════════════════════════════════════════════════════════════════════════

void CommandHistory::Execute(std::unique_ptr<EditorCommand> cmd, EditorCore& core)
{
    cmd->Execute(core);

    // 新操作发生时清空 redo 栈
    m_redoStack.clear();

    // 压入 undo 栈
    m_undoStack.push_back(std::move(cmd));

    // 超出上限时从头部移除最旧的记录
    while (static_cast<int>(m_undoStack.size()) > MAX_HISTORY)
        m_undoStack.pop_front();
}

void CommandHistory::Undo(EditorCore& core)
{
    if (m_undoStack.empty()) return;

    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    cmd->Undo(core);

    m_redoStack.push_back(std::move(cmd));
}

void CommandHistory::Redo(EditorCore& core)
{
    if (m_redoStack.empty()) return;

    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    cmd->Execute(core);

    m_undoStack.push_back(std::move(cmd));

    // 保持上限
    while (static_cast<int>(m_undoStack.size()) > MAX_HISTORY)
        m_undoStack.pop_front();
}

void CommandHistory::Clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
}

std::string CommandHistory::GetUndoDescription() const
{
    if (m_undoStack.empty()) return "";
    return m_undoStack.back()->GetDescription();
}

std::string CommandHistory::GetRedoDescription() const
{
    if (m_redoStack.empty()) return "";
    return m_redoStack.back()->GetDescription();
}

} // namespace sakura::editor
