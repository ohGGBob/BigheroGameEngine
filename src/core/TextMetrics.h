#pragma once
#include <vector>
#include <cstddef>
#include <cmath>
#include <string>

namespace bighero {

// TextMetrics: measured layout metrics for a run of text — width, height,
// ascender/descender, line count, and per-character advance table. Pure
// CPU-side measurement snapshot used by UI/text layout.
class TextMetrics {
public:
    TextMetrics() {}
    explicit TextMetrics(float fontSize) : fontSize_(fontSize) {}

    void SetFontSize(float s) { fontSize_ = s < 1 ? 1 : s; }
    float FontSize() const { return fontSize_; }
    void SetLineHeight(float h) { lineHeight_ = h < 1 ? 1 : h; }
    float LineHeight() const { return lineHeight_; }
    void SetText(const char* t) { text_ = t ? t : ""; }
    const char* Text() const { return text_.c_str(); }

    void SetWidth(float w) { width_ = w < 0 ? 0 : w; }
    float Width() const { return width_; }
    void SetHeight(float h) { height_ = h < 0 ? 0 : h; }
    float Height() const { return height_; }

    void AddCharAdvance(float w) { advances_.push_back(w); }
    std::size_t AdvanceCount() const { return advances_.size(); }
    float Advance(std::size_t i) const {
        return i < advances_.size() ? advances_[i] : 0.0f;
    }
    float TotalAdvance() const {
        float s = 0.0f;
        for (auto a : advances_) s += a;
        return s;
    }
    void SetLineCount(std::size_t n) { lines_ = n; }
    std::size_t LineCount() const { return lines_; }
    void SetWrapped(bool w) { wrapped_ = w; }
    bool IsWrapped() const { return wrapped_; }

private:
    float fontSize_ = 16.0f, lineHeight_ = 20.0f;
    float width_ = 0, height_ = 0;
    std::string text_;
    std::vector<float> advances_;
    std::size_t lines_ = 0;
    bool wrapped_ = false;
};

} // namespace bighero
