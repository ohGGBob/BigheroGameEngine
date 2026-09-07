#pragma once

namespace bighero {

// Rasterizer state: fill mode, cull mode, and winding order for triangle
// rendering. CPU-side state payload for a draw submission.
class RasterizerState {
public:
    enum class Fill { Solid, Wireframe, Point };
    enum class Cull { None, Back, Front, Both };
    enum class Winding { CW, CCW };

    RasterizerState() {}

    void SetFill(Fill f) { fill_ = f; }
    Fill FillMode() const { return fill_; }
    void SetCull(Cull c) { cull_ = c; }
    Cull CullMode() const { return cull_; }
    void SetWinding(Winding w) { winding_ = w; }
    Winding WindingOrder() const { return winding_; }

    void SetDepthBias(float b) { depthBias_ = b; }
    float DepthBias() const { return depthBias_; }
    void SetScissor(bool s) { scissor_ = s; }
    bool Scissor() const { return scissor_; }
    void SetMultisample(bool m) { multisample_ = m; }
    bool Multisample() const { return multisample_; }

    static RasterizerState Default() {
        RasterizerState r;
        r.fill_ = Fill::Solid;
        r.cull_ = Cull::Back;
        r.winding_ = Winding::CCW;
        return r;
    }
    static RasterizerState Wireframe() {
        RasterizerState r;
        r.fill_ = Fill::Wireframe;
        r.cull_ = Cull::None;
        r.winding_ = Winding::CCW;
        return r;
    }

private:
    Fill fill_ = Fill::Solid;
    Cull cull_ = Cull::Back;
    Winding winding_ = Winding::CCW;
    float depthBias_ = 0;
    bool scissor_ = false;
    bool multisample_ = true;
};

} // namespace bighero
