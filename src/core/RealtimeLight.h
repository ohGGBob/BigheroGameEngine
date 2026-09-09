#pragma once
#include <cmath>

namespace bighero {

// RealtimeLight: a runtime point/spot/directional light instance with the
// per-frame position/direction/color/range the shading pass consumes.
// Self-contained, std-lib only.
struct RealtimeLight {
    enum class Type { Directional, Point, Spot };

    Type type = Type::Point;
    // Position (point/spot) or direction (directional), world space.
    float x = 0, y = 0, z = 0;
    float colorR = 1, colorG = 1, colorB = 1;
    float intensity = 1.0f;
    // Attenuation range (point/spot) and inner/outer spot angles.
    float range = 10.0f;
    float spotInnerDeg = 20.0f, spotOuterDeg = 40.0f;
    // Shadow casting flag.
    bool castsShadow = false;

    RealtimeLight() = default;
    static RealtimeLight Directional(float dx, float dy, float dz) {
        RealtimeLight l; l.type = Type::Directional; l.x = dx; l.y = dy; l.z = dz;
        return l;
    }
    static RealtimeLight Point(float px, float py, float pz, float range_) {
        RealtimeLight l; l.type = Type::Point; l.x = px; l.y = py; l.z = pz;
        l.range = range_; return l;
    }

    // Attenuation factor for a point light at given distance.
    float Attenuation(float distance) const {
        if (range <= 0) return 0;
        if (distance >= range) return 0;
        float t = 1.0f - distance / range;
        return t * t;
    }
};

} // namespace bighero
