#pragma once
#include <cmath>

namespace bighero {

// ShaderQuad: describes a fullscreen/blit quad vertex layout used by the
// post-process and compositor passes. Pure descriptor, std-lib only.
struct ShaderQuad {
    // Fullscreen triangle/quad mode.
    enum class Layout { Triangle, QuadStrip, TwoTriangles };

    Layout layout = Layout::QuadStrip;
    // Whether the quad covers the whole target viewport.
    bool fullscreen = true;
    // Normalized UV rect to sample from the source texture.
    float uvX = 0, uvY = 0, uvW = 1, uvH = 1;
    // Flip Y for render-target (Direct3D-style) sources.
    bool flipY = false;

    ShaderQuad() = default;

    // Build UVs for a fullscreen clip-space quad (two triangles).
    void FullscreenUVs(float out[8]) const {
        // (0,0) top-left pairs into (x,y,u,v).
        out[0] = -1; out[1] = -1; out[2] = uvX;          out[3] = flipY ? uvY + uvH : uvY;
        out[4] =  1; out[5] = -1; out[6] = uvX + uvW;      out[7] = flipY ? uvY + uvH : uvY;
        out[8] = -1; out[9] =  1; out[10] = uvX;           out[11] = flipY ? uvY : uvY + uvH;
        out[12] =  1; out[13] =  1; out[14] = uvX + uvW;   out[15] = flipY ? uvY : uvY + uvH;
    }
};

} // namespace bighero
