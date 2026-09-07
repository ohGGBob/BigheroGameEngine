#pragma once
#include <utility>
#include <vector>
#include <cstddef>

namespace bighero {

// Simple axis-aligned layout box model for UI. Children are laid out either
// in a horizontal row or vertical column inside the parent box.
class LayoutBox {
public:
    enum class Orientation { Horizontal, Vertical };

    struct Box {
        float x = 0, y = 0, w = 0, h = 0;
        float pad = 0;
        float spacing = 0;
        Orientation orientation = Orientation::Vertical;
        std::vector<Box> children;
    };

    // Compute child positions given the parent box dimensions.
    static void Layout(Box& box) {
        float cursor = box.pad;
        for (auto& c : box.children) {
            if (box.orientation == Orientation::Vertical) {
                c.x = box.x + box.pad;
                c.y = box.y + cursor;
                cursor += c.h + box.spacing;
            } else {
                c.x = box.x + cursor;
                c.y = box.y + box.pad;
                cursor += c.w + box.spacing;
            }
            Layout(c); // recurse
        }
    }

    // Add a child box and return its index.
    static std::size_t AddChild(Box& parent, Box child) {
        parent.children.push_back(child);
        return parent.children.size() - 1;
    }

    // Total content width/height including padding.
    static void Measure(Box& box, float& outW, float& outH) {
        float w = 0, h = 0;
        if (box.orientation == Orientation::Vertical) {
            float ch = 0, cw = 0;
            for (auto& c : box.children) { ch += c.h + box.spacing; if (c.w > cw) cw = c.w; }
            if (!box.children.empty()) ch -= box.spacing;
            h = ch + box.pad * 2;
            w = cw + box.pad * 2;
        } else {
            float cw = 0, ch = 0;
            for (auto& c : box.children) { cw += c.w + box.spacing; if (c.h > ch) ch = c.h; }
            if (!box.children.empty()) cw -= box.spacing;
            w = cw + box.pad * 2;
            h = ch + box.pad * 2;
        }
        outW = w; outH = h;
    }
};

} // namespace bighero
