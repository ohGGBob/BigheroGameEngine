#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// Verlet integration for a single particle in 2D. Stores current position and
// previous position; velocity is implicit. Standard for cloth/rope simulation.
struct VerletParticle {
    float x, y;
    float px, py; // previous position
    bool pinned;

    VerletParticle() : x(0), y(0), px(0), py(0), pinned(false) {}
    VerletParticle(float x_, float y_) : x(x_), y(y_), px(x_), py(y_), pinned(false) {}

    float VelocityX() const { return x - px; }
    float VelocityY() const { return y - py; }
};

// Verlet integrator over a collection of particles with optional gravity and
// damping. Stateless except for the particle list (caller owns stepping).
class Verlet {
public:
    // Integrate one particle by dt. Caller supplies gravity (gx, gy) and damping.
    static void Integrate(VerletParticle& p,
                          float gx, float gy,
                          float damping, float dt) {
        if (p.pinned) return;
        float vx = (p.x - p.px) * (1.0f - damping);
        float vy = (p.y - p.py) * (1.0f - damping);
        p.px = p.x;
        p.py = p.y;
        p.x += vx + gx * dt * dt;
        p.y += vy + gy * dt * dt;
    }

    // Integrate a whole particle array.
    static void IntegrateBatch(std::vector<VerletParticle>& pts,
                               float gx, float gy,
                               float damping, float dt) {
        for (auto& p : pts) Integrate(p, gx, gy, damping, dt);
    }
};

} // namespace bighero
