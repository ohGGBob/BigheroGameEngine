#pragma once
#include "render/Image.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// 反射探针彩色立方图捕获目标（U2-L2 GPU 捕获 v1，单探针）：
// RGBA16F 彩色立方图（带 mip 链，供 roughness→LOD 预过滤近似）+ 深度立方图，
// 6 面独立渲染通道（颜色+深度）。逐帧用法：
//   1. RecordProbeCapture 起始：PrepareFrame(cmd)（SHADER_READ → COLOR_ATTACHMENT 屏障）
//   2. 逐面 RecordFace(cmd, face, drawScene)（drawScene 绑定捕获管线绘制场景）
//   3. 收尾 FinishFrame(cmd)：mip 链生成（vkCmdBlitImage 逐级降采样）+ 转 SHADER_READ
// 尺寸固定、不依赖交换链，创建一次即可。
class ReflectionCapture
{
  public:
    static constexpr int kFaceCount = 6;

    ReflectionCapture() = default;
    ~ReflectionCapture() { Destroy(); }

    ReflectionCapture(const ReflectionCapture&) = delete;
    ReflectionCapture& operator=(const ReflectionCapture&) = delete;

    void Create(const Context& ctx, uint32_t size = 128, uint32_t mipLevels = 4);
    void Destroy();

    // 录制单面（颜色+深度通道；调用方在回调内绑定捕获管线并绘制场景）。
    // 每帧首次调用前须先 PrepareFrame。
    void RecordFace(VkCommandBuffer cmd, int face, const std::function<void(VkCommandBuffer, int face)>& drawScene);

    // 全部面录制完成后调用：mip 链生成 + 全层级转 SHADER_READ_ONLY。
    void FinishFrame(VkCommandBuffer cmd);

    // 帧起始屏障：SHADER_READ_ONLY → COLOR_ATTACHMENT（未捕获过/首帧时安全空转或正向转换）
    void PrepareFrame(VkCommandBuffer cmd);

    [[nodiscard]] VkImageView View() const noexcept { return colorImage_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_; }
    [[nodiscard]] uint32_t Size() const noexcept { return size_; }
    [[nodiscard]] uint32_t MipLevels() const noexcept { return mipLevels_; }

  private:
    const Context* ctx_ = nullptr;
    Image colorImage_; // RGBA16F 立方图（mip 链）
    Image depthImage_; // D32 立方图（单 mip）
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::array<VkFramebuffer, kFaceCount> framebuffers_{};
    std::array<VkImageView, kFaceCount> colorFaceViews_{}; // 帧缓冲用（layer=f，mip0）
    std::array<VkImageView, kFaceCount> depthFaceViews_{};
    uint32_t size_ = 0;
    uint32_t mipLevels_ = 1;
    bool colorInShaderRead_ = false; // 颜色立方图是否处于 SHADER_READ（FinishFrame 后为 true）
};
} // namespace BigHero