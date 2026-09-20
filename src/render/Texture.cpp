#include "render/Texture.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Buffer.h"
#include "render/Context.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_HDR // 启用HDR支持（白名单机制下必须用 STBI_ONLY_HDR）
#include <stb_image.h>

namespace BigHero
{
namespace
{
// IEEE 754 单精度 -> 半精度转换（用于HDR纹理上传，R16G16B16A16_SFLOAT）
uint16_t toHalf(float f) noexcept
{
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127;
    const uint32_t mant = x & 0x7FFFFFu;
    if (exp < -24)
        return static_cast<uint16_t>(sign); // 下溢为0
    if (exp > 15)
        return static_cast<uint16_t>(sign | 0x7C00u); // 上溢为Inf
    if (exp < -14)                                    // 非规格化
    {
        const uint32_t m = mant | 0x800000u;
        const int shift = -exp - 14;
        uint16_t r = static_cast<uint16_t>(sign | (m >> (shift + 13)));
        if (m & (1u << (shift + 12)))
            ++r;
        return r;
    }
    uint16_t r = static_cast<uint16_t>(sign | ((exp + 15) << 10) | (mant >> 13));
    if (mant & 0x1000u)
        ++r; // 舍入
    return r;
}
// 像素上传 + mip生成 + 采样器创建的公共尾部流程
void UploadPixels(const Context& ctx, const void* pixels, uint32_t width, uint32_t height, VkDeviceSize byteSize,
                  VkFormat format, Image& outImage, VkDevice device, VkSampler& outSampler)
{
    // GPU mip链需要blit + 采样线性过滤支持；不支持时退化为单级mip
    uint32_t mipLevels = Image::CalculateMipLevels(width, height);
    bool filterLinear = true;
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(ctx.PhysicalDevice(), format, &props);
        const bool canSampleLinear =
            (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        const bool canBlit = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0 &&
                             (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
        if (!canSampleLinear)
            filterLinear = false;
        if (!canSampleLinear || !canBlit)
            mipLevels = 1;
    }
    const VkFilter magFilter = filterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const VkFilter minFilter = filterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const VkSamplerMipmapMode mipmapMode =
        filterLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;

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
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = minFilter;
    samplerInfo.mipmapMode = mipmapMode;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = ctx.SamplerAnisotropyEnabled() ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = ctx.SamplerAnisotropyEnabled() ? ctx.MaxSamplerAnisotropy() : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    // maxLod 必须 <= 被采样图像 levelCount - 1（VUID-VkSamplerCreateInfo-maxLod-01973）。
    // 图像共 mipLevels 级（上文保证 >= 1），故上限为 mipLevels - 1；写成 mipLevels
    // 会越界 1 级，AMD 驱动据此做未定义的 lod 钳制，可致 GPU 页错误 → DEVICE_LOST。
    samplerInfo.maxLod = static_cast<float>(mipLevels - 1);
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

void Texture::CreateSolid(const Context& ctx, uint8_t r, uint8_t g, uint8_t b, bool sRGB)
{
    Destroy();
    device_ = ctx.Device();

    const std::array<uint8_t, 4> solid = {r, g, b, 255};
    const VkFormat format = sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    UploadPixels(ctx, solid.data(), 1, 1, sizeof(solid), format, image_, device_, sampler_);
}

void Texture::CreateFromFloatPixels(const Context& ctx, uint32_t width, uint32_t height, const float* pixels)
{
    Destroy();
    device_ = ctx.Device();

    // 转半精度：R16G16B16A16_SFLOAT 支持线性过滤，且避免 AMD 驱动对 32-bit 浮点纹理采样的不稳定
    std::vector<uint16_t> halfPixels(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < halfPixels.size(); ++i)
        halfPixels[i] = toHalf(pixels[i]);

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(halfPixels.size()) * sizeof(uint16_t);
    UploadPixels(ctx, halfPixels.data(), width, height, byteSize, VK_FORMAT_R16G16B16A16_SFLOAT, image_, device_,
                 sampler_);

    LOG_INFO("HDR浮点纹理创建成功: " << width << "x" << height);
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
