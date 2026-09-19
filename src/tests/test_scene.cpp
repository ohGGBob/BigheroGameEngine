// 场景与编辑器（默认场景 / 网格布局 / 变换层级 / Gizmo / 模型矩阵 / 场景序列化）单元测试。
// 2026-09-04 测试工程化重构：由单体 test_main.cpp 拆分而来，每个原分区封装为独立 TEST_CASE。
// 2026-09-19 追加 OrbitCamera 穿地解析约束与 pitch 输入域回归（UPGRADE_PLAN P1-11 / 第二轮评估 D10）。
#include "editor/Gizmo.h"
#include "framework/test_common.h"
#include "render/InstanceBuffer.h"
#include "scene/Camera.h"
#include "scene/CubeMesh.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/Transform.h"

#include <algorithm>

using namespace BigHero;

TEST_CASE("Scene.DefaultScene")
{
    // ---- 场景 ----
    const std::vector<Scene::SceneObject> scene = Scene::BuildDefaultScene();
    CHECK(scene.size() == 6);
    CHECK(scene.front().meshId == 0); // 首个为共享立方体
    CHECK(scene.back().meshId == 1);  // 末个为外部圆环体
    for (const Scene::SceneObject& obj : scene)
        CHECK(obj.scale > 0.0f);
}

TEST_CASE("Scene.MeshLayout")
{
    // ---- 网格顶点/索引布局 ----
    const std::vector<Scene::Vertex> cube = Scene::BuildCubeVertices();
    CHECK(cube.size() == Scene::kCubeVertexCount);
    const std::vector<Scene::Vertex> all = Scene::BuildSceneVertices();
    CHECK(all.size() == Scene::kCubeVertexCount + 4); // 立方体 + 4 顶点地面
    const std::vector<uint32_t> indices = Scene::BuildSceneIndices();
    CHECK(indices.size() == Scene::kCubeIndexCount + Scene::kGroundIndexCount);
    CHECK(Scene::kCubeIndexCount == 36);
    CHECK(Scene::kGroundIndexCount == 6);

    // 顶点输入步幅必须与结构大小一致，否则 GPU 读取错位
    CHECK(Scene::Vertex::getBindingDesc().stride == sizeof(Scene::Vertex));
    {
        const auto attrs = Scene::Vertex::getAttrDesc();
        CHECK(attrs.size() == 5);
        CHECK(attrs[0].location == 0);
        CHECK(attrs[4].location == 4);
    }
}

TEST_CASE("Scene.TransformHierarchy")
{
    // ---- 变换层级（Transform Hierarchy，纯CPU TRS + 父级级联） ----
    {
        using namespace Scene;

        // 1) TRS 矩阵：平移+旋转+缩放
        Transform leaf;
        leaf.translation = glm::vec3(2.0f, 0.0f, 0.0f);
        leaf.rotation = RotationEulerDeg(0.0f, 90.0f, 0.0f); // 绕Y转90°：+X轴->+Z轴
        leaf.scale = glm::vec3(2.0f, 2.0f, 2.0f);
        const glm::mat4 m = LocalToMatrix(leaf);
        // 原点(0,0,0)经 S(2) -> R(90°) -> T(2,0,0) => (2,0,0)
        const glm::vec4 origin = m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(glm::distance(glm::vec3(origin), glm::vec3(2.0f, 0.0f, 0.0f)) < 1e-4f);
        // 局部 +X 方向(1,0,0) 先缩放2 -> (2,0,0) 再绕Y90° -> (0,0,-2)（Z轴朝相机）
        const glm::vec4 xAxis = m * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        CHECK(glm::distance(glm::vec3(xAxis), glm::vec3(0.0f, 0.0f, -2.0f)) < 1e-4f);

        // 2) 层级：root(parent=-1) + child(parent=0)
        std::vector<Transform> nodes(2);
        nodes[0].translation = glm::vec3(10.0f, 0.0f, 0.0f); // 父世界位置
        nodes[0].parent = Transform::kNoParent;
        nodes[1].translation = glm::vec3(0.0f, 5.0f, 0.0f); // 子局部偏移
        nodes[1].parent = 0;
        const glm::vec3 childWorld = WorldPosition(nodes[1], nodes);
        CHECK(glm::distance(childWorld, glm::vec3(10.0f, 5.0f, 0.0f)) < 1e-4f);
        // 父矩阵即自身局部矩阵（根）
        CHECK(glm::distance(glm::vec3(LocalToWorldMatrix(nodes[0], nodes)[3]), glm::vec3(10.0f, 0.0f, 0.0f)) < 1e-4f);

        // 3) 带旋转的父级：子局部偏移受父旋转影响
        std::vector<Transform> rotNodes(2);
        rotNodes[0].rotation = RotationEulerDeg(0.0f, 90.0f, 0.0f); // 父绕Y90°
        rotNodes[0].parent = Transform::kNoParent;
        rotNodes[1].translation = glm::vec3(1.0f, 0.0f, 0.0f); // 子沿父局部+X
        rotNodes[1].parent = 0;
        // 父局部+X 经父旋转90° -> 世界-Z，故子世界位置=(0,0,-1)
        const glm::vec3 rw = WorldPosition(rotNodes[1], rotNodes);
        CHECK(glm::distance(rw, glm::vec3(0.0f, 0.0f, -1.0f)) < 1e-4f);

        // 4) 世界 AABB：原点居中单位立方体 2x 缩放 + 平移到 (1,1,1) => [(0,0,0),(2,2,2)]
        Transform aabbNode;
        aabbNode.translation = glm::vec3(1.0f);
        aabbNode.scale = glm::vec3(2.0f);
        const auto aabb = WorldAabb(aabbNode, std::vector<Transform>{aabbNode}, glm::vec3(-0.5f), glm::vec3(0.5f));
        CHECK(glm::distance(aabb[0], glm::vec3(0.0f, 0.0f, 0.0f)) < 1e-4f);
        CHECK(glm::distance(aabb[1], glm::vec3(2.0f, 2.0f, 2.0f)) < 1e-4f);

        // 5) 非均匀缩放下的世界 AABB（x方向拉长，盒子保守包含8角点）
        Transform scaleNode;
        scaleNode.scale = glm::vec3(4.0f, 1.0f, 1.0f);
        const auto saabb = WorldAabb(LocalToMatrix(scaleNode), glm::vec3(-0.5f), glm::vec3(0.5f));
        CHECK(glm::distance(saabb[0], glm::vec3(-2.0f, -0.5f, -0.5f)) < 1e-4f);
        CHECK(glm::distance(saabb[1], glm::vec3(2.0f, 0.5f, 0.5f)) < 1e-4f);
    }
}

