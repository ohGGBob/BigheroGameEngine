#pragma once
#include <vector>
#include <cstdint>

namespace bighero {

// Computes tangent-space normals from a height map using finite differences.
// Output is a vector of per-texel normal (nx, ny, nz) floats (nx, nz are the
// slope components, ny is the up component). Pure standard library.
class NormalMap {
public:
    static void GenerateFromHeights(const std::vector<float>& heights,
                                    int width, int height,
                                    float strength,  // increase -> more slope
                                    std::vector<float>& outNx,
                                    std::vector<float>& outNy,
                                    std::vector<float>& outNz) {
        outNx.assign((std::size_t)width * height, 0);
        outNy.assign((std::size_t)width * height, 0);
        outNz.assign((std::size_t)width * height, 0);
        if (width < 1 || height < 1 || heights.size() < (std::size_t)width * height) return;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int xl = (x - 1 < 0) ? 0 : x - 1;
                int xr = (x + 1 >= width) ? width - 1 : x + 1;
                int yl = (y - 1 < 0) ? 0 : y - 1;
                int yr = (y + 1 >= height) ? height - 1 : y + 1;
                float hR = At(heights, width, xr, y);
                float hL = At(heights, width, xl, y);
                float hD = At(heights, width, x, yr);
                float hU = At(heights, width, x, yl);
                float dx = (hR - hL) * strength;
                float dz = (hD - hU) * strength;
                // Tangent-space normal ~= (-dx, 1, -dz), normalized.
                float inv = 1.0f / std::sqrt(dx * dx + 1.0f + dz * dz);
                std::size_t idx = (std::size_t)y * width + x;
                outNx[idx] = -dx * inv;
                outNy[idx] = 1.0f * inv;
                outNz[idx] = -dz * inv;
            }
        }
    }

private:
    static float At(const std::vector<float>& h, int w, int x, int y) {
        return h[(std::size_t)y * w + x];
    }
};

} // namespace bighero
