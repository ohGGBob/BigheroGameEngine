// BigHero Game Engine —— 纯逻辑单元测试
// 仅覆盖不依赖 GPU / 窗口系统的头文件内联逻辑，运行时无需初始化 Vulkan。
#include "scene/Scene.h"
#include "scene/CubeMesh.h"
#include "scene/Transform.h"
#include "scene/MtlMaterial.h"
#include "render/ubo_structs.h"
#include "render/Frustum.h"
#include "render/InstanceBuffer.h"
#include "render/HdrImage.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <array>
#include <string>
#include <vector>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace
{
    int g_failures = 0;

    void check(bool cond, const char* expr, const char* file, int line)
    {
        if (!cond)
        {
            std::printf("FAIL: %s  (%s:%d)\n", expr, file, line);
            ++g_failures;
        }
    }
}

#define CHECK(cond) ::check((cond), #cond, __FILE__, __LINE__)

int main()
{
    using namespace BigHero;

    // ---- 场景 ----
    const std::vector<Scene::SceneObject> scene = Scene::BuildDefaultScene();
    CHECK(scene.size() == 6);
    CHECK(scene.front().meshId == 0);          // 首个为共享立方体
    CHECK(scene.back().meshId == 1);           // 末个为外部圆环体
    for (const Scene::SceneObject& obj : scene)
        CHECK(obj.scale > 0.0f);

    // ---- 网格顶点/索引布局 ----
    const std::vector<Scene::Vertex> cube = Scene::BuildCubeVertices();
    CHECK(cube.size() == Scene::kCubeVertexCount);
    const std::vector<Scene::Vertex> all = Scene::BuildSceneVertices();
    CHECK(all.size() == Scene::kCubeVertexCount + 4);   // 立方体 + 4 顶点地面
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

    // ---- UBO 布局（std140 严格对齐） ----
    CHECK(sizeof(Render::CameraUBO) == 128);
    // GpuPointLight 必须为 16 的倍数：std140 规则要求"结构体数组"步长=大小向上取整到16，
    // 故元素取 48 字节（位置/强度/颜色/半径/阴影标志 + 填充），CPU 数组步长=GPU 步长。
    CHECK(sizeof(Render::GpuPointLight) == 48);
    CHECK(offsetof(Render::LightUBO, lightSpaceMatrix) % 16 == 0);
    CHECK(offsetof(Render::LightUBO, lights) % 16 == 0);
    CHECK(offsetof(Render::LightUBO, lights[1]) - offsetof(Render::LightUBO, lights[0]) == 48);
    // 点光源立方体阴影 UBO：6 个 mat4 紧密数组，每 mat4 64 字节
    CHECK(sizeof(Render::PointShadowUBO) == 6 * 64);
    CHECK(offsetof(Render::PointShadowUBO, faceMatrices[1]) - offsetof(Render::PointShadowUBO, faceMatrices[0]) == 64);

    // ---- 视锥剔除（纯数学，无 GPU 依赖） ----
    // 单位立方体裁剪盒（VP=identity）：可见区为 x,y,z ∈ [-1,1]
    {
        const Render::Frustum idF = Render::Frustum::FromViewProj(glm::mat4(1.0f));
        CHECK(idF.IntersectsSphere(glm::vec3(0.0f), 0.1f));        // 中心在内
        CHECK(idF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 0.5f), 0.1f)); // 偏内
        CHECK(!idF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 5.0f), 0.1f)); // 远处被远平面剔除
        CHECK(!idF.IntersectsSphere(glm::vec3(5.0f, 0.0f, 0.0f), 0.1f)); // 右侧被右平面剔除
    }
    // 透视相机（Vulkan NDC z∈[0,1]）：相机位于 (0,0,5) 看向原点
    {
        const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
            glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const Render::Frustum camF = Render::Frustum::FromViewProj(proj * view);
        CHECK(camF.IntersectsSphere(glm::vec3(0.0f), 1.0f));       // 原点在相机前方
        CHECK(camF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 4.9f), 0.5f)); // 贴近近平面
        CHECK(!camF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 20.0f), 1.0f));  // 相机后方
        CHECK(!camF.IntersectsSphere(glm::vec3(0.0f, 100.0f, 0.0f), 1.0f)); // 视野上方之外
    }
    // 立方体局部包围球半径 ≈ 0.5*sqrt(3)
    CHECK(std::fabs(Scene::kCubeBoundingRadius - 0.8660254f) < 1e-4f);

    // ---- 实例化布局（逐实例输入 std140） ----
    // 实例步长须为 16 的倍数，且模型矩阵 4 行各按 16 字节对齐。
    // model(mat4,64) + tint(vec4,16) + metallic(4) + roughness(4) + pad[2](8) = 96 = 6*16。
    CHECK(sizeof(Render::InstanceData) % 16 == 0);
    CHECK(sizeof(Render::InstanceData) == 96);
    CHECK(offsetof(Render::InstanceData, tint) == 64);       // model 64 字节之后
    CHECK(offsetof(Render::InstanceData, metallic) == 80);   // tint 16 字节之后
    CHECK(offsetof(Render::InstanceData, roughness) == 84);
    {
        const VkVertexInputBindingDescription binding = Render::InstanceBuffer::GetBindingDesc();
        CHECK(binding.binding == 1);
        CHECK(binding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE);
        CHECK(binding.stride == sizeof(Render::InstanceData));
        const auto attrs = Render::InstanceBuffer::GetAttrDesc();
        CHECK(attrs.size() == 6);   // 4 行 model + tint + metallic/roughness，无空槽
        CHECK(attrs[0].location == 5);  // model[0] 行
        CHECK(attrs[3].location == 8);  // model[3] 行
        CHECK(attrs[4].location == 9);  // tint
        CHECK(attrs[5].location == 10); // metallic/roughness
        for (const auto& a : attrs)
            CHECK(a.binding == 1);      // 全部走实例 binding
    }

    // ---- HDR 环境贴图加载器（纯CPU，RGBE解码） ----
    {
        // 手工构造一张 16x2 的 Radiance .hdr：两行，全行 RLE 压缩，各通道恒定值。
        // R=200,G=100,B=50,E=140 => scale=2^(140-128-8)=16 => (3200,1600,800)。
        const std::string header = "#?RADIANCE\n"
                                   "FORMAT=32-bit_rle_rgbe\n"
                                   "\n"
                                   "-Y 2 +X 16\n";
        std::vector<uint8_t> bytes(header.begin(), header.end());
        const std::vector<uint8_t> scanline = {
            2, 2, 0, 16,          // RLE 头，跨度=16
            144, 200,             // R 通道：重复16次值200
            144, 100,             // G 通道
            144, 50,              // B 通道
            144, 140              // E 通道
        };
        bytes.insert(bytes.end(), scanline.begin(), scanline.end());
        bytes.insert(bytes.end(), scanline.begin(), scanline.end()); // 第二行

        HdrImage img;
        img.LoadFromMemory(std::move(bytes));
        CHECK(img.IsValid());
        CHECK(img.Width() == 16);
        CHECK(img.Height() == 2);
        CHECK(img.Exposure() == 1.0f); // 未声明 EXPOSURE，默认1
        CHECK(img.Pixels().size() == 32);
        const glm::vec4 p = img.Pixels()[0];
        CHECK(std::fabs(p.r - 3200.0f) < 1e-3f);
        CHECK(std::fabs(p.g - 1600.0f) < 1e-3f);
        CHECK(std::fabs(p.b - 800.0f) < 1e-3f);
        CHECK(std::fabs(p.a - 1.0f) < 1e-6f);
        // 其余像素一致（RLE 展开正确）
        bool allSame = true;
        for (const auto& q : img.Pixels())
            allSame = allSame && glm::distance(q, p) < 1e-4f;
        CHECK(allSame);

        // RGBEToLinear：E=128 时 scale=1/256=0.00390625
        const uint8_t rgbe[4] = { 255, 0, 0, 128 };
        const glm::vec3 c = HdrImage::RGBEToLinear(rgbe);
        CHECK(std::fabs(c.r - 255.0f / 256.0f) < 1e-4f);
        CHECK(c.g == 0.0f && c.b == 0.0f);
        // E=0 => 纯黑
        const uint8_t black[4] = { 255, 255, 255, 0 };
        CHECK(HdrImage::RGBEToLinear(black) == glm::vec3(0.0f));
    }

    // ---- HDR 等距柱状投影 -> 立方图（纯CPU采样一致性） ----
    {
        // 简单纬度梯度：上半球(北)亮、下半球(南)暗，经度无关。
        const uint32_t w = 64, h = 32;
        std::vector<glm::vec3> equi(static_cast<size_t>(w) * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(h - 1); // 0..1 (上->下)
            const float light = (v < 0.5f) ? 1.0f : 0.2f; // 上半亮下半暗
            for (uint32_t x = 0; x < w; ++x)
                equi[static_cast<size_t>(y) * w + x] = glm::vec3(light);
        }

        // SampleEquirect：+Y 方向（天顶）应落在上半球（亮），-Y 方向（天底）在下半球（暗）
        const glm::vec3 top = HdrImage::SampleEquirect(glm::vec3(0, 1, 0), equi.data(), w, h);
        const glm::vec3 bottom = HdrImage::SampleEquirect(glm::vec3(0, -1, 0), equi.data(), w, h);
        CHECK(top.r > 0.5f);
        CHECK(bottom.r < 0.5f);

        // EquirectToCube：6 面均正确生成，+Y 面（索引2）应整体亮、-Y 面（索引3）应整体暗
        const auto faces = HdrImage::EquirectToCube(equi.data(), w, h, 8);
        CHECK(faces.size() == 6);
        CHECK(faces[0].size() == 64); // 8x8
        float topAvg = 0.0f, bottomAvg = 0.0f;
        for (const auto& px : faces[2]) topAvg += px.r;
        for (const auto& px : faces[3]) bottomAvg += px.r;
        topAvg /= static_cast<float>(faces[2].size());
        bottomAvg /= static_cast<float>(faces[3].size());
        CHECK(topAvg > 0.5f);
        CHECK(bottomAvg < 0.5f);

        // 无效输入抛异常
        bool threw = false;
        try { (void)HdrImage::EquirectToCube(nullptr, w, h, 8); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }

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
        nodes[1].translation = glm::vec3(0.0f, 5.0f, 0.0f);  // 子局部偏移
        nodes[1].parent = 0;
        const glm::vec3 childWorld = WorldPosition(nodes[1], nodes);
        CHECK(glm::distance(childWorld, glm::vec3(10.0f, 5.0f, 0.0f)) < 1e-4f);
        // 父矩阵即自身局部矩阵（根）
        CHECK(glm::distance(glm::vec3(LocalToWorldMatrix(nodes[0], nodes)[3]),
                glm::vec3(10.0f, 0.0f, 0.0f)) < 1e-4f);

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
        const auto aabb = WorldAabb(aabbNode, std::vector<Transform>{ aabbNode },
            glm::vec3(-0.5f), glm::vec3(0.5f));
        CHECK(glm::distance(aabb[0], glm::vec3(0.0f, 0.0f, 0.0f)) < 1e-4f);
        CHECK(glm::distance(aabb[1], glm::vec3(2.0f, 2.0f, 2.0f)) < 1e-4f);

        // 5) 非均匀缩放下的世界 AABB（x方向拉长，盒子保守包含8角点）
        Transform scaleNode;
        scaleNode.scale = glm::vec3(4.0f, 1.0f, 1.0f);
        const auto saabb = WorldAabb(LocalToMatrix(scaleNode), glm::vec3(-0.5f), glm::vec3(0.5f));
        CHECK(glm::distance(saabb[0], glm::vec3(-2.0f, -0.5f, -0.5f)) < 1e-4f);
        CHECK(glm::distance(saabb[1], glm::vec3(2.0f, 0.5f, 0.5f)) < 1e-4f);
    }

    // ---- Wavefront .mtl 材质解析（纯CPU） ----
    {
        using namespace Scene;

        const std::string mtl =
            "# 测试材质库\n"
            "newmtl Gold\n"
            "Ka 0.1 0.1 0.1\n"
            "Kd 1.0 0.8 0.3\n"
            "Ks 0.6 0.5 0.2\n"
            "Ns 128\n"
            "d 1.0\n"
            "illum 2\n"
            "map_Kd gold_albedo.png\n"
            "\n"
            "newmtl Matte\n"
            "Kd 0.5 0.5 0.5\n"
            "Ns 4\n"
            "Tr 0.4\n";

        const std::vector<MtlMaterial> mats = ParseMtl(mtl);
        CHECK(mats.size() == 2);
        CHECK(mats[0].name == "Gold");
        CHECK(glm::distance(mats[0].diffuse, glm::vec3(1.0f, 0.8f, 0.3f)) < 1e-4f);
        CHECK(glm::distance(mats[0].specular, glm::vec3(0.6f, 0.5f, 0.2f)) < 1e-4f);
        CHECK(std::fabs(mats[0].shininess - 128.0f) < 1e-4f);
        CHECK(mats[0].opacity == 1.0f);
        CHECK(mats[0].mapKd == "gold_albedo.png");
        CHECK(mats[0].HasMapKd());

        CHECK(mats[1].name == "Matte");
        CHECK(std::fabs(mats[1].diffuse.r - 0.5f) < 1e-4f);
        // Tr 0.4 -> opacity = 0.6
        CHECK(std::fabs(mats[1].opacity - 0.6f) < 1e-4f);

        // 空/纯注释文件不抛异常，返回空
        CHECK(ParseMtl("").empty());
        CHECK(ParseMtl("# only a comment\n").empty());

        // usemtl 前面聚合子网格：faceMaterials 每 3 索引一条三角形
        const std::vector<std::string> faceMats = { "Gold", "Gold", "Matte", "Matte", "Gold" };
        const auto sub = GroupFacesByMaterial(faceMats, mats);
        // Gold(2三角) -> Matte(2三角) -> Gold(1三角)：3 个子网格
        CHECK(sub.size() == 3);
        CHECK(sub[0].materialIndex == 0);
        CHECK(sub[0].firstIndex == 0);
        CHECK(sub[0].indexCount == 6); // 2 三角形
        CHECK(sub[1].materialIndex == 1);
        CHECK(sub[1].firstIndex == 6);
        CHECK(sub[1].indexCount == 6);
        CHECK(sub[2].materialIndex == 0);
        CHECK(sub[2].firstIndex == 12);
        CHECK(sub[2].indexCount == 3);

        // 未知名材质 -> materialIndex == -1（视为未指定）
        const std::vector<std::string> unknownFace = { "Nope" };
        const auto u = GroupFacesByMaterial(unknownFace, mats);
        CHECK(u.size() == 1 && u[0].materialIndex == -1);
    }

    if (g_failures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test failure(s).\n", g_failures);
    return 1;
}
