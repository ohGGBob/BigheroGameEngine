#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bighero {

// Minimal 2D physics world: holds rigid bodies and circle colliders, performs
// a simple pairwise contact resolution, and applies positional correction.
// Intentionally lightweight and self-contained (no broadphase/joints here).
class PhysicsWorld2D {
public:
    struct Body {
        float x = 0, y = 0;
        float vx = 0, vy = 0;
        float mass = 1.0f;
        float invMass = 1.0f;
        float radius = 0.5f;
        bool dynamic = true;
    };

    std::size_t AddBody(float x, float y, float mass = 1.0f) {
        Body b;
        b.x = x; b.y = y; b.mass = mass <= 0 ? 1 : mass;
        b.invMass = 1.0f / b.mass;
        bodies_.push_back(b);
        return bodies_.size() - 1;
    }
    std::size_t BodyCount() const { return bodies_.size(); }
    Body& GetBody(std::size_t i) { return bodies_[i]; }

    void SetGravity(float gx, float gy) { gx_ = gx; gy_ = gy; }
    void SetIterations(int it) { iterations_ = it < 1 ? 1 : it; }

    // Integrate all dynamic bodies and resolve circle collisions.
    void Update(float dt) {
        // Integrate.
        for (auto& b : bodies_) {
            if (!b.dynamic) continue;
            b.vx += gx_ * dt;
            b.vy += gy_ * dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
        }
        // Resolve contacts.
        for (int it = 0; it < iterations_; ++it) {
            for (std::size_t i = 0; i < bodies_.size(); ++i) {
                for (std::size_t j = i + 1; j < bodies_.size(); ++j) {
                    Resolve(bodies_[i], bodies_[j]);
                }
            }
        }
    }

    void Clear() { bodies_.clear(); }

private:
    void Resolve(Body& a, Body& b) {
        float dx = b.x - a.x, dy = b.y - a.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        float minDist = a.radius + b.radius;
        if (dist >= minDist || dist < 1e-6f) return;
        float nx = dx / dist, ny = dy / dist;
        float overlap = minDist - dist;
        // Positional correction (split by inverse mass).
        float totalInv = a.invMass + b.invMass;
        if (totalInv <= 0) return;
        float corr = overlap / totalInv * 0.8f;
        a.x -= nx * corr * a.invMass;
        a.y -= ny * corr * a.invMass;
        b.x += nx * corr * b.invMass;
        b.y += ny * corr * b.invMass;
        // Impulse (simple elastic-ish with restitution 0).
        float rvx = b.vx - a.vx, rvy = b.vy - a.vy;
        float vn = rvx * nx + rvy * ny;
        if (vn >= 0) return;
        float j = -(1.0f + restitution_) * vn / totalInv;
        a.vx -= j * nx * a.invMass;
        a.vy -= j * ny * a.invMass;
        b.vx += j * nx * b.invMass;
        b.vy += j * ny * b.invMass;
    }

    std::vector<Body> bodies_;
    float gx_ = 0, gy_ = -9.81f;
    float restitution_ = 0.0f;
    int iterations_ = 2;
};

} // namespace bighero