TEST_CASE("Editor.Gizmo")
{
    // ---- 编辑器 Gizmo 纯逻辑（屏幕空间投影/轴拾取/平移/旋转，无 GPU 依赖） ----
    {
        using namespace BigHero::Editor;

        // 1) 投影：identity 视投影下，世界原点投影到视口中心；+X/+Y 边界对应视口边角。
        {
            const glm::mat4 vp = glm::mat4(1.0f);
            const glm::vec2 view(800.0f, 600.0f);
            const glm::vec2 o = ProjectWorldToScreen(glm::vec3(0.0f), vp, view);
            CHECK(std::fabs(o.x - 400.0f) < 1e-3f);
            CHECK(std::fabs(o.y - 300.0f) < 1e-3f);
            // 世界 +X(1,0,0) -> NDC(1,0) -> 屏幕右中 (800,300)
            const glm::vec2 xTip = ProjectWorldToScreen(glm::vec3(1.0f, 0.0f, 0.0f), vp, view);
            CHECK(std::fabs(xTip.x - 800.0f) < 1e-3f);
            CHECK(std::fabs(xTip.y - 300.0f) < 1e-3f);
            // 世界 +Y(0,1,0) -> NDC(0,1) -> 屏幕上中 (400,0)
            const glm::vec2 yTip = ProjectWorldToScreen(glm::vec3(0.0f, 1.0f, 0.0f), vp, view);
            CHECK(std::fabs(yTip.x - 400.0f) < 1e-3f);
            CHECK(std::fabs(yTip.y - 0.0f) < 1e-3f);
        }

        // 2) 投影：相机后方的点返回无效标记 (-1e9,-1e9)
        {
            const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
            const glm::mat4 view =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec2 vp(800.0f, 600.0f);
            // 世界 (0,0,10) 在相机（位于 z=5 看向 -z）后方
            const glm::vec2 behind = ProjectWorldToScreen(glm::vec3(0.0f, 0.0f, 10.0f), proj * view, vp);
            CHECK(behind.x < -1e8f && behind.y < -1e8f);
        }

        // 3) 轴拾取：一般偏置视角下三轴屏幕方向明显分离、互不退化。
        //    把鼠标放在某轴投影端点（远离原点，避开三轴在原点交叉），应拾取该轴。
        {
            const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 800.0f / 600.0f, 0.1f, 100.0f);
            const glm::mat4 view =
                glm::lookAt(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 vp = proj * view;
            const glm::vec2 vpp(800.0f, 600.0f);
            const glm::vec3 origin(0.0f);
            const glm::vec2 o = ProjectWorldToScreen(origin, vp, vpp);
            CHECK(o.x > -1e8f);
            const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
            for (int a = 0; a < 3; ++a)
            {
                const glm::vec2 tip = ProjectWorldToScreen(origin + GizmoAxisVector(axes[a]), vp, vpp);
                CHECK(tip.x > -1e8f);
                // 鼠标精确落在自己轴的投影端点上（armPx 足够大以覆盖整条轴）
                const GizmoAxis picked = PickAxis(origin, vp, vpp, tip, 12.0f, 10000.0f);
                CHECK(picked == axes[a]);
            }
            // 视口左上角远离所有轴 -> 无拾取
            const GizmoAxis none = PickAxis(origin, vp, vpp, glm::vec2(2.0f, 2.0f), 12.0f, 10000.0f);
            CHECK(none == GizmoAxis::None);
        }

        // 4) 平移拖拽：identity 下沿 +屏幕X 拖拽应产生正向世界位移（符号正确，数值合理）
        {
            const glm::mat4 vp = glm::mat4(1.0f);
            const glm::vec2 view(800.0f, 600.0f);
            // X 轴屏幕方向 (1,0)：pxPerUnit = 400（原点(400,300)->X尖(800,300)）
            const float dpos = TranslateDragDelta(glm::vec3(0.0f), GizmoAxis::X, vp, view, glm::vec2(10.0f, 0.0f));
            CHECK(dpos > 0.0f); // 向右拖 -> 沿 +X 正向
            const float dneg = TranslateDragDelta(glm::vec3(0.0f), GizmoAxis::X, vp, view, glm::vec2(-10.0f, 0.0f));
            CHECK(dneg < 0.0f); // 向左拖 -> 反向
            const float dperp = TranslateDragDelta(glm::vec3(0.0f), GizmoAxis::X, vp, view, glm::vec2(0.0f, 10.0f));
            CHECK(std::fabs(dperp) < 1e-4f);         // 垂直方向无分量 -> 0
            CHECK(std::fabs(dpos - 0.025f) < 1e-3f); // 10px / 400pxPerUnit
        }

        // 5) 旋转拖拽：90° 扫角应得 ±π/2；从->到反向符号翻转；退化（起点=中心）为 0
        {
            const float kHalfPi = 1.5707963f;
            const glm::vec2 c(400.0f, 300.0f);
            const glm::vec2 from(400.0f, 200.0f); // 中心上方
            const glm::vec2 to(500.0f, 300.0f);   // 中心右方
            const float ang = RotateDragAngle(c, from, to);
            CHECK(std::fabs(ang - kHalfPi) < 1e-3f);
            const float angRev = RotateDragAngle(c, to, from);
            CHECK(angRev < 0.0f);
            CHECK(std::fabs(angRev + kHalfPi) < 1e-3f);
            CHECK(std::fabs(RotateDragAngle(c, c, to)) < 1e-5f);
        }
    }
}

