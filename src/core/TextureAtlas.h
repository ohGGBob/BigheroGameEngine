#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// Texture atlas: pack named rectangular regions into a single texture space.
// Simple shelf (row) packer with query by name.
class TextureAtlas {
public:
    struct Region {
        std::string name;
        int x, y, w, h;
    };

    // Reserve/pack a region of the given size; returns its index, or -1 if it
    // cannot fit (falls to a new row when shelf is full).
    int AddRegion(const char* name, int w, int h) {
        if (w <= 0 || h <= 0 || w > pageW_ || h > pageH_) return -1;
        int rx = cursorX_, ry = cursorY_;
        if (rx + w > pageW_) { // wrap to next row
            rx = 0; ry += rowHeight_;
            cursorX_ = 0;
            rowHeight_ = 0;
            if (ry + h > pageH_) return -1;
        }
        cursorX_ = rx + w;
        if (h > rowHeight_) rowHeight_ = h;
        regions_.push_back({std::string(name), rx, ry, w, h});
        return (int)regions_.size() - 1;
    }

    int FindRegion(const char* name) const {
        for (std::size_t i = 0; i < regions_.size(); ++i)
            if (regions_[i].name == name) return (int)i;
        return -1;
    }
    const Region* GetRegion(int index) const {
        if (index < 0 || index >= (int)regions_.size()) return nullptr;
        return &regions_[index];
    }

    // Normalized UVs of a region.
    void GetUVs(int index, float& u0, float& v0, float& u1, float& v1) const {
        const Region* r = GetRegion(index);
        if (!r || pageW_ <= 0 || pageH_ <= 0) { u0=v0=0; u1=v1=1; return; }
        u0 = (float)r->x / pageW_;
        v0 = (float)r->y / pageH_;
        u1 = (float)(r->x + r->w) / pageW_;
        v1 = (float)(r->y + r->h) / pageH_;
    }

    void SetPageSize(int w, int h) { pageW_ = w; pageH_ = h; cursorX_ = cursorY_ = 0; rowHeight_ = 0; }
    std::size_t RegionCount() const { return regions_.size(); }
    bool Empty() const { return regions_.empty(); }
    void Clear() { regions_.clear(); cursorX_ = cursorY_ = 0; rowHeight_ = 0; }

private:
    std::vector<Region> regions_;
    int pageW_ = 0, pageH_ = 0;
    int cursorX_ = 0, cursorY_ = 0;
    int rowHeight_ = 0;
};

} // namespace bighero
