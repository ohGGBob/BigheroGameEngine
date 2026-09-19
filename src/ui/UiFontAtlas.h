#pragma once
// 运行时 UI 字体图集——GPU 侧：stb_truetype 动态字集光栅化 + R8 图集上传 + 描述符。
// 首次用到某字形时才光栅化进图集（512×512 起步，放不下时按倍扩容并整图重光栅化，
// 上限 2048²）；扩容后自增 Version() 并自动重写描述符（调用方无须干预）。
// 缓存键/装箱/LRU 纯逻辑见 UiFontCore.h（可离线单测）。

#include "ui/UiFontCore.h"
#include "render/Image.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct stbtt_fontinfo;

namespace BigHero
{
class Context;

namespace Ui
{
class UiFontAtlas
{
  public:
    static constexpr uint32_t kInitialSize = 512;
    static constexpr uint32_t kMaxSize = 2048;

    UiFontAtlas() = default;
    ~UiFontAtlas() { Destroy(); }
    UiFontAtlas(const UiFontAtlas&) = delete;
    UiFontAtlas& operator=(const UiFontAtlas&) = delete;

    // 读入 TTF/TTC 字体文件字节并解析（失败置 FontLoaded=false，优雅降级为无文本）
    void LoadTtf(const std::string& path);
    [[nodiscard]] bool FontLoaded() const noexcept { return fontLoaded_; }

    // 创建图集图像（白像素保留于 (0,0)）+ 采样器 + 描述符布局/池/集合
    void Init(const Context& ctx);
    void Destroy();
    [[nodiscard]] bool IsValid() const noexcept { return atlas_.View() != VK_NULL_HANDLE && set_ != VK_NULL_HANDLE; }

    // 取字形：命中缓存直接返回；未命中光栅化进图集（必要时扩容重建整图）。
    // 字体未加载/码点无字形时返回空字形（advance 回退表驱动，调用方用 Advance() 取度量）。
    [[nodiscard]] const GlyphInfo& Glyph(const Context& ctx, uint32_t codepoint, float sizePx);

    // 仅取 x 推进度量（不进图集）；字体未加载返回 0
    [[nodiscard]] float Advance(uint32_t codepoint, float sizePx);
    // 字体首行基线高度（正像素，自顶部到基线）；未加载回退 sizePx * 0.8f
    [[nodiscard]] float Ascent(float sizePx);

    // ---- 渲染侧资源 ----
    [[nodiscard]] VkImageView View() const noexcept { return atlas_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] VkDescriptorSetLayout Layout() const noexcept { return layout_; }
    [[nodiscard]] VkDescriptorSet Set() const noexcept { return set_; }
    [[nodiscard]] uint64_t Version() const noexcept { return version_; } // 扩容/重建后 +1
    [[nodiscard]] glm::vec2 WhitePixelUv() const noexcept
    {
        return glm::vec2(0.5f / static_cast<float>(shelf_.width), 0.5f / static_cast<float>(shelf_.height));
    }
    [[nodiscard]] uint32_t TextureWidth() const noexcept { return shelf_.width; }
    [[nodiscard]] uint32_t TextureHeight() const noexcept { return shelf_.height; }

  private:
    void CreateAtlasImage(const Context& ctx, uint32_t size);
    void CreateDescriptors(VkDevice dev);
    void BindDescriptor(); // 当前图像视图写入描述符集合（Init/扩容后调用）
    void RasterizeGlyph(uint32_t codepoint, float sizePx, GlyphInfo& out); // 纯 CPU 光栅化（不上传）
    void UploadGlyphBitmap(const Context& ctx, uint32_t codepoint, float sizePx, const GlyphInfo& info);
    void UploadRegion(const Context& ctx, uint32_t u, uint32_t v, uint32_t w, uint32_t h, const uint8_t* pixels);
    bool GrowAtlas(const Context& ctx); // 倍增尺寸并按插入序重光栅化全部字形

    stbtt_fontinfo* font_ = nullptr;        // stbtt_InitFont 就绪后非空（实现文件内分配）
    std::vector<uint8_t> fontBytes_;        // 字体文件字节（stbtt 持有指针，须与 font_ 同寿命）
    bool fontLoaded_ = false;
    float ascentScale_ = 0.0f;              // 基线度量缓存（像素级，随 Ascent 换算）

    AtlasShelf shelf_;
    GlyphCache cache_;
    GlyphInfo missing_;                     // 无字形回退（空白 + 0 advance）
    uint64_t version_ = 0;

    Image atlas_;                           // R8_UNORM 单通道图集
    VkImageLayout atlasLayout_ = VK_IMAGE_LAYOUT_UNDEFINED; // 空闲态布局（UploadRegion 维护）
    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};
} // namespace Ui
} // namespace BigHero
