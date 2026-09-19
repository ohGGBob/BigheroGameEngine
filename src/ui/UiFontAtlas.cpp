// 运行时 UI 字体图集 GPU 实现：stb_truetype 动态字集光栅化 + R8 图集上传 + 描述符维护。
// 纯逻辑（键编码/shelf 装箱/LRU）在 UiFontCore.h；本文件是唯一定义 STB_TRUETYPE_IMPLEMENTATION 的翻译单元。
#include "ui/UiFontAtlas.h"

#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Buffer.h"
#include "render/Context.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <cstring>
#include <fstream>
#include <vector>

namespace BigHero::Ui
{
namespace
{
// 读取整个文件（字体文件最大几 MB，一次性读入内存与 stbtt 语义一致）
bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    const std::streamsize size = file.tellg();
    if (size <= 0)
        return false;
    file.seekg(0);
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}
} // namespace

void UiFontAtlas::LoadTtf(const std::string& path)
{
    fontLoaded_ = false;
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes) || bytes.empty())
    {
        LOG_WARN("字体读取失败: " << path << "（运行时 UI 文本将不可用）");
        return;
    }
    // TTC 支持：取首个字体面（与编辑器覆盖层加载 wqy/msyh.ttc 的行为一致）
    const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    auto* info = new stbtt_fontinfo{};
    if (offset < 0 || !stbtt_InitFont(info, bytes.data(), offset))
    {
        LOG_WARN("字体解析失败: " << path << "（stbtt_InitFont 失败）");
        delete info;
        return;
    }
    delete font_;
    font_ = info;
    fontBytes_ = std::move(bytes); // stbtt 持有内部指针，字节须与 font_ 同寿命
    fontLoaded_ = true;
    LOG_INFO("运行时 UI 字体已加载: " << path);
}

void UiFontAtlas::Init(const Context& ctx)
{
    Destroy();
    device_ = ctx.Device();
    CreateAtlasImage(ctx, kInitialSize);
    CreateDescriptors(device_);
}

void UiFontAtlas::CreateAtlasImage(const Context& ctx, uint32_t size)
{
    shelf_.width = size;
    shelf_.height = size;
    shelf_.Reset();
    atlas_.Create(ctx, size, size, VK_FORMAT_R8_UNORM,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    atlasLayout_ = VK_IMAGE_LAYOUT_UNDEFINED; // 新图像空闲态由 UploadRegion 维护流转
    // 保留 (0,0) 白像素：纯色矩形统一采样白点直出（shader: alpha *= tex.r）
    const uint8_t white = 255;
    UploadRegion(ctx, 0, 0, 1, 1, &white);
}

void UiFontAtlas::CreateDescriptors(VkDevice dev)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &layout_), "创建 UI 图集描述符布局");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &pool_), "创建 UI 图集描述符池");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout_;
    VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &set_), "分配 UI 图集描述符集");

    // 线性采样 + clamp-to-edge（字形间 1px 间隙抑制线性采样串色）
    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_LINEAR;
    samp.minFilter = VK_FILTER_LINEAR;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(vkCreateSampler(dev, &samp, nullptr, &sampler_), "创建 UI 图集采样器");

    BindDescriptor();
}

void UiFontAtlas::BindDescriptor()
{
    if (set_ == VK_NULL_HANDLE || atlas_.View() == VK_NULL_HANDLE)
        return;
    VkDescriptorImageInfo imageInfo{sampler_, atlas_.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void UiFontAtlas::Destroy()
{
    cache_.Clear();
    if (device_ != VK_NULL_HANDLE)
    {
        if (sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_, sampler_, nullptr);
        if (set_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE)
            vkFreeDescriptorSets(device_, pool_, 1, &set_);
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, pool_, nullptr);
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    }
    atlas_.Destroy();
    atlasLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    sampler_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    delete font_;
    font_ = nullptr;
    fontBytes_.clear();
    fontBytes_.shrink_to_fit();
    fontLoaded_ = false;
    version_ = 0;
    missing_ = GlyphInfo{};
}

float UiFontAtlas::Advance(uint32_t codepoint, float sizePx)
{
    if (!fontLoaded_)
        return 0.0f;
    int advanceRaw = 0, lsb = 0;
    const float scale = stbtt_ScaleForPixelHeight(font_, sizePx);
    stbtt_GetCodepointHMetrics(font_, static_cast<int>(codepoint), &advanceRaw, &lsb);
    return advanceRaw * scale;
}

float UiFontAtlas::Ascent(float sizePx)
{
    if (!fontLoaded_)
        return sizePx * 0.8f;
    int ascentRaw = 0, descentRaw = 0, lineGap = 0;
    const float scale = stbtt_ScaleForPixelHeight(font_, sizePx);
    stbtt_GetFontVMetrics(font_, &ascentRaw, &descentRaw, &lineGap);
    return ascentRaw * scale;
}

const GlyphInfo& UiFontAtlas::Glyph(const Context& ctx, uint32_t codepoint, float sizePx)
{
    if (!fontLoaded_ || !IsValid())
        return missing_;
    const GlyphKey key = MakeGlyphKey(codepoint, sizePx);
    if (const GlyphInfo* cached = cache_.Find(key))
        return *cached;

    // 首次用到该字形：光栅化 + 装箱 + 上传（动态字集）
    GlyphInfo info;
    RasterizeGlyph(codepoint, sizePx, info);
    AtlasShelf::Slot slot{};
    if (shelf_.Alloc(info.w, info.h, slot) != AtlasShelf::Result::Ok)
    {
        if (!GrowAtlas(ctx))
        {
            LOG_WARN("UI 图集已达上限 " << kMaxSize << "，字形丢弃（codepoint=" << codepoint << "）");
            cache_.Insert(key, GlyphInfo{}); // 记空白防逐帧重复尝试
            return missing_;
        }
        // 扩容后重试装箱（shelf 已复位、缓存已重光栅化）
        if (shelf_.Alloc(info.w, info.h, slot) != AtlasShelf::Result::Ok)
        {
            cache_.Insert(key, GlyphInfo{});
            return missing_;
        }
    }
    info.u = static_cast<uint16_t>(slot.u);
    info.v = static_cast<uint16_t>(slot.v);
    if (!info.Empty())
        UploadGlyphBitmap(ctx, codepoint, sizePx, info);
    cache_.Insert(key, info);
    const GlyphInfo* stored = cache_.Peek(key);
    return stored != nullptr ? *stored : missing_;
}

// 光标位图（纯 CPU，不上传）：bounding box + 承载/推进度量
void UiFontAtlas::RasterizeGlyph(uint32_t codepoint, float sizePx, GlyphInfo& out)
{
    const float scale = stbtt_ScaleForPixelHeight(font_, sizePx);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(font_, static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);
    out.bearingX = static_cast<int16_t>(x0);
    out.bearingY = static_cast<int16_t>(y0);
    int advanceRaw = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(font_, static_cast<int>(codepoint), &advanceRaw, &lsb);
    out.advance = static_cast<uint16_t>(static_cast<float>(advanceRaw) * scale + 0.5f);

    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w <= 0 || h <= 0)
    {
        out.w = out.h = 0; // 空白字形（空格/无轮廓），仅保留 advance
        return;
    }
    out.w = static_cast<uint16_t>(w);
    out.h = static_cast<uint16_t>(h);
}

