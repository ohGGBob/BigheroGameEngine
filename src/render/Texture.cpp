#include "render/Texture.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Buffer.h"
#include "render/Context.h"

#include <array>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include <stb_image.h>

namespace BigHero
{
namespace
{
// 像素上传 + mip生成 + 采样器创建的公共尾部流程
void UploadPixels(const Context& ctx, const void* pixels, uint32_t width, uint32_t height, VkDeviceSize byteSize,
                  VkFormat format, Image& outImage, VkDevice device, VkSampler& outSampler)
{
    // GPU mip链需要blit线性过滤支持；不支持时退化为单级mip
    uint32_t mipLevels = Image::CalculateMipLevels(width, height);
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(ctx.PhysicalDevice(), format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0)
            mipLevels = 1;
    }

    Buffer staging;
    staging.Create(ctx, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.UploadData(ctx, pixels, byteSize);

    outImage.Create(ctx, width, height, format,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

    outImage.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    outImage.CopyFromBuffer(ctx, staging.Get());
    if (mipLevels > 1)
    {
        outImage.GenerateMipmaps(ctx);
    }
    else
    {
        outImage.TransitionLayout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = ctx.SamplerAnisotropyEnabled() ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = ctx.SamplerAnisotropyEnabled() ? ctx.MaxSamplerAnisotropy() : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &outSampler), "创建采样器");
}
} // namespace

void Texture::CreateFromFile(const Context& ctx, const char* path, bool sRGB)
{
    Destroy();
    device_ = ctx.Device();

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr)
        throw std::runtime_error(std::string("Texture: 加载纹理失败 -> ") + path);

    const auto w = static_cast<uint32_t>(width);
    const auto h = static_cast<uint32_t>(height);
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(w) * h * 4;

    // 颜色贴图用SRGB格式（硬件采样时gamma->线性）；法线/数据贴图用UNORM
    const VkFormat format = sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    UploadPixels(ctx, pixels, w, h, byteSize, format, image_, device_, sampler_);
    stbi_image_free(pixels);

    LOG_INFO("纹理加载成功: " << path << " (" << w << "x" << h << ")");
}

void Texture::CreateFlatNormal(const Context& ctx)
{
    Destroy();
    device_ = ctx.Device();

    // RG编码(0.5,0.5) B编码(1.0)：无扰动的切线空间法线
    const std::array<uint8_t, 4> flatNormal = {128, 128, 255, 255};
    UploadPixels(ctx, flatNormal.data(), 1, 1, sizeof(flatNormal), VK_FORMAT_R8G8B8A8_UNORM, image_, device_, sampler_);
}

void Texture::CreateCheckerboard(const Context& ctx, uint32_t size, uint32_t cells)
{
    Destroy();
    device_ = ctx.Device();

    // 生成RGBA8棋盘格像素
    const uint32_t cellSize = size / cells;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y)
    {
        for (uint32_t x = 0; x < size; ++x)
        {
            const bool bright = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const uint8_t r = bright ? 235 : 70;
            const uint8_t g = bright ? 235 : 72;
            const uint8_t b = bright ? 240 : 95;
            const size_t offset = (static_cast<size_t>(y) * size + x) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = 255;
        }
    }

    UploadPixels(ctx, pixels.data(), size, size, static_cast<VkDeviceSize>(pixels.size()), VK_FORMAT_R8G8B8A8_SRGB,
                 image_, device_, sampler_);

    LOG_INFO("棋盘格纹理创建成功: " << size << "x" << size);
}

void Texture::Destroy()
{
    if (sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    image_.Destroy();
    device_ = VK_NULL_HANDLE;
}

void Texture::MoveFrom(Texture& other) noexcept
{
    image_ = std::move(other.image_);
    sampler_ = other.sampler_;
    device_ = other.device_;

    other.sampler_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
}
} // namespace BigHero

