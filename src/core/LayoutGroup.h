#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// LayoutGroup: arranges child layout rects in a row/column/grid keeping a
// spacing and alignment. Pure CPU-side layout computation. Self-contained.
class LayoutGroup {
public:
    enum class Direction { Horizontal, Vertical, Grid };

    LayoutGroup() {}
    explicit LayoutGroup(Direction d) : dir_(d) {}

    void SetDirection(Direction d) { dir_ = d; }
    Direction Current() const { return dir_; }
    void SetSpacing(float s) { spacing_ = s; }
    float Spacing() const { return spacing_; }
    void SetPadding(float p) { padding_ = p; }
    float Padding() const { return padding_; }

    // Given child sizes (height or width depending on direction), compute
    // total extent of the group.
    float TotalExtent(const std::vector<float>& sizes) const {
        float total = 0.0f;
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            total += sizes[i];
            if (i) total += spacing_;
        }
        return total + padding_ * 2.0f;
    }

    // Compute the position of a child along the main axis given prior sizes.
    float ChildOffset(const std::vector<float>& priorBefore) const {
        float off = padding_;
        for (auto s : priorBefore) off += s + spacing_;
        return off;
    }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetAlign(int a) { align_ = a; }
    int Align() const { return align_; }

private:
    Direction dir_ = Direction::Horizontal;
    float spacing_ = 4.0f, padding_ = 4.0f;
    int align_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
