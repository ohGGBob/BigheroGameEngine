#pragma once
#include "render/Image.h"
#include <vulkan/vulkan.h>
#include <cstdint>

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

        // 从图像文件（PNG/JPG/BMP等，stb_image支持）加载纹理，上传GPU并创建采样器
        void CreateFromFile(const Context& ctx, const char* path);
        // 生成 size×size RGBA8 棋盘格（cells×cells 格），上传GPU并创建采样器
        void CreateCheckerboard(const Context& ctx, uint32_t size = 512, uint32_t cells = 8);
        void Destroy();

        [[nodiscard]] VkImageView View() const noexcept { return image_.View(); }
        [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
        [[nodiscard]] bool IsValid() const noexcept { return sampler_ != VK_NULL_HANDLE && image_.View() != VK_NULL_HANDLE; }

    private:
        void MoveFrom(Texture& other) noexcept;

        Image image_;
        VkSampler sampler_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
    };
}
