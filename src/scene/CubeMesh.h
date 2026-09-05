#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
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
// [24..28) 地面平面（20x20，法线朝上）
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

// 地面平面：y=0，法线朝上，从上方看逆时针
inline std::vector<Vertex> BuildGroundVertices()
{
    constexpr float kHalf = 10.0f;
    const std::array<glm::vec3, 4> corners = {glm::vec3(-kHalf, 0.0f, kHalf), glm::vec3(kHalf, 0.0f, kHalf),
                                              glm::vec3(kHalf, 0.0f, -kHalf), glm::vec3(-kHalf, 0.0f, -kHalf)};
    const std::array<glm::vec2, 4> uvs = {glm::vec2(0.0f, 0.0f), glm::vec2(5.0f, 0.0f), glm::vec2(5.0f, 5.0f),
                                          glm::vec2(0.0f, 5.0f)};

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
