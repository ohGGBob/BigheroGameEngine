#pragma once
#include "Verlet.h"
#include <vector>
#include <cmath>
#include <cstddef>
#include <algorithm>

namespace bighero {

// Lightweight 2D soft body: a grid of verlet particles connected by distance
// constraints along rows (and optionally columns + diagonals). Pure standard
// library; caller drives per-frame updates.
class SoftBody2D {
public:
    void Build(float originX, float originY, float spacing,
               int cols, int rows, bool pinTopRow = false) {
        particles_.clear(); constraints_.clear();
        cols_ = cols; rows_ = rows;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                VerletParticle p(originX + (float)c * spacing, originY + (float)r * spacing);
                if (pinTopRow && r == 0) p.pinned = true;
                particles_.push_back(p);
            }
        }
        // Horizontal links.
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols - 1; ++c) {
                int a = Idx(c, r), b = Idx(c + 1, r);
                constraints_.push_back({a, b, spacing});
            }
        }
        // Vertical links.
        for (int r = 0; r < rows - 1; ++r) {
            for (int c = 0; c < cols; ++c) {
                int a = Idx(c, r), b = Idx(c, r + 1);
                constraints_.push_back({a, b, spacing});
            }
        }
        // Diagonals for shear stability.
        float diag = spacing * std::sqrt(2.0f);
        for (int r = 0; r < rows - 1; ++r) {
            for (int c = 0; c < cols - 1; ++c) {
                constraints_.push_back({Idx(c, r), Idx(c + 1, r + 1), diag});
                constraints_.push_back({Idx(c + 1, r), Idx(c, r + 1), diag});
            }
        }
    }

    void Simulate(float gx, float gy, float damping, float dt, int iterations = 3) {
        Verlet::IntegrateBatch(particles_, gx, gy, damping, dt);
        for (int it = 0; it < iterations; ++it) {
            for (auto& cn : constraints_) SolveConstraint(cn);
        }
    }

    std::size_t Count() const { return particles_.size(); }
    VerletParticle& Get(int i) { return particles_[i]; }
    VerletParticle& At(int c, int r) { return particles_[Idx(c, r)]; }

private:
    int Idx(int c, int r) const { return r * cols_ + c; }

    struct Constraint { int a, b; float rest; };

    void SolveConstraint(const Constraint& cn) {
        VerletParticle& a = particles_[cn.a];
        VerletParticle& b = particles_[cn.b];
        float dx = b.x - a.x, dy = b.y - a.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-10f) return;
        float diff = (dist - cn.rest) / dist * 0.5f;
        if (!a.pinned) { a.x += dx * diff; a.y += dy * diff; }
        if (!b.pinned) { b.x -= dx * diff; b.y -= dy * diff; }
    }

    std::vector<VerletParticle> particles_;
    std::vector<Constraint> constraints_;
    int cols_ = 0, rows_ = 0;
};

} // namespace bighero
