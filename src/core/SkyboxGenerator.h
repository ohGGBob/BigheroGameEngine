#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// Generates a procedural skybox cubemap as a set of 6 faces (or a single
// equirectangular gradient). Pure standard library; outputs RGBA8 per face.
class SkyboxGenerator {
public:
    // Generate a gradient skybox into a single RGBA8 image buffer of size
    // width x height using an equirectangular mapping (u=longitude, v=latitude).
    // zenith is the top color, horizon the bottom color. Returns image buffer.
    static std::vector<std::uint8_t> GenerateEquirect(
        int width, int height,
        float topR, float topG, float topB,
        float botR, float botG, float botB) {
        std::vector<std::uint8_t> img((std::size_t)width * height * 4, 255);
        if (width < 1 || height < 1) return img;
        for (int y = 0; y < height; ++y) {
            float t = (float)y / (float)(height - 1); // 0 top, 1 bottom
            float r = topR + (botR - topR) * t;
            float g = topG + (botG - topG) * t;
            float b = topB + (botB - topB) * t;
            for (int x = 0; x < width; ++x) {
                std::size_t idx = ((std::size_t)y * width + x) * 4;
                img[idx + 0] = (std::uint8_t)(r * 255.0f);
                img[idx + 1] = (std::uint8_t)(g * 255.0f);
                img[idx + 2] = (std::uint8_t)(b * 255.0f);
                img[idx + 3] = 255;
            }
        }
        return img;
    }

    // Generate one face of a cubemap as a simple directional gradient.
    // face in [0,5] selects the axis; returns RGBA8 face at size x size.
    static std::vector<std::uint8_t> GenerateFace(
        int face, int size,
        float topR, float topG, float topB,
        float botR, float botG, float botB) {
        std::vector<std::uint8_t> img((std::size_t)size * size * 4, 255);
        if (size < 1) return img;
        (void)face;
        for (int y = 0; y < size; ++y) {
            float t = (float)y / (float)(size - 1);
            float r = topR + (botR - topR) * t;
            float g = topG + (botG - topG) * t;
            float b = topB + (botB - topB) * t;
            for (int x = 0; x < size; ++x) {
                std::size_t idx = ((std::size_t)y * size + x) * 4;
                img[idx + 0] = (std::uint8_t)(r * 255.0f);
                img[idx + 1] = (std::uint8_t)(g * 255.0f);
                img[idx + 2] = (std::uint8_t)(b * 255.0f);
                img[idx + 3] = 255;
            }
        }
        return img;
    }
};

} // namespace bighero
