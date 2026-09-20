#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Scene
{
// 顶点：位置/法线/UV/顶点色/切线（PBR法线贴图所需的TBN基础）
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 color;
    glm::vec3 tangent;

    static VkVertexInputBindingDescription getBindingDesc()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttrDesc()
    {
        std::vector<VkVertexInputAttributeDescription> attrs(5);
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, pos);
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, normal);
        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(Vertex, uv);
        attrs[3].binding = 0;
        attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[3].offset = offsetof(Vertex, color);
        attrs[4].binding = 0;
        attrs[4].location = 4;
        attrs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[4].offset = offsetof(Vertex, tangent);
        return attrs;
    }
};

// ---- 顶点/索引缓冲布局常量 ----
// [0..24)  单位立方体（中心在原点，边长1，逐面顶点色）
// [24..28) 地面平面（1000x1000，法线朝上）
inline constexpr uint32_t kCubeVertexCount = 24;
inline constexpr uint32_t kGroundVertexBase = 24;
inline constexpr uint32_t kCubeIndexCount = 36;
inline constexpr uint32_t kGroundIndexOffset = 36;
inline constexpr uint32_t kGroundIndexCount = 6;
// 单位立方体局部包围球半径（半边长 0.5，半对角线 = 0.5*sqrt(3)），供视锥剔除使用
inline constexpr float kCubeBoundingRadius = 0.8660254f;

// 单位立方体：中心在原点，6面24顶点，从外看逆时针绕序，切线沿UV的+u方向
inline std::vector<Vertex> BuildCubeVertices()
{
    std::vector<Vertex> verts;
    verts.reserve(kCubeVertexCount);

    const std::array<glm::vec3, 6> faceColors = {glm::vec3(0.85f, 0.33f, 0.30f), glm::vec3(0.33f, 0.72f, 0.38f),
                                                 glm::vec3(0.30f, 0.52f, 0.92f), glm::vec3(0.95f, 0.78f, 0.30f),
                                                 glm::vec3(0.72f, 0.42f, 0.88f), glm::vec3(0.35f, 0.80f, 0.80f)};

    struct FaceDef
    {
        glm::vec3 normal;
        glm::vec3 tangent;                // UV的+u方向
        std::array<glm::vec3, 4> corners; // 从外看逆时针
    };
    const std::array<FaceDef, 6> faces = {FaceDef{glm::vec3(0, 0, 1),
                                                  glm::vec3(1, 0, 0),
                                                  {glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, 0.5f),
                                                   glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, 0.5f)}},
                                          FaceDef{glm::vec3(0, 0, -1),
                                                  glm::vec3(-1, 0, 0),
                                                  {glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, -0.5f, -0.5f),
                                                   glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, -0.5f)}},
                                          FaceDef{glm::vec3(0, 1, 0),
                                                  glm::vec3(1, 0, 0),
                                                  {glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f),
                                                   glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(-0.5f, 0.5f, -0.5f)}},
                                          FaceDef{glm::vec3(0, -1, 0),
                                                  glm::vec3(1, 0, 0),
                                                  {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, -0.5f, -0.5f),
                                                   glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(-0.5f, -0.5f, 0.5f)}},
                                          FaceDef{glm::vec3(1, 0, 0),
                                                  glm::vec3(0, 0, -1),
                                                  {glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, -0.5f),
                                                   glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f)}},
                                          FaceDef{glm::vec3(-1, 0, 0),
                                                  glm::vec3(0, 0, 1),
                                                  {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, -0.5f, 0.5f),
                                                   glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, -0.5f)}}};

    const std::array<glm::vec2, 4> faceUVs = {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f),
                                              glm::vec2(0.0f, 1.0f)};

    uint32_t faceIndex = 0;
    for (const FaceDef& face : faces)
    {
        for (uint32_t i = 0; i < 4; ++i)
        {
            Vertex v{};
            v.pos = face.corners[i];
            v.normal = face.normal;
            v.uv = faceUVs[i];
            v.color = faceColors[faceIndex];
            v.tangent = face.tangent;
            verts.push_back(v);
        }
        ++faceIndex;
    }
    return verts;
}

