#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// DistanceField: a uniform-grid signed-distance field over a scalar density
// volume. Queries the field value at a point and finds the closest occupied
// sample. Pure CPU-side, self-contained.
class DistanceField {
public:
    DistanceField() {}
    DistanceField(std::size_t nx, std::size_t ny, std::size_t nz, float spacing)
        : nx_(nx<1?1:nx), ny_(ny<1?1:ny), nz_(nz<1?1:nz),
          spacing_(spacing<1e-6f?1e-6f:spacing) {
        data_.assign(nx_*ny_*nz_, 0.0f);
    }

    void Resize(std::size_t nx, std::size_t ny, std::size_t nz, float spacing) {
        nx_=nx<1?1:nx; ny_=ny<1?1:ny; nz_=nz<1?1:nz;
        spacing_=spacing<1e-6f?1e-6f:spacing;
        data_.assign(nx_*ny_*nz_, 0.0f);
    }
    std::size_t NX() const { return nx_; }
    std::size_t NY() const { return ny_; }
    std::size_t NZ() const { return nz_; }
    float Spacing() const { return spacing_; }

    // Set field value at grid (i,j,k); returns false if out of bounds.
    bool Set(std::size_t i, std::size_t j, std::size_t k, float v) {
        if (i>=nx_||j>=ny_||k>=nz_) return false;
        data_[(k*ny_+j)*nx_+i] = v;
        return true;
    }
    bool Get(std::size_t i, std::size_t j, std::size_t k, float& out) const {
        if (i>=nx_||j>=ny_||k>=nz_) return false;
        out = data_[(k*ny_+j)*nx_+i];
        return true;
    }
    std::size_t SampleCount() const { return data_.size(); }

    // Trilinearly interpolate the field at continuous (x,y,z) in grid units.
    float Sample(float x, float y, float z) const {
        float cx = x < 0 ? 0 : (x > nx_-1 ? nx_-1 : x);
        float cy = y < 0 ? 0 : (y > ny_-1 ? ny_-1 : y);
        float cz = z < 0 ? 0 : (z > nz_-1 ? nz_-1 : z);
        std::size_t i0=(std::size_t)cx, j0=(std::size_t)cy, k0=(std::size_t)cz;
        std::size_t i1=i0+1< nx_?i0+1:i0, j1=j0+1<ny_?j0+1:j0, k1=k0+1<nz_?k0+1:k0;
        float fx=cx-i0, fy=cy-j0, fz=cz-k0;
        float c000=data_[(k0*ny_+j0)*nx_+i0], c100=data_[(k0*ny_+j0)*nx_+i1];
        float c010=data_[(k0*ny_+j1)*nx_+i0], c110=data_[(k0*ny_+j1)*nx_+i1];
        float c001=data_[(k1*ny_+j0)*nx_+i0], c101=data_[(k1*ny_+j0)*nx_+i1];
        float c011=data_[(k1*ny_+j1)*nx_+i0], c111=data_[(k1*ny_+j1)*nx_+i1];
        float a0=c000+(c100-c000)*fx, a1=c010+(c110-c010)*fx;
        float b0=c001+(c101-c001)*fx, b1=c011+(c111-c011)*fx;
        float c0=a0+(a1-a0)*fy, c1=b0+(b1-b0)*fy;
        return c0+(c1-c0)*fz;
    }

private:
    std::size_t nx_=1, ny_=1, nz_=1;
    float spacing_=1.0f;
    std::vector<float> data_;
};

} // namespace bighero