void UiFontAtlas::UploadGlyphBitmap(const Context& ctx, uint32_t codepoint, float sizePx, const GlyphInfo& info)
{
    const float scale = stbtt_ScaleForPixelHeight(font_, sizePx);
    std::vector<uint8_t> pixels(static_cast<size_t>(info.w) * static_cast<size_t>(info.h), 0);
    stbtt_MakeCodepointBitmap(font_, pixels.data(), info.w, info.h, info.w, scale, scale,
                              static_cast<int>(codepoint));
    UploadRegion(ctx, info.u, info.v, info.w, info.h, pixels.data());
}

void UiFontAtlas::UploadRegion(const Context& ctx, uint32_t u, uint32_t v, uint32_t w, uint32_t h,
                               const uint8_t* pixels)
{
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h);
    if (bytes == 0 || pixels == nullptr)
        return;

    // SubmitOneTime 内部 QueueWaitIdle：staging 生命周期可 wholly 置于 lambda 内。
    // 空闲态布局始终维护在 atlasLayout_：首次（UNDEFINED）整体转换丢弃无碍；后续上传自
    // SHADER_READ_ONLY 转入 TRANSFER_DST，避免 UNDEFINED 转换把已上传字形整体作废。
    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            Buffer staging;
            staging.Create(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            staging.UploadData(ctx, pixels, bytes);

            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.oldLayout = atlasLayout_;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = atlas_.Get();
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            toTransfer.srcAccessMask =
                atlasLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT : 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            const VkPipelineStageFlags srcStage =
                atlasLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                                         : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &toTransfer);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {static_cast<int32_t>(u), static_cast<int32_t>(v), 0};
            region.imageExtent = {w, h, 1};
            vkCmdCopyBufferToImage(cmd, staging.Get(), atlas_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier toShader = toTransfer;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &toShader);
        });
    atlasLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// 倍增图集并按插入序重光栅化全部字形。旧图像可能被在飞帧采样：先设备级 WaitIdle 再销毁
//（扩容为低频事件——首次用到新字形/新字号时一次，几百帧内通常 1~3 次）。
bool UiFontAtlas::GrowAtlas(const Context& ctx)
{
    const uint32_t next = shelf_.width * 2;
    if (next > kMaxSize)
        return false;
    LOG_INFO("UI 字体图集扩容: " << shelf_.width << " -> " << next << "（已缓存 " << cache_.Size() << " 字形）");
    ctx.WaitIdle();

    const std::vector<GlyphKey> order = cache_.Order();
    const uint32_t newSize = next;
    atlas_.Destroy();
    CreateAtlasImage(ctx, newSize); // 复位 shelf + 白像素

    cache_.Clear();
    for (const GlyphKey key : order)
    {
        const uint32_t cp = GlyphKeyCodepoint(key);
        const float sz = GlyphKeySizePx(key);
        GlyphInfo info;
        RasterizeGlyph(cp, sz, info);
        AtlasShelf::Slot slot{};
        if (shelf_.Alloc(info.w, info.h, slot) != AtlasShelf::Result::Ok)
        {
            cache_.Insert(key, GlyphInfo{}); // 扩容后仍装不下（理论不可达），防御性记空
            continue;
        }
        info.u = static_cast<uint16_t>(slot.u);
        info.v = static_cast<uint16_t>(slot.v);
        if (!info.Empty())
            UploadGlyphBitmap(ctx, cp, sz, info);
        cache_.Insert(key, info);
    }
    ++version_;
    BindDescriptor(); // 图集视图变化：重写描述符集合
    return true;
}
} // namespace BigHero::Ui
