#pragma once
#include "render/Image.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace BigHero
{
    class Context;

    // IBL环境光照资源：
    // 1) CPU程序化生成HDR天空立方图（渐变天空+太阳+地面反弹）
    // 2) GPU预计算：辐照度立方图（漫反射卷积）+ GGX预滤波立方图（镜面mip链）+ BRDF LUT
    // 全部资源一次生成，静态使用，不随窗口重建
    class EnvironmentLighting
    {
    public:
        EnvironmentLighting() = default;
        ~EnvironmentLighting() { Destroy(); }

        EnvironmentLighting(const EnvironmentLighting&) = delete;
        EnvironmentLighting& operator=(const EnvironmentLighting&) = delete;

        void Create(const Context& ctx);
        void Destroy();

        [[nodiscard]] VkImageView EnvView() const noexcept { return envCubemap_.View(); }
        [[nodiscard]] VkImageView IrradianceView() const noexcept { return irradianceCubemap_.View(); }
        [[nodiscard]] VkImageView PrefilteredView() const noexcept { return prefilteredCubemap_.View(); }
        [[nodiscard]] VkImageView BrdfLutView() const noexcept { return brdfLut_.View(); }
        [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
        [[nodiscard]] uint32_t PrefilteredMipLevels() const noexcept { return kPrefilterMips; }

    private:
        static constexpr uint32_t kEnvSize = 128;
        static constexpr uint32_t kIrradianceSize = 32;
        static constexpr uint32_t kPrefilterSize = 128;
        static constexpr uint32_t kPrefilterMips = 5;
        static constexpr uint32_t kBrdfSize = 512;

        // 程序化天空：渐变+太阳盘+光晕+地面反弹（HDR线性值）
        [[nodiscard]] static glm::vec3 SampleSky(glm::vec3 dir);

        void createRenderPasses(const Context& ctx);
        void createColorImage(const Context& ctx, Image& image, uint32_t size,
            VkFormat format, uint32_t mipLevels, uint32_t layers);
        void destroyGenerationResources();

        const Context* ctx_ = nullptr;

        Image envCubemap_;
        Image irradianceCubemap_;
        Image prefilteredCubemap_;
        Image brdfLut_;
        VkSampler sampler_ = VK_NULL_HANDLE;

        // 生成期离屏资源（生成完毕后可释放）
        VkRenderPass cubeColorPass_ = VK_NULL_HANDLE;  // RGBA16F 颜色通道
        VkRenderPass brdfColorPass_ = VK_NULL_HANDLE;  // RG16F 颜色通道
        std::vector<VkFramebuffer> irradianceFramebuffers_;
        std::vector<VkImageView> prefilterFaceViews_;
        std::vector<VkFramebuffer> prefilterFramebuffers_;
        VkImageView brdfView_ = VK_NULL_HANDLE;
        VkFramebuffer brdfFramebuffer_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout envSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorPool envDescriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet envSet_ = VK_NULL_HANDLE;
    };
}
