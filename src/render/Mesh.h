#pragma once
#include "render/Buffer.h"
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
}

namespace BigHero::Render
{
using ::BigHero::Buffer;
using BigHero::Context;

// 网格资源：顶点+索引缓冲对，RAII管理，支持多网格间切换绑定
class Mesh
{
  public:
    Mesh() = default;
    ~Mesh() { Destroy(); }

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& other) noexcept { MoveFrom(other); }
    Mesh& operator=(Mesh&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    // 从内存顶点/索引数据创建（staging上传到设备本地内存）
    template<typename VertexT>
    void Create(const Context& ctx, const std::vector<VertexT>& vertices, const std::vector<uint32_t>& indices)
    {
        // 包围球：以顶点质心为圆心，半径取顶点到质心的最大距离（保守且严格包围全部顶点，
        // 用于视锥剔除时不会误剔）。中心点/半径均在模型局部空间。
        if (!vertices.empty())
        {
            glm::vec3 c(0.0f);
            for (const auto& v : vertices)
                c += v.pos;
            c /= static_cast<float>(vertices.size());
            float r = 0.0f;
            for (const auto& v : vertices)
                r = std::max(r, glm::distance(c, v.pos));
            boundingCenter_ = c;
            boundingRadius_ = r;
        }
        Create(ctx, vertices.data(), static_cast<VkDeviceSize>(vertices.size() * sizeof(VertexT)),
               static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()));
    }

    void Create(const Context& ctx, const void* vertexData, VkDeviceSize vertexBytes, uint32_t vertexCount,
                const uint32_t* indices, uint32_t indexCount);
    void Destroy();

    // 绑定顶点/索引缓冲（VkIndexType_UINT32）
    void Bind(VkCommandBuffer cmd) const;

    // 索引化绘制；可指定子范围（用于共享缓冲内的多个网格段）
    void DrawIndexed(VkCommandBuffer cmd, uint32_t indexCount, uint32_t firstIndex = 0) const;

    // 索引化实例化绘制：一次调用渲染 instanceCount 个实例（实例数据由 binding1 提供）
    void DrawIndexedInstanced(VkCommandBuffer cmd, uint32_t indexCount, uint32_t firstIndex,
                              uint32_t instanceCount) const;

    [[nodiscard]] uint32_t IndexCount() const noexcept { return indexCount_; }
    [[nodiscard]] uint32_t VertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] VkBuffer GetVertexBuffer() const noexcept { return vertexBuffer_.Get(); }
    [[nodiscard]] VkBuffer GetIndexBuffer() const noexcept { return indexBuffer_.Get(); }
    [[nodiscard]] bool IsValid() const noexcept { return vertexBuffer_.IsValid() && indexBuffer_.IsValid(); }

    // 模型局部空间包围球（视锥剔除用）：圆心 + 半径
    [[nodiscard]] const glm::vec3& BoundingCenter() const noexcept { return boundingCenter_; }
    [[nodiscard]] float BoundingRadius() const noexcept { return boundingRadius_; }

  private:
    void MoveFrom(Mesh& other) noexcept;

    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    glm::vec3 boundingCenter_{0.0f};
    float boundingRadius_ = 0.0f;
};
} // namespace BigHero::Render
