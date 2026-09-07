#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// 2D separable Gaussian blur on RGBA8 image. Pure standard library.
class GaussianBlur {
public:
    // Blur a width x height RGBA8 image with the given sigma. Returns new buffer.
    static std::vector<std::uint8_t> Apply(
        const std::vector<std::uint8_t>& src, int width, int height, float sigma) {
        if (width < 1 || height < 1 || src.size() < (std::size_t)width * height * 4) return src;
        std::vector<float> kernel = MakeKernel(sigma);
        int radius = (int)(kernel.size() / 2);

        // Horizontal pass.
        std::vector<std::uint8_t> tmp(src.size());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float r = 0, g = 0, b = 0, a = 0;
                for (int k = -radius; k <= radius; ++k) {
                    int sx = x + k;
                    if (sx < 0) sx = 0;
                    if (sx >= width) sx = width - 1;
                    float w = kernel[k + radius];
                    std::size_t idx = ((std::size_t)y * width + sx) * 4;
                    r += src[idx + 0] * w;
                    g += src[idx + 1] * w;
                    b += src[idx + 2] * w;
                    a += src[idx + 3] * w;
                }
                std::size_t o = ((std::size_t)y * width + x) * 4;
                tmp[o + 0] = Clamp(r); tmp[o + 1] = Clamp(g);
                tmp[o + 2] = Clamp(b); tmp[o + 3] = Clamp(a);
            }
        }

        // Vertical pass.
        std::vector<std::uint8_t> out(src.size());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float r = 0, g = 0, b = 0, a = 0;
                for (int k = -radius; k <= radius; ++k) {
                    int sy = y + k;
                    if (sy < 0) sy = 0;
                    if (sy >= height) sy = height - 1;
                    float w = kernel[k + radius];
                    std::size_t idx = ((std::size_t)sy * width + x) * 4;
                    r += tmp[idx + 0] * w;
                    g += tmp[idx + 1] * w;
                    b += tmp[idx + 2] * w;
                    a += tmp[idx + 3] * w;
                }
                std::size_t o = ((std::size_t)y * width + x) * 4;
                out[o + 0] = Clamp(r); out[o + 1] = Clamp(g);
                out[o + 2] = Clamp(b); out[o + 3] = Clamp(a);
            }
        }
        return out;
    }

    static std::vector<float> MakeKernel(float sigma) {
        int radius = (int)std::ceil(sigma * 3.0f);
        if (radius < 1) radius = 1;
        std::vector<float> k(2 * radius + 1);
        float sum = 0.0f;
        float twoSigma2 = 2.0f * sigma * sigma;
        for (int i = -radius; i <= radius; ++i) {
            float v = std::exp(-(float)(i * i) / (twoSigma2 + 1e-8f));
            k[i + radius] = v;
            sum += v;
        }
        if (sum > 1e-8f) for (float& v : k) v /= sum;
        return k;
    }

private:
    static std::uint8_t Clamp(float v) {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return (std::uint8_t)(v + 0.5f);
    }
};

} // namespace bighero
