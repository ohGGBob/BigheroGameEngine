#pragma once
// U1-E3 Play Mode（编辑态/运行态分离）：纯逻辑状态机，无 GPU/窗口依赖，可离线单测。
//
// 对标 Unity Play Mode 的最小可用版：
//   - 三态：Editor（编辑态）/ Playing（运行态）/ Paused（暂停）。
//   - 进入 Play：调用方拍全量场景快照（SceneSnapshot）存为"编辑态底稿"；
//     仿真系统（物理/自转/粒子/导航代理/动画/脚本）仅在此后 State()==Playing 时推进，
//     编辑态/暂停一律冻结——场景静态（这是与既有"每帧推进"行为的关键差异）。
//   - Stop：取回底稿快照交调用方 LoadPacket 全量还原（实体增删/属性/父子层级/自转角），
//     回到编辑态；还原是"恢复"而非"编辑"，不入撤销栈。
//   - 撤销栈边界：Play 期间的场景变化是运行时状态，编辑命令不入栈
//     （AllowsSceneEditCommands 供 Application 收口所有 commandStack_.Execute 入口）。
//
// 分层与 game/SceneCommand.h 一致：状态与判定在此，快照拍摄/还原的副作用留在
// Application（SceneSnapshotTarget 实现者），保证本类可离线单测（不依赖 Application/Vulkan）。

#include "game/SceneCommand.h"

#include <utility>

namespace BigHero::Game
{
// Play Mode 三态（枚举值经 EditorPanel::playModeState 传给 UI 面板，勿改序）
enum class PlayModeState : int
{
    Editor = 0,  // 编辑态：仿真冻结，编辑操作即时生效并入撤销栈
    Playing = 1, // 运行态：仿真系统按 dt 推进
    Paused = 2,  // 暂停：冻结推进但保持运行状态（不还原）
};

// Play Mode 状态机：状态迁移 + 仿真门 + 撤销栈边界判定 + 编辑态底稿保管
class PlayModeController
{
  public:
    PlayModeController() = default;
    PlayModeController(const PlayModeController&) = delete;
    PlayModeController& operator=(const PlayModeController&) = delete;

    [[nodiscard]] PlayModeState State() const noexcept { return state_; }
    [[nodiscard]] bool IsEditor() const noexcept { return state_ == PlayModeState::Editor; }
    [[nodiscard]] bool IsPlaying() const noexcept { return state_ == PlayModeState::Playing; }
    [[nodiscard]] bool IsPaused() const noexcept { return state_ == PlayModeState::Paused; }
    // Play 会话进行中（运行或暂停；此时编辑态底稿有效）
    [[nodiscard]] bool IsActive() const noexcept { return state_ != PlayModeState::Editor; }
    // 仿真推进门：仅运行态推进（编辑态/暂停一律冻结——U1-E3 行为变化：编辑态场景静态）
    [[nodiscard]] bool ShouldSimulate() const noexcept { return state_ == PlayModeState::Playing; }
    // 撤销栈边界：仅编辑态允许场景编辑命令入栈（运行/暂停期间的场景变化是运行时状态）
    [[nodiscard]] bool AllowsSceneEditCommands() const noexcept { return state_ == PlayModeState::Editor; }

    // 编辑态底稿（进入 Play 瞬间的全量场景快照；仅 Play 会话期间有效）
    [[nodiscard]] const SceneSnapshot& Baseline() const noexcept { return baseline_; }

    // 进入 Play：拍底稿快照（由调用方 Snapshot() 传入）并切到运行态。
    // 已在 Play 会话中时 no-op 返回 false。
    bool EnterPlay(const SceneSnapshot& baseline)
    {
        if (state_ != PlayModeState::Editor)
            return false;
        baseline_ = baseline;
        state_ = PlayModeState::Playing;
        return true;
    }

    // 暂停：冻结推进但保持状态（不还原）。仅运行态有效。
    bool Pause()
    {
        if (state_ != PlayModeState::Playing)
            return false;
        state_ = PlayModeState::Paused;
        return true;
    }

    // 恢复：暂停 -> 运行。仅暂停态有效。
    bool Resume()
    {
        if (state_ != PlayModeState::Paused)
            return false;
        state_ = PlayModeState::Playing;
        return true;
    }

    // 运行/暂停切换（面板暂停按钮）。
    bool TogglePause()
    {
        if (state_ == PlayModeState::Playing)
            return Pause();
        if (state_ == PlayModeState::Paused)
            return Resume();
        return false;
    }

    // 停止：回到编辑态并经 outBaseline 取回底稿快照（调用方负责 LoadPacket 全量还原）。
    // 编辑态下 no-op 返回 false（不触碰 outBaseline）。
    bool Stop(SceneSnapshot& outBaseline)
    {
        if (state_ == PlayModeState::Editor)
            return false;
        outBaseline = std::move(baseline_);
        baseline_ = SceneSnapshot{};
        state_ = PlayModeState::Editor;
        return true;
    }

    // Ctrl+P 语义：编辑态 -> 进入 Play；运行/暂停 -> 停止还原。
    // 返回是否发生了状态迁移（供调用方决定是否执行还原副作用）。
    bool TogglePlayStop(const SceneSnapshot& baseline, SceneSnapshot& outBaseline)
    {
        if (state_ == PlayModeState::Editor)
            return EnterPlay(baseline);
        return Stop(outBaseline);
    }

  private:
    PlayModeState state_ = PlayModeState::Editor;
    SceneSnapshot baseline_; // 编辑态底稿：进入 Play 瞬间的全量场景快照
};

} // namespace BigHero::Game