// 地面平面：y=0，法线朝上，从上方看逆时针。
// 1000×1000（半边 500）：对角 707m 超出相机 farZ 500m，任何视角下地面边缘都先被
// 远平面裁剪——旧 20×20 地面会在视野内露出边缘线与外侧天空亮带（画面右侧白色竖条）。
// UV 保持 4m/格 密度（500/4=125）；尺寸与 PhysicsHost 静态地面盒（halfExtents 500）对齐
inline std::vector<Vertex> BuildGroundVertices()
{
    constexpr float kHalf = 500.0f;
    const std::array<glm::vec3, 4> corners = {glm::vec3(-kHalf, 0.0f, kHalf), glm::vec3(kHalf, 0.0f, kHalf),
                                              glm::vec3(kHalf, 0.0f, -kHalf), glm::vec3(-kHalf, 0.0f, -kHalf)};
    const std::array<glm::vec2, 4> uvs = {glm::vec2(0.0f, 0.0f), glm::vec2(125.0f, 0.0f), glm::vec2(125.0f, 125.0f),
                                          glm::vec2(0.0f, 125.0f)};

    std::vector<Vertex> verts;
    verts.reserve(4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        Vertex v{};
        v.pos = corners[i];
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv = uvs[i];
        v.color = glm::vec3(0.75f, 0.76f, 0.78f);
        v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        verts.push_back(v);
    }
    return verts;
}

// ========================================================================
// 程序化球体 / 胶囊（人物部件网格，meshId=3/4）
// ========================================================================
// 单位球：半径 0.5，中心在原点，UV 球（沿 Y 轴分段）。法线=径向单位向量，
// 切线沿经线方向（UV 的 +v 方向），color=白（部件外观全部由实例 tint 决定）。
inline constexpr float kSphereBoundingRadius = 0.5f;

inline void BuildSphereVertices(std::vector<Vertex>& verts, std::vector<uint32_t>& idxs, uint32_t sectors = 20,
                                uint32_t segments = 10, float radius = 0.5f)
{
    verts.clear();
    idxs.clear();
    for (uint32_t seg = 0; seg <= segments; ++seg)
    {
        const float v = static_cast<float>(seg) / static_cast<float>(segments);
        const float phi = v * glm::pi<float>(); // 0..pi（北极→南极）
        const float y = radius * std::cos(phi);
        const float ringR = radius * std::sin(phi);
        for (uint32_t sec = 0; sec <= sectors; ++sec)
        {
            const float u = static_cast<float>(sec) / static_cast<float>(sectors);
            const float theta = u * 2.0f * glm::pi<float>();
            Vertex vt{};
            vt.pos = glm::vec3(ringR * std::cos(theta), y, ringR * std::sin(theta));
            vt.normal = glm::normalize(vt.pos);
            vt.uv = glm::vec2(u, v);
            vt.color = glm::vec3(1.0f);
            // 切线：经线方向（+v），与法线正交
            const glm::vec3 dpdu = glm::vec3(-ringR * std::sin(theta), 0.0f, ringR * std::cos(theta));
            vt.tangent = glm::length(dpdu) > 1e-5f ? glm::normalize(dpdu) : glm::vec3(1.0f, 0.0f, 0.0f);
            verts.push_back(vt);
        }
    }
    idxs.reserve(sectors * segments * 6);
    const uint32_t stride = sectors + 1;
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        for (uint32_t sec = 0; sec < sectors; ++sec)
        {
            const uint32_t a = seg * stride + sec;
            const uint32_t b = a + 1;
            const uint32_t c = a + stride;
            const uint32_t d = c + 1;
            idxs.push_back(a);
            idxs.push_back(c);
            idxs.push_back(b);
            idxs.push_back(b);
            idxs.push_back(c);
            idxs.push_back(d);
        }
    }
}

