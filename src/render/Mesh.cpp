#include "render/Mesh.h"
#include "core/VkCheck.h"
#include "render/Context.h"

namespace BigHero::Render
{
void Mesh::Create(const Context& ctx, const void* vertexData, VkDeviceSize vertexBytes, uint32_t vertexCount,
                  const uint32_t* indices, uint32_t indexCount)
{
    Destroy();

    if (vertexData == nullptr || vertexBytes == 0 || vertexCount == 0 || indices == nullptr || indexCount == 0)
        throw std::runtime_error("Mesh::Create: 无效的网格数据");

    vertexBuffer_.Create(ctx, vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vertexBuffer_.UploadData(ctx, vertexData, vertexBytes);

    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indexCount) * sizeof(uint32_t);
    indexBuffer_.Create(ctx, indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    indexBuffer_.UploadData(ctx, indices, indexBytes);

    vertexCount_ = vertexCount;
    indexCount_ = indexCount;
}

void Mesh::Destroy()
{
    vertexBuffer_.Destroy();
    indexBuffer_.Destroy();
    vertexCount_ = 0;
    indexCount_ = 0;
}

void Mesh::Bind(VkCommandBuffer cmd) const
{
    const VkDeviceSize offset = 0;
    VkBuffer vb = vertexBuffer_.Get();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::DrawIndexed(VkCommandBuffer cmd, uint32_t indexCount, uint32_t firstIndex) const
{
    vkCmdDrawIndexed(cmd, indexCount, 1, firstIndex, 0, 0);
}

void Mesh::DrawIndexedInstanced(VkCommandBuffer cmd, uint32_t indexCount, uint32_t firstIndex,
                                uint32_t instanceCount) const
{
    if (instanceCount == 0)
        return;
    vkCmdDrawIndexed(cmd, indexCount, instanceCount, firstIndex, 0, 0);
}

void Mesh::MoveFrom(Mesh& other) noexcept
{
    vertexBuffer_ = std::move(other.vertexBuffer_);
    indexBuffer_ = std::move(other.indexBuffer_);
    vertexCount_ = other.vertexCount_;
    indexCount_ = other.indexCount_;
    boundingCenter_ = other.boundingCenter_;
    boundingRadius_ = other.boundingRadius_;

    other.vertexCount_ = 0;
    other.indexCount_ = 0;
    other.boundingCenter_ = glm::vec3(0.0f);
    other.boundingRadius_ = 0.0f;
}
} // namespace BigHero::Render
