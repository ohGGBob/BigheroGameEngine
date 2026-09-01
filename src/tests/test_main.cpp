// BigHero Game Engine —— 纯逻辑单元测试
// 仅覆盖不依赖 GPU / 窗口系统的头文件内联逻辑，运行时无需初始化 Vulkan。
#include "scene/Scene.h"
#include "scene/CubeMesh.h"
#include "scene/Transform.h"
#include "scene/MtlMaterial.h"
#include "scene/GltfLoader.h"
#include "scene/Skeleton.h"
#include "scene/Animation.h"
#include "scene/SkinnedMesh.h"
#include "render/Skinning.h"
#include "render/GpuAllocator.h"
#include "core/ecs.h"
#include "core/AssetCache.h"
#include "render/ubo_structs.h"
#include "render/Frustum.h"
#include "render/InstanceBuffer.h"
#include "render/HdrImage.h"
#include "editor/Gizmo.h"

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

    // ---- glTF 二进制缓冲构造辅助（供各 glTF 系测试复用） ----
    inline std::string B64Encode(const std::vector<unsigned char>& bytes)
    {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((bytes.size() + 2) / 3) * 4);
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            const unsigned a = bytes[i];
            const unsigned b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
            const unsigned c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
            out += tbl[a >> 2];
            out += tbl[((a & 3) << 4) | (b >> 4)];
            out += (i + 1 < bytes.size()) ? tbl[((b & 0xF) << 2) | (c >> 6)] : '=';
            out += (i + 2 < bytes.size()) ? tbl[c & 0x3F] : '=';
        }
        return out;
    }

    inline void AppendFloat(std::vector<unsigned char>& v, float x)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
        v.insert(v.end(), p, p + 4);
    }

    inline void AppendU16(std::vector<unsigned char>& v, uint16_t x)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
        v.insert(v.end(), p, p + 2);
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

    // ---- glTF 2.0 加载器（纯CPU，base64 内嵌缓冲） ----
    {
        using namespace BigHero::Scene;

        // base64 编码辅助（构造 data URI 用）
        const auto b64enc = [](const std::vector<unsigned char>& bytes) -> std::string
        {
            static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((bytes.size() + 2) / 3) * 4);
            for (size_t i = 0; i < bytes.size(); i += 3)
            {
                const unsigned a = bytes[i];
                const unsigned b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
                const unsigned c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
                out += tbl[a >> 2];
                out += tbl[((a & 3) << 4) | (b >> 4)];
                out += (i + 1 < bytes.size()) ? tbl[((b & 0xF) << 2) | (c >> 6)] : '=';
                out += (i + 2 < bytes.size()) ? tbl[c & 0x3F] : '=';
            }
            return out;
        };
        const auto appendF = [](std::vector<unsigned char>& v, float x)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
            v.insert(v.end(), p, p + 4);
        };
        const auto appendU16 = [](std::vector<unsigned char>& v, uint16_t x)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
            v.insert(v.end(), p, p + 2);
        };

        // 构造三角形：3 顶点（POSITION/NORMAL VEC3 float，TEXCOORD_0 VEC2 float）+ 3 索引 UINT16
        std::vector<unsigned char> bin;
        // positions
        appendF(bin, 0.0f); appendF(bin, 0.0f); appendF(bin, 0.0f);
        appendF(bin, 1.0f); appendF(bin, 0.0f); appendF(bin, 0.0f);
        appendF(bin, 0.0f); appendF(bin, 1.0f); appendF(bin, 0.0f);
        // normals
        appendF(bin, 0.0f); appendF(bin, 0.0f); appendF(bin, 1.0f);
        appendF(bin, 0.0f); appendF(bin, 0.0f); appendF(bin, 1.0f);
        appendF(bin, 0.0f); appendF(bin, 0.0f); appendF(bin, 1.0f);
        // uvs
        appendF(bin, 0.0f); appendF(bin, 0.0f);
        appendF(bin, 1.0f); appendF(bin, 0.0f);
        appendF(bin, 0.0f); appendF(bin, 1.0f);
        // indices (UINT16)
        appendU16(bin, 0); appendU16(bin, 1); appendU16(bin, 2);

        const std::string dataUri = "data:application/octet-stream;base64," + b64enc(bin);

        const std::string gltf =
            std::string("{")
            + "\"asset\":{\"version\":\"2.0\"},"
            + "\"buffers\":[{\"uri\":\"" + dataUri + "\",\"byteLength\":" + std::to_string(bin.size()) + "}],"
            + "\"bufferViews\":["
            +   "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
            +   "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
            +   "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},"
            +   "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}"
            + "],"
            + "\"accessors\":["
            +   "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
            +   "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
            +   "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
            +   "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
            + "],"
            + "\"meshes\":[{\"primitives\":[{"
            +   "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},"
            +   "\"indices\":3,\"mode\":4"
            + "}]}],"
            + "\"materials\":[{\"name\":\"Red\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}}],"
            + "\"nodes\":[{\"mesh\":0}]"
            + "}";

        const GltfModel m = LoadGltfFromMemory(gltf);

        // 几何：3 顶点、3 索引、1 子网格
        CHECK(m.vertices.size() == 3);
        CHECK(m.indices.size() == 3);
        CHECK(m.primitives.size() == 1);
        CHECK(m.primitives[0].firstIndex == 0);
        CHECK(m.primitives[0].indexCount == 3);
        CHECK(m.primitives[0].materialIndex == -1); // primitive 未指定 material

        // 顶点位置
        CHECK(glm::distance(m.vertices[0].pos, glm::vec3(0, 0, 0)) < 1e-5f);
        CHECK(glm::distance(m.vertices[1].pos, glm::vec3(1, 0, 0)) < 1e-5f);
        CHECK(glm::distance(m.vertices[2].pos, glm::vec3(0, 1, 0)) < 1e-5f);
        // 法线 +Z
        CHECK(glm::distance(m.vertices[0].normal, glm::vec3(0, 0, 1)) < 1e-5f);
        // UV
        CHECK(glm::distance(m.vertices[2].uv, glm::vec2(0, 1)) < 1e-5f);
        // 缺失顶点色 -> 白
        CHECK(glm::distance(m.vertices[0].color, glm::vec3(1)) < 1e-5f);
        // 索引
        CHECK(m.indices[0] == 0 && m.indices[1] == 1 && m.indices[2] == 2);

        // 材质解析
        CHECK(m.materials.size() == 1);
        CHECK(m.materials[0].name == "Red");
        CHECK(glm::distance(m.materials[0].baseColorFactor, glm::vec4(1, 0, 0, 1)) < 1e-5f);

        // ---- 使用原始几何推导切线：退化为 +X（无 TANGENT、UV 与位置相关） ----
        // 仅验证不崩溃

        // ---- 非法版本应抛异常 ----
        const std::string badVer =
            std::string("{") + "\"asset\":{\"version\":\"1.0\"},\"meshes\":[]" + "}";
        bool threw = false;
        try { LoadGltfFromMemory(badVer); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);

        // ---- 骨骼蒙皮：2 关节（3 节点）层级 + 1 蒙皮顶点 ----
        {
            // 矩阵逐元素比较辅助（glm 对 mat4 无 distance）
            const auto matClose = [](const glm::mat4& a, const glm::mat4& b, float eps) -> bool
            {
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        if (std::fabs(a[c][r] - b[c][r]) > eps)
                            return false;
                return true;
            };
            // 构建缓冲：
            //   [0..12)   POSITION  (0,0,0)   VEC3 float
            //   [12..24)  NORMAL    (0,0,1)   VEC3 float
            //   [24..28)  JOINTS_0  [0,1,0,0] VEC4 u8
            //   [28..44)  WEIGHTS_0 [0.5,0.5,0,0] VEC4 float
            //   [44..172) inverseBindMatrices 2 个单位 MAT4（128 字节）
            std::vector<unsigned char> sbin;
            appendF(sbin, 0.0f); appendF(sbin, 0.0f); appendF(sbin, 0.0f);   // pos
            appendF(sbin, 0.0f); appendF(sbin, 0.0f); appendF(sbin, 1.0f);   // normal
            sbin.push_back(0); sbin.push_back(1); sbin.push_back(0); sbin.push_back(0); // joints
            appendF(sbin, 0.5f); appendF(sbin, 0.5f); appendF(sbin, 0.0f); appendF(sbin, 0.0f); // weights
            // 2 个单位逆绑定矩阵（列主序单位阵 16 float）
            for (int j = 0; j < 2; ++j)
            {
                for (int k = 0; k < 16; ++k)
                    appendF(sbin, (k % 5 == 0) ? 1.0f : 0.0f); // 对角线 1（k%5==0: 0,5,10,15）
            }

            const std::string sUri = "data:application/octet-stream;base64," + b64enc(sbin);
            const std::string sGltf =
                std::string("{")
                + "\"asset\":{\"version\":\"2.0\"},"
                + "\"buffers\":[{\"uri\":\"" + sUri + "\",\"byteLength\":" + std::to_string(sbin.size()) + "}],"
                + "\"bufferViews\":["
                +   "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12},"
                +   "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":12},"
                +   "{\"buffer\":0,\"byteOffset\":24,\"byteLength\":4},"
                +   "{\"buffer\":0,\"byteOffset\":28,\"byteLength\":16},"
                +   "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":128}"
                + "],"
                + "\"accessors\":["
                +   "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                +   "{\"bufferView\":1,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                +   "{\"bufferView\":2,\"componentType\":5121,\"count\":1,\"type\":\"VEC4\"},"
                +   "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"VEC4\"},"
                +   "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}"
                + "],"
                + "\"nodes\":["
                +   "{\"mesh\":0,\"children\":[1]},"
                +   "{\"translation\":[0,1,0],\"children\":[2]},"
                +   "{\"translation\":[0,1,0]}"
                + "],"
                + "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":4}],"
                + "\"meshes\":[{\"primitives\":[{"
                +   "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},"
                +   "\"mode\":4"
                + "}]}]"
                + "}";

            const GltfModel sm = LoadGltfFromMemory(sGltf);

            // 节点层级：node1 父=0，node2 父=1
            CHECK(sm.nodeParents.size() == 3);
            CHECK(sm.nodeParents[0] == -1);
            CHECK(sm.nodeParents[1] == 0);
            CHECK(sm.nodeParents[2] == 1);
            // 关节与逆绑定矩阵
            CHECK(sm.jointNodes.size() == 2);
            CHECK(sm.jointNodes[0] == 1 && sm.jointNodes[1] == 2);
            CHECK(sm.inverseBindMatrices.size() == 2);
            // 逆绑定矩阵为单位阵
            CHECK(matClose(sm.inverseBindMatrices[0], glm::mat4(1.0f), 1e-5f));
            // 蒙皮顶点数据
            CHECK(sm.jointIndices.size() == 1);
            CHECK(sm.jointWeights.size() == 1);
            CHECK(sm.jointIndices[0] == glm::u8vec4(0, 1, 0, 0));
            CHECK(glm::distance(sm.jointWeights[0], glm::vec4(0.5f, 0.5f, 0, 0)) < 1e-5f);

            // Skeleton 计算
            const Skeleton skel(sm);
            CHECK(skel.HasSkin());
            CHECK(skel.JointCount() == 2);

            std::vector<glm::mat4> jointGlobal;
            skel.ComputeGlobalJointMatrices(jointGlobal);
            CHECK(jointGlobal.size() == 2);
            // node1 全局 = T(0,1,0)，node2 全局 = T(0,1,0)*T(0,1,0)=T(0,2,0)
            CHECK(matClose(jointGlobal[0], glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)), 1e-4f));
            CHECK(matClose(jointGlobal[1], glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0)), 1e-4f));

            // 皮肤矩阵 = 全局 * 逆绑定（单位阵）-> 等于全局
            std::vector<glm::mat4> skinMat;
            skel.ComputeSkinMatrices(skinMat);
            CHECK(skinMat.size() == 2);
            CHECK(matClose(skinMat[0], jointGlobal[0], 1e-4f));

            // CPU 蒙皮：(0,0,0) 顶点，权重 (0.5,0.5) -> (0,1.5,0)
            const std::vector<glm::vec3> pIn = { glm::vec3(0, 0, 0) };
            const std::vector<glm::vec3> nIn = { glm::vec3(0, 0, 1) };
            std::vector<glm::vec3> pOut, nOut;
            skel.SkinVertices(sm.jointIndices, sm.jointWeights, pIn, nIn, pOut, nOut);
            CHECK(glm::distance(pOut[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);
        }
    }

    // ---- glTF 动画系统（纯CPU，LINEAR/STEP 插值） ----
    {
        using namespace BigHero::Scene;

        // 构造：1 节点，2 条采样器（rotation + translation），1 条动画。
        // 缓冲布局：
        //   [0..32)   rotation 关键帧 2 个（VEC4 quat，各16字节）: [1,0,0,0] 静止, [sin45,sin45,0,0] 绕X转90°
        //   [32..56)  translation 关键帧 2 个（VEC3，各12字节）: (0,0,0) -> (2,0,0)
        //   [56..64)  input 时间戳 2 个（SCALAR float）: 0.0, 1.0
        std::vector<unsigned char> abin;
        const auto appendF = [](std::vector<unsigned char>& v, float x)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
            v.insert(v.end(), p, p + 4);
        };
        const auto b64enc = [](const std::vector<unsigned char>& bytes) -> std::string
        {
            static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((bytes.size() + 2) / 3) * 4);
            for (size_t i = 0; i < bytes.size(); i += 3)
            {
                const unsigned a = bytes[i];
                const unsigned b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
                const unsigned c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
                out += tbl[a >> 2];
                out += tbl[((a & 3) << 4) | (b >> 4)];
                out += (i + 1 < bytes.size()) ? tbl[((b & 0xF) << 2) | (c >> 6)] : '=';
                out += (i + 2 < bytes.size()) ? tbl[c & 0x3F] : '=';
            }
            return out;
        };
        // rotation 关键帧（glTF 存储 (x,y,z,w)）：静止 [0,0,0,1] 与 绕X转90° [s2,0,0,s2]
        const float s2 = std::sqrt(2.0f) * 0.5f;
        appendF(abin, 0.0f); appendF(abin, 0.0f); appendF(abin, 0.0f); appendF(abin, 1.0f);
        appendF(abin, s2); appendF(abin, 0.0f); appendF(abin, 0.0f); appendF(abin, s2);
        // translation 关键帧：(0,0,0) -> (2,0,0)
        appendF(abin, 0.0f); appendF(abin, 0.0f); appendF(abin, 0.0f);
        appendF(abin, 2.0f); appendF(abin, 0.0f); appendF(abin, 0.0f);
        // input 时间戳：0.0, 1.0（两个采样器共用）
        appendF(abin, 0.0f); appendF(abin, 1.0f);

        const std::string aUri = "data:application/octet-stream;base64," + b64enc(abin);
        const std::string aGltf =
            std::string("{")
            + "\"asset\":{\"version\":\"2.0\"},"
            + "\"buffers\":[{\"uri\":\"" + aUri + "\",\"byteLength\":" + std::to_string(abin.size()) + "}],"
            + "\"bufferViews\":["
            +   "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":32},"
            +   "{\"buffer\":0,\"byteOffset\":32,\"byteLength\":24},"
            +   "{\"buffer\":0,\"byteOffset\":56,\"byteLength\":8}"
            + "],"
            + "\"accessors\":["
            +   "{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
            +   "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
            +   "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}"
            + "],"
            + "\"nodes\":[{\"translation\":[0,0,0],\"rotation\":[0,0,0,1],\"scale\":[1,1,1]}],"
            + "\"animations\":[{"
            +   "\"name\":\"TestAnim\","
            +   "\"samplers\":["
            +     "{\"input\":2,\"output\":0,\"interpolation\":\"LINEAR\"},"
            +     "{\"input\":2,\"output\":1,\"interpolation\":\"LINEAR\"}"
            +   "],"
            +   "\"channels\":["
            +     "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}},"
            +     "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"translation\"}}"
            +   "]"
            + "}],"
            + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":4}]}]"
            + "}";

        const GltfModel am = LoadGltfFromMemory(aGltf);
        CHECK(am.animations.size() == 1);
        CHECK(am.animations[0].name == "TestAnim");
        CHECK(am.animations[0].channels.size() == 2);
        CHECK(am.animations[0].samplers.size() == 2);
        // 采样器输入时间与输出值已解析
        CHECK(am.animations[0].samplers[0].times.size() == 2);
        CHECK(std::fabs(am.animations[0].samplers[0].times[1] - 1.0f) < 1e-5f);
        CHECK(am.animations[0].samplers[0].values.size() == 2);
        CHECK(glm::distance(am.animations[0].samplers[0].values[1], glm::vec4(s2, 0, 0, s2)) < 1e-5f);

        const AnimationPlayer player(am);
        CHECK(player.IsValid());
        CHECK(player.AnimationCount() == 1);
        CHECK(std::fabs(player.Duration() - 1.0f) < 1e-5f);

        std::vector<glm::vec3> T;
        std::vector<glm::quat> R;
        std::vector<glm::vec3> S;

        // t=0：静止旋转，平移 (0,0,0)
        player.Sample(0.0f, false, T, R, S);
        CHECK(T.size() == 1);
        CHECK(glm::distance(T[0], glm::vec3(0, 0, 0)) < 1e-4f);
        CHECK(std::fabs(R[0].w - 1.0f) < 1e-4f && std::fabs(R[0].x) < 1e-4f);

        // t=1：绕X转90°（w=cos45, x=sin45），平移 (2,0,0)
        player.Sample(1.0f, false, T, R, S);
        CHECK(glm::distance(T[0], glm::vec3(2, 0, 0)) < 1e-4f);
        CHECK(std::fabs(R[0].w - s2) < 1e-4f);
        CHECK(std::fabs(R[0].x - s2) < 1e-4f);
        CHECK(std::fabs(R[0].y) < 1e-4f);

        // t=0.5：slerp 到 45°（绕X转45°：w=cos22.5, x=sin22.5），平移 lerp 到 (1,0,0)
        player.Sample(0.5f, false, T, R, S);
        CHECK(glm::distance(T[0], glm::vec3(1, 0, 0)) < 1e-4f);
        const float c225 = std::cos(glm::radians(22.5f));
        const float s225 = std::sin(glm::radians(22.5f));
        CHECK(std::fabs(R[0].w - c225) < 1e-3f);
        CHECK(std::fabs(R[0].x - s225) < 1e-3f);
        CHECK(std::fabs(R[0].y) < 1e-3f);

        // loop=true：t=1.5 回绕到 0.5，平移 (1,0,0)
        player.Sample(1.5f, true, T, R, S);
        CHECK(glm::distance(T[0], glm::vec3(1, 0, 0)) < 1e-4f);

        // 越界动画索引 -> IsValid()==false，Sample 保持模型默认值
        const AnimationPlayer bad(am, 99);
        CHECK(!bad.IsValid());
        std::vector<glm::vec3> bT, bS; std::vector<glm::quat> bR;
        bad.Sample(0.0f, false, bT, bR, bS);
        CHECK(bT.size() == 1);
        CHECK(glm::distance(bT[0], glm::vec3(0, 0, 0)) < 1e-4f); // 默认平移
    }

    // ---- 骨骼动画端到端管线（SkinnedMesh：动画采样 -> 皮肤矩阵 -> 蒙皮顶点） ----
    {
        using namespace BigHero::Scene;

        // 3 节点层级：node0(根) -> node1(关节0) -> node2(关节1)，各带 T(0,1,0)。
        // 动画只驱动 node1 的 translation：(0,1,0) -> (0,3,0)；
        // 因层级级联，node2 被父节点带动：(0,2,0) -> (0,4,0)。
        // 蒙皮顶点在原点、权重 (0.5,0.5)：
        //   t=0   -> 0.5*(0,1,0)+0.5*(0,2,0) = (0,1.5,0)
        //   t=1   -> 0.5*(0,3,0)+0.5*(0,4,0) = (0,3.5,0)
        std::vector<unsigned char> bin;
        AppendFloat(bin, 0.0f); AppendFloat(bin, 0.0f); AppendFloat(bin, 0.0f);   // POSITION
        AppendFloat(bin, 0.0f); AppendFloat(bin, 0.0f); AppendFloat(bin, 1.0f);   // NORMAL
        bin.push_back(0); bin.push_back(1); bin.push_back(0); bin.push_back(0);   // JOINTS_0
        AppendFloat(bin, 0.5f); AppendFloat(bin, 0.5f);
        AppendFloat(bin, 0.0f); AppendFloat(bin, 0.0f);                           // WEIGHTS_0
        for (int j = 0; j < 2; ++j)      // 2 个单位逆绑定矩阵（列主序单位阵）
            for (int k = 0; k < 16; ++k)
                AppendFloat(bin, (k % 5 == 0) ? 1.0f : 0.0f);
        AppendFloat(bin, 0.0f); AppendFloat(bin, 1.0f);                           // 动画时间 [0,1]
        AppendFloat(bin, 0.0f); AppendFloat(bin, 1.0f); AppendFloat(bin, 0.0f);   // 关键帧0 (0,1,0)
        AppendFloat(bin, 0.0f); AppendFloat(bin, 3.0f); AppendFloat(bin, 0.0f);   // 关键帧1 (0,3,0)

        // bufferViews 偏移：POSITION(0,12) NORMAL(12,12) JOINTS(24,4) WEIGHTS(28,16)
        //                   IBM(44,128) 时间(172,8) 采样值(180,24)
        const std::string uri = "data:application/octet-stream;base64," + B64Encode(bin);
        const std::string gltf =
            std::string("{")
            + "\"asset\":{\"version\":\"2.0\"},"
            + "\"buffers\":[{\"uri\":\"" + uri + "\",\"byteLength\":" + std::to_string(bin.size()) + "}],"
            + "\"bufferViews\":["
            +   "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12},"
            +   "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":12},"
            +   "{\"buffer\":0,\"byteOffset\":24,\"byteLength\":4},"
            +   "{\"buffer\":0,\"byteOffset\":28,\"byteLength\":16},"
            +   "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":128},"
            +   "{\"buffer\":0,\"byteOffset\":172,\"byteLength\":8},"
            +   "{\"buffer\":0,\"byteOffset\":180,\"byteLength\":24}"
            + "],"
            + "\"accessors\":["
            +   "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
            +   "{\"bufferView\":1,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
            +   "{\"bufferView\":2,\"componentType\":5121,\"count\":1,\"type\":\"VEC4\"},"
            +   "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"VEC4\"},"
            +   "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
            +   "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
            +   "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}"
            + "],"
            + "\"nodes\":["
            +   "{\"mesh\":0,\"children\":[1]},"
            +   "{\"translation\":[0,1,0],\"children\":[2]},"
            +   "{\"translation\":[0,1,0]}"
            + "],"
            + "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":4}],"
            + "\"animations\":[{"
            +   "\"name\":\"Move\","
            +   "\"samplers\":[{\"input\":5,\"output\":6,\"interpolation\":\"LINEAR\"}],"
            +   "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]"
            + "}],"
            + "\"meshes\":[{\"primitives\":[{"
            +   "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},"
            +   "\"mode\":4"
            + "}]}]"
            + "}";

        const GltfModel m = LoadGltfFromMemory(gltf);
        CHECK(m.animations.size() == 1);
        CHECK(m.jointNodes.size() == 2);

        const SkinnedMesh skinned(m);
        CHECK(skinned.HasSkeleton());
        CHECK(skinned.AnimationCount() == 1);
        CHECK(skinned.VertexCount() == 1);

        std::vector<glm::vec3> pos, nrm;

        // 绑定姿态（无动画）：(0,1.5,0)
        skinned.EvaluateBind(pos, nrm);
        CHECK(pos.size() == 1);
        CHECK(glm::distance(pos[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);

        // t=0：关键帧起点，与绑定姿态一致
        skinned.Evaluate(0, 0.0f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);

        // t=1：node1 升到 (0,3,0)，连带 node2 到 (0,4,0) -> 顶点 (0,3.5,0)
        skinned.Evaluate(0, 1.0f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 3.5f, 0)) < 1e-3f);

        // t=0.5：线性插值 node1=(0,2,0)、node2=(0,3,0) -> 顶点 (0,2.5,0)
        skinned.Evaluate(0, 0.5f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 2.5f, 0)) < 1e-3f);

        // 动画下标越界 -> 回退绑定姿态
        skinned.Evaluate(99, 1.0f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);
        // 法线仍为单位长度（蒙皮后归一化）
        CHECK(std::fabs(glm::length(nrm[0]) - 1.0f) < 1e-3f);

        // ---- AnimationState：时间推进 / 速度 / 暂停 / 重置 ----
        AnimationState st;
        CHECK(st.time == 0.0f);
        st.Advance(0.5f);
        CHECK(std::fabs(st.time - 0.5f) < 1e-6f);
        st.speed = 2.0f;
        st.Advance(0.5f);
        CHECK(std::fabs(st.time - 1.5f) < 1e-6f);        // 速度倍率生效
        st.playing = false;
        st.Advance(1.0f);
        CHECK(std::fabs(st.time - 1.5f) < 1e-6f);        // 暂停不推进
        st.playing = true;
        st.Reset();
        CHECK(st.time == 0.0f);

        // ---- AnimationBlender：多动画加权混合 ----
        // 同一动画在 t=0 与 t=1 各占 50% 权重，混合后 node1=(0,2,0) -> 顶点 (0,2.5,0)
        AnimationBlender blender(m);
        blender.AddLayer(0, 1.0f, 0.0f);
        blender.AddLayer(0, 1.0f, 1.0f);
        CHECK(blender.LayerCount() == 2);
        std::vector<glm::vec3> bt, bs;
        std::vector<glm::quat> br;
        blender.Sample(false, bt, br, bs);
        skinned.EvaluatePose(bt, br, bs, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 2.5f, 0)) < 1e-3f);

        // 权重归一化：单层权重 2.0 等价于权重 1，结果为 t=1 的姿态
        blender.Clear();
        CHECK(blender.LayerCount() == 0);
        blender.AddLayer(0, 2.0f, 1.0f);
        blender.Sample(false, bt, br, bs);
        skinned.EvaluatePose(bt, br, bs, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 3.5f, 0)) < 1e-3f);

        // 非等权混合：t=1 占 3/4、t=0 占 1/4 -> node1=(0,2.5,0) -> 顶点 (0,3,0)
        blender.Clear();
        blender.AddLayer(0, 1.0f, 0.0f);
        blender.AddLayer(0, 3.0f, 1.0f);
        blender.Sample(false, bt, br, bs);
        skinned.EvaluatePose(bt, br, bs, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 3.0f, 0)) < 1e-3f);

        // 越界动画下标 / 非正权重被忽略，不产生层
        AnimationBlender empty(m);
        empty.AddLayer(99, 1.0f, 0.0f);
        empty.AddLayer(0, 0.0f, 0.0f);
        CHECK(empty.LayerCount() == 0);

        // ---- GPU 蒙皮：骨骼调色板布局与蒙皮顶点布局 ----
        // std140：mat4[128] 紧密排布，数组步长 64 字节，可整体 memcpy 上传
        CHECK(sizeof(Render::SkinningUBO) == 128 * 64);
        CHECK(Render::GetUboByteSize<Render::SkinningUBO>() == sizeof(Render::SkinningUBO));
        CHECK(offsetof(Render::SkinningUBO, boneMatrices[1])
            - offsetof(Render::SkinningUBO, boneMatrices[0]) == 64);

        // 蒙皮顶点布局：前 5 属性复用基础顶点，权重/关节占 location 11/12
        const auto skAttrs = Render::SkinnedVertex::getAttrDesc();
        CHECK(skAttrs.size() == 7);
        CHECK(Render::SkinnedVertex::getBindingDesc().stride == sizeof(Render::SkinnedVertex));
        CHECK(skAttrs[0].location == 0);
        CHECK(skAttrs[4].location == 4);
        CHECK(skAttrs[5].location == 11);
        CHECK(skAttrs[6].location == 12);
        CHECK(skAttrs[5].format == VK_FORMAT_R32G32B32A32_SFLOAT); // weights
        CHECK(skAttrs[6].format == VK_FORMAT_R8G8B8A8_UINT);       // joints
        // 前几个属性偏移与 Scene::Vertex 完全一致，便于复用同一片段着色器
        CHECK(offsetof(Render::SkinnedVertex, pos) == offsetof(Vertex, pos));
        CHECK(offsetof(Render::SkinnedVertex, normal) == offsetof(Vertex, normal));
        CHECK(offsetof(Render::SkinnedVertex, uv) == offsetof(Vertex, uv));
        CHECK(offsetof(Render::SkinnedVertex, tangent) == offsetof(Vertex, tangent));

        // 调色板：默认全单位矩阵（等价绑定姿态、无变形）
        Render::SkinningPalette palette;
        CHECK(palette.MaxBones() == 128);
        CHECK(palette.Data().boneMatrices[0] == glm::mat4(1.0f));
        CHECK(palette.Data().boneMatrices[127] == glm::mat4(1.0f));

        // SetBone / 越界保护
        const glm::mat4 t2 = glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0));
        CHECK(palette.SetBone(1, t2));
        CHECK(palette.Data().boneMatrices[1] == t2);
        CHECK(!palette.SetBone(128, t2)); // 越界返回 false

        // SetBones 批量 + 超限保护（超限时不修改任何内容）
        CHECK(palette.SetBones(std::vector<glm::mat4>{ t2, t2 }));
        CHECK(!palette.SetBones(std::vector<glm::mat4>(129, t2)));

        // SetFromMesh：CPU 姿态 -> GPU 调色板（t=1 时关节应已动画到位）
        Render::SkinningPalette meshPal;
        CHECK(meshPal.SetFromMesh(skinned, 0, 1.0f, false));
        // 关节0（node1）全局 = T(0,3,0)；关节1（node2）被父节点带动 = T(0,4,0)
        CHECK(glm::distance(glm::vec3(meshPal.Data().boneMatrices[0][3]),
            glm::vec3(0, 3, 0)) < 1e-3f);
        CHECK(glm::distance(glm::vec3(meshPal.Data().boneMatrices[1][3]),
            glm::vec3(0, 4, 0)) < 1e-3f);
        // 未使用的槽位保持单位矩阵
        CHECK(meshPal.Data().boneMatrices[2] == glm::mat4(1.0f));
    }

    // ---- GPU 显存分配器（VMA 替代：块内子分配 + 多块管理） ----
    {
        using namespace BigHero::Render;

        // 单块子分配器：容量 1024，粒度 256
        GpuBlockAllocator blk(1024, 256);
        CHECK(blk.Capacity() == 1024);
        CHECK(blk.Used() == 0);
        CHECK(blk.AllocationCount() == 0);

        // 分配大小向上取整到粒度 256
        const GpuAllocation a0 = blk.Alloc(100, 256);
        CHECK(a0.valid && a0.offset == 0 && a0.size == 256);
        const GpuAllocation a1 = blk.Alloc(300, 256); // roundUp 512
        CHECK(a1.valid && a1.offset == 256 && a1.size == 512);
        const GpuAllocation a2 = blk.Alloc(256, 256);
        CHECK(a2.valid && a2.offset == 768 && a2.size == 256);
        CHECK(blk.Used() == 1024);
        CHECK(blk.AllocationCount() == 3);

        // 块满 -> 分配失败
        CHECK(!blk.Alloc(1, 256).valid);

        // 释放中间段后非空，可复用
        blk.Free(a1);
        CHECK(blk.Used() == 512);
        CHECK(blk.AllocationCount() == 2);
        const GpuAllocation a1b = blk.Alloc(400, 256); // roundUp 512，落入 [256,768)
        CHECK(a1b.valid && a1b.offset == 256 && a1b.size == 512);
        CHECK(blk.Used() == 1024);
        CHECK(!blk.Alloc(1, 256).valid); // 真正满了

        // 对齐 > 粒度：偏移向上取整到 align
        GpuBlockAllocator blk2(4096, 256);
        blk2.Alloc(100, 256); // [0,256)
        const GpuAllocation aligned = blk2.Alloc(100, 512); // free [256,4096) -> off roundUp(256,512)=512
        CHECK(aligned.valid && aligned.offset == 512);

        // 合并：释放全部段后整块回归连续空闲，可再分配满块
        GpuBlockAllocator blk3(1024, 256);
        GpuAllocation x0 = blk3.Alloc(256, 256); // [0,256)
        GpuAllocation x1 = blk3.Alloc(256, 256); // [256,512)
        GpuAllocation x2 = blk3.Alloc(256, 256); // [512,768)
        GpuAllocation x3 = blk3.Alloc(256, 256); // [768,1024)
        blk3.Free(x0); // [0,256)
        blk3.Free(x2); // [512,768)
        blk3.Free(x1); // [256,512) -> 与 [0,256) 合并 [0,512)
        blk3.Free(x3); // [768,1024) -> 合并 [0,1024)
        CHECK(blk3.Empty());
        CHECK(blk3.Used() == 0);
        const GpuAllocation whole = blk3.Alloc(1024, 256);
        CHECK(whole.valid && whole.offset == 0 && whole.size == 1024);

        // 多块分配器：假后端只计数，不真正分配显存
        int created = 0;
        GpuAllocator mgr(1024, 256, 4, [&](uint32_t, VkDeviceSize) { ++created; return VK_NULL_HANDLE; });
        CHECK(mgr.BlockCount() == 0);
        CHECK(mgr.BlockSize() == 1024);
        CHECK(mgr.MaxBlocks() == 4);

        std::vector<GpuAllocation> ms;
        for (int i = 0; i < 4; ++i) { const GpuAllocation x = mgr.Alloc(256, 256); CHECK(x.valid); ms.push_back(x); }
        CHECK(mgr.BlockCount() == 1);
        CHECK(created == 1); // 仅创建块 0
        // 块 0 满 -> 新建块 1
        const GpuAllocation mb = mgr.Alloc(256, 256);
        CHECK(mb.valid && mb.block == 1);
        CHECK(mgr.BlockCount() == 2);
        CHECK(created == 2);

        // 释放块 0 首个分配，后续分配优先复用已有块（块数不再增长）
        mgr.Free(ms[0]);
        const GpuAllocation mc = mgr.Alloc(256, 256);
        CHECK(mc.valid && mc.block == 0);
        CHECK(mgr.BlockCount() == 2);
        CHECK(created == 2);

        // 越界/无效句柄释放安全
        mgr.Free(GpuAllocation{});
        mgr.Free(ms[1]); mgr.Free(ms[2]); mgr.Free(ms[3]); mgr.Free(mb); mgr.Free(mc);
        CHECK(mgr.BlockCount() == 2); // 块不主动回收（简单容量管理）
    }

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
            const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec2 vp(800.0f, 600.0f);
            // 世界 (0,0,10) 在相机（位于 z=5 看向 -z）后方
            const glm::vec2 behind = ProjectWorldToScreen(glm::vec3(0.0f, 0.0f, 10.0f), proj * view, vp);
            CHECK(behind.x < -1e8f && behind.y < -1e8f);
        }

        // 3) 轴拾取：一般偏置视角下三轴屏幕方向明显分离、互不退化。
        //    把鼠标放在某轴投影端点（远离原点，避开三轴在原点交叉），应拾取该轴。
        {
            const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 800.0f / 600.0f, 0.1f, 100.0f);
            const glm::mat4 view = glm::lookAt(glm::vec3(3.0f, 2.0f, 5.0f),
                glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 vp = proj * view;
            const glm::vec2 vpp(800.0f, 600.0f);
            const glm::vec3 origin(0.0f);
            const glm::vec2 o = ProjectWorldToScreen(origin, vp, vpp);
            CHECK(o.x > -1e8f);
            const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
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
            CHECK(std::fabs(dperp) < 1e-4f); // 垂直方向无分量 -> 0
            CHECK(std::fabs(dpos - 0.025f) < 1e-3f); // 10px / 400pxPerUnit
        }

        // 5) 旋转拖拽：90° 扫角应得 ±π/2；从->到反向符号翻转；退化（起点=中心）为 0
        {
            const float kHalfPi = 1.5707963f;
            const glm::vec2 c(400.0f, 300.0f);
            const glm::vec2 from(400.0f, 200.0f); // 中心上方
            const glm::vec2 to(500.0f, 300.0f);    // 中心右方
            const float ang = RotateDragAngle(c, from, to);
            CHECK(std::fabs(ang - kHalfPi) < 1e-3f);
            const float angRev = RotateDragAngle(c, to, from);
            CHECK(angRev < 0.0f);
            CHECK(std::fabs(angRev + kHalfPi) < 1e-3f);
            CHECK(std::fabs(RotateDragAngle(c, c, to)) < 1e-5f);
        }
    }

    // ---- ECS 组件系统（纯CPU） ----
    {
        using namespace BigHero::Core;

        // 实体生命周期：Create / Alive / Destroy
        Registry reg;
        const Entity e0 = reg.Create();
        const Entity e1 = reg.Create();
        CHECK(e0.Index() == 1);                 // index 0 保留为空实体哨兵
        CHECK(e1.Index() == 2);
        CHECK(reg.Alive(e0));
        CHECK(reg.Alive(e1));
        CHECK(!e0.IsNull());                    // 有效实体值与空实体哨兵不可混淆

        // 销毁后 Alive 为 false
        reg.Destroy(e0);
        CHECK(!reg.Alive(e0));

        // index 复用 + version 递增：重建 e0 的 index 得到新版本
        const Entity e0b = reg.Create();
        CHECK(e0b.Index() == e0.Index());       // 复用同一 index
        CHECK(e0b.Version() == e0.Version() + 1); // version 递增
        CHECK(reg.Alive(e0b));
        CHECK(!reg.Alive(e0));                  // 旧句柄失效（版本不匹配）

        // 组件增删查
        struct Health { int hp = 0; };
        struct Position { float x = 0, y = 0, z = 0; };

        reg.Add<Health>(e1, 100);
        CHECK(reg.Has<Health>(e1));
        CHECK(reg.Get<Health>(e1).hp == 100);
        CHECK(!reg.Has<Position>(e1));
        reg.Add<Position>(e1, 1.0f, 2.0f, 3.0f);
        CHECK(reg.Has<Position>(e1));
        CHECK(std::fabs(reg.Get<Position>(e1).x - 1.0f) < 1e-4f);
        CHECK(std::fabs(reg.Get<Position>(e1).z - 3.0f) < 1e-4f);

        reg.Remove<Health>(e1);
        CHECK(!reg.Has<Health>(e1));
        CHECK(reg.Has<Position>(e1));           // 移除一个组件不影响其他

        // View 迭代：只遍历同时拥有全部组件的实体
        reg.Add<Health>(e0b, 50);
        reg.Add<Position>(e0b, 7.0f, 8.0f, 9.0f);
        // e1 只有 Position（Health 已移除）-> 不应出现在 View<Health, Position> 中
        int seen = 0;
        MakeView<Health, Position>(reg).Each(
            [&](Health& h, Position& p)
            {
                ++seen;
                CHECK(h.hp == 50);
                CHECK(std::fabs(p.x - 7.0f) < 1e-4f);
            });
        CHECK(seen == 1);

        // 单组件 View 迭代数量
        int posCount = 0;
        MakeView<Position>(reg).Each([&](Position&) { ++posCount; });
        CHECK(posCount == 2);   // e0b 与 e1

        // swap-pop 紧凑性：移除中间实体后 dense 中剩余实体仍有效、大小收缩
        Registry reg2;
        const Entity a = reg2.Create();
        const Entity b = reg2.Create();
        const Entity c = reg2.Create();
        reg2.Add<Position>(a, 1.0f, 0.0f, 0.0f);
        reg2.Add<Position>(b, 2.0f, 0.0f, 0.0f);
        reg2.Add<Position>(c, 3.0f, 0.0f, 0.0f);
        CHECK(reg2.Get<Position>(a).x == 1.0f);
        CHECK(reg2.Get<Position>(c).x == 3.0f);
        reg2.Destroy(b);                        // 销毁中间实体，移除组件
        CHECK(!reg2.Alive(b));
        CHECK(!reg2.Has<Position>(b));
        // a/c 组件仍可访问，池中剩余 2 个
        CHECK(reg2.Get<Position>(a).x == 1.0f);
        CHECK(reg2.Get<Position>(c).x == 3.0f);
        CHECK(reg2.Pool<Position>().Size() == 2);
    }

    // ---- 引用计数 LRU 资源缓存（纯CPU） ----
    {
        using namespace BigHero::Core;

        struct Texture { int id = 0; };

        int loads = 0;
        AssetCache<Texture> cache(2, [&](const std::string& key) {
            ++loads;
            auto t = std::make_shared<Texture>();
            t->id = std::atoi(key.c_str());
            return t;
        });

        // 未命中加载并缓存
        auto a = cache.Load("10");
        CHECK(a != nullptr);
        CHECK(a->id == 10);
        CHECK(loads == 1);
        CHECK(cache.Size() == 1);

        // 命中：不重复加载，返回同一对象
        auto a2 = cache.Load("10");
        CHECK(loads == 1);            // 未再次调用工厂
        CHECK(a2.get() == a.get());   // 同一对象

        // 容量淘汰：容量 2，再加载 2 个（软上限：被引用条目不淘汰）
        auto b = cache.Load("20");
        auto c = cache.Load("30");
        CHECK(loads == 3);
        // 因 a/b/c 仍被外部引用，实际无法淘汰，Size 超容量（软上限）——验证引用保护：
        CHECK(cache.Size() == 3);     // 三者均被引用，软容量不强制淘汰

        // 释放外部引用后再加载新键，应淘汰最旧的未引用条目
        a.reset(); a2.reset();
        auto d = cache.Load("40");
        CHECK(loads == 4);
        CHECK(cache.Size() == 3);     // 淘汰 "10"（已无引用），保留 20/30/40
        CHECK(!cache.Contains("10")); // "10" 已被淘汰
        CHECK(cache.Contains("20"));
        CHECK(cache.Contains("30"));
        CHECK(cache.Contains("40"));

        // Get 不触发加载
        auto g = cache.Get("20");
        CHECK(g != nullptr && g->id == 20);
        CHECK(loads == 4);
        // 不存在的键 Get 返回 nullptr
        CHECK(cache.Get("nope") == nullptr);

        // Remove 手动移除
        cache.Remove("30");
        CHECK(!cache.Contains("30"));
        CHECK(cache.Size() == 2);

        // SetCapacity 缩小触发淘汰：释放全部外部引用后，应淘汰 LRU 端条目
        g.reset(); b.reset(); d.reset();
        cache.SetCapacity(1);
        CHECK(cache.Size() == 1);
        // 释放后仅剩一个未引用条目；LRU 端 "20" 被淘汰，MRU 端 "40" 保留
        CHECK(cache.Contains("40"));
        CHECK(!cache.Contains("20"));

        // 工厂返回 nullptr（加载失败）→ 不缓存
        AssetCache<Texture> failCache(4, [](const std::string&) { return std::shared_ptr<Texture>(); });
        auto f = failCache.Load("x");
        CHECK(f == nullptr);
        CHECK(failCache.Size() == 0); // 失败不缓存

        // Clear
        cache.Clear();
        CHECK(cache.Size() == 0);
        CHECK(cache.Empty());

        // AssetManager：按类型注册多个缓存并统一加载
        struct Mesh2 { int m = 0; };
        AssetManager mgr;
        mgr.Cache<Texture>(4, [&](const std::string& key) {
            auto t = std::make_shared<Texture>();
            t->id = std::atoi(key.c_str());
            return t;
        });
        mgr.Cache<Mesh2>(2, [](const std::string&) { return std::make_shared<Mesh2>(); });

        auto ta = mgr.Load<Texture>("7");
        CHECK(ta != nullptr && ta->id == 7);
        auto ma = mgr.Load<Mesh2>("m0");
        CHECK(ma != nullptr);
        // 命中复用
        auto ta2 = mgr.Load<Texture>("7");
        CHECK(ta2.get() == ta.get());
        // 类型隔离：Texture 与 Mesh2 各自独立缓存
        CHECK(mgr.Cache<Texture>().Size() == 1);
        CHECK(mgr.Cache<Mesh2>().Size() == 1);
    }

    if (g_failures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test failure(s).\n", g_failures);
    return 1;
}
