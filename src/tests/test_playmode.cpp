// U1-E3 Play Mode（编辑态/运行态分离）单元测试。
//
// 覆盖规格的五项验证（全部离线纯逻辑，不触碰 Vulkan/Application 实例）：
//   ① 编辑态下推进 N 帧 Spin 角度不变（冻结）；
//   ② 进入 Play 推进后角度变化；
//   ③ Stop 后快照逐位还原（含父子层级/实体数/自转角）；
//   ④ Pause 保持状态（冻结推进但不还原，恢复后从暂停点继续）；
//   ⑤ Play 期间运行变化不入撤销栈、编辑态编辑照旧入栈（命令栈层面验证）。
//
// 主循环语义复刻：仿真推进门 = PlayModeController::ShouldSimulate()（与
// Application 主循环同源判定）；快照/还原 = EcsScene BuildPacket/LoadPacket
// （与 Application::Snapshot/RestoreScene 同构）。
#include "framework/test_common.h"

#include "game/CommandStack.h"
#include "game/PlayMode.h"
#include "game/SceneCommand.h"
#include "scene/EcsScene.h"

using namespace BigHero;

namespace
{
// 构造带二级父子链与自转的测试场景：#0(父) <- #1(子) <- #2(孙)
std::vector<Scene::SceneObject> MakeTestScene()
{
    Scene::SceneObject parent;
    parent.position = glm::vec3(1.0f, 2.0f, 3.0f);
    parent.rotation = glm::vec3(10.0f, 20.0f, 30.0f);
    parent.scale = 1.5f;
    parent.tint = glm::vec3(0.9f, 0.8f, 0.7f);
    parent.spinSpeed = 90.0f; // 度/秒
    parent.phase = 10.0f;
    parent.meshId = 0;
    parent.metallic = 0.25f;
    parent.roughness = 0.75f;

    Scene::SceneObject child = parent;
    child.position = glm::vec3(0.0f, 1.0f, 0.0f);
    child.scale = 0.5f;
    child.rotation = glm::vec3(0.0f, 0.0f, 45.0f);
    child.tint = glm::vec3(0.2f, 0.4f, 0.6f);
    child.spinSpeed = -45.0f;
    child.phase = 200.0f;
    child.metallic = 1.0f;
    child.roughness = 0.1f;
    child.parentIndex = 0;

    Scene::SceneObject grand = child;
    grand.position = glm::vec3(0.0f, 0.5f, 0.0f);
    grand.scale = 0.25f;
    grand.rotation = glm::vec3(0.0f);
    grand.tint = glm::vec3(1.0f, 1.0f, 0.0f);
    grand.spinSpeed = 30.0f;
    grand.phase = 0.0f;
    grand.metallic = 0.0f;
    grand.roughness = 0.9f;
    grand.parentIndex = 1;

    return {parent, child, grand};
}

// 最小快照目标：与 Application 的 Snapshot/RestoreScene 同构（BuildPacket / LoadPacket）
struct TestSnapshotTarget : Game::SceneSnapshotTarget
{
    Scene::EcsScene& ecs;
    explicit TestSnapshotTarget(Scene::EcsScene& s) : ecs(s) {}

    [[nodiscard]] Game::SceneSnapshot Snapshot() const override
    {
        Game::SceneSnapshot s;
        s.objects = ecs.BuildPacket(&s.spins);
        return s;
    }
    void RestoreScene(const Game::SceneSnapshot& snap) override { ecs.LoadPacket(snap.objects, &snap.spins); }
};

// 复刻 Application 主循环的仿真推进门：仅 ShouldSimulate() 时推进一帧（含自转系统）
void StepFrame(Scene::EcsScene& ecs, const Game::PlayModeController& pm, float dt)
{
    if (pm.ShouldSimulate())
        ecs.UpdateSpins(dt);
}

// 快照是否逐位一致（SceneSnapshotsDiffer 为精确比较：字段/父子层级/自转角任一不同即真）
bool SameSnapshot(const Game::SceneSnapshot& a, const Game::SceneSnapshot& b)
{
    return !Game::SceneSnapshotsDiffer(a, b);
}
} // namespace

