#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// GPU sampler handle descriptor pairing filter/wrap state to a backend
// sampler object. CPU-side bookkeeping for texture sampling.
class GpuSampler {
public:
    enum class Filter { Point, Bilinear, Trilinear, Anisotropic };
    enum class Wrap { Repeat, Clamp, Mirror };

    GpuSampler() {}
    GpuSampler(Filter f, Wrap u, Wrap v)
        : filter_(f), wrapU_(u), wrapV_(v) {}

    void SetFilter(Filter f) { filter_ = f; }
    Filter FilterMode() const { return filter_; }
    void SetWrapU(Wrap w) { wrapU_ = w; }
    Wrap WrapU() const { return wrapU_; }
    void SetWrapV(Wrap w) { wrapV_ = w; }
    Wrap WrapV() const { return wrapV_; }
    void SetHandle(std::uint64_t h) { handle_ = h; }
    std::uint64_t Handle() const { return handle_; }
    void SetAnisotropy(float level) { anisotropy_ = level < 1 ? 1 : level; }
    float Anisotropy() const { return anisotropy_; }
    void SetMaxAnisotropy(int maxAniso) { maxAniso_ = maxAniso; }
    int MaxAnisotropy() const { return maxAniso_; }

    bool IsValid() const { return handle_ != 0; }

private:
    Filter filter_ = Filter::Bilinear;
    Wrap wrapU_ = Wrap::Repeat, wrapV_ = Wrap::Repeat;
    std::uint64_t handle_ = 0;
    float anisotropy_ = 1.0f;
    int maxAniso_ = 16;
};

} // namespace bighero