TEST_CASE("Scene.ModelMatrix")
{
    // ---- 模型矩阵计算（纯数学，实例填充与阴影绘制共用） ----
    {
        using namespace BigHero::Scene;

        // 单位物体：零位移、无旋转、缩放1、无自转 → 单位矩阵
        SceneObject identity{};
        identity.position = glm::vec3(0.0f);
        identity.scale = 1.0f;
        identity.rotation = glm::vec3(0.0f);
        const glm::mat4 m = ComputeObjectModelMatrix(identity, 0.0f);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                CHECK(std::fabs(m[i][j] - (i == j ? 1.0f : 0.0f)) < 1e-5f);

        // 平移 (3,4,5) → 第4列（列主序 m[3]）
        SceneObject translated{};
        translated.position = glm::vec3(3.0f, 4.0f, 5.0f);
        translated.scale = 1.0f;
        const glm::mat4 mt = ComputeObjectModelMatrix(translated, 0.0f);
        CHECK(std::fabs(mt[3][0] - 3.0f) < 1e-5f);
        CHECK(std::fabs(mt[3][1] - 4.0f) < 1e-5f);
        CHECK(std::fabs(mt[3][2] - 5.0f) < 1e-5f);

        // 缩放 scale=2 → 对角线
        SceneObject scaled{};
        scaled.scale = 2.0f;
        const glm::mat4 ms = ComputeObjectModelMatrix(scaled, 0.0f);
        CHECK(std::fabs(ms[0][0] - 2.0f) < 1e-5f);
        CHECK(std::fabs(ms[1][1] - 2.0f) < 1e-5f);
        CHECK(std::fabs(ms[2][2] - 2.0f) < 1e-5f);

        // 绕Y自转90°：x轴(1,0,0)→(0,0,-1)，列主序第一列 m[0]
        SceneObject rotated{};
        rotated.scale = 1.0f;
        const glm::mat4 mr = ComputeObjectModelMatrix(rotated, 90.0f);
        CHECK(std::fabs(mr[0][0] - 0.0f) < 1e-5f);
        CHECK(std::fabs(mr[0][2] - (-1.0f)) < 1e-5f);
        CHECK(std::fabs(mr[2][0] - 1.0f) < 1e-5f);
        CHECK(std::fabs(mr[2][2] - 0.0f) < 1e-5f);
    }
}

