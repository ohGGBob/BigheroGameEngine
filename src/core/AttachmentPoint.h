#pragma once
#include <cmath>
#include <cstddef>

namespace bighero {

// AttachmentPoint: a named socket on an entity (e.g. hand, head, weapon) that
// other objects can be attached to. Stores a local offset + orientation.
// Standard-library only, self-contained.
class AttachmentPoint {
public:
    AttachmentPoint() {}
    AttachmentPoint(const char* name, float ox, float oy, float oz)
        : name_(name ? name : "") { SetOffset(ox, oy, oz); }

    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_ ? name_ : ""; }

    void SetOffset(float ox, float oy, float oz) { ox_=ox; oy_=oy; oz_=oz; }
    void Offset(float& ox, float& oy, float& oz) const { ox=ox_; oy=oy_; oz=oz_; }

    void SetRotation(float sy) { yaw_ = sy; }
    float Yaw() const { return yaw_; }

    void SetScale(float s) { scale_ = s > 0 ? s : 1.0f; }
    float Scale() const { return scale_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // World position = parent position + offset (optionally rotated by parent
    // yaw for a simple 2D spine). For a 3D parent we just translate.
    void WorldPosition(float parentX, float parentY, float parentZ,
                       float& wx, float& wy, float& wz) const {
        wx = parentX + ox_;
        wy = parentY + oy_;
        wz = parentZ + oz_;
    }

private:
    const char* name_ = "";
    float ox_=0, oy_=0, oz_=0;
    float yaw_=0, scale_=1.0f;
    bool enabled_=true;
};

} // namespace bighero
