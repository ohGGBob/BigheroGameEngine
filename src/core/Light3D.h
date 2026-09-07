#pragma once
#include <cmath>

namespace bighero {

// 3D light source (directional / point / spot) with color & intensity.
struct Light3D {
    enum Kind { Directional, Point, Spot };

    Kind kind = Directional;
    float r = 1.0f, g = 1.0f, b = 1.0f;   // color
    float intensity = 1.0f;
    float posX = 0, posY = 0, posZ = 0;   // position (point/spot)
    float dirX = 0, dirY = -1, dirZ = 0;  // direction (directional/spot)
    float spotAngle = 0.5f;               // spot cone half-angle (radians)
    float range = 100.0f;                 // attenuation range (point/spot)

    void SetColor(float cr, float cg, float cb) { r=cr; g=cg; b=cb; }
    void SetPosition(float x, float y, float z) { posX=x; posY=y; posZ=z; }
    void SetDirection(float x, float y, float z) { dirX=x; dirY=y; dirZ=z; NormalizeDir(); }
    void NormalizeDir() {
        float len = std::sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ);
        if (len > 0) { dirX/=len; dirY/=len; dirZ/=len; }
    }

    // Cosine of angle between incoming light direction and a surface normal.
    // lightDir is normalized vector from surface to light; returns 0 if pointing away.
    float Diffuse(float nx, float ny, float nz) const {
        float lx, ly, lz;
        if (kind == Directional) { lx = -dirX; ly = -dirY; lz = -dirZ; }
        else { lx = -dirX; ly = -dirY; lz = -dirZ; } // point uses dir too for simplicity
        return std::fmax(0.0f, lx*nx + ly*ny + lz*nz);
    }

    // Simple distance-based attenuation (1 = full at source, falls to 0 at range).
    float Attenuation(float px, float py, float pz) const {
        if (kind == Directional) return 1.0f;
        float dx = px - posX, dy = py - posY, dz = pz - posZ;
        float d = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (range <= 0) return 1.0f;
        float t = 1.0f - d / range;
        return t > 0 ? t : 0.0f;
    }
};

} // namespace bighero
