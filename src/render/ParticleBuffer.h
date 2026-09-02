#pragma once
// 粒子实例化顶点缓冲（GPU 端逐实例数据，world-space billboard）。
//
// 布局（std140 对齐，步长 32 字节）：
//   position(vec3, 12) + size(float, 4) + color(vec3, 12) + pad(float, 4)。
// 顶点着色器以 gl_VertexIndex 生成单位四边形、用 camRight/camUp 世界轴展开为面向相机的公告板。
// 该缓冲由升级 17-3 的粒子 GPU 实例化渲染使用。

#include "render/Buffer.h"
#include "render/Context.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// 单粒子 GPU 顶点数据（与 particle.vert.glsl 输入一一对应）
struct ParticleInstance
{
    glm::vec3 position{0.0f};
    float size = 1.0f;
    glm::vec3 color{1.0f};
    float pad = 0.0f;
};
static_assert(sizeof(ParticleInstance) == 32, "ParticleInstance 步长须为 16 的倍数（32 字节）");

// 粒子实例缓冲：设备本地顶点缓冲，每帧 staging 上传存活粒子。
class ParticleBuffer
{
  public:
    ParticleBuffer() = default;
    ~ParticleBuffer() { Destroy(); }

    ParticleBuffer(const ParticleBuffer&) = delete;
    ParticleBuffer& operator=(const ParticleBuffer&) = delete;

    // 分配 maxParticles 容量
    void Create(const Context& ctx, uint32_t maxParticles)
    {
        Destroy();
        capacity_ = maxParticles > 0 ? maxParticles : 1;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(capacity_) * sizeof(ParticleInstance);
        buffer_.Create(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    // 上传 count 个实例（容量裁剪）
    void Upload(const Context& ctx, const ParticleInstance* data, uint32_t count) const
    {
        if (data == nullptr || count == 0 || !IsValid())
            return;
        const uint32_t capped = std::min(count, capacity_);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(capped) * sizeof(ParticleInstance);
        buffer_.UploadData(ctx, data, bytes);
    }

    // 绑定为 binding 0（实例率）
    void Bind(VkCommandBuffer cmd) const
    {
        const VkDeviceSize offset = 0;
        VkBuffer vb = buffer_.Get();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    }

    [[nodiscard]] static VkVertexInputBindingDescription GetBindingDesc()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = static_cast<uint32_t>(sizeof(ParticleInstance));
        binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return binding;
    }

    [[nodiscard]] static std::vector<VkVertexInputAttributeDescription> GetAttrDesc()
    {
        std::vector<VkVertexInputAttributeDescription> attrs(3);
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = static_cast<uint32_t>(offsetof(ParticleInstance, position));
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = static_cast<uint32_t>(offsetof(ParticleInstance, color));
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32_SFLOAT;
        attrs[2].offset = static_cast<uint32_t>(offsetof(ParticleInstance, size));
        return attrs;
    }

    [[nodiscard]] bool IsValid() const noexcept { return buffer_.IsValid(); }

    void Destroy()
    {
        buffer_.Destroy();
        capacity_ = 0;
    }

  private:
    Buffer buffer_;
    uint32_t capacity_ = 0;
};
} // namespace BigHero::Render