// ---- 状态机本体：三态迁移与底稿保管 ----
TEST_CASE("PlayMode.StateMachine")
{
    using Game::PlayModeController;
    using Game::PlayModeState;

    PlayModeController pm;
    CHECK(pm.State() == PlayModeState::Editor);
    CHECK(pm.IsEditor());
    CHECK(!pm.IsActive());
    CHECK(!pm.ShouldSimulate());
    CHECK(pm.AllowsSceneEditCommands()); // 编辑态允许编辑命令入栈

    const std::vector<Scene::SceneObject> objs = MakeTestScene();
    Game::SceneSnapshot baseline;
    baseline.objects = objs;

    Game::SceneSnapshot out;
    // 编辑态下 Stop / 暂停 / 切换暂停均无效
    CHECK(!pm.Stop(out));
    CHECK(!pm.Pause());
    CHECK(!pm.Resume());
    CHECK(!pm.TogglePause());
    CHECK(!pm.Stop(out));
    CHECK(pm.IsEditor());

    // 进入 Play：Playing + ShouldSimulate + 底稿保存
    CHECK(pm.EnterPlay(baseline));
    CHECK(pm.State() == PlayModeState::Playing);
    CHECK(pm.ShouldSimulate());
    CHECK(pm.IsActive());
    CHECK(!pm.AllowsSceneEditCommands()); // 运行态：编辑命令不入栈
    CHECK(pm.Baseline().objects.size() == objs.size());

    // 重复进入 Play 无效（已在会话中）
    CHECK(!pm.EnterPlay(baseline));
    CHECK(pm.IsPlaying());

    // 暂停：冻结（不模拟）但会话仍活跃；恢复回到 Playing
    CHECK(pm.Pause());
    CHECK(pm.IsPaused());
    CHECK(pm.IsActive());
    CHECK(!pm.ShouldSimulate());
    CHECK(!pm.AllowsSceneEditCommands()); // 暂停态同样不入栈
    CHECK(!pm.Pause());
    CHECK(pm.Resume());
    CHECK(pm.IsPlaying());
    CHECK(!pm.Resume());

    // TogglePause：Playing -> Paused -> Playing
    CHECK(pm.TogglePause());
    CHECK(pm.IsPaused());
    CHECK(pm.TogglePause());
    CHECK(pm.IsPlaying());

    // Stop：取回底稿并回编辑态；再次 Stop 无效
    CHECK(pm.Stop(out));
    CHECK(pm.IsEditor());
    CHECK(!pm.IsActive());
    CHECK(SameSnapshot(out, baseline));   // 底稿逐位取回
    CHECK(pm.Baseline().objects.empty()); // 底稿已清空（会话外无意义）
    CHECK(!pm.Stop(out));

    // Ctrl+P 语义：Editor -> Play；Playing -> Stop
    Game::SceneSnapshot out2;
    CHECK(pm.TogglePlayStop(baseline, out2));
    CHECK(pm.IsPlaying());
    CHECK(pm.TogglePlayStop(baseline, out2));
    CHECK(pm.IsEditor());
    CHECK(SameSnapshot(out2, baseline));
}

// ① 编辑态冻结：推进 N 帧 Spin 角度不变（仿真门关闭）
TEST_CASE("PlayMode.EditorFreeze")
{
    Scene::EcsScene ecs;
    ecs.LoadPacket(MakeTestScene());

    const Game::SceneSnapshot initial = [&]
    {
        Game::SceneSnapshot s;
        s.objects = ecs.BuildPacket(&s.spins);
        return s;
    }();

    Game::PlayModeController pm; // 默认编辑态
    CHECK(!pm.ShouldSimulate());

    // 复刻主循环：编辑态推进 60 帧（1s @60fps）
    for (int i = 0; i < 60; ++i)
        StepFrame(ecs, pm, 1.0f / 60.0f);

    const Game::SceneSnapshot after = [&]
    {
        Game::SceneSnapshot s;
        s.objects = ecs.BuildPacket(&s.spins);
        return s;
    }();

    // 自转角逐位不变（冻结），其余属性亦无变化
    REQUIRE(initial.spins.size() == after.spins.size());
    for (size_t i = 0; i < initial.spins.size(); ++i)
        CHECK_EQ(initial.spins[i], after.spins[i]);
    CHECK(SameSnapshot(initial, after));
}