// 单位胶囊：半径 0.5、柱体高 0.7（不含半球）、沿 Y 轴、中心在原点
// （总高 = 2*radius + cylinderHeight = 1.7，包围半径 ≈ 1.0）。
inline constexpr float kCapsuleBoundingRadius = 1.0f;

inline void BuildCapsuleVertices(std::vector<Vertex>& verts, std::vector<uint32_t>& idxs, uint32_t sectors = 16,
                                 uint32_t capSegments = 5, float radius = 0.5f, float cylinderHeight = 0.7f)
{
    verts.clear();
    idxs.clear();
    const float halfCyl = cylinderHeight * 0.5f;
    const uint32_t segs = capSegments;
    // 顶点柱：纬度采样 [0..2*segs] 对应 南半球→赤道→北半球（半圆每段 capSegments 份）
    const uint32_t latCount = 2 * segs + 1;
    for (uint32_t lat = 0; lat <= latCount; ++lat)
    {
        // 纬度角：-pi/2（南极）→ +pi/2（北极）跨越 pi（半球各 capSegments 份）
        const float phi =
            -glm::half_pi<float>() + static_cast<float>(lat) / static_cast<float>(latCount) * glm::pi<float>();
        const float cy = radius * std::sin(phi);
        const float ringR = radius * std::cos(phi);
        // 注：半球原点在柱端平面；北半球 y = halfCyl + cy，南半球 y = -halfCyl + cy
        const float finalY = (lat <= segs) ? (-halfCyl + cy) : (halfCyl + cy);
        for (uint32_t sec = 0; sec <= sectors; ++sec)
        {
            const float u = static_cast<float>(sec) / static_cast<float>(sectors);
            const float theta = u * 2.0f * glm::pi<float>();
            Vertex vt{};
            vt.pos = glm::vec3(ringR * std::cos(theta), finalY, ringR * std::sin(theta));
            vt.normal = glm::vec3(std::cos(theta) * std::cos(phi), std::sin(phi), std::sin(theta) * std::cos(phi));
            vt.uv = glm::vec2(u, static_cast<float>(lat) / static_cast<float>(latCount));
            vt.color = glm::vec3(1.0f);
            const glm::vec3 dpdu = glm::vec3(-ringR * std::sin(theta), 0.0f, ringR * std::cos(theta));
            vt.tangent = glm::length(dpdu) > 1e-5f ? glm::normalize(dpdu) : glm::vec3(1.0f, 0.0f, 0.0f);
            verts.push_back(vt);
        }
    }
    idxs.reserve(latCount * sectors * 6);
    for (uint32_t lat = 0; lat < latCount; ++lat)
    {
        for (uint32_t sec = 0; sec < sectors; ++sec)
        {
            const uint32_t a = lat * (sectors + 1) + sec;
            const uint32_t b = a + 1;
            const uint32_t c = a + (sectors + 1);
            const uint32_t d = c + 1;
            idxs.push_back(a);
            idxs.push_back(c);
            idxs.push_back(b);
            idxs.push_back(b);
            idxs.push_back(c);
            idxs.push_back(d);
        }
    }
}

// 组合场景顶点：立方体+地面（顺序与常量布局一致）
inline std::vector<Vertex> BuildSceneVertices()
{
    std::vector<Vertex> verts = BuildCubeVertices();
    std::vector<Vertex> ground = BuildGroundVertices();
    verts.insert(verts.end(), ground.begin(), ground.end());
    return verts;
}

inline std::vector<uint32_t> BuildSceneIndices()
{
    std::vector<uint32_t> indices;
    indices.reserve(kGroundIndexOffset + kGroundIndexCount);

    // 立方体6面，每面2个三角形
    for (uint32_t face = 0; face < 6; ++face)
    {
        const uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    // 地面平面
    indices.push_back(kGroundVertexBase + 0);
    indices.push_back(kGroundVertexBase + 1);
    indices.push_back(kGroundVertexBase + 2);
    indices.push_back(kGroundVertexBase + 0);
    indices.push_back(kGroundVertexBase + 2);
    indices.push_back(kGroundVertexBase + 3);

    return indices;
}
} // namespace BigHero::Scene
