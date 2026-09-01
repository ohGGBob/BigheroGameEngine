#pragma once
#include "render/Image.h"
#include <cstdint>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// 程序化纹理：生成棋盘格图案并上传为可采样图像
class Texture
{
  public:
    Texture() = default;
    ~Texture() { Destroy(); }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept { MoveFrom(other); }
    Texture& operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    // 从图像文件（PNG/JPG/BMP，stb_image支持）加载纹理并创建采样器
    // sRGB=true用于反照率类颜色贴图；法线/数据类贴图传false（UNORM采样）
    void CreateFromFile(const Context& ctx, const char* path, bool sRGB = true);
    // 生成 size×size RGBA8 棋盘格（cells×cells 格），上传GPU并创建采样器
    void CreateCheckerboard(const Context& ctx, uint32_t size = 512, uint32_t cells = 8);
    // 1x1平坦法线图（0.5,0.5,1），用作法线贴图缺失时的回退
    void CreateFlatNormal(const Context& ctx);
    void Destroy();

    [[nodiscard]] VkImageView View() const noexcept { return image_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] bool IsValid() const noexcept
    {
        return sampler_ != VK_NULL_HANDLE && image_.View() != VK_NULL_HANDLE;
    }

  private:
    void MoveFrom(Texture& other) noexcept;

    Image image_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};
} // namespace BigHero
