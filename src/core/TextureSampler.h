#pragma once
#include <cstdint>

namespace bighero {

// TextureSampler: describes GPU texture sampling/filtering state.
// Self-contained, std-lib only.
class TextureSampler {
public:
    enum class Filter : uint8_t { Nearest = 0, Bilinear = 1, Trilinear = 2, Anisotropic = 3 };
    enum class Wrap : uint8_t { Repeat = 0, Clamp = 1, Mirror = 2, MirrorOnce = 3 };

    TextureSampler() = default;
    TextureSampler(Filter minFilter, Filter magFilter, Wrap wrapU, Wrap wrapV, Wrap wrapW = Wrap::Repeat,
                   float mipBias = 0, uint32_t maxAniso = 1, float lodMin = 0, float lodMax = 1000)
        : minFilter_(minFilter), magFilter_(magFilter), wrapU_(wrapU), wrapV_(wrapV),
          wrapW_(wrapW), mipBias_(mipBias), maxAniso_(maxAniso), lodMin_(lodMin), lodMax_(lodMax) {}

    void SetMinFilter(Filter f) { minFilter_ = f; }
    Filter MinFilter() const { return minFilter_; }
    void SetMagFilter(Filter f) { magFilter_ = f; }
    Filter MagFilter() const { return magFilter_; }
    void SetWrapU(Wrap w) { wrapU_ = w; }
    Wrap WrapU() const { return wrapU_; }
    void SetWrapV(Wrap w) { wrapV_ = w; }
    Wrap WrapV() const { return wrapV_; }
    void SetWrapW(Wrap w) { wrapW_ = w; }
    Wrap WrapW() const { return wrapW_; }
    void SetMipBias(float b) { mipBias_ = b; }
    float MipBias() const { return mipBias_; }
    void SetMaxAnisotropy(uint32_t a) { maxAniso_ = a; }
    uint32_t MaxAnisotropy() const { return maxAniso_; }
    void SetLodRange(float lo, float hi) { lodMin_ = lo; lodMax_ = hi; }
    float LodMin() const { return lodMin_; }
    float LodMax() const { return lodMax_; }

    uint32_t Pack() const {
        return (uint32_t)minFilter_ | ((uint32_t)magFilter_ << 2) |
               ((uint32_t)wrapU_ << 4) | ((uint32_t)wrapV_ << 6) |
               ((uint32_t)wrapW_ << 8);
    }

private:
    Filter minFilter_ = Filter::Bilinear;
    Filter magFilter_ = Filter::Bilinear;
    Wrap wrapU_ = Wrap::Repeat;
    Wrap wrapV_ = Wrap::Repeat;
    Wrap wrapW_ = Wrap::Repeat;
    float mipBias_ = 0;
    uint32_t maxAniso_ = 1;
    float lodMin_ = 0;
    float lodMax_ = 1000;
};

} // namespace bighero
