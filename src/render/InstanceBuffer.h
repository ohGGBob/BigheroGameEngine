#pragma once
#include "render/Buffer.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// 逐实例数据（instance-rate 顶点输入，std140 布局）：
//   model(mat4, 64) + tint(vec4, 16) + metallic(4) + roughness(4) + pad[2](8)
//   + emissive(vec4, 16) + probeIrradiance(vec4, 16) = 128 字节。
// 结构体对齐为 16（因含 mat4/vec4），故 sizeof = 128 = 8*16，满足实例步长须为 16 的倍数、
// 保证模型矩阵每行按 16 字节对齐且跨实例边界连续对齐的要求。
// emissive（自发光，线性 HDR）：直接叠加到最终颜色，不受光照调制——霓虹灯牌 / 窗格 /
// 全息展台用；顶点属性 location 13（11/12 为 GPU 蒙皮的权重与关节索引，故顺延）。
// probeIrradiance（SH9 光照探针逐对象辐照度，线性 HDR）：CPU 在填充实例缓冲时按该实体世界
// 矩阵平移列（world[3]）采样探针体得到，片元着色器乘 albedo 叠加为间接环境光；未烘焙时恒为
// 0（渲染零变化）。顶点属性 location 14。
struct InstanceData
{
    glm::mat4 model{1.0f};    // 模型矩阵（世界变换），offset 0..63
    glm::vec4 tint{1.0f};     // 反照率乘数（rgb），w = 顶点 alpha，offset 64..79
    float metallic = 0.0f;    // PBR 金属度，offset 80
    float roughness = 0.5f;   // PBR 粗糙度，offset 84
    float pad[2]{};           // 补齐到 96（保持 metallic/roughness 所在 vec4 完整）
    glm::vec4 emissive{0.0f}; // 自发光（rgb，线性 HDR），offset 96..111
    glm::vec4 probeIrradiance{0.0f}; // 逐对象探针辐照度（rgb，可直接乘 albedo），offset 112..127
};
static_assert(sizeof(InstanceData) == 128, "InstanceData 必须为 16 的倍数以对齐 mat4 列");
static_assert(offsetof(InstanceData, emissive) == 96, "emissive 偏移须为 96");
static_assert(offsetof(InstanceData, probeIrradiance) == 112, "probeIrradiance 偏移须为 112");

// 实例缓冲：保存逐实例数据，按最大实例数分配为设备本地顶点缓冲，
// 每帧用宿主可见 staging 缓冲上传当前可见实例数据，一次 vkCmdDrawIndexedInstanced 驱动多实例绘制。
class InstanceBuffer
{
  public:
    InstanceBuffer() = default;
    ~InstanceBuffer() { Destroy(); }

    InstanceBuffer(const InstanceBuffer&) = delete;
    InstanceBuffer& operator=(const InstanceBuffer&) = delete;

    InstanceBuffer(InstanceBuffer&& other) noexcept { MoveFrom(other); }
    InstanceBuffer& operator=(InstanceBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    // 分配 maxInstances 容量（DEVICE_LOCAL，作为绑定0顶点输入）
    void Create(const Context& ctx, uint32_t maxInstances);

    // 上传 count 个实例数据（自动做输入参数校验与容量裁剪）
    void Upload(const Context& ctx, const InstanceData* data, uint32_t count) const;

    void Destroy();

    // 绑定实例缓冲到命令缓冲
    void Bind(VkCommandBuffer cmd) const;

    // 实例缓冲的输入绑定描述（binding=1，VK_VERTEX_INPUT_RATE_INSTANCE）
    [[nodiscard]] static VkVertexInputBindingDescription GetBindingDesc();
    // 实例缓冲的逐实例属性（location 5..10 + 13 + 14，对齐 vert.glsl 逐实例输入）
    [[nodiscard]] static std::vector<VkVertexInputAttributeDescription> GetAttrDesc();

    [[nodiscard]] uint32_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool IsValid() const noexcept { return buffer_.IsValid(); }
    [[nodiscard]] VkBuffer Get() const noexcept { return buffer_.Get(); }

  private:
    void MoveFrom(InstanceBuffer& other) noexcept;

    Buffer buffer_;
    uint32_t capacity_ = 0;
};
} // namespace BigHero::Render
