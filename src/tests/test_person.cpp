// 1.3 人物宿主测试：Q 版人物 = 球/胶囊 ECS 父子骨骼树（Todo 3）。
//
// 覆盖：
//   - SpawnPerson 生成 9 部件（1 根 + 8 子），父下标挂接正确（head←body、limbs←body）；
//   - 世界矩阵 = 父子级联（经生产 ForEachRenderableWorld 消费路径）；
//   - Update 姿态写回（Standing 微收臂、Walking 摆腿、Squat 前倾降重心）；
//   - ApplyParams 体型变化重建布局、仅外观变化零副作用；
//   - RemovePerson 整组删除不漂移；多人物共存独立；
//   - 失效清理：实体被外部销毁后 Update 自动移除宿主条目。
#include "framework/test_common.h"

#include "scene/EcsScene.h"
#include "scene/PersonHost.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace BigHero;
using namespace BigHero::Scene;

namespace
{
Scene::PersonParams DefaultParams()
{
    Scene::PersonParams p;
    p.position = glm::vec3(1.0f, 0.0f, 2.0f);
    p.height = 1.6f;
    p.pose = Scene::PersonPose::Standing;
    p.poseSpeed = 1.0f;
    return p;
}

int ParentIdx(const EcsScene& s, size_t i)
{
    const Core::Entity e = s.At(i);
    const auto* p = s.Registry().TryGet<ecs::Parent>(e);
    if (p == nullptr || p->parent.IsNull())
        return -1;
    const auto& order = s.Order();
    for (size_t k = 0; k < order.size(); ++k)
        if (order[k].Index() == p->parent.Index())
            return static_cast<int>(k);
    return -1;
}

glm::mat4 LocalOf(const EcsScene& s, size_t i)
{
    const Core::Entity e = s.At(i);
    const auto& t = s.Registry().Get<ecs::Transform>(e);
    const auto& sp = s.Registry().Get<ecs::Spin>(e);
    return ComputeEntityModelMatrix(t, sp.angle);
}

// 父子级联期望世界矩阵。
glm::mat4 ExpectedWorld(const EcsScene& s, std::vector<glm::mat4>& memo, std::vector<char>& done, size_t i)
{
    if (done[i])
        return memo[i];
    const glm::mat4 local = LocalOf(s, i);
    const int p = ParentIdx(s, i);
    memo[i] = (p < 0) ? local : ExpectedWorld(s, memo, done, static_cast<size_t>(p)) * local;
    done[i] = 1;
    return memo[i];
}

bool MatEqual(const glm::mat4& a, const glm::mat4& b)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-3f)
                return false;
    return true;
}

std::vector<glm::mat4> CollectWorld(const EcsScene& s)
{
    std::vector<glm::mat4> out;
    out.reserve(s.ObjectCount());
    s.ForEachRenderableWorld([&](const ecs::Transform&, const ecs::Renderable&, const ecs::Spin&, const glm::mat4& w)
                             { out.push_back(w); });
    return out;
}

bool AnyNaN(const EcsScene& s)
{
    bool bad = false;
    s.ForEachRenderableWorld(
        [&](const ecs::Transform&, const ecs::Renderable&, const ecs::Spin&, const glm::mat4& w)
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(w[c][r]))
                        bad = true;
        });
    return bad;
}
} // namespace