TEST_CASE("Scene.Serialization")
{
    // ---- 场景序列化（JSON 往返一致性） ----
    {
        using namespace BigHero::Scene;

        // 构造一个非平凡场景
        SceneData original;
        original.version = 1;
        original.cameraFov = 75.0f;
        original.light.direction = glm::vec3(0.3f, -1.0f, -0.5f);
        original.light.color = glm::vec3(1.0f, 0.9f, 0.8f);
        original.light.intensity = 4.5f;
        original.light.ambient = 0.2f;
        original.light.shadowStrength = 0.8f;
        original.light.shadowBias = 0.003f;
        original.light.iblStrength = 1.2f;
        original.light.exposure = 1.5f;

        SerializablePointLight pl0;
        pl0.position = glm::vec3(1.0f, 2.0f, 3.0f);
        pl0.color = glm::vec3(1.0f, 0.5f, 0.2f);
        pl0.intensity = 50.0f;
        pl0.radius = 12.0f;
        pl0.castsShadow = true;
        original.pointLights.push_back(pl0);

        SceneObject obj0;
        obj0.position = glm::vec3(1.5f, 0.5f, -2.0f);
        obj0.scale = 1.3f;
        obj0.tint = glm::vec3(0.8f, 0.6f, 0.4f);
        obj0.spinSpeed = 45.0f;
        obj0.phase = 30.0f;
        obj0.meshId = 0;
        obj0.metallic = 0.7f;
        obj0.roughness = 0.3f;
        obj0.rotation = glm::vec3(10.0f, 20.0f, 30.0f);
        original.objects.push_back(obj0);

        SceneObject obj1;
        obj1.position = glm::vec3(-1.0f, 1.0f, 1.0f);
        obj1.scale = 2.0f;
        obj1.tint = glm::vec3(0.2f, 0.8f, 0.9f);
        obj1.spinSpeed = -20.0f;
        obj1.phase = 0.0f;
        obj1.meshId = 1;
        obj1.metallic = 0.0f;
        obj1.roughness = 0.9f;
        obj1.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
        original.objects.push_back(obj1);

        // 序列化 → 反序列化 → 逐字段比较
        const std::string json = SerializeScene(original);
        CHECK(!json.empty());
        CHECK(json.find("\"version\"") != std::string::npos);
        CHECK(json.find("\"objects\"") != std::string::npos);
        CHECK(json.find("\"pointLights\"") != std::string::npos);

        SceneData loaded;
        const bool ok = DeserializeScene(json, loaded);
        CHECK(ok);
        CHECK(loaded.version == 1);
        CHECK(std::fabs(loaded.cameraFov - 75.0f) < 1e-4f);
        CHECK(loaded.objects.size() == 2);
        CHECK(loaded.pointLights.size() == 1);

        // 方向光字段
        CHECK(std::fabs(loaded.light.direction.x - 0.3f) < 1e-4f);
        CHECK(std::fabs(loaded.light.direction.y - (-1.0f)) < 1e-4f);
        CHECK(std::fabs(loaded.light.intensity - 4.5f) < 1e-4f);
        CHECK(std::fabs(loaded.light.ambient - 0.2f) < 1e-4f);
        CHECK(std::fabs(loaded.light.shadowStrength - 0.8f) < 1e-4f);
        CHECK(std::fabs(loaded.light.shadowBias - 0.003f) < 1e-4f);
        CHECK(std::fabs(loaded.light.iblStrength - 1.2f) < 1e-4f);
        CHECK(std::fabs(loaded.light.exposure - 1.5f) < 1e-4f);

        // 点光源字段
        CHECK(std::fabs(loaded.pointLights[0].position.x - 1.0f) < 1e-4f);
        CHECK(std::fabs(loaded.pointLights[0].position.y - 2.0f) < 1e-4f);
        CHECK(std::fabs(loaded.pointLights[0].position.z - 3.0f) < 1e-4f);
        CHECK(std::fabs(loaded.pointLights[0].intensity - 50.0f) < 1e-4f);
        CHECK(std::fabs(loaded.pointLights[0].radius - 12.0f) < 1e-4f);
        CHECK(loaded.pointLights[0].castsShadow == true);

        // 物体 0 字段
        CHECK(std::fabs(loaded.objects[0].position.x - 1.5f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].position.y - 0.5f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].position.z - (-2.0f)) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].scale - 1.3f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].tint.r - 0.8f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].spinSpeed - 45.0f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].phase - 30.0f) < 1e-4f);
        CHECK(loaded.objects[0].meshId == 0);
        CHECK(std::fabs(loaded.objects[0].metallic - 0.7f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].roughness - 0.3f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].rotation.x - 10.0f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].rotation.y - 20.0f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[0].rotation.z - 30.0f) < 1e-4f);

        // 物体 1 字段
        CHECK(std::fabs(loaded.objects[1].position.x - (-1.0f)) < 1e-4f);
        CHECK(std::fabs(loaded.objects[1].scale - 2.0f) < 1e-4f);
        CHECK(loaded.objects[1].meshId == 1);
        CHECK(std::fabs(loaded.objects[1].metallic - 0.0f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[1].roughness - 0.9f) < 1e-4f);
        CHECK(std::fabs(loaded.objects[1].rotation.y - 90.0f) < 1e-4f);

        // 空场景往返
        SceneData empty;
        const std::string emptyJson = SerializeScene(empty);
        SceneData emptyLoaded;
        CHECK(DeserializeScene(emptyJson, emptyLoaded));
        CHECK(emptyLoaded.objects.empty());
        CHECK(emptyLoaded.pointLights.empty());

        // 非法 JSON 应返回 false 而非崩溃
        CHECK(!DeserializeScene("not json at all", emptyLoaded));
        CHECK(!DeserializeScene("{\"objects\": [", emptyLoaded));

        // ---- MsgPack 往返一致性（引擎实际使用的二进制格式） ----
        const std::vector<uint8_t> bin = SerializeSceneToMsgPack(original);
        CHECK(!bin.empty());

        SceneData mpLoaded;
        CHECK(DeserializeSceneFromMsgPack(bin, mpLoaded));
        CHECK(mpLoaded.version == 1);
        CHECK(std::fabs(mpLoaded.cameraFov - 75.0f) < 1e-4f);
        CHECK(mpLoaded.objects.size() == 2);
        CHECK(mpLoaded.pointLights.size() == 1);
        CHECK(std::fabs(mpLoaded.light.intensity - 4.5f) < 1e-4f);
        CHECK(std::fabs(mpLoaded.objects[0].metallic - 0.7f) < 1e-4f);
        CHECK(mpLoaded.pointLights[0].castsShadow == true);

        // 损坏 MsgPack 应返回 false 而非崩溃
        std::vector<uint8_t> corrupted = bin;
        std::fill(corrupted.begin(), corrupted.end(), uint8_t{0xAB});
        CHECK(!DeserializeSceneFromMsgPack(corrupted, mpLoaded));
        std::vector<uint8_t> truncated(bin.begin(), bin.begin() + bin.size() / 2);
        CHECK(!DeserializeSceneFromMsgPack(truncated, mpLoaded));
        CHECK(!DeserializeSceneFromMsgPack({}, mpLoaded));

        // ---- 版本门禁：未来版本文件显式拒绝（JSON 与 MsgPack 双路径） ----
        {
            SceneData future = original;
            future.version = kCurrentSceneVersion + 1;
            SceneData rejected;
            CHECK(!DeserializeScene(SerializeScene(future), rejected));
            CHECK(!DeserializeSceneFromMsgPack(SerializeSceneToMsgPack(future), rejected));
        }
    }
}

