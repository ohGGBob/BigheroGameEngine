// 编辑器层级树（Hierarchy）单元测试 —— Unity 对标清单 U1-E1。
// 覆盖四条验收线：
//   1. 拖拽改父后 parentIndex 正确（EcsScene::SetParent + 包投影，含父下标高于自身）；
//   2. 防环校验生效（Hierarchy::WouldCreateCycle 纯逻辑）；
//   3. 删除父节点后子节点提升为根（EcsScene::DestroyAt）；
//   4. 撤销恢复原父子关系（CommandStack + SceneSnapshotCommand 经真实 LoadPacket 还原）。
// 另覆盖树构建 / 搜索过滤保留祖先链等面板纯逻辑。
#include "framework/test_common.h"

#include "editor/HierarchyModel.h"
#include "game/CommandStack.h"
#include "game/SceneCommand.h"
#include "scene/EcsScene.h"

#include <memory>
#include <vector>

using namespace BigHero;

namespace
{
Scene::SceneObject MakeObj(const glm::vec3& pos, uint32_t meshId = 0, int32_t parentIndex = -1)
{
    Scene::SceneObject o{};
    o.position = pos;
    o.scale = 1.0f;
    o.tint = glm::vec3(1.0f);
    o.spinSpeed = 0.0f;
    o.phase = 0.0f; // SceneObject 的 phase 无默认初始化器，显式清零保证确定性
    o.meshId = meshId;
    o.parentIndex = parentIndex;
    return o;
}

// 场景快照目标：Snapshot/RestoreScene 走真实 EcsScene（与 Application 同构）
struct MockTarget : Game::SceneSnapshotTarget
{
    Scene::EcsScene world;

    [[nodiscard]] Game::SceneSnapshot Snapshot() const override
    {
        Game::SceneSnapshot s;
        s.objects = world.BuildPacket();
        return s;
    }
    void RestoreScene(const Game::SceneSnapshot& snap) override { world.LoadPacket(snap.objects); }
};
} // namespace

// ---------------------------------------------------------------------------
// 面板纯逻辑：树构建 / 防环 / 过滤
// ---------------------------------------------------------------------------

TEST_CASE("Hierarchy.BuildTreeRootsAndChildren")
{
    // 0 根；1、2 挂 0；3 越界父视为根；4 自环视为根；5 挂 3
    std::vector<Scene::SceneObject> objs;
    objs.push_back(MakeObj(glm::vec3(0.0f)));
    objs.push_back(MakeObj(glm::vec3(1.0f), 0, 0));
    objs.push_back(MakeObj(glm::vec3(2.0f), 0, 0));
    objs.push_back(MakeObj(glm::vec3(3.0f), 0, 99)); // 越界父 -> 根
    objs.push_back(MakeObj(glm::vec3(4.0f), 0, 4));  // 自环 -> 根
    objs.push_back(MakeObj(glm::vec3(5.0f), 0, 3));

    const Editor::Hierarchy::Tree tree = Editor::Hierarchy::BuildTree(objs);
    CHECK((tree.roots == std::vector<int>{0, 3, 4})); // 整体加括号：防宏参数被 {} 内逗号拆开
    CHECK((tree.children[0] == std::vector<int>{1, 2})); // 子按稳定序排列
    CHECK((tree.children[3] == std::vector<int>{5}));
    CHECK(tree.children[1].empty());
}

TEST_CASE("Hierarchy.WouldCreateCycleGuards")
{
    // 链：2 -> 1 -> 0（2 的父是 1，1 的父是 0）
    std::vector<Scene::SceneObject> objs;
    objs.push_back(MakeObj(glm::vec3(0.0f)));
    objs.push_back(MakeObj(glm::vec3(1.0f), 0, 0));
    objs.push_back(MakeObj(glm::vec3(2.0f), 0, 1));

    // 祖先拖入自己的子树 -> 成环，必须拒绝
    CHECK(Editor::Hierarchy::WouldCreateCycle(objs, 0, 1)); // 0 拖到 1（自己的子）
    CHECK(Editor::Hierarchy::WouldCreateCycle(objs, 0, 2)); // 0 拖到 2（自己的孙）
    CHECK(Editor::Hierarchy::WouldCreateCycle(objs, 1, 2));
    CHECK(Editor::Hierarchy::WouldCreateCycle(objs, 1, 1)); // 自己做自己的父
    CHECK(Editor::Hierarchy::WouldCreateCycle(objs, 2, 2));

    // 合法方向（子树外的目标 / 挂到根）
    CHECK(!Editor::Hierarchy::WouldCreateCycle(objs, 2, 0)); // 孙拖到根
    CHECK(!Editor::Hierarchy::WouldCreateCycle(objs, 1, -1)); // 挂到根
    CHECK(!Editor::Hierarchy::WouldCreateCycle(objs, 0, -1));
    CHECK(!Editor::Hierarchy::WouldCreateCycle(objs, 5, 0)); // 非法源不误报（上层按范围忽略）
}