TEST_CASE("PersonHost.SpawnBuildsBoneTree")
{
    EcsScene s;
    PersonHost h(s);
    const int idx = h.SpawnPerson(DefaultParams());
    CHECK(idx == 0);
    CHECK(h.Count() == 1);
    CHECK(s.ObjectCount() == PersonHost::kPartCount); // 全新场景：正好 9 部件

    // 根：无父；其余部件父关系按布局（head/hair/eyes←head，arms/legs←body）
    const int body = h.RootOrderIndex(0);
    CHECK(body == 0);
    CHECK(ParentIdx(s, 0) == -1);
    CHECK(ParentIdx(s, body + PersonHost::kHead) == body);
    CHECK(ParentIdx(s, body + PersonHost::kHair) == body + PersonHost::kHead);
    CHECK(ParentIdx(s, body + PersonHost::kEyeL) == body + PersonHost::kHead);
    CHECK(ParentIdx(s, body + PersonHost::kEyeR) == body + PersonHost::kHead);
    CHECK(ParentIdx(s, body + PersonHost::kArmL) == body);
    CHECK(ParentIdx(s, body + PersonHost::kArmR) == body);
    CHECK(ParentIdx(s, body + PersonHost::kLegL) == body);
    CHECK(ParentIdx(s, body + PersonHost::kLegR) == body);

    // 网格类型：根=胶囊(4)，头/发/眼=球(3)，臂/腿=胶囊(4)
    const auto meshIdOf = [&](size_t i) { return s.Registry().Get<ecs::Renderable>(s.At(i)).meshId; };
    CHECK(meshIdOf(static_cast<size_t>(body)) == 4);
    CHECK(meshIdOf(static_cast<size_t>(body + PersonHost::kHead)) == 3);
    CHECK(meshIdOf(static_cast<size_t>(body + PersonHost::kLegR)) == 4);

    // PartOrderIndices 与稳定序一致
    const std::vector<int> parts = h.PartOrderIndices(0);
    CHECK(parts.size() == PersonHost::kPartCount);
    for (size_t i = 0; i < parts.size(); ++i)
        CHECK(parts[i] == static_cast<int>(i));
}

TEST_CASE("PersonHost.WorldMatrixCascades")
{
    EcsScene s;
    PersonHost h(s);
    (void)h.SpawnPerson(DefaultParams());
    h.Update(0.1f);

    std::vector<glm::mat4> memo(s.ObjectCount(), glm::mat4(1.0f));
    std::vector<char> done(s.ObjectCount(), 0);
    const std::vector<glm::mat4> got = CollectWorld(s);
    CHECK(got.size() == s.ObjectCount());
    for (size_t i = 0; i < s.ObjectCount(); ++i)
        CHECK(MatEqual(got[i], ExpectedWorld(s, memo, done, i)));
    CHECK(!AnyNaN(s));
}

TEST_CASE("PersonHost.PosesWriteBack")
{
    EcsScene s;
    PersonHost h(s);

    // 站立：微收臂（-4°），根不转
    Scene::PersonParams p = DefaultParams();
    (void)h.SpawnPerson(p);
    h.Update(0.016f);
    const size_t body = 0;
    glm::vec3 rot = s.Registry().Get<ecs::Transform>(s.At(body)).rotation;
    CHECK(std::fabs(rot.x) < 1e-3f); // 站立根不倾
    glm::vec3 armRot = s.Registry().Get<ecs::Transform>(s.At(body + PersonHost::kArmL)).rotation;
    CHECK(std::fabs(armRot.x - (-4.0f)) < 1e-3f);

    // 行走：摆腿交替（legL +phase → legR -phase）
    h.ApplyParams(0, p); // 保持体型走行走
    p.pose = Scene::PersonPose::Walking;
    h.ApplyParams(0, p);
    h.Update(0.3f);
    const glm::vec3 l1 = s.Registry().Get<ecs::Transform>(s.At(body + PersonHost::kLegL)).rotation;
    const glm::vec3 r1 = s.Registry().Get<ecs::Transform>(s.At(body + PersonHost::kLegR)).rotation;
    CHECK(std::fabs(l1.x + r1.x) < 1e-3f); // 对侧
    CHECK(std::fabs(l1.x) > 10.0f);        // 确实在摆

    // 蹲坐：根前倾 + 重心下移（根 y 比站立时低），legs 前屈
    p.pose = Scene::PersonPose::Squat;
    h.ApplyParams(0, p);
    const float yStanding = s.Registry().Get<ecs::Transform>(s.At(body)).position.y;
    for (int i = 0; i < 30; ++i)
        h.Update(0.1f); // 让缓动收敛
    const float ySquat = s.Registry().Get<ecs::Transform>(s.At(body)).position.y;
    CHECK(ySquat < yStanding - 0.1f);
    const glm::vec3 legRot = s.Registry().Get<ecs::Transform>(s.At(body + PersonHost::kLegL)).rotation;
    CHECK(legRot.x > 40.0f);
}