TEST_CASE("Scene.TransformBatch")
{
    // ---- 批量世界矩阵 ComputeAllWorldMatrices（单趟 O(n)，支持任意存储顺序） ----
    using namespace BigHero::Scene;

    // 1) 与逐节点递归 LocalToWorldMatrix 结果一致（含任意顺序：父在子之后存储）
    {
        // 层级：0(根) -> 1 -> 2；3 为独立根；4 的父是 3（父存储在子之后，打乱顺序）。
        std::vector<Transform> ts(5);
        ts[0].translation = glm::vec3(1.0f, 0.0f, 0.0f);
        ts[0].rotation = RotationEulerDeg(0.0f, 90.0f, 0.0f);
        ts[1].translation = glm::vec3(0.0f, 2.0f, 0.0f);
        ts[1].parent = 0;
        ts[2].translation = glm::vec3(0.0f, 0.0f, 3.0f);
        ts[2].scale = glm::vec3(2.0f);
        ts[2].parent = 1;
        ts[3].translation = glm::vec3(5.0f, 0.0f, 0.0f); // 独立根
        ts[4].translation = glm::vec3(0.0f, 1.0f, 0.0f);
        ts[4].parent = 3; // 父节点存储在子之后

        const std::vector<glm::mat4> batch = ComputeAllWorldMatrices(ts);
        CHECK(batch.size() == ts.size());
        for (size_t i = 0; i < ts.size(); ++i)
        {
            const glm::mat4 ref = LocalToWorldMatrix(ts[i], ts);
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    CHECK(std::fabs(batch[i][c][r] - ref[c][r]) < 1e-4f);
        }
    }

    // 2) 空数组与全根数组
    {
        const std::vector<Transform> empty;
        CHECK(ComputeAllWorldMatrices(empty).empty());

        std::vector<Transform> roots(3);
        roots[0].translation = glm::vec3(1.0f, 0.0f, 0.0f);
        roots[1].translation = glm::vec3(0.0f, 2.0f, 0.0f);
        roots[2].translation = glm::vec3(0.0f, 0.0f, 3.0f);
        const std::vector<glm::mat4> w = ComputeAllWorldMatrices(roots);
        CHECK(w.size() == 3);
        CHECK(glm::distance(glm::vec3(w[1][3]), glm::vec3(0.0f, 2.0f, 0.0f)) < 1e-4f);
    }

    // 3) 悬空父索引安全回退（不越界、不崩溃），退化为局部矩阵
    {
        std::vector<Transform> ts(1);
        ts[0].translation = glm::vec3(7.0f, 8.0f, 9.0f);
        ts[0].parent = 99; // 越界父索引
        const std::vector<glm::mat4> w = ComputeAllWorldMatrices(ts);
        CHECK(w.size() == 1);
        CHECK(glm::distance(glm::vec3(w[0][3]), glm::vec3(7.0f, 8.0f, 9.0f)) < 1e-4f);
    }

    // 4) 深层链：100 级层级一次求值正确（批量接口避免 O(n^2) 递归开销）
    {
        constexpr int kDepth = 100;
        std::vector<Transform> ts(kDepth);
        for (int i = 0; i < kDepth; ++i)
        {
            ts[i].translation = glm::vec3(0.0f, 1.0f, 0.0f); // 每级 +1 Y
            ts[i].parent = (i == 0) ? Transform::kNoParent : (i - 1);
        }
        const std::vector<glm::mat4> w = ComputeAllWorldMatrices(ts);
        CHECK(w.size() == static_cast<size_t>(kDepth));
        CHECK(std::fabs(glm::vec3(w[kDepth - 1][3]).y - static_cast<float>(kDepth)) < 1e-3f);
    }
}

