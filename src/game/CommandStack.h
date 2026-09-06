#pragma once
// 编辑器撤销/重做命令栈（纯逻辑，无 GPU/窗口依赖，可离线单测）。
//
// 设计：
//   - Command：可撤销命令抽象基类，Do()/Undo() 实现正反操作；Name() 供 UI 展示。
//   - CommandStack：维护"已执行(可撤销)"与"已撤销(可重做)"两条栈。
//     Execute() 执行并压入撤销栈、清空重做栈；Undo()/Redo() 在两栈间搬运。
//     撤销栈超过 maxDepth 时丢弃最旧命令（LRU 裁剪），避免无限增长。
//   - 命令以 unique_ptr 拥有，栈负责生命周期；拷贝被禁止（命令含副作用状态）。
//
// 商业化增强：
//   - MaxDepth()/SetMaxDepth()：查询/调整历史深度上限（SetMaxDepth 会即时裁剪最旧命令）。
//   - ModificationCount()：单调递增的修改计数器，每次 Execute/Undo/Redo 加一，
//     供编辑器判断"是否有未保存改动"（与自定义基值比较），避免保存后误报脏状态。
//
// 该模块是"玩法+工具一轮"升级 17-4 的核心，场景增删/属性编辑/关节操作均可纳入。

#include <cstddef>
#include <memory>
#include <vector>

namespace BigHero::Game
{
// 可撤销命令
class Command
{
  public:
    virtual ~Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command() = default;

    // 正向执行
    virtual void Do() = 0;
    // 反向撤销（须使状态回到 Do() 之前）
    virtual void Undo() = 0;
    // 命令名（编辑器日志/提示用）
    [[nodiscard]] virtual const char* Name() const noexcept { return "Command"; }
};

// 撤销/重做栈
class CommandStack
{
  public:
    explicit CommandStack(size_t maxDepth = 100) : maxDepth_(maxDepth > 0 ? maxDepth : 1) {}

    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    // 执行新命令：Do() + 压入撤销栈 + 清空重做栈。超出深度则丢弃最旧。
    void Execute(std::unique_ptr<Command> cmd)
    {
        cmd->Do();
        undoStack_.push_back(std::move(cmd));
        redoStack_.clear();
        TrimToDepth();
        ++modificationCount_;
    }

    [[nodiscard]] bool CanUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redoStack_.empty(); }

    // 撤销：弹出撤销栈顶，Undo()，压入重做栈。
    void Undo()
    {
        if (undoStack_.empty())
            return;
        auto cmd = std::move(undoStack_.back());
        undoStack_.pop_back();
        cmd->Undo();
        redoStack_.push_back(std::move(cmd));
        ++modificationCount_;
    }

    // 重做：弹出重做栈顶，Do()，压回撤销栈。
    void Redo()
    {
        if (redoStack_.empty())
            return;
        auto cmd = std::move(redoStack_.back());
        redoStack_.pop_back();
        cmd->Do();
        undoStack_.push_back(std::move(cmd));
        ++modificationCount_;
    }

    // 清空全部历史（不执行任何操作，也不递增修改计数）。
    void Clear()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

    [[nodiscard]] size_t UndoCount() const noexcept { return undoStack_.size(); }
    [[nodiscard]] size_t RedoCount() const noexcept { return redoStack_.size(); }

    // 历史深度上限（可在构造后调整；缩小会立即裁剪最旧命令）。
    [[nodiscard]] size_t MaxDepth() const noexcept { return maxDepth_; }
    void SetMaxDepth(size_t maxDepth)
    {
        maxDepth_ = maxDepth > 0 ? maxDepth : 1;
        TrimToDepth();
    }

    // 单调递增的修改计数：每次 Execute/Undo/Redo +1，Clear 不改变。
    // 用于编辑器脏标记：保存时记录基线，之后该值变化即表示有未保存改动。
    [[nodiscard]] std::size_t ModificationCount() const noexcept { return modificationCount_; }

    // 当前可撤销命令名（UI 提示，如"撤销：移动物体"）；空栈返回 ""
    [[nodiscard]] const char* TopUndoName() const noexcept
    {
        if (undoStack_.empty())
            return "";
        return undoStack_.back()->Name();
    }
    [[nodiscard]] const char* TopRedoName() const noexcept
    {
        if (redoStack_.empty())
            return "";
        return redoStack_.back()->Name();
    }

  private:
    // 撤销栈超过 maxDepth 时丢弃最旧命令（LRU 裁剪）。
    void TrimToDepth()
    {
        while (undoStack_.size() > maxDepth_)
            undoStack_.erase(undoStack_.begin());
    }

    size_t maxDepth_;
    std::vector<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
    std::size_t modificationCount_ = 0;
};

} // namespace BigHero::Game
