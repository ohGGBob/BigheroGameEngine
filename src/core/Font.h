#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Font: a font asset metadata container. Holds a family name, size, a set of
// glyph advances per character code, and a texture atlas id. Pure data.
class Font {
public:
    struct Glyph {
        std::uint64_t textureId;
        float u0, v0, u1, v1;
        float advance;
        float bearing;
    };

    Font() {}
    explicit Font(const std::string& family, float size)
        : family_(family), size_(size < 0 ? 0 : size) {}

    void SetFamily(const std::string& f) { family_ = f; }
    const std::string& Family() const { return family_; }
    void SetSize(float s) { size_ = s < 0 ? 0 : s; }
    float Size() const { return size_; }
    void SetAtlas(std::uint64_t id) { atlas_ = id; }
    std::uint64_t Atlas() const { return atlas_; }

    void AddGlyph(char c, const Glyph& g) { glyphs_[c] = g; }
    bool HasGlyph(char c) const { return glyphs_.count(c) != 0; }
    bool TryGetGlyph(char c, Glyph& out) const {
        auto it = glyphs_.find(c);
        if (it == glyphs_.end()) return false;
        out = it->second; return true;
    }

    // Approximate total advance width of a UTF-8 string (ASCII only here).
    float MeasureWidth(const std::string& text) const {
        float w = 0;
        for (char c : text) {
            auto it = glyphs_.find(c);
            if (it != glyphs_.end()) w += it->second.advance;
            else w += size_ * 0.5f; // fallback advance
        }
        return w;
    }

    std::size_t GlyphCount() const { return glyphs_.size(); }
    std::vector<char> AvailableChars() const {
        std::vector<char> out;
        for (auto& kv : glyphs_) out.push_back(kv.first);
        return out;
    }

private:
    std::string family_;
    float size_ = 14.0f;
    std::uint64_t atlas_ = 0;
    std::map<char, Glyph> glyphs_;
};

} // namespace bighero