TEST_CASE("Hierarchy.FilterVisibleKeepsAncestors")
{
    // 0 根（立方体）<- 1（球）<- 2（glTF）；3 为独立根（立方体）
    std::vector<Scene::SceneObject> objs;
    objs.push_back(MakeObj(glm::vec3(0.0f), 0));
    objs.push_back(MakeObj(glm::vec3(1.0f), 3, 0));
    objs.push_back(MakeObj(glm::vec3(2.0f), 2, 1));
    objs.push_back(MakeObj(glm::vec3(3.0f), 0));
    const Editor::Hierarchy::Tree tree = Editor::Hierarchy::BuildTree(objs);

    // 空过滤：全部可见
    CHECK((Editor::Hierarchy::FilterVisible(objs, tree, "") == std::vector<uint8_t>{1, 1, 1, 1}));
    CHECK((Editor::Hierarchy::FilterVisible(objs, tree, "   ") == std::vector<uint8_t>{1, 1, 1, 1}));

    // 命中深叶（glTF）：祖先链 0、1 保留，无关节点 3 隐藏
    CHECK((Editor::Hierarchy::FilterVisible(objs, tree, "glTF") == std::vector<uint8_t>{1, 1, 1, 0}));

    // 大小写不敏感（ASCII 折叠）
    CHECK((Editor::Hierarchy::FilterVisible(objs, tree, "gltf") == std::vector<uint8_t>{1, 1, 1, 0}));

    // 命中中间节点：其子树与祖先可见，旁支隐藏
    CHECK((Editor::Hierarchy::FilterVisible(objs, tree, "球") == std::vector<uint8_t>{1, 1, 1, 0}));

    // 无命中：全隐藏（UI 显示"无匹配项"）
    CHECK((Editor::Hierarchy::FilterVisible(objs, tree, "不存在") == std::vector<uint8_t>{0, 0, 0, 0}));
}

TEST_CASE("Hierarchy.DisplayName")
{
    std::vector<Scene::SceneObject> objs;
    objs.push_back(MakeObj(glm::vec3(0.0f), 0));
    const Editor::Hierarchy::Tree tree = Editor::Hierarchy::BuildTree(objs);
    CHECK(Editor::Hierarchy::DisplayName(objs, tree, 0) == "立方体 #0");
    CHECK(Editor::Hierarchy::DisplayName(objs, tree, 7) == "?"); // 越界安全
}

// ---------------------------------------------------------------------------
// ECS 数据路径：改父投影 / 删除提升 / 撤销还原
// ---------------------------------------------------------------------------

TEST_CASE("Hierarchy.ReparentProjectsPacket")
{
    // ---- 拖拽改父等价操作：SetParent 后包投影 parentIndex 正确（含父下标高于自身） ----
    Scene::EcsScene world;
    for (int i = 0; i < 3; ++i)
        (void)world.CreateObject(MakeObj(glm::vec3(float(i), 0.0f, 0.0f)));

    // 2 挂到 0（父下标 < 自身，常规情形）
    world.SetParent(2, 0);
    auto packet = world.BuildPacket();
    CHECK(packet[2].parentIndex == 0);
    CHECK(packet[0].parentIndex == -1);

    // 0 挂到 2（父下标 > 自身：编辑器拖拽后包序不再满足"父先于子"约定）。
    // 先把 2 摘到根——2 此时仍挂在 0 下，直接 0->2 会成环，数据层 SetParent 必须拒绝。
    CHECK(!world.SetParent(0, 2)); // 成环拒绝且原状保持
    CHECK(world.BuildPacket()[0].parentIndex == -1);
    CHECK(world.BuildPacket()[2].parentIndex == 0);
    world.SetParent(2, -1);
    world.SetParent(0, 2);
    packet = world.BuildPacket();
    CHECK(packet[0].parentIndex == 2);
    CHECK(packet[2].parentIndex == -1);

    // 包 round-trip（主循环 SyncSceneEdits 路径）不得把"后挂"父级误清为根
    world.SyncFromPacket(packet);
    packet = world.BuildPacket();
    CHECK(packet[0].parentIndex == 2);

    // 拖到空白处：清除父级挂到根
    world.SetParent(0, -1);
    packet = world.BuildPacket();
    CHECK(packet[0].parentIndex == -1);
    CHECK(packet[2].parentIndex == -1);

    // 非法父下标（越界/自环）安全解挂为根
    world.SetParent(1, 42);
    CHECK(world.BuildPacket()[1].parentIndex == -1);
    world.SetParent(1, 1);
    CHECK(world.BuildPacket()[1].parentIndex == -1);
}

