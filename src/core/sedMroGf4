#pragma once
#include "Verlet.h"
#include <vector>
#include <cmath>
#include <cstddef>
#include <algorithm>

namespace bighero {

// 2D rope simulation using a chain of verlet particles connected by distance
// constraints. Pure standard library; caller drives per-frame updates.
class Rope2D {
public:
    void Build(float startX, float startY, float segLength, int count, bool pinStart) {
        particles_.clear();
        for (int i = 0; i < count; ++i) {
            VerletParticle p(startX, startY + (float)i * segLength);
            if (i == 0) p.pinned = pinStart;
            particles_.push_back(p);
        }
        segLength_ = segLength;
    }

    void Simulate(float gx, float gy, float damping, float dt, int iterations = 2) {
        Verlet::IntegrateBatch(particles_, gx, gy, damping, dt);
        for (int it = 0; it < iterations; ++it) {
            for (int i = 1; i < (int)particles_.size(); ++i) {
                SatisfyConstraint(particles_[i - 1], particles_[i]);
            }
        }
    }

    std::size_t Count() const { return particles_.size(); }
    VerletParticle& Get(int i) { return particles_[i]; }
    float SegLength() const { return segLength_; }
    float TotalLength() const { return segLength_ * (float)std::max(0, (int)particles_.size() - 1); }

private:
    void SatisfyConstraint(VerletParticle& a, VerletParticle& b) {
        float dx = b.x - a.x, dy = b.y - a.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-10f) return;
        float diff = (dist - segLength_) / dist * 0.5f;
        if (!a.pinned) { a.x += dx * diff; a.y += dy * diff; }
        if (!b.pinned) { b.x -= dx * diff; b.y -= dy * diff; }
    }

    std::vector<VerletParticle> particles_;
    float segLength_ = 1.0f;
};

} // namespace bighero
