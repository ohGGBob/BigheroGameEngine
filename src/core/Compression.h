#pragma once
#include <cstddef>

namespace bighero {

// Compression: describes the compression mode applied to a texture/mesh asset.
// Pure descriptor + block-size helpers for compressed texture formats.
class Compression {
public:
    enum class Mode {
        None,
        Deflate,
        Gzip,
        Zstd,
        LZ4,
        BC1, BC3, BC5, BC7,
        ETC2, ASTC
    };

    Compression() {}
    explicit Compression(Mode m) : mode_(m) {}

    void SetMode(Mode m) { mode_ = m; }
    Mode Current() const { return mode_; }
    void SetQuality(float q) { quality_ = q < 0 ? 0 : (q > 1 ? 1 : q); }
    float Quality() const { return quality_; }
    void SetBlockSize(std::size_t bytes) { blockSize_ = bytes; }
    std::size_t BlockSize() const { return blockSize_; }

    bool IsLossy() const {
        return mode_ == Mode::BC1 || mode_ == Mode::BC3 ||
               mode_ == Mode::BC5 || mode_ == Mode::BC7 ||
               mode_ == Mode::ETC2 || mode_ == Mode::ASTC;
    }
    bool IsBlockBased() const {
        return IsLossy();
    }
    bool IsCompressed() const { return mode_ != Mode::None; }

    // 4x4 block byte size for BC/ASTC formats, else 0.
    std::size_t BlockBytesPer4x4() const {
        switch (mode_) {
            case Mode::BC1: return 8;
            case Mode::BC3: return 16;
            case Mode::BC5: return 16;
            case Mode::BC7: return 16;
            default: return 0;
        }
    }

    void Reset() { mode_ = Mode::None; quality_ = 0.5f; blockSize_ = 0; }

private:
    Mode mode_ = Mode::None;
    float quality_ = 0.5f;
    std::size_t blockSize_ = 0;
};

} // namespace bighero
