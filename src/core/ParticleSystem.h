#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>
#include <random>

namespace bighero {

// ParticleSystem: high-level orchestrator for a pool of particles. Each
// particle has position (exposed here as CPU fp values), velocity, lifetime,
// and color weights. Extremely lightweight pure-stdlib implementation.
class ParticleSystem {
public:
    struct Particle {
        float x, y, z;
        float vx, vy, vz;
        float life, maxLife;
        float size;
        float r, g, b, a;
        bool active;
    };

    explicit ParticleSystem(std::size_t capacity = 1024)
        : capacity_(capacity < 1 ? 1 : capacity) {
        particles_.resize(capacity_);
    }

    void SetCapacity(std::size_t c) {
        capacity_ = c < 1 ? 1 : c;
        particles_.resize(capacity_);
    }
    std::size_t Capacity() const { return capacity_; }

    // Spawn a particle at (x,y,z). Returns index or -1 if pool full.
    int Emit(float x, float y, float z, float vx, float vy, float vz,
             float life, float size) {
        int idx = FindFree();
        if (idx < 0) return -1;
        Particle& p = particles_[(std::size_t)idx];
        p.x = x; p.y = y; p.z = z;
        p.vx = vx; p.vy = vy; p.vz = vz;
        p.maxLife = life < 0 ? 0 : life;
        p.life = p.maxLife;
        p.size = size;
        p.r = p.g = p.b = 1.0f; p.a = 1.0f;
        p.active = true;
        ++active_;
        return idx;
    }

    // Advance all active particles by dt.
    void Update(float dt, float gravity = -9.8f, float damp = 0.0f) {
        for (Particle& p : particles_) {
            if (!p.active) continue;
            p.vy += gravity * dt;
            if (damp != 0) { p.vx *= (1 - damp * dt); p.vy *= (1 - damp * dt); p.vz *= (1 - damp * dt); }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.z += p.vz * dt;
            p.life -= dt;
            if (p.life <= 0) { p.active = false; --active_; }
        }
    }

    void Clear() {
        for (Particle& p : particles_) p.active = false;
        active_ = 0;
    }

    std::size_t ActiveCount() const { return active_; }
    const Particle& At(std::size_t i) const { return particles_[i]; }
    void SetColor(std::size_t i, float r, float g, float b, float a) {
        if (i >= particles_.size()) return;
        particles_[i].r = r; particles_[i].g = g; particles_[i].b = b; particles_[i].a = a;
    }

private:
    int FindFree() {
        for (std::size_t i = 0; i < particles_.size(); ++i)
            if (!particles_[i].active) return (int)i;
        return -1;
    }

    std::vector<Particle> particles_;
    std::size_t capacity_;
    std::size_t active_ = 0;
};

} // namespace bighero
