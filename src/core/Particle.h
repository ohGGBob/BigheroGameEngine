#pragma once
#include <cmath>

namespace bighero {

// Simple 2D particle with position, velocity, age, and lifetime.
struct Particle {
    float x, y;
    float vx, vy;
    float life;      // remaining lifetime in seconds
    float maxLife;
    float size;
    // optional color
    float r, g, b, a;
    bool alive;

    Particle() : x(0), y(0), vx(0), vy(0), life(0), maxLife(0),
                 size(1.0f), r(1), g(1), b(1), a(1), alive(false) {}

    void Spawn(float px, float py, float pvx, float pvy, float lifetime,
               float sz = 1.0f, float cr = 1.0f, float cg = 1.0f, float cb = 1.0f) {
        x = px; y = py; vx = pvx; vy = pvy;
        life = lifetime; maxLife = lifetime; size = sz;
        r = cr; g = cg; b = cb; a = 1.0f;
        alive = true;
    }

    // Euler integrate by dt with gravity/ drag; returns false when expired.
    bool Update(float gx, float gy, float drag, float dt) {
        if (!alive) return false;
        vx += gx * dt; vy += gy * dt;
        vx *= (1.0f - drag); vy *= (1.0f - drag);
        x += vx * dt; y += vy * dt;
        life -= dt;
        if (life <= 0.0f) { alive = false; return false; }
        a = life / maxLife; // fade out
        return true;
    }

    float Alpha() const { return alive ? a : 0.0f; }
};

} // namespace bighero
