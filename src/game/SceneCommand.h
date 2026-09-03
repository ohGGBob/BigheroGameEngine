#pragma once
// 场景快照命令（纯逻辑，无 GPU/窗口依赖，可离线单测）。
//
// 设计：
//   - SceneSnapshot：场景可还原状态的轻量副本（物体列表 / 自转角 / 可见性标志）。
//   - SceneSnapshotTarget：快照的读写接口（由 Application 实现，持有真实场景状态）。
//   - SceneSnapshotCommand：把"一次编辑"封装为可撤销命令——Do() 还原 after，Undo() 还原 before。
//     场景增删与属性编辑（移动/缩放/改色/改材质）都只是 before/after 两个快照的差异，
//     因此复用同一个命令即可统一纳入 CommandStack。
//
// 该模块由升级 17 的编辑器撤销/重做提炼而来，升级 20 进一步把"物体属性连续编辑"也纳入其中，
// 并抽离为独立纯逻辑头文件以便离线单测（不依赖 Application / Vulkan）。

#include "game/CommandStack.h"
#include "scene/Scene.h"

#include <cstddef>
#include <vector>

namespace BigHero::Game
{
// 场景可还原状态副本
struct SceneSnapshot
{
    std::vector<Scene::SceneObject> objects;
    std::vector<float> spins;
    std::vector<uint8_t> visibility;
};

// 场景快照的读写目标（Application 实现）
class SceneSnapshotTarget
{
  public:
    virtual ~SceneSnapshotTarget() = default;
    SceneSnapshotTarget(const SceneSnapshotTarget&) = delete;
    SceneSnapshotTarget& operator=(const SceneSnapshotTarget&) = delete;
    SceneSnapshotTarget() = default;

    // 抓取当前场景可还原快照
    [[nodiscard]] virtual SceneSnapshot Snapshot() const = 0;
    // 还原到给定快照（命令栈 Do/Undo 调用）
    virtual void RestoreScene(const SceneSnapshot& snap) = 0;
};

// 场景编辑的可撤销命令：Do 还原 after、Undo 还原 before
class SceneSnapshotCommand : public Command
{
  public:
    SceneSnapshotCommand(SceneSnapshotTarget* target, SceneSnapshot before, SceneSnapshot after, const char* name)
        : target_(target), before_(std::move(before)), after_(std::move(after)), name_(name)
    {
    }

    void Do() override
    {
        if (target_)
            target_->RestoreScene(after_);
    }
    void Undo() override
    {
        if (target_)
            target_->RestoreScene(before_);
    }
    [[nodiscard]] const char* Name() const noexcept override { return name_; }

  private:
    SceneSnapshotTarget* target_;
    SceneSnapshot before_;
    SceneSnapshot after_;
    const char* name_;
};

// 两个快照是否不同（用于手势提交时判断"是否真的改了东西"，避免空命令）
[[nodiscard]] inline bool SceneSnapshotsDiffer(const SceneSnapshot& a, const SceneSnapshot& b) noexcept
{
    if (a.objects.size() != b.objects.size() || a.spins.size() != b.spins.size() ||
        a.visibility.size() != b.visibility.size())
        return true;
    for (size_t i = 0; i < a.objects.size(); ++i)
        if (a.objects[i].position != b.objects[i].position || a.objects[i].scale != b.objects[i].scale ||
            a.objects[i].tint != b.objects[i].tint || a.objects[i].rotation != b.objects[i].rotation ||
            a.objects[i].spinSpeed != b.objects[i].spinSpeed || a.objects[i].metallic != b.objects[i].metallic ||
            a.objects[i].roughness != b.objects[i].roughness || a.objects[i].physicsType != b.objects[i].physicsType ||
            a.objects[i].physicsShape != b.objects[i].physicsShape ||
            a.objects[i].physicsMass != b.objects[i].physicsMass ||
            a.objects[i].physicsFriction != b.objects[i].physicsFriction ||
            a.objects[i].physicsRestitution != b.objects[i].physicsRestitution)
            return true;
    for (size_t i = 0; i < a.spins.size(); ++i)
        if (a.spins[i] != b.spins[i])
            return true;
    for (size_t i = 0; i < a.visibility.size(); ++i)
        if (a.visibility[i] != b.visibility[i])
            return true;
    return false;
}

} // namespace BigHero::Game
