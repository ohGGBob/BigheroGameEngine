#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// BillboardRenderer: renders a quad always facing the camera. Stores the
// facing mode, scale, material id, and an optional size. Pure config container
// consumed by the render backend.
class BillboardRenderer {
public:
    enum class Mode { FaceCamera, FaceCameraY, Aligned };

    BillboardRenderer() {}
    explicit BillboardRenderer(std::uint64_t materialId) : materialId_(materialId) {}

    void SetMode(Mode m) { mode_ = m; }
    Mode CurrentMode() const { return mode_; }
    void SetMaterialId(std::uint64_t id) { materialId_ = id; }
    std::uint64_t MaterialId() const { return materialId_; }
    void SetWidth(float w) { width_ = w < 0 ? 0 : w; }
    float Width() const { return width_; }
    void SetHeight(float h) { height_ = h < 0 ? 0 : h; }
    float Height() const { return height_; }
    void SetAnchorY(float y) { anchorY_ = y; }
    float AnchorY() const { return anchorY_; }
    void SetCameraFaceAxis(std::size_t axis) { faceAxis_ = axis % 3; }
    std::size_t CameraFaceAxis() const { return faceAxis_; }

    void SetCastShadows(bool c) { castShadows_ = c; }
    bool CastShadows() const { return castShadows_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    Mode mode_ = Mode::FaceCameraY;
    std::uint64_t materialId_ = 0;
    float width_ = 1.0f, height_ = 1.0f, anchorY_ = 0.0f;
    std::size_t faceAxis_ = 1;
    bool castShadows_ = false, enabled_ = true;
};

} // namespace bighero
