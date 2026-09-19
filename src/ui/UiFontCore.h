#pragma once
// 运行时 UI 字体图集——纯逻辑部分（无 GPU 依赖，可离线单测）：
//   - GlyphKey：字形缓存键编码（码点 + 1/4 px 量化字号 → u64）；
//   - GlyphInfo：图集区域 + 承载/推进度量；
//   - AtlasShelf：shelf 装箱光标（(0,0) 保留白像素；容量不足返回 NeedGrow 触发扩容）；
//   - GlyphCache：命中缓存 + 插入序（扩容重排光栅化用）+ LRU 修剪。
// GPU 侧（stb_truetype 光栅化 / Image 上传 / 描述符）见 UiFontAtlas.h/.cpp。

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BigHero::Ui
{
// ---- 字形缓存键：高 32 位 = 1/4 px 量化字号，低 32 位 = Unicode 码点 ----
using GlyphKey = uint64_t;

[[nodiscard]] inline GlyphKey MakeGlyphKey(uint32_t codepoint, float sizePx)
{
    // 1/4 px 量化（UI 字号变化粒度远粗于此；0/负字号钳为 0）
    const uint32_t quant =
        sizePx > 0.0f ? static_cast<uint32_t>(sizePx * 4.0f + 0.5f) : 0u;
    return (static_cast<uint64_t>(quant) << 32) | static_cast<uint64_t>(codepoint);
}
[[nodiscard]] inline uint32_t GlyphKeyCodepoint(GlyphKey key) noexcept
{
    return static_cast<uint32_t>(key & 0xFFFFFFFFull);
}
[[nodiscard]] inline float GlyphKeySizePx(GlyphKey key) noexcept
{
    return static_cast<float>(key >> 32) * 0.25f;
}

// ---- 字形信息（图集像素坐标与度量） ----
struct GlyphInfo
{
    uint16_t u = 0, v = 0;    // 图集区域左上角（像素）
    uint16_t w = 0, h = 0;    // 位图尺寸（空白字形为 0）
    int16_t bearingX = 0;     // 相对笔点的横向承载（右为正）
    int16_t bearingY = 0;     // 相对基线的纵向承载（上为正；绘制时取负得屏幕 y）
    uint16_t advance = 0;     // x 推进（像素）
    [[nodiscard]] bool Empty() const noexcept { return w == 0 || h == 0; }
};

// ---- 图集 shelf 装箱器 ----
// (0,0) 像素保留为白色（纯色矩形经白像素直出），光标从 (1,1) 起；字形间 1px 间隙
// 抑制线性采样串色。行内放不下则换行；换行仍放不下（含单字形超尺寸）返回 NeedGrow。
struct AtlasShelf
{
    uint32_t width = 512;
    uint32_t height = 512;
    uint32_t cursorX = 1;
    uint32_t cursorY = 1;
    uint32_t rowHeight = 0;

    struct Slot
    {
        uint32_t u = 0;
        uint32_t v = 0;
    };
    enum class Result : uint8_t
    {
        Ok,
        NeedGrow
    };

    void Reset() noexcept
    {
        cursorX = 1;
        cursorY = 1;
        rowHeight = 0;
    }

    // 为 w×h 字形分配区域（含左右 1px 间隙）；1px 外边距由光标起点 (1,1) 与边界检查承担
    Result Alloc(uint32_t w, uint32_t h, Slot& out) noexcept
    {
        if (w == 0 || h == 0)
        {
            out = {0, 0}; // 空白字形不占图集
            return Result::Ok;
        }
        // 单字形 + 两侧 1px 间隙须装得下
        if (w + 2 > width || h + 2 > height)
            return Result::NeedGrow;
        if (cursorX + w + 1 > width)
        {
            cursorX = 1;
            cursorY += rowHeight + 1;
            rowHeight = 0;
        }
        if (cursorY + h + 1 > height - 1)
            return Result::NeedGrow;
        out = {cursorX, cursorY};
        cursorX += w + 1;
        rowHeight = rowHeight < h ? h : rowHeight;
        return Result::Ok;
    }
};

// ---- 字形缓存 ----
// 命中统计/插入序/LRU 均为纯逻辑：Find 命中即刷新 LRU 时戳；Trim 按最久未用驱逐。
// Order 为插入序（扩容时按此序重光栅化，保证图集重建后区域一致可用）。
class GlyphCache
{
  public:
    struct Entry
    {
        GlyphInfo info;
        uint64_t lastUse = 0;
    };

    // 命中返回指针（只读用途）；同时刷新 LRU 时戳
    [[nodiscard]] const GlyphInfo* Find(GlyphKey key)
    {
        const auto it = map_.find(key);
        if (it == map_.end())
            return nullptr;
        it->second.lastUse = ++clock_;
        return &it->second.info;
    }

    // 插入/更新：已存在则原地更新（不重复进插入序——扩容重光栅化路径依赖此语义）
    void Insert(GlyphKey key, const GlyphInfo& info)
    {
        const auto it = map_.find(key);
        if (it != map_.end())
        {
            it->second.info = info;
            it->second.lastUse = ++clock_;
            return;
        }
        Entry e;
        e.info = info;
        e.lastUse = ++clock_;
        map_.emplace(key, e);
        order_.push_back(key);
    }

    [[nodiscard]] const GlyphInfo* Peek(GlyphKey key) const
    {
        const auto it = map_.find(key);
        return it != map_.end() ? &it->second.info : nullptr;
    }

    [[nodiscard]] size_t Size() const noexcept { return map_.size(); }
    [[nodiscard]] const std::vector<GlyphKey>& Order() const noexcept { return order_; }
    [[nodiscard]] uint64_t Clock() const noexcept { return clock_; }

    // LRU 修剪：驱逐最久未用至 maxCount；返回驱逐数
    size_t Trim(size_t maxCount)
    {
        if (map_.size() <= maxCount)
            return 0;
        const size_t evictCount = map_.size() - maxCount;
        // 部分排序：nth_element 按字典序不满足 LRU，这里直接稳定排序（缓存规模小，O(n log n) 足够）
        std::vector<std::pair<uint64_t, GlyphKey>> byAge;
        byAge.reserve(map_.size());
        for (const auto& [k, e] : map_)
            byAge.emplace_back(e.lastUse, k);
        // 插入排序按 lastUse 升序（规模小且近似有序）
        for (size_t i = 1; i < byAge.size(); ++i)
        {
            auto cur = byAge[i];
            size_t j = i;
            while (j > 0 && byAge[j - 1].first > cur.first)
            {
                byAge[j] = byAge[j - 1];
                --j;
            }
            byAge[j] = cur;
        }
        size_t evicted = 0;
        for (size_t i = 0; i < evictCount && i < byAge.size(); ++i)
        {
            map_.erase(byAge[i].second);
            ++evicted;
        }
        // 重建插入序（仅保留存活键）
        std::vector<GlyphKey> kept;
        kept.reserve(order_.size());
        for (const GlyphKey k : order_)
            if (map_.find(k) != map_.end())
                kept.push_back(k);
        order_.swap(kept);
        return evicted;
    }

    void Clear() noexcept
    {
        map_.clear();
        order_.clear();
    }

  private:
    std::unordered_map<GlyphKey, Entry> map_;
    std::vector<GlyphKey> order_; // 插入序
    uint64_t clock_ = 0;
};
} // namespace BigHero::Ui
