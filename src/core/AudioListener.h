#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// AudioListener: a listener entity in 3D audio space. Holds position,
// velocity and orientation vectors used by the audio backend for spatial
// panning. Pure data container.
class AudioListener {
public:
    AudioListener() {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }

    void SetVelocity(float vx, float vy, float vz) { vx_=vx; vy_=vy; vz_=vz; }
    void Velocity(float& vx, float& vy, float& vz) const { vx=vx_; vy=vy_; vz=vz_; }

    void SetForward(float fx, float fy, float fz) { fx_=fx; fy_=fy; fz_=fz; }
    void SetUp(float ux, float uy, float uz) { ux_=ux; uy_=uy; uz_=uz; }

    void SetVolume(float v) { volume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
    float Volume() const { return volume_; }
    void SetMuted(bool m) { muted_ = m; }
    bool Muted() const { return muted_; }

    void SetRolloff(float f) { rolloff_ = f < 0 ? 0 : f; }
    float Rolloff() const { return rolloff_; }

    // Distance-based attenuation factor (linear rolloff clamp).
    float AttenuationFor(float distance) const {
        if (rolloff_ <= 0) return 1.0f;
        float a = 1.0f - rolloff_ * distance;
        return a < 0 ? 0 : (a > 1 ? 1 : a);
    }

    std::uint64_t Id() const { return id_; }
    void SetId(std::uint64_t id) { id_ = id; }

private:
    float px_=0, py_=0, pz_=0;
    float vx_=0, vy_=0, vz_=0;
    float fx_=0, fy_=0, fz_=-1;
    float ux_=0, uy_=1, uz_=0;
    float volume_ = 1.0f;
    float rolloff_ = 0.0f;
    bool muted_ = false;
    std::uint64_t id_ = 0;
};

} // namespace bighero