// ============================================================================
// OrbitCamera（src/scene/Camera.h）：pitch 输入域恢复 + minEyeY 穿地解析约束
// （UPGRADE_PLAN P1-11 / 第二轮评估 D10 治本）。纯逻辑，无 GPU 依赖。
// ============================================================================

namespace
{
// 旧实现参考（c1433f5 收窄输入域之前的球面定位公式）：约束未触发时必须与它一致（回归守卫）
glm::vec3 ReferenceOrbitEye(const glm::vec3& target, float yaw, float pitch, float distance)
{
    const float cosPitch = std::cos(pitch);
    return target + distance * glm::vec3(cosPitch * std::sin(yaw), std::sin(pitch), cosPitch * std::cos(yaw));
}
} // namespace

TEST_CASE("OrbitCamera.NegativePitchDomain")
{
    // pitch 下限恢复 -1.4（约-80°）：大幅向下拖拽可越过水平线（旧实现被夹到 0）
    OrbitCamera cam;
    cam.Orbit(0.0f, 10000.0f);
    CHECK_NEAR(cam.Pitch(), OrbitCamera::kPitchMin, 1e-6f);
    CHECK_NEAR(OrbitCamera::kPitchMin, -1.4f, 1e-6f);

    // 上限不变：1.53（约88°）
    OrbitCamera camUp;
    camUp.Orbit(0.0f, -10000.0f);
    CHECK_NEAR(camUp.Pitch(), OrbitCamera::kPitchMax, 1e-6f);
    CHECK_NEAR(OrbitCamera::kPitchMax, 1.53f, 1e-6f);

    // SetPitch 输入域同步恢复：负值可写入，越界被夹取
    OrbitCamera camSet;
    camSet.SetPitch(-1.2f);
    CHECK_NEAR(camSet.Pitch(), -1.2f, 1e-6f);
    camSet.SetPitch(-5.0f);
    CHECK_NEAR(camSet.Pitch(), OrbitCamera::kPitchMin, 1e-6f);
    camSet.SetPitch(2.0f);
    CHECK_NEAR(camSet.Pitch(), OrbitCamera::kPitchMax, 1e-6f);
}

