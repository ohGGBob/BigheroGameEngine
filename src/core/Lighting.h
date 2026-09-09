#pragma once
#include <cmath>

namespace bighero {

// Lighting: an aggregate light descriptor storing ambient color, a directional
// sun light, and simple light-count/intensity limits for the renderer.
struct Light {
    enum class Type { Ambient, Directional, Point, Spot };

    Type type = Type::Directional;
    // Direction (for directional/spot) or position (for point).
    float x = 0, y = 1, z = 0;
    float colorR = 1, colorG = 1, colorB = 1;
    float intensity = 1.0f;
    // Point/spot attenuation range and angle.
    float range = 10.0f;
    float spotAngleDeg = 30.0f;

    Light() = default;
    static Light Directional(float dirX, float dirY, float dirZ) {
        Light l; l.type = Type::Directional; l.x = dirX; l.y = dirY; l.z = dirZ;
        return l;
    }
    static Light Point(float px, float py, float pz, float range_) {
        Light l; l.type = Type::Point; l.x = px; l.y = py; l.z = pz; l.range = range_;
        return l;
    }
};

// Lighting: global lighting settings (ambient + light limits).
struct Lighting {
    // Ambient light (ambient color + sky/ground blend).
    float ambientR = 0.2f, ambientG = 0.2f, ambientB = 0.2f;
    float ambientIntensity = 1.0f;
    // Max number of realtime lights to evaluate.
    int maxRealtimeLights = 8;
    // Enable high-dynamic-range lighting.
    bool hdr = true;

    Lighting() = default;

    void SetAmbient(float r, float g, float b) {
        ambientR = r; ambientG = g; ambientB = b;
    }
    // Scale the ambient color by intensity, clamped to [0,1] per channel.
    void SampleAmbient(float& r, float& g, float& b) const {
        r = Clamp01(ambientR * ambientIntensity);
        g = Clamp01(ambientG * ambientIntensity);
        b = Clamp01(ambientB * ambientIntensity);
    }
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
};

} // namespace bighero
