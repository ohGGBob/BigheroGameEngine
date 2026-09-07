#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Sampler state: filter, wrap, and address modes for texture sampling.
// CPU-side state payload that maps to a sampler object at submission time.
class SamplerState {
public:
    enum class Filter { Point, Bilinear, Trilinear, Anisotropic };
    enum class Wrap { Repeat, Clamp, Mirror, ClampToEdge };

    SamplerState() {}

    void SetFilter(Filter f) { filter_ = f; }
    Filter FilterMode() const { return filter_; }
    void SetWrapU(Wrap w) { wrapU_ = w; }
    Wrap WrapU() const { return wrapU_; }
    void SetWrapV(Wrap w) { wrapV_ = w; }
    Wrap WrapV() const { return wrapV_; }
    void SetWrapW(Wrap w) { wrapW_ = w; }
    Wrap WrapW() const { return wrapW_; }

    void SetAnisotropy(float level) { anisotropy_ = level < 1 ? 1 : level; }
    float Anisotropy() const { return anisotropy_; }
    void SetLodBias(float b) { lodBias_ = b; }
    float LodBias() const { return lodBias_; }
    void SetMaxLod(float lod) { maxLod_ = lod; }
    float MaxLod() const { return maxLod_; }

    static SamplerState PointClamp() {
        SamplerState s;
        s.filter_ = Filter::Point;
        s.wrapU_ = s.wrapV_ = s.wrapW_ = Wrap::Clamp;
        return s;
    }
    static SamplerState LinearRepeat() {
        SamplerState s;
        s.filter_ = Filter::Bilinear;
        s.wrapU_ = s.wrapV_ = s.wrapW_ = Wrap::Repeat;
        return s;
    }

private:
    Filter filter_ = Filter::Bilinear;
    Wrap wrapU_ = Wrap::Repeat, wrapV_ = Wrap::Repeat, wrapW_ = Wrap::Repeat;
    float anisotropy_ = 1.0f;
    float lodBias_ = 0.0f;
    float maxLod_ = 1000.0f;
};

} // namespace bighero