TEST_CASE("OrbitCamera.GroundConstraint")
{
    // ① pitch=-80° 时眼点被约束抬升，不低于 minEyeY（默认 0.05，即地面 y=0 + 余量）
    CHECK_NEAR(OrbitCamera{}.GetMinEyeY(), 0.05f, 1e-7f);

    OrbitCamera cam; // 默认 target=(0,0.5,0)、distance=7
    cam.Orbit(0.0f, 10000.0f);
    const float rawPitch = cam.Pitch(); // Update 前的原始输入域 pitch（-1.4）
    REQUIRE(rawPitch < -1.0f);
    // 约束前按旧公式会钻入地面：0.5 + 7·sin(-1.4) ≈ -6.40
    CHECK_LT(0.5f + cam.GetDistance() * std::sin(rawPitch), -6.0f);

    cam.Update(1.7778f);
    CHECK_GE(cam.Position().y, cam.GetMinEyeY());
    CHECK_NEAR(cam.Position().y, cam.GetMinEyeY(), 1e-4f); // 抬升 pitch 后眼点恰好贴合下限

    // ComputePosition（应用层碰撞查询依赖）与渲染位置一致
    const glm::vec3 cp = cam.ComputePosition();
    CHECK_NEAR(cp.x, cam.Position().x, 1e-6f);
    CHECK_NEAR(cp.y, cam.Position().y, 1e-6f);
    CHECK_NEAR(cp.z, cam.Position().z, 1e-6f);

    // 约束只调 pitch/距离，不改 yaw：水平偏移方向仍与 yaw 一致
    const glm::vec2 horiz(cam.Position().x - cam.Target().x, cam.Position().z - cam.Target().z);
    const float len = glm::length(horiz);
    REQUIRE(len > 0.0f);
    CHECK_NEAR(horiz.x / len, std::sin(cam.Yaw()), 1e-4f);
    CHECK_NEAR(horiz.y / len, std::cos(cam.Yaw()), 1e-4f);

    // D10 原始场景复现：负 pitch + 满距离（40）拉远不再钻地全黑
    OrbitCamera farCam;
    farCam.SetPitch(-0.5f); // c1433f5 会被夹到 0，现可写入
    farCam.SetDistance(40.0f);
    farCam.Update(1.7778f);
    CHECK_GE(farCam.Position().y, farCam.GetMinEyeY());
    CHECK_NEAR(farCam.Position().y, farCam.GetMinEyeY(), 1e-4f);
}

TEST_CASE("OrbitCamera.UnconstrainedRegression")
{
    // ② 回归守卫：眼点不低于 minEyeY 的常规参数域内，位置/view 与旧球面公式逐位一致
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    struct Params
    {
        float yaw;
        float pitch;
        float distance;
        glm::vec3 target;
    };
    const Params cases[] = {
        {0.8f, 0.45f, 7.0f, glm::vec3(0.0f, 0.5f, 0.0f)},    // 默认构造状态
        {0.0f, 0.0f, 7.0f, glm::vec3(0.0f, 0.5f, 0.0f)},     // pitch=0 边界（c1433f5 的上限，须保持不变）
        {2.0f, 1.53f, 40.0f, glm::vec3(0.0f, 0.5f, 0.0f)},   // 上限俯视 + 满距离
        {-1.0f, -0.05f, 3.0f, glm::vec3(1.0f, 2.0f, -1.0f)}, // 轻微负 pitch（目标抬高，不触地）
        {0.5f, 0.3f, 1.5f, glm::vec3(0.0f, 0.5f, 0.0f)},     // 最近距离
        {-0.6f, -0.8f, 2.0f, glm::vec3(0.0f, 3.0f, 0.0f)},   // 深负 pitch（目标抬高）
    };
    for (const Params& pc : cases)
    {
        OrbitCamera cam;
        cam.SetTarget(pc.target);
        cam.SetYaw(pc.yaw);
        cam.SetPitch(pc.pitch);
        cam.SetDistance(pc.distance);
        cam.Update(1.7778f);

        // 约束未触发：位置与旧实现一致
        const glm::vec3 ref = ReferenceOrbitEye(pc.target, pc.yaw, pc.pitch, pc.distance);
        CHECK_NEAR(cam.Position().x, ref.x, 1e-6f);
        CHECK_NEAR(cam.Position().y, ref.y, 1e-6f);
        CHECK_NEAR(cam.Position().z, ref.z, 1e-6f);
        CHECK_GE(ref.y, cam.GetMinEyeY()); // 且确实未触发约束（前置有效性）

        // view 矩阵与旧实现一致
        const glm::mat4 refView = glm::lookAt(ref, pc.target, worldUp);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK_NEAR(cam.View()[c][r], refView[c][r], 1e-6f);

        // ComputePosition 与渲染位置一致
        const glm::vec3 cp = cam.ComputePosition();
        CHECK_NEAR(cp.x, ref.x, 1e-6f);
        CHECK_NEAR(cp.y, ref.y, 1e-6f);
        CHECK_NEAR(cp.z, ref.z, 1e-6f);
    }
}

