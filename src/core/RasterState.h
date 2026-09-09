#pragma once
#include <cstdint>

namespace bighero {

// RasterState: describes fixed-function rasterizer state for a graphics
// pipeline (cull mode, fill mode, front-face winding, depth bias, etc.).
// Self-contained, std-lib only.
class RasterState {
public:
    enum class CullMode : uint8_t { None = 0, Back = 1, Front = 2, FrontAndBack = 3 };
    enum class FillMode : uint8_t { Solid = 0, Wireframe = 1, Point = 2 };
    enum class FrontFace : uint8_t { CounterClockwise = 0, Clockwise = 1 };

    RasterState() = default;
    RasterState(CullMode cull, FillMode fill = FillMode::Solid, FrontFace front = FrontFace::CounterClockwise)
        : cull_(cull), fill_(fill), front_(front) {}

    void SetCullMode(CullMode c) { cull_ = c; }
    CullMode Cull() const { return cull_; }
    void SetFillMode(FillMode f) { fill_ = f; }
    FillMode Fill() const { return fill_; }
    void SetFrontFace(FrontFace f) { front_ = f; }
    FrontFace Front() const { return front_; }

    void SetDepthBias(float constant, float slope = 0, float clampVal = 0) {
        depthBias_ = constant; slopeBias_ = slope; clampBias_ = clampVal;
    }
    float DepthBias() const { return depthBias_; }
    float SlopeBias() const { return slopeBias_; }
    float ClampBias() const { return clampBias_; }

    void SetLineWidth(float w) { lineWidth_ = w; }
    float LineWidth() const { return lineWidth_; }

    void EnableDepthClamp(bool b) { depthClamp_ = b; }
    bool DepthClamp() const { return depthClamp_; }

    uint32_t Pack() const {
        return (uint32_t)cull_ | ((uint32_t)fill_ << 2) | ((uint32_t)front_ << 4);
    }

private:
    CullMode cull_ = CullMode::Back;
    FillMode fill_ = FillMode::Solid;
    FrontFace front_ = FrontFace::CounterClockwise;
    float depthBias_ = 0, slopeBias_ = 0, clampBias_ = 0, lineWidth_ = 1.0f;
    bool depthClamp_ = false;
};

} // namespace bighero
