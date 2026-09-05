#include "render/InstanceBuffer.h"
#include "core/VkCheck.h"
#include "render/Context.h"

#include <algorithm>
#include <stdexcept>

namespace BigHero::Render
{
void InstanceBuffer::Create(const Context& ctx, uint32_t maxInstances)
{
    Destroy();
    if (maxInstances == 0)
        throw std::runtime_error("InstanceBuffer::Create: 最大实例数为0");

    capacity_ = maxInstances;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(capacity_) * sizeof(InstanceData);
    // 设备本地顶点缓冲 + 每次上传走 Buffer::UploadData 内部 staging
    buffer_.Create(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

void InstanceBuffer::Upload(const Context& ctx, const InstanceData* data, uint32_t count) const
{
    if (data == nullptr || count == 0 || !IsValid())
        return;
    const uint32_t capped = std::min(count, capacity_);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(capped) * sizeof(InstanceData);
    buffer_.UploadData(ctx, data, bytes);
}

void InstanceBuffer::Bind(VkCommandBuffer cmd) const
{
    const VkDeviceSize offset = 0;
    VkBuffer vb = buffer_.Get();
    vkCmdBindVertexBuffers(cmd, 1, 1, &vb, &offset); // binding 1 = 实例缓冲
}

VkVertexInputBindingDescription InstanceBuffer::GetBindingDesc()
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 1;
    binding.stride = static_cast<uint32_t>(sizeof(InstanceData));
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> InstanceBuffer::GetAttrDesc()
{
    // 逐实例属性（locations 5..10，对应 vert.glsl 逐实例输入）：
    //  5,6,7,8 : model 矩阵 4 行（每行一个 R32G32B32A32_SFLOAT，offset = 行*16）
    //  9        : tint (vec4)
    //  10       : metallic / roughness / pad / pad（4 float）
    std::vector<VkVertexInputAttributeDescription> attrs(6);

    for (uint32_t row = 0; row < 4; ++row)
    {
        attrs[row].location = 5 + row;
        attrs[row].binding = 1;
        attrs[row].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[row].offset = static_cast<uint32_t>(row * sizeof(glm::vec4));
    }
    attrs[4].location = 9;
    attrs[4].binding = 1;
    attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[4].offset = static_cast<uint32_t>(offsetof(InstanceData, tint));
    attrs[5].location = 10;
    attrs[5].binding = 1;
    attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[5].offset = static_cast<uint32_t>(offsetof(InstanceData, metallic));

    return attrs;
}

void InstanceBuffer::Destroy()
{
    buffer_.Destroy();
    capacity_ = 0;
}

void InstanceBuffer::MoveFrom(InstanceBuffer& other) noexcept
{
    buffer_ = std::move(other.buffer_);
    capacity_ = other.capacity_;
    other.capacity_ = 0;
}
} // namespace BigHero::Render