TEST_CASE("PersonHost.ApplyParamsScaleOnlyRebuilds")
{
    EcsScene s;
    PersonHost h(s);
    Scene::PersonParams p = DefaultParams();
    (void)h.SpawnPerson(p);
    const float bodyScale0 = s.Registry().Get<ecs::Transform>(s.At(0)).scale;
    const glm::vec3 headPos0 = s.Registry().Get<ecs::Transform>(s.At(PersonHost::kHead)).position;

    // 仅外观变化：布局不动
    Scene::PersonParams q = p;
    q.clothTint = glm::vec3(0.9f, 0.1f, 0.1f);
    q.pose = Scene::PersonPose::Wave;
    h.ApplyParams(0, q);
    CHECK(std::fabs(s.Registry().Get<ecs::Transform>(s.At(0)).scale - bodyScale0) < 1e-6f);
    CHECK(glm::length(s.Registry().Get<ecs::Transform>(s.At(PersonHost::kHead)).position - headPos0) < 1e-6f);

    // 体型变化：布局重建（head 位置、body scale 都变）
    Scene::PersonParams r = q;
    r.height = 2.1f;
    h.ApplyParams(0, r);
    CHECK(s.Registry().Get<ecs::Transform>(s.At(0)).scale > bodyScale0);
    CHECK(glm::length(s.Registry().Get<ecs::Transform>(s.At(PersonHost::kHead)).position - headPos0) > 0.05f);
    CHECK(!AnyNaN(s));
}

TEST_CASE("PersonHost.RemovePersonNoDrift")
{
    EcsScene s;
    PersonHost h(s);
    (void)h.SpawnPerson(DefaultParams()); // person 0 → 稳定序 [0..8]
    Scene::PersonParams p = DefaultParams();
    p.position = glm::vec3(5.0f, 0.0f, 5.0f);
    (void)h.SpawnPerson(p); // person 1 → 稳定序 [9..17]

    h.RemovePerson(0);
    CHECK(h.Count() == 1);
    CHECK(s.ObjectCount() == PersonHost::kPartCount); // 只剩一个人物
    // 剩余人物部件仍完整（原 person 1 的下标漂移后依然为 0..8）
    const std::vector<int> parts = h.PartOrderIndices(0);
    CHECK(parts.size() == PersonHost::kPartCount);
    // 根位置未被误删/错配：仍在 (5, y, 5) 附近
    const glm::vec3 rootPos = s.Registry().Get<ecs::Transform>(s.At(0)).position;
    CHECK(std::fabs(rootPos.x - 5.0f) < 1e-3f);
    CHECK(std::fabs(rootPos.z - 5.0f) < 1e-3f);
    h.Update(0.016f); // 清理扫描不误删
    CHECK(h.Count() == 1);
    const auto parts2 = h.PartOrderIndices(0);
    CHECK(parts2 == parts);
}

TEST_CASE("PersonHost.StaleEntityCleanup")
{
    EcsScene s;
    PersonHost h(s);
    (void)h.SpawnPerson(DefaultParams());
    CHECK(h.Count() == 1);
    // 外部销毁全部实体（模拟读档重建）→ Update 自动移除宿主条目
    while (s.ObjectCount() > 0)
        s.DestroyAt(s.ObjectCount() - 1);
    CHECK(h.Count() == 1); // 销毁本身不通知宿主
    h.Update(0.016f);
    CHECK(h.Count() == 0);
    (void)h.SpawnPerson(DefaultParams()); // 清除后可重新生成
    CHECK(h.Count() == 1);
    h.Update(0.016f);
    CHECK(h.Count() == 1);
    CHECK(!AnyNaN(s));
}