#pragma once
#include <vector>
#include <cstdint>

namespace bighero {

// GizmoDrawer: collects primitive shape draw requests (line, box, sphere,
// circle) for editor / debug overlay rendering. Pure CPU-side command list.
class GizmoDrawer {
public:
    enum class Prim { Line, Box, Sphere, Circle };

    struct Command {
        Prim prim;
        float x0, y0, z0;
        float x1, y1, z1;
        float r, g, b, a;
        float thickness;
    };

    GizmoDrawer() {}

    void SetColor(float r, float g, float b, float a = 1.0f) {
        cr_ = Clamp01(r); cg_ = Clamp01(g); cb_ = Clamp01(b); ca_ = Clamp01(a);
    }
    void SetThickness(float t) { thickness_ = t < 0 ? 0 : t; }

    void Line(float x0, float y0, float z0, float x1, float y1, float z1) {
        Push(Prim::Line, x0, y0, z0, x1, y1, z1);
    }
    void Box(float cx, float cy, float cz, float hx, float hy, float hz) {
        Push(Prim::Box, cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz);
    }
    void Sphere(float cx, float cy, float cz, float radius) {
        Push(Prim::Sphere, cx - radius, cy - radius, cz - radius, cx + radius, cy + radius, cz + radius);
    }
    void Circle2D(float cx, float cy, float radius) {
        Push(Prim::Circle, cx - radius, cy, 0, cx + radius, cy, 0);
    }

    std::size_t Count() const { return commands_.size(); }
    const Command& At(std::size_t i) const { return commands_[i]; }
    void Clear() { commands_.clear(); }

private:
    void Push(Prim p, float x0, float y0, float z0, float x1, float y1, float z1) {
        Command c;
        c.prim = p;
        c.x0 = x0; c.y0 = y0; c.z0 = z0;
        c.x1 = x1; c.y1 = y1; c.z1 = z1;
        c.r = cr_; c.g = cg_; c.b = cb_; c.a = ca_;
        c.thickness = thickness_;
        commands_.push_back(c);
    }
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

    std::vector<Command> commands_;
    float cr_ = 1, cg_ = 1, cb_ = 1, ca_ = 1;
    float thickness_ = 1.0f;
};

} // namespace bighero
