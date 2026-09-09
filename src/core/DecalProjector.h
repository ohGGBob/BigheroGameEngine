#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// DecalProjector: projects a texture decal onto a surface along a defined
// box (position + half-extents). Holds the decal texture, UV tiling, angle
// and enable state. Pure data container.
class DecalProjector {
public:
    DecalProjector() {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }

    void SetSize(float sx, float sy, float sz) {
        sx_ = sx < 0 ? 0 : sx; sy_ = sy < 0 ? 0 : sy; sz_ = sz < 0 ? 0 : sz;
    }
    void Size(float& sx, float& sy, float& sz) const { sx=sx_; sy=sy_; sz=sz_; }

    void SetNormal(float nx, float ny, float nz) {
        float len = std::sqrt(nx*nx+ny*ny+nz*nz);
        if (len > 0) { nx_=nx/len; ny_=ny/len; nz_=nz/len; }
        else { nx_=0; ny_=1; nz_=0; }
    }
    void Normal(float& nx, float& ny, float& nz) const { nx=nx_; ny=ny_; nz=nz_; }

    void SetTexture(std::uint64_t t) { tex_ = t; }
    std::uint64_t Texture() const { return tex_; }

    void SetTiling(float tu, float tv) { tu_ = tu; tv_ = tv; }
    void Tiling(float& tu, float& tv) const { tu=tu_; tv=tv_; }

    void SetAngle(float deg) { angle_ = deg; }
    float Angle() const { return angle_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetOpacity(float o) { opacity_ = o < 0 ? 0 : (o > 1 ? 1 : o); }
    float Opacity() const { return opacity_; }

    // Is a world point within the decal's AABB (half-extents around center)?
    bool Contains(float x, float y, float z) const {
        return x >= px_-sx_ && x <= px_+sx_ &&
               y >= py_-sy_ && y <= py_+sy_ &&
               z >= pz_-sz_ && z <= pz_+sz_;
    }

private:
    float px_=0, py_=0, pz_=0;
    float sx_=1, sy_=1, sz_=1;
    float nx_=0, ny_=1, nz_=0;
    std::uint64_t tex_ = 0;
    float tu_=1, tv_=1;
    float angle_ = 0;
    bool enabled_ = true;
    float opacity_ = 1.0f;
};

} // namespace bighero
