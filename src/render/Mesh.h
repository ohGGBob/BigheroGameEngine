#pragma once
#include "render/Buffer.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace BigHero
{
    class Context;
}

namespace BigHero::Render
{
    using BigHero::Context;
    using ::BigHero::Buffer;

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
        void Create(const Context& ctx, const std::vector<VertexT>& vertices,
            const std::vector<uint32_t>& indices)
        {
            Create(ctx, vertices.data(),
                static_cast<VkDeviceSize>(vertices.size() * sizeof(VertexT)),
                static_cast<uint32_t>(vertices.size()),
                indices.data(), static_cast<uint32_t>(indices.size()));
        }

        void Create(const Context& ctx, const void* vertexData, VkDeviceSize vertexBytes,
            uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount);
        void Destroy();

        // 绑定顶点/索引缓冲（VkIndexType_UINT32）
        void Bind(VkCommandBuffer cmd) const;

        // 索引化绘制；可指定子范围（用于共享缓冲内的多个网格段）
        void DrawIndexed(VkCommandBuffer cmd, uint32_t indexCount, uint32_t firstIndex = 0) const;

        [[nodiscard]] uint32_t IndexCount() const noexcept { return indexCount_; }
        [[nodiscard]] uint32_t VertexCount() const noexcept { return vertexCount_; }
        [[nodiscard]] VkBuffer GetVertexBuffer() const noexcept { return vertexBuffer_.Get(); }
        [[nodiscard]] VkBuffer GetIndexBuffer() const noexcept { return indexBuffer_.Get(); }
        [[nodiscard]] bool IsValid() const noexcept { return vertexBuffer_.IsValid() && indexBuffer_.IsValid(); }

    private:
        void MoveFrom(Mesh& other) noexcept;

        Buffer vertexBuffer_;
        Buffer indexBuffer_;
        uint32_t vertexCount_ = 0;
        uint32_t indexCount_ = 0;
    };
}
