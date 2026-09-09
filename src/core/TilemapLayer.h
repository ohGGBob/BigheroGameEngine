#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace bighero {

// A tilemap layer: a 2D grid of tile indices plus a tile size and a set of
// animated/loop regions. Building block for 2D tile-based rendering.
class TilemapLayer {
public:
    TilemapLayer() {}
    TilemapLayer(int cols, int rows, int tileW, int tileH)
        : cols_(cols), rows_(rows), tileW_(tileW), tileH_(tileH),
          tiles_((std::size_t)cols * rows, 0) {}

    void Resize(int cols, int rows) {
        cols_ = cols; rows_ = rows;
        tiles_.assign((std::size_t)cols * rows, 0);
    }
    int Cols() const { return cols_; }
    int Rows() const { return rows_; }
    void SetTileSize(int tw, int th) { tileW_ = tw; tileH_ = th; }
    int TileW() const { return tileW_; }
    int TileH() const { return tileH_; }
    std::size_t TileCount() const { return tiles_.size(); }

    void SetTile(int x, int y, int tile) {
        if (x >= 0 && x < cols_ && y >= 0 && y < rows_)
            tiles_[(std::size_t)y * cols_ + x] = tile;
    }
    int GetTile(int x, int y) const {
        if (x < 0 || x >= cols_ || y < 0 || y >= rows_) return 0;
        return tiles_[(std::size_t)y * cols_ + x];
    }

    // World-space position of the top-left corner of tile (x,y).
    void TileOrigin(int x, int y, float& wx, float& wy) const {
        wx = (float)(x * tileW_);
        wy = (float)(y * tileH_);
    }

    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const { return visible_; }
    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_.c_str(); }

    bool IsSolid(int x, int y) const {
        return GetTile(x, y) == 0; // 0 = empty/solid placeholder
    }

private:
    int cols_ = 0, rows_ = 0;
    int tileW_ = 32, tileH_ = 32;
    std::vector<int> tiles_;
    std::string name_;
    bool visible_ = true;
};

} // namespace bighero
