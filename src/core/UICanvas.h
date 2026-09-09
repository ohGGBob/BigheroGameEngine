#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// UICanvas: a drawable surface (render target) associated with screen-space
// UI. Tracks its pixel size, clear color, and a list of canvas roots that
// belong to it. No rendering backend.
class UICanvas {
public:
    UICanvas() {}
    UICanvas(int width, int height) { Resize(width, height); }

    void Resize(int width, int height) {
        width_ = width < 1 ? 1 : width;
        height_ = height < 1 ? 1 : height;
    }
    int Width() const { return width_; }
    int Height() const { return height_; }
    float Aspect() const { return width_ / (float)height_; }

    void SetClear(float r, float g, float b, float a = 1.0f) {
        cr_=r; cg_=g; cb_=b; ca_=a;
    }
    void ClearColor(float& r, float& g, float& b, float& a) const { r=cr_; g=cg_; b=cb_; a=ca_; }

    void SetSortingOrder(int order) { sortOrder_ = order; }
    int SortingOrder() const { return sortOrder_; }

    void AddRoot(void* node) { roots_.push_back(node); }
    std::size_t RootCount() const { return roots_.size(); }
    void* Root(std::size_t i) const { return i < roots_.size() ? roots_[i] : nullptr; }
    void ClearRoots() { roots_.clear(); }

private:
    int width_ = 1, height_ = 1;
    float cr_=0, cg_=0, cb_=0, ca_=1;
    int sortOrder_ = 0;
    std::vector<void*> roots_;
};

} // namespace bighero
