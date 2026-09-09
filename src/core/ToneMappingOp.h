#pragma once
#include <cstddef>

namespace bighero {

// ToneMappingOp: describes a selectable tone-mapping operator and its params.
// Holds the op enum plus exposure/gamma; provides a scalar map function.
// Pure CPU-side descriptor + math used by the post-process/bloom chain.
class ToneMappingOp {
public:
    enum class Op { None, Reinhard, ACESFilmic, Filmic, Linear };

    ToneMappingOp() {}
    explicit ToneMappingOp(Op op) : op_(op) {}

    void SetOp(Op o) { op_ = o; }
    Op Current() const { return op_; }
    void SetExposure(float e) { exposure_ = e; }
    float Exposure() const { return exposure_; }
    void SetGamma(float g) { gamma_ = g < 0.01f ? 0.01f : g; }
    float Gamma() const { return gamma_; }

    // Apply the operator to a linear HDR value -> [0,1] display value.
    float Map(float v) const {
        float x = v * exposure_;
        float out;
        switch (op_) {
            case Op::None:      out = x; break;
            case Op::Reinhard:  out = x / (1.0f + x); break;
            case Op::Linear:    out = x; break;
            case Op::Filmic: {
                // simple filmic response approximation
                float y = x;
                out = (y*(2.51f*y + 0.03f)) / (y*(2.43f*y + 0.59f) + 0.14f);
                break;
            }
            case Op::ACESFilmic: {
                float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
                float y = x;
                out = (y*(a*y + b)) / (y*(c*y + d) + e);
                break;
            }
            default: out = x; break;
        }
        if (out < 0) out = 0;
        if (out > 1) out = 1;
        if (gamma_ != 1.0f) {
            out = std::pow(out, 1.0f / gamma_);
        }
        return out;
    }

    void Reset() { op_ = Op::None; exposure_ = 1.0f; gamma_ = 2.2f; }

private:
    Op op_ = Op::None;
    float exposure_ = 1.0f, gamma_ = 2.2f;
};

} // namespace bighero
