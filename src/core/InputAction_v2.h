#pragma once
#include <cstdint>

namespace bighero {

// InputAction: an abstracted input action that binds a physical input (keyboard
// key, mouse button, gamepad button or axis) to a logical action, with trigger
// / edge detection and deadzone. Self-contained, std-lib only.
class InputAction {
public:
    enum class Source : int {
        KeyboardKey = 0, MouseButton = 1, GamepadButton = 2,
        GamepadAxis = 3, MouseAxis = 4
    };

    InputAction() = default;
    InputAction(Source src, int inputId) : source_(src), inputId_(inputId) {}

    void SetSource(Source s, int id) { source_=s; inputId_=id; }
    Source GetSource() const { return source_; }
    int GetInputId() const { return inputId_; }

    void SetDeadzone(float d) { deadzone_ = d < 0 ? 0 : (d > 1 ? 1 : d); }
    float Deadzone() const { return deadzone_; }

    void SetRawValue(float v) { raw_ = v; }
    float RawValue() const { return raw_; }

    // Apply deadzone and remap to [0,1].
    float Processed() const {
        float v = raw_;
        if (v < -deadzone_) return (v + deadzone_) / (1.0f - deadzone_);
        if (v >  deadzone_) return (v - deadzone_) / (1.0f - deadzone_);
        return 0;
    }
    bool IsPressed() const { return raw_ < -deadzone_ || raw_ > deadzone_; }
    bool IsReleased() const { return !IsPressed(); }

    // Edge detection against previous processed value.
    bool WasPressed() const { return IsPressed() && !prevPressed_; }
    bool WasReleased() const { return !IsPressed() && prevPressed_; }

    void Reset() { raw_=0; prevPressed_=false; }
    void Update() { prevPressed_ = IsPressed(); }

private:
    Source source_ = Source::KeyboardKey;
    int inputId_ = 0;
    float deadzone_ = 0.1f;
    float raw_ = 0;
    bool prevPressed_ = false;
};

} // namespace bighero
