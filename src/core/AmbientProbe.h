#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// AmbientProbe: captures low-frequency ambient light as a small set of SH
// coefficients. Holds 3 channels x N coefficients. Pure data container for
// IBL ambient sampling.
class AmbientProbe {
public:
    AmbientProbe() {}
    explicit AmbientProbe(std::size_t coefficientCount)
        : coeffCount_(coefficientCount) {
        coeffs_.resize(coefficientCount * 3, 0.0f);
    }

    void ResizeCoefficients(std::size_t n) {
        coeffCount_ = n;
        coeffs_.resize(n * 3, 0.0f);
    }
    std::size_t CoefficientCount() const { return coeffCount_; }

    void SetCoefficient(std::size_t i, float r, float g, float b) {
        if (i >= coeffCount_) return;
        coeffs_[i*3+0] = r; coeffs_[i*3+1] = g; coeffs_[i*3+2] = b;
    }
    void GetCoefficient(std::size_t i, float& r, float& g, float& b) const {
        if (i >= coeffCount_) { r=g=b=0; return; }
        r = coeffs_[i*3+0]; g = coeffs_[i*3+1]; b = coeffs_[i*3+2];
    }

    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }

    // Simple average color (coefficient 0 / 3 gives the DC term in SH).
    void AverageColor(float& r, float& g, float& b) const {
        r = g = b = 0.0f;
        if (coeffCount_ == 0) return;
        r = coeffs_[0]; g = coeffs_[1]; b = coeffs_[2];
    }

    void Clear() { coeffs_.assign(coeffs_.size(), 0.0f); }

private:
    std::size_t coeffCount_ = 0;
    std::vector<float> coeffs_;
    float intensity_ = 1.0f;
    float px_=0, py_=0, pz_=0;
};

} // namespace bighero
