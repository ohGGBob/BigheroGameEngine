#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// CollisionManifold: the contact information for a resolved collision — a set
// of contact points, a shared normal, and penetration. Pure data container
// the physics solver reads/writes during resolution.
class CollisionManifold {
public:
    struct Point { float px,py,pz; float pen; };

    CollisionManifold() {}
    CollisionManifold(std::size_t a, std::size_t b) : bodyA_(a), bodyB_(b) {}

    void SetBodies(std::size_t a, std::size_t b) { bodyA_=a; bodyB_=b; }
    std::size_t BodyA() const { return bodyA_; }
    std::size_t BodyB() const { return bodyB_; }

    void SetNormal(float nx, float ny, float nz) { nx_=nx; ny_=ny; nz_=nz; }
    void Normal(float& nx, float& ny, float& nz) const { nx=nx_; ny=ny_; nz=nz_; }

    void AddPoint(float px, float py, float pz, float pen) {
        points_.push_back({px,py,pz,pen});
    }
    std::size_t PointCount() const { return points_.size(); }
    bool GetPoint(std::size_t i, Point& out) const {
        if (i >= points_.size()) return false;
        out = points_[i]; return true;
    }

    float MaxPenetration() const {
        float m = 0.0f;
        for (auto& p : points_) if (p.pen > m) m = p.pen;
        return m;
    }
    void Clear() { points_.clear(); }
    void Reset() { Clear(); nx_=ny_=0; nz_=1; bodyA_=bodyB_=0; }

private:
    std::size_t bodyA_=0, bodyB_=0;
    float nx_=0, ny_=0, nz_=1;
    std::vector<Point> points_;
};

} // namespace bighero
