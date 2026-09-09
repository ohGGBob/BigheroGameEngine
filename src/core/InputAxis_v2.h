#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// InputAxis: a named 1D input axis mapping a positive and negative source
// (e.g. keyboard A/D, stick x) to a single analog value in [-1,1].
// Self-contained, std-lib only.
class InputAxis {
public:
    InputAxis() = default;
    explicit InputAxis(int axisId) : axisId_(axisId) {}

    void SetId(int id) { axisId_ = id; }
    int Id() const { return axisId_; }

    void SetName(const char* name) { name_ = name ? name : ""; }
    const char* Name() const { return name_.c_str(); }

    // Bind positive/negative source ids (e.g. Keyboard::Key::D / A).
    void SetSources(int positiveSrc, int negativeSrc) {
        positiveSrc_ = positiveSrc; negativeSrc_ = negativeSrc;
    }

    void SetPositive(float v) { pos_ = v; }
    void SetNegative(float v) { neg_ = v; }

    // Combine positive and negative contributions into [-1,1].
    float Value() const {
        float v = pos_ - neg_;
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        return v;
    }
    // Deadzone-filtered value.
    float Value(float deadzone) const {
        float v = Value();
        if (v > -deadzone && v < deadzone) return 0;
        return v;
    }
    bool IsActive() const { return Value() != 0.0f; }
    void EndFrame() { pos_ = 0; neg_ = 0; }

private:
    int axisId_ = 0;
    std::string name_;
    int positiveSrc_ = 0, negativeSrc_ = 0;
    float pos_ = 0, neg_ = 0;
};

} // namespace bighero