// ② 进入 Play 推进后角度变化（仿真门开启，自转按 speed*dt 积分）
TEST_CASE("PlayMode.PlayAdvances")
{
    Scene::EcsScene ecs;
    ecs.LoadPacket(MakeTestScene());

    Game::PlayModeController pm;
    CHECK(pm.EnterPlay(Game::SceneSnapshot{})); // 底稿内容与本用例无关

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i)
        StepFrame(ecs, pm, 1.0f / 60.0f); // 累计 1s

    Game::SceneSnapshot s;
    s.objects = ecs.BuildPacket(&s.spins);
    REQUIRE(s.spins.size() == 3);
    // 90°/s × 1s = 90°，phase 10 -> 100（未回绕，逐位可算）
    CHECK_NEAR(s.spins[0], 100.0f, 1e-3f);
    CHECK_NEAR(s.spins[1], 155.0f, 1e-3f); // -45°/s，200 -> 155
    CHECK_NEAR(s.spins[2], 30.0f, 1e-3f);  // 30°/s，0 -> 30
}

// ③ Stop 后快照逐位还原（运行期自转/移动/增删实体/改父全部回到底稿）
TEST_CASE("PlayMode.StopRestoresBitExact")
{
    Scene::EcsScene ecs;
    ecs.LoadPacket(MakeTestScene());

    const Game::SceneSnapshot baseline = [&]
    {
        Game::SceneSnapshot s;
        s.objects = ecs.BuildPacket(&s.spins);
        return s;
    }();

    Game::PlayModeController pm;
    REQUIRE(pm.EnterPlay(baseline));

    // ---- 运行期变化：推进 + 修改 + 增 + 删 + 改父 ----
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i)
        StepFrame(ecs, pm, dt);                               // 自转前进
    ecs.SetObjectPosition(0, glm::vec3(42.0f, -7.0f, 0.25f)); // 属性修改
    ecs.SetObjectScale(1, 9.0f);

    Scene::SceneObject extra;
    extra.position = glm::vec3(5.0f, 0.5f, 5.0f);
    extra.scale = 1.0f;
    extra.spinSpeed = 120.0f;
    extra.phase = 15.0f;
    (void)ecs.CreateObject(extra); // 实体增加（#3）

    ecs.DestroyAt(1); // 实体删除（#1 子节点销毁，#2 提升为根）
    REQUIRE(ecs.ObjectCount() == 3);

    Game::SceneSnapshot mutated;
    mutated.objects = ecs.BuildPacket(&mutated.spins);
    // 前置确认：运行期快照确已偏离底稿（实体数/属性/层级/自转角至少一项不同）
    REQUIRE(!SameSnapshot(mutated, baseline));

    // ---- Stop：底稿全量还原 ----
    Game::SceneSnapshot out;
    REQUIRE(pm.Stop(out));
    ecs.LoadPacket(out.objects, &out.spins); // 与 Application::RestoreScene 同构

    const Game::SceneSnapshot restored = [&]
    {
        Game::SceneSnapshot s;
        s.objects = ecs.BuildPacket(&s.spins);
        return s;
    }();

    CHECK(restored.objects.size() == baseline.objects.size()); // 实体数还原
    REQUIRE(restored.objects.size() == 3);
    // 父子层级逐位还原：#1 父 = #0，#2 父 = #1
    CHECK_EQ(restored.objects[1].parentIndex, baseline.objects[1].parentIndex);
    CHECK_EQ(restored.objects[2].parentIndex, baseline.objects[2].parentIndex);
    CHECK_EQ(restored.objects[1].parentIndex, 0);
    CHECK_EQ(restored.objects[2].parentIndex, 1);
    // 自转角逐位还原
    REQUIRE(restored.spins.size() == baseline.spins.size());
    for (size_t i = 0; i < baseline.spins.size(); ++i)
        CHECK_EQ(restored.spins[i], baseline.spins[i]);
    // 全量（属性/材质/物理/旋转/缩放/父子）逐位一致
    CHECK(SameSnapshot(restored, baseline));
}

// ④ Pause 保持状态：冻结推进但不还原，恢复后从暂停点继续
TEST_CASE("PlayMode.PauseKeepsState")
{
    Scene::EcsScene ecs;
    ecs.LoadPacket(MakeTestScene());

    Game::PlayModeController pm;
    REQUIRE(pm.EnterPlay(Game::SceneSnapshot{}));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) // 0.5s：90°/s -> +45°
        StepFrame(ecs, pm, dt);

    Game::SceneSnapshot atPause;
    atPause.objects = ecs.BuildPacket(&atPause.spins);
    CHECK_NEAR(atPause.spins[0], 55.0f, 1e-3f); // phase 10 + 45

    REQUIRE(pm.Pause());
    CHECK(pm.IsActive()); // 会话保持（不还原场景）
    CHECK(!pm.ShouldSimulate());
    for (int i = 0; i < 30; ++i) // 暂停期间推进 30 帧：全部被门挡下
        StepFrame(ecs, pm, dt);

    Game::SceneSnapshot paused;
    paused.objects = ecs.BuildPacket(&paused.spins);
    for (size_t i = 0; i < atPause.spins.size(); ++i)
        CHECK_EQ(paused.spins[i], atPause.spins[i]); // 状态逐位保持
    CHECK(SameSnapshot(paused, atPause));

    REQUIRE(pm.Resume());
    for (int i = 0; i < 30; ++i) // 再推进 0.5s：从暂停点继续到 100°
        StepFrame(ecs, pm, dt);

    Game::SceneSnapshot resumed;
    resumed.objects = ecs.BuildPacket(&resumed.spins);
    CHECK_NEAR(resumed.spins[0], 100.0f, 1e-3f);
    CHECK(!SameSnapshot(resumed, atPause)); // 恢复后确有推进
}

