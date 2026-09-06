#pragma once
// 撤销/重做管理：命令栈 + 场景快照命令
// 原 Application 中 commandStack_, undoKeyHeld_, redoKeyHeld_, SceneSnapshotCommand 等逻辑

#include "ISubSystem.h"
#include "game/CommandStack.h"
#include "game/SceneCommand.h"
#include <memory>

namespace BigHero::App
{

class UndoRedoManager final : public ISubSystem
{
public:
    UndoRedoManager() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "UndoRedoManager"; }
    [[nodiscard]] int Priority() const noexcept override { return 20; }

    void Init() override {}

    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        // 撤销/重做由 Application 处理输入，这里只提供接口
    }

    void PreRender(uint32_t) override {}

    void OnSwapchainRecreated() override {}

    void OnRenderPassRecreated() override {}

    // 执行场景快照命令
    template<typename... Args>
    void ExecuteSceneCommand(const Game::SceneSnapshot& before, const Game::SceneSnapshot& after, Args&&... args)
    {
        suppressEditGesture_ = true;
        commandStack_.Execute(std::make_unique<Game::SceneSnapshotCommand>(
            sceneSnapshotTarget_, before, after, std::forward<Args>(args)...));
    }

    void Undo()
    {
        commandStack_.Undo();
        suppressEditGesture_ = true;
    }

    void Redo()
    {
        commandStack_.Redo();
        suppressEditGesture_ = true;
    }

    // 属性编辑手势：滑块/调色板/Gizmo 拖拽
    void BeginEditGesture(const Game::SceneSnapshot& snapshot)
    {
        editGestureBefore_ = snapshot;
        editGestureActive_ = true;
    }

    void EndEditGesture(const Game::SceneSnapshot& snapshot)
    {
        if (editGestureActive_ && sceneSnapshotTarget_ && !suppressEditGesture_)
        {
            if (editGestureBefore_.value().HasChanges(snapshot))
            {
                commandStack_.Execute(std::make_unique<Game::SceneSnapshotCommand>(
                    *sceneSnapshotTarget_, editGestureBefore_.value(), snapshot, "属性编辑"));
            }
        }
        editGestureActive_ = false;
        editGestureBefore_.reset();
    }

    void BeginGizmoGesture(const Game::SceneSnapshot& snapshot)
    {
        gizmoEditBefore_ = snapshot;
        gizmoEditActive_ = true;
    }

    void EndGizmoGesture(const Game::SceneSnapshot& snapshot)
    {
        if (gizmoEditActive_ && sceneSnapshotTarget_ && !suppressEditGesture_)
        {
            if (gizmoEditBefore_->HasChanges(snapshot))
            {
                commandStack_.Execute(std::make_unique<Game::SceneSnapshotCommand>(
                    *sceneSnapshotTarget_, gizmoEditBefore_->value(), snapshot, "Gizmo 变换"));
            }
        }
        gizmoEditActive_ = false;
        gizmoEditBefore_.reset();
    }

    void SuppressGestureThisFrame() { suppressEditGesture_ = true; }

    void SetTarget(Game::SceneSnapshotTarget* target) { sceneSnapshotTarget_ = target; }

    [[nodiscard]] bool CanUndo() const noexcept { return commandStack_.CanUndo(); }
    [[nodiscard]] bool CanRedo() const noexcept { return commandStack_.CanRedo(); }
    [[nodiscard]] const char* TopUndoName() const noexcept { return commandStack_.TopUndoName(); }
    [[nodiscard]] const char* TopRedoName() const noexcept { return commandStack_.TopRedoName(); }

    [[nodiscard]] const char* Name() const noexcept override { return "UndoRedoManager"; }
    [[nodiscard]] int Priority() const noexcept override { return 20; }

private:
    Game::CommandStack commandStack_;
    Game::SceneSnapshotTarget* sceneSnapshotTarget_ = nullptr;
    bool suppressEditGesture_ = false;
    std::optional<Game::SceneSnapshot> editGestureBefore_;
    bool editGestureActive_ = false;
    std::optional<Game::SceneSnapshot> gizmoEditBefore_;
    bool gizmoEditActive_ = false;
};

} // namespace BigHero::App