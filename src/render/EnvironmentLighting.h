#pragma once
#include "render/Image.h"
#include "render/Texture.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

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

    // HDR文件加载接口
    bool CreateFromFile(const Context& ctx, const std::string& filePath);

    [[nodiscard]] VkImageView EnvView() const noexcept { return envCubemap_.View(); }
    [[nodiscard]] VkImageView IrradianceView() const noexcept { return irradianceCubemap_.View(); }
    [[nodiscard]] VkImageView PrefilteredView() const noexcept { return prefilteredCubemap_.View(); }
    [[nodiscard]] VkImageView BrdfLutView() const noexcept { return brdfLut_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] uint32_t PrefilteredMipLevels() const noexcept { return kPrefilterMips; }
    [[nodiscard]] bool IsLoaded() const noexcept { return hdrLoaded_; }

  private:
    static constexpr uint32_t kEnvSize = 128;
    static constexpr uint32_t kIrradianceSize = 32;
    static constexpr uint32_t kPrefilterSize = 128;
    static constexpr uint32_t kPrefilterMips = 5;
    static constexpr uint32_t kBrdfSize = 512;

    // 程序化天空：渐变+太阳盘+光晕+地面反弹（HDR线性值）
    [[nodiscard]] static glm::vec3 SampleSky(glm::vec3 dir);

    void createRenderPasses(const Context& ctx);
    void createColorImage(const Context& ctx, Image& image, uint32_t size, VkFormat format, uint32_t mipLevels,
                          uint32_t layers);
    void destroyGenerationResources();

    // HDR文件加载实现
    bool LoadHDRTexture(const std::string& filePath);
    bool CreateCubemapFromHDR(int width, int height, void* pixels);

  private:
    // 私有辅助方法
    void setupIBL(const Context& ctx);
    void createCubePipeline();
    void createCubeFramebuffer();
    void renderCubeMapFaces(Texture& sourceTexture);
    void destroyCubePipeline();
    void destroyCubeFramebuffer();
    void createSampler(const Context& ctx);

    // Helper functions
    VkCommandBuffer beginCommandBuffer();
    void endCommandBuffer(VkCommandBuffer commandBuffer);

    const Context* ctx_ = nullptr;
    bool hdrLoaded_ = false;

    Image envCubemap_;
    Image irradianceCubemap_;
    Image prefilteredCubemap_;
    Image brdfLut_;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // 生成期离屏资源（生成完毕后可释放）
    VkRenderPass cubeColorPass_ = VK_NULL_HANDLE; // RGBA16F 颜色通道
    VkRenderPass brdfColorPass_ = VK_NULL_HANDLE; // RG16F 颜色通道
    std::vector<VkFramebuffer> irradianceFramebuffers_;
    std::vector<VkImageView> irradianceFaceViews_; // 帧缓冲附件视图，须保活至帧缓冲销毁
    std::vector<VkImageView> prefilterFaceViews_;
    std::vector<VkFramebuffer> prefilterFramebuffers_;
    VkImageView brdfView_ = VK_NULL_HANDLE;
    VkFramebuffer brdfFramebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout envSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool envDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet envSet_ = VK_NULL_HANDLE;

    // Cube map generation resources
    Render::GraphicsPipeline cubePipe_;
    VkDescriptorSetLayout cubeSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool cubeDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet cubeSet_ = VK_NULL_HANDLE;
    VkFramebuffer cubeFramebuffer_[6] = {VK_NULL_HANDLE};
    VkImageView cubeFaceViews_[6] = {VK_NULL_HANDLE};
};
} // namespace BigHero
