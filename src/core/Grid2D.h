#pragma once
// 网格（Grid2D）：2D 均匀网格容器，提供按行列的安全访问与邻域迭代。
// 纯标准库、仅头文件。
//
// 商业化价值：瓦片地图、寻路格点、图像/高度图、碰撞分区的核心容器；
// 将二维数据铺平为一维存储，保证缓存友好，并提供行列索引访问。

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace BigHero::Core
{
template<typename T> class Grid2D
{
  public:
    Grid2D() = default;
    Grid2D(size_t w, size_t h, const T& fill = T())
    {
        Resize(w, h, fill);
    }

    void Resize(size_t w, size_t h, const T& fill = T())
    {
        w_ = w;
        h_ = h;
        cells_.assign(w * h, fill);
    }

    [[nodiscard]] size_t Width() const { return w_; }
    [[nodiscard]] size_t Height() const { return h_; }
    [[nodiscard]] size_t Count() const { return cells_.size(); }
    [[nodiscard]] bool Empty() const { return cells_.empty(); }

    T& At(size_t x, size_t y)
    {
        if (x >= w_ || y >= h_)
            throw std::out_of_range("Grid2D index out of range");
        return cells_[y * w_ + x];
    }
    const T& At(size_t x, size_t y) const
    {
        if (x >= w_ || y >= h_)
            throw std::out_of_range("Grid2D index out of range");
        return cells_[y * w_ + x];
    }

    // 越界返回默认构造值（安全读取，不抛异常）。
    T Get(size_t x, size_t y) const
    {
        if (x >= w_ || y >= h_)
            return T{};
        return cells_[y * w_ + x];
    }

    bool InBounds(size_t x, size_t y) const { return x < w_ && y < h_; }

    // 填充整个网格。
    void Fill(const T& v) { for (auto& c : cells_) c = v; }

    const std::vector<T>& Data() const { return cells_; }
    std::vector<T>& Data() { return cells_; }

    // 邻居数量（4 邻接：左右上下）。
    static constexpr int Neighbor4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

  private:
    size_t w_ = 0, h_ = 0;
    std::vector<T> cells_;
};
} // namespace BigHero::Core
