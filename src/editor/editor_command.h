#pragma once

// editor_command.h — 谱面编辑器 Command 模式（撤销/重做）
// 使用 Command 模式封装所有编辑操作，支持最多 200 步撤销/重做。
//
// 类层级：
//   EditorCommand (抽象基类)
//     ├── PlaceNoteCommand    放置键盘音符
//     ├── DeleteNoteCommand   删除键盘音符
//     ├── MoveNoteCommand     移动键盘音符（改变 time/lane）
//     ├── ModifyNoteCommand   修改音符属性（duration/type 等）
//     └── BatchCommand        批量操作（组合多条命令为一步）
//
//   CommandHistory            管理 undo/redo 双端队列，最大保留 200 步

#include "game/note.h"

#include <string>
#include <vector>
#include <deque>
#include <memory>

namespace sakura::editor
{

class EditorCore;  // 前向声明，避免循环包含

// ─────────────────────────────────────────────────────────────────────────────
// EditorCommand — 抽象基类
// ─────────────────────────────────────────────────────────────────────────────
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    // 执行操作（初次 + Redo 时调用）
    virtual void Execute(EditorCore& core) = 0;

    // 撤销操作（Undo 时调用）
    virtual void Undo(EditorCore& core) = 0;

    // 操作描述（用于工具提示/日志）
    virtual std::string GetDescription() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// PlaceNoteCommand — 放置键盘音符
// ─────────────────────────────────────────────────────────────────────────────
class PlaceNoteCommand final : public EditorCommand
{
public:
    explicit PlaceNoteCommand(const sakura::game::KeyboardNote& note);

    void Execute(EditorCore& core) override;
    void Undo   (EditorCore& core) override;
    std::string GetDescription() const override;

private:
    sakura::game::KeyboardNote m_note;
    int m_insertedIndex = -1;   // Execute 后记录插入索引，Undo 用
};

// ─────────────────────────────────────────────────────────────────────────────
// DeleteNoteCommand — 删除键盘音符
// ─────────────────────────────────────────────────────────────────────────────
class DeleteNoteCommand final : public EditorCommand
{
public:
    DeleteNoteCommand(int index, const sakura::game::KeyboardNote& savedNote);

    void Execute(EditorCore& core) override;
    void Undo   (EditorCore& core) override;
    std::string GetDescription() const override;

private:
    int                        m_originalIndex;
    sakura::game::KeyboardNote m_savedNote;
};

// ─────────────────────────────────────────────────────────────────────────────
// MoveNoteCommand — 移动键盘音符（改变 time / lane）
// ─────────────────────────────────────────────────────────────────────────────
class MoveNoteCommand final : public EditorCommand
{
public:
    MoveNoteCommand(int oldIndex,
                    const sakura::game::KeyboardNote& oldNote,
                    int newTimeMs, int newLane);

    void Execute(EditorCore& core) override;
    void Undo   (EditorCore& core) override;
    std::string GetDescription() const override;

private:
    int m_oldIndex;
    int m_newIndex = -1;
    sakura::game::KeyboardNote m_oldNote;
    sakura::game::KeyboardNote m_newNote;
};

// ─────────────────────────────────────────────────────────────────────────────
// ModifyNoteCommand — 修改音符属性（duration / type 等）
// ─────────────────────────────────────────────────────────────────────────────
class ModifyNoteCommand final : public EditorCommand
{
public:
    ModifyNoteCommand(int index,
                      const sakura::game::KeyboardNote& oldNote,
                      const sakura::game::KeyboardNote& newNote);

    void Execute(EditorCore& core) override;
    void Undo   (EditorCore& core) override;
    std::string GetDescription() const override;

private:
    int                        m_index;
    sakura::game::KeyboardNote m_oldNote;
    sakura::game::KeyboardNote m_newNote;
};

// ─────────────────────────────────────────────────────────────────────────────
// BatchCommand — 批量操作
// ─────────────────────────────────────────────────────────────────────────────
class BatchCommand final : public EditorCommand
{
public:
    explicit BatchCommand(std::string description);

    void Add(std::unique_ptr<EditorCommand> cmd);
    bool IsEmpty() const { return m_commands.empty(); }

    void Execute(EditorCore& core) override;
    void Undo   (EditorCore& core) override;
    std::string GetDescription() const override;

private:
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    std::string m_description;
};

// ─────────────────────────────────────────────────────────────────────────────
// CommandHistory — 双端队列式撤销/重做历史（最大 200 步）
// ─────────────────────────────────────────────────────────────────────────────
class CommandHistory
{
public:
    static constexpr int MAX_HISTORY = 200;

    CommandHistory() = default;

    // 执行命令并压入 undo 栈，同时清空 redo 栈
    void Execute(std::unique_ptr<EditorCommand> cmd, EditorCore& core);

    void Undo(EditorCore& core);
    void Redo(EditorCore& core);
    void Clear();

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    // 返回下一步撤销/重做的描述文本
    std::string GetUndoDescription() const;
    std::string GetRedoDescription() const;

    int GetUndoCount() const { return static_cast<int>(m_undoStack.size()); }
    int GetRedoCount() const { return static_cast<int>(m_redoStack.size()); }

private:
    std::deque<std::unique_ptr<EditorCommand>> m_undoStack;
    std::deque<std::unique_ptr<EditorCommand>> m_redoStack;
};

} // namespace sakura::editor