TEST_CASE("Hierarchy.DestroyPromotesChildrenToRoot")
{
    // ---- 删除带子树的实体：直接子节点提升为根，孙辈跟随各自父级 ----
    Scene::EcsScene world;
    // 0 <- 1 <- 2（链）
    (void)world.CreateObject(MakeObj(glm::vec3(0.0f), 0, -1));
    (void)world.CreateObject(MakeObj(glm::vec3(1.0f), 0, 0));
    (void)world.CreateObject(MakeObj(glm::vec3(2.0f), 0, 1));

    world.DestroyAt(0); // 删除根：子 1 提升为根，孙 2 仍挂在 1 下
    auto packet = world.BuildPacket();
    REQUIRE(packet.size() == 2);
    CHECK(packet[0].parentIndex == -1); // 旧 1：提升为根
    CHECK(packet[1].parentIndex == 0);  // 旧 2：跟随旧 1（下标收缩后为 0）

    // 删除叶子：其余不变
    world.DestroyAt(1);
    packet = world.BuildPacket();
    REQUIRE(packet.size() == 1);
    CHECK(packet[0].parentIndex == -1);
}

TEST_CASE("Hierarchy.LoadPacketRestoresOutOfOrderParents")
{
    // ---- undo/读档还原：包中父下标 >= 自身（编辑器改父产物）必须逐位还原 ----
    std::vector<Scene::SceneObject> objs;
    objs.push_back(MakeObj(glm::vec3(0.0f), 0, 2)); // 0 挂到 2（父在后）
    objs.push_back(MakeObj(glm::vec3(1.0f), 0, -1));
    objs.push_back(MakeObj(glm::vec3(2.0f), 0, 1)); // 2 挂到 1（父在前）

    Scene::EcsScene world;
    world.LoadPacket(objs);
    const auto packet = world.BuildPacket();
    REQUIRE(packet.size() == 3);
    CHECK(packet[0].parentIndex == 2); // CreateObject 一趟挂不上的，LoadPacket 第二趟兜底
    CHECK(packet[1].parentIndex == -1);
    CHECK(packet[2].parentIndex == 1);
}

TEST_CASE("Hierarchy.ReparentUndoRedoViaCommandStack")
{
    // ---- 撤销栈闭环：改父命令 Undo 恢复原父子关系，Redo 重放 ----
    MockTarget target;
    Scene::EcsScene& world = target.world;
    for (int i = 0; i < 3; ++i)
        (void)world.CreateObject(MakeObj(glm::vec3(float(i), 0.0f, 0.0f)));

    Game::CommandStack stack;
    const Game::SceneSnapshot before = target.Snapshot();
    CHECK(before.objects[2].parentIndex == -1);

    // 编辑器拖拽等价操作：2 -> 0
    world.SetParent(2, 0);
    const Game::SceneSnapshot after = target.Snapshot();
    CHECK(Game::SceneSnapshotsDiffer(before, after)); // 纯父子变化必须被判为差异（否则不会入栈）

    stack.Execute(std::make_unique<Game::SceneSnapshotCommand>(&target, before, after, "改变父子关系"));
    CHECK(target.Snapshot().objects[2].parentIndex == 0);

    stack.Undo(); // 撤销：恢复原父子关系（经 LoadPacket 全量还原）
    CHECK(target.Snapshot().objects[2].parentIndex == -1);
    CHECK(target.Snapshot().objects[0].parentIndex == -1);

    stack.Redo(); // 重做：再次挂接
    CHECK(target.Snapshot().objects[2].parentIndex == 0);

    // 跨"后挂父"状态的撤销还原：先构造 1 -> 2（父下标 2 高于自身 1，不成环），
    // 再改 2 为根；撤销后包中仍含"父下标高于子"的关系，验证 LoadPacket 二趟挂父兜底。
    const Game::SceneSnapshot snapHigh = target.Snapshot();
    world.SetParent(1, 2); // 1 挂到 2（父下标在后）
    const Game::SceneSnapshot afterHigh = target.Snapshot();
    REQUIRE(Game::SceneSnapshotsDiffer(snapHigh, afterHigh));
    stack.Execute(std::make_unique<Game::SceneSnapshotCommand>(&target, snapHigh, afterHigh, "改变父子关系"));
    CHECK(target.Snapshot().objects[1].parentIndex == 2);

    const Game::SceneSnapshot snapDetach = target.Snapshot(); // 含"后挂父"（1->2）的可还原状态
    world.SetParent(2, -1);                                   // 2 脱离为根（1 仍挂在 2 下）
    stack.Execute(std::make_unique<Game::SceneSnapshotCommand>(&target, snapDetach, target.Snapshot(), "改变父子关系"));
    CHECK(target.Snapshot().objects[2].parentIndex == -1);

    stack.Undo(); // 撤销："后挂父"关系（1 -> 2）逐位还原
    CHECK(target.Snapshot().objects[1].parentIndex == 2);
    CHECK(target.Snapshot().objects[2].parentIndex == 0);
}
