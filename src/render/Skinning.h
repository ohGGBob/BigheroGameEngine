#pragma once
// GPU 蒙皮（Skinning）：骨骼矩阵调色板与蒙皮顶点布局。
//
// CPU 侧只负责把骨骼矩阵"打包"进 std140 调色板（SkinningUBO），
// 逐顶点蒙皮在顶点着色器（shaders/skinned.vert.glsl）中完成，
// 从而把 O(顶点数 × 关节数) 的开销从 CPU 转移到 GPU，支持大规模角色。
//
// 数据流：
//   SkinnedMesh（CPU 姿态求值）
//     --GetSkinMatrices--> SkinningPalette（调色板，含越界保护）
//       --memcpy--> SkinningUBO（set 3 binding 0）
//         --按关节索引采样--> skinned.vert.glsl（GPU 蒙皮）
//
// 约定：
//   - 蒙皮顶点前 5 个属性（location 0~4）与 Scene::Vertex 完全一致，
//     便于同一套材质/片段着色器复用；权重/关节占用 location 11/12
//     （5~10 已被逐实例属性：模型矩阵 5~8、tint 9、材质参数 10 占用）。
//   - 调色板下标即 SkinnedMesh 的关节下标（与 JOINTS_0 语义一致）。

#include "render/ubo_structs.h"
#include "scene/SkinnedMesh.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// 蒙皮顶点：基础顶点 + 关节索引与权重。
struct SkinnedVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 color;
    glm::vec3 tangent;
    glm::vec4 weights;  // 蒙皮权重（至多 4 关节，和为 1）
    glm::u8vec4 joints; // 关节索引（骨骼调色板下标）

    static VkVertexInputBindingDescription getBindingDesc()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(SkinnedVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttrDesc()
    {
        std::vector<VkVertexInputAttributeDescription> attrs(7);
        attrs[0] = Attr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, pos));
        attrs[1] = Attr(1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, normal));
        attrs[2] = Attr(2, VK_FORMAT_R32G32_SFLOAT, offsetof(SkinnedVertex, uv));
        attrs[3] = Attr(3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, color));
        attrs[4] = Attr(4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, tangent));
        attrs[5] = Attr(11, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SkinnedVertex, weights));
        attrs[6] = Attr(12, VK_FORMAT_R8G8B8A8_UINT, offsetof(SkinnedVertex, joints));
        return attrs;
    }

  private:
    static VkVertexInputAttributeDescription Attr(uint32_t location, VkFormat format, uint32_t offset)
    {
        VkVertexInputAttributeDescription a{};
        a.binding = 0;
        a.location = location;
        a.format = format;
        a.offset = offset;
        return a;
    }
};

// 骨骼调色板：CPU 侧填充并持有 SkinningUBO，可整体一次性上传。
class SkinningPalette
{
  public:
    SkinningPalette() { Reset(); }

    // 全部骨骼置为单位矩阵（等价于绑定姿态、无变形）
    void Reset()
    {
        for (glm::mat4& m : ubo_.boneMatrices)
            m = glm::mat4(1.0f);
    }

    // 设置单个骨骼矩阵；下标越界返回 false（不写入）
    bool SetBone(uint32_t index, const glm::mat4& m)
    {
        if (index >= kMaxSkinBones)
            return false;
        ubo_.boneMatrices[index] = m;
        return true;
    }

    // 批量设置；数量超过 kMaxSkinBones 时返回 false 且不修改任何内容
    bool SetBones(const std::vector<glm::mat4>& mats)
    {
        if (mats.size() > kMaxSkinBones)
            return false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(mats.size()); ++i)
            ubo_.boneMatrices[i] = mats[i];
        return true;
    }

    // 直接用 SkinnedMesh 在某动画时刻的皮肤矩阵填充（CPU 姿态 -> GPU 调色板）
    bool SetFromMesh(const Scene::SkinnedMesh& mesh, size_t animIndex, float time, bool loop)
    {
        std::vector<glm::mat4> mats;
        mesh.GetSkinMatrices(animIndex, time, loop, mats);
        return SetBones(mats);
    }

    [[nodiscard]] const SkinningUBO& Data() const noexcept { return ubo_; }
    [[nodiscard]] SkinningUBO& Data() noexcept { return ubo_; }
    [[nodiscard]] static constexpr uint32_t MaxBones() noexcept { return kMaxSkinBones; }

  private:
    SkinningUBO ubo_{};
};
} // namespace BigHero::Render

