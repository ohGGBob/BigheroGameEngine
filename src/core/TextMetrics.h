#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace bighero {

// Lightweight raster text measurement using a per-char width table or a
// uniform fallback advance. Used for pre-layout without a real font loader.
class TextMetrics {
public:
    // Monospace: all chars use `advance`. Proportional uses the width table
    // when a char is present, else falls back to `defaultAdvance`.
    explicit TextMetrics(float defaultAdvance = 8.0f, float lineHeight = 16.0f,
                         bool monospace = false)
        : defaultAdvance_(defaultAdvance), lineHeight_(lineHeight), monospace_(monospace) {}

    void SetCharacterWidth(char c, float w) { widths_[c] = w; }
    void SetLineHeight(float h) { lineHeight_ = h; }
    float LineHeight() const { return lineHeight_; }
    float DefaultAdvance() const { return defaultAdvance_; }

    float MeasureWidth(const std::string& text) const {
        float w = 0;
        for (char c : text) {
            if (monospace_) w += defaultAdvance_;
            else {
                auto it = widths_.find(c);
                w += (it != widths_.end()) ? it->second : defaultAdvance_;
            }
        }
        return w;
    }

    // Width of the widest line plus line count height (wrap by '\n').
    void Measure(const std::string& text, float& outW, float& outH) const {
        float maxW = 0;
        std::size_t lines = 1;
        float cur = 0;
        for (char c : text) {
            if (c == '\n') { if (cur > maxW) maxW = cur; cur = 0; ++lines; continue; }
            if (monospace_) cur += defaultAdvance_;
            else {
                auto it = widths_.find(c);
                cur += (it != widths_.end()) ? it->second : defaultAdvance_;
            }
            if (cur > maxW) maxW = cur;
        }
        if (cur > maxW) maxW = cur;
        outW = maxW;
        outH = (float)lines * lineHeight_;
    }

    std::size_t CharacterCount(const std::string& text) const { return text.size(); }

private:
    float defaultAdvance_;
    float lineHeight_;
    bool monospace_;
    std::map<char,float> widths_;
};

} // namespace bighero