TEST_CASE("OrbitCamera.UndergroundTargetFallback")
{
    // 退化分支：目标点低于 minEyeY（target.y=-2，处于 Pan 允许区间）且 distance 不足以越过地面
    // → pitch 抬至上限仍越界，沿视线方向推出距离兜底（视线朝向不变），眼点贴合 minEyeY
    OrbitCamera cam;
    cam.SetTarget(glm::vec3(0.0f, -2.0f, 0.0f));
    cam.SetDistance(1.5f);
    cam.Update(1.7778f);

    CHECK_GE(cam.Position().y, cam.GetMinEyeY());
    CHECK_NEAR(cam.Position().y, cam.GetMinEyeY(), 1e-4f);
    // 眼点被沿视线推远（距离参数本身不被修改）
    const float effDist = glm::length(cam.Position() - cam.Target());
    CHECK_GT(effDist, cam.GetDistance());
    // 视线仍指向目标点：方向 pitch 被抬至上限（自上而下看）
    const glm::vec3 dir = glm::normalize(cam.Target() - cam.Position());
    CHECK_NEAR(dir.y, -std::sin(OrbitCamera::kPitchMax), 1e-3f);
}

TEST_CASE("OrbitCamera.ElevatedTargetLargeDistance")
{
    // ③ 目标抬高 + 大距离：任意 pitch（含 -80°）下眼点不再低于地面
    OrbitCamera cam;
    cam.SetTarget(glm::vec3(0.0f, 8.0f, 0.0f));
    cam.SetDistance(40.0f);
    cam.SetPitch(-1.4f);
    cam.Update(1.7778f);
    CHECK_GE(cam.Position().y, cam.GetMinEyeY());
    CHECK_NEAR(cam.Position().y, cam.GetMinEyeY(), 1e-4f);

    // 调用方传入更高的地面高度（SetMinEyeY）同样生效
    OrbitCamera custom;
    custom.SetTarget(glm::vec3(0.0f, 8.0f, 0.0f));
    custom.SetDistance(40.0f);
    custom.SetPitch(-1.4f);
    custom.SetMinEyeY(2.0f);
    custom.Update(1.7778f);
    CHECK_NEAR(custom.GetMinEyeY(), 2.0f, 1e-7f);
    CHECK_GE(custom.Position().y, 2.0f);
    CHECK_NEAR(custom.Position().y, 2.0f, 1e-4f);
}

TEST_CASE("OrbitCamera.BelowHorizonViewDirection")
{
    // ④ 恢复负 pitch 后"俯视地平线以下"（相机低于目标高度、仰视目标底部）的视线方向正确
    OrbitCamera cam;
    cam.SetTarget(glm::vec3(0.0f, 5.0f, 0.0f));
    cam.SetDistance(4.0f); // 足够近，负 pitch 不触发穿地约束：5 + 4·sin(-1.4) ≈ 1.06 > 0.05
    cam.SetYaw(0.0f);
    cam.SetPitch(-1.4f);
    cam.Update(1.7778f);

    const glm::vec3 eye = cam.Position();
    CHECK_GE(eye.y, cam.GetMinEyeY());
    CHECK_LT(eye.y, cam.Target().y); // 相机确实低于目标高度（地平线以下视角）

    // 视线（eye→target）朝上：能看到目标底部
    const glm::vec3 viewDir = glm::normalize(cam.Target() - eye);
    CHECK_GT(viewDir.y, 0.0f);
    // 方向 pitch 与位置 pitch 严格互逆（SyncFromOrbit 的 asin 反推公式成立）
    CHECK_NEAR(std::asin(std::clamp(viewDir.y, -1.0f, 1.0f)), -OrbitCamera::kPitchMin, 1e-4f);
    // yaw=0：相机位于 target +Z 侧，视线水平分量指向 -Z、无 X 分量
    CHECK_LT(viewDir.z, 0.0f);
    CHECK_NEAR(viewDir.x, 0.0f, 1e-5f);
}
