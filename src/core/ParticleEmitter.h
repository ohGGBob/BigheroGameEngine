#pragma once
#include "Particle.h"
#include <vector>
#include <cstddef>
#include <cmath>
#include <cstdint>

namespace bighero {

// Simple particle emitter: spawns particles with spread, updates all, and
// recycles dead slots. Pure standard library.
class ParticleEmitter {
public:
    explicit ParticleEmitter(std::size_t capacity = 1024)
        : particles_(capacity), next_(0) {}

    void SetEmitRate(float perSecond) { emitRate_ = perSecond; }
    float EmitRate() const { return emitRate_; }

    // Emit 'count' particles from (ex, ey) with given base velocity and spread.
    void Emit(std::size_t count, float ex, float ey,
              float baseVx, float baseVy, float spread,
              float lifetime, float size,
              std::uint32_t rngSeed) {
        std::uint32_t seed = rngSeed;
        for (std::size_t i = 0; i < count; ++i) {
            seed = seed * 1664525u + 1013904223u;
            float ang = ((seed >> 8) & 0xFFFFu) / 65535.0f * 6.2831853f;
            float spd = spread * ((seed >> 16) & 0xFFFFu) / 65535.0f;
            float vx = baseVx + std::cos(ang) * spd;
            float vy = baseVy + std::sin(ang) * spd;
            particles_[next_].Spawn(ex, ey, vx, vy, lifetime, size);
            next_ = (next_ + 1) % particles_.size();
        }
    }

    void Update(float gx, float gy, float drag, float dt) {
        for (auto& p : particles_) p.Update(gx, gy, drag, dt);
    }

    std::size_t AliveCount() const {
        std::size_t n = 0;
        for (const auto& p : particles_) if (p.alive) ++n;
        return n;
    }

    const std::vector<Particle>& Particles() const { return particles_; }
    std::size_t Capacity() const { return particles_.size(); }

private:
    std::vector<Particle> particles_;
    std::size_t next_ = 0;
    float emitRate_ = 0.0f;
};

} // namespace bighero