// ⑤ 撤销栈边界：运行变化不入栈、Stop 还原不入栈、编辑态编辑照旧入栈
TEST_CASE("PlayMode.UndoStackBoundary")
{
    using Game::CommandStack;
    using Game::PlayModeController;
    using Game::SceneSnapshotCommand;

    Scene::EcsScene ecs;
    ecs.LoadPacket(MakeTestScene());
    TestSnapshotTarget target(ecs);
    CommandStack stack;

    PlayModeController pm;
    const glm::vec3 originPos = MakeTestScene()[0].position;

    // ---- 编辑态：属性编辑照旧入栈 ----
    const Game::SceneSnapshot beforeEdit = target.Snapshot();
    REQUIRE(pm.AllowsSceneEditCommands());
    ecs.SetObjectPosition(0, glm::vec3(5.0f, 5.0f, 5.0f));
    // 与 Application::ExecuteEditCommand 同构的收口：仅编辑态入栈
    {
        const Game::SceneSnapshot after = target.Snapshot();
        if (pm.AllowsSceneEditCommands())
            stack.Execute(std::make_unique<SceneSnapshotCommand>(&target, beforeEdit, after, "编辑物体属性"));
    }
    CHECK(stack.UndoCount() == 1);
    CHECK(stack.CanUndo());

    // ---- 进入 Play：运行变化不入栈 ----
    Game::SceneSnapshot playBaseline = target.Snapshot();
    REQUIRE(pm.EnterPlay(playBaseline));
    CHECK(!pm.AllowsSceneEditCommands());
    ecs.SetObjectPosition(1, glm::vec3(9.0f, 9.0f, 9.0f)); // 运行期修改
    for (int i = 0; i < 10; ++i)
        StepFrame(ecs, pm, 1.0f / 60.0f); // 运行期自转
    {
        const Game::SceneSnapshot after = target.Snapshot();
        if (pm.AllowsSceneEditCommands()) // 运行态：收口拦截，Execute 不发生
            stack.Execute(std::make_unique<SceneSnapshotCommand>(&target, playBaseline, after, "运行期变化"));
    }
    CHECK(stack.UndoCount() == 1); // 撤销栈未增长（运行不是编辑）
    CHECK(stack.TopUndoName() == std::string("编辑物体属性"));

    // ---- Stop：底稿还原不经撤销栈 ----
    Game::SceneSnapshot out;
    REQUIRE(pm.Stop(out));
    CHECK(pm.AllowsSceneEditCommands());
    target.RestoreScene(out);      // 与 Application::StopPlayMode 同构（直接 Restore，不入栈）
    CHECK(stack.UndoCount() == 1); // 还原是恢复不是编辑，栈深不变

    // 还原后：整个场景逐位回到进入 Play 前的编辑态（含 #0 的 Play 前编辑、#1 未被运行期修改污染）
    const Game::SceneSnapshot restored = target.Snapshot();
    CHECK(SameSnapshot(restored, playBaseline));
    CHECK(restored.objects[0].position == glm::vec3(5.0f, 5.0f, 5.0f)); // Play 前的编辑保留
    CHECK(restored.objects[1].position != glm::vec3(9.0f, 9.0f, 9.0f)); // 运行期修改已被还原丢弃

    // ---- 撤销：跨 Play 边界回到进入 Play 前的编辑起点 ----
    stack.Undo();
    CHECK(stack.UndoCount() == 0);
    const Game::SceneSnapshot undone = target.Snapshot();
    CHECK(undone.objects[0].position == originPos);          // 编辑被撤销
    CHECK(Game::SceneSnapshotsDiffer(undone, playBaseline)); // 与 Play 前状态不同
}
