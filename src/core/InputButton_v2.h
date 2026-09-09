#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// InputButton: a logical button (e.g. "Jump", "Fire") that can be bound to a
// physical source, with edge detection (pressed/released) and hold count.
// Self-contained, std-lib only.
class InputButton {
public:
    InputButton() = default;
    explicit InputButton(int buttonId) : buttonId_(buttonId) {}

    void SetId(int id) { buttonId_ = id; }
    int Id() const { return buttonId_; }

    void SetName(const char* name) { name_ = name ? name : ""; }
    const char* Name() const { return name_.c_str(); }

    void SetSource(int source) { source_ = source; }
    int Source() const { return source_; }

    // Feed a physical press state (0/1).
    void SetState(bool pressed) {
        if (pressed && !down_) justPressed_ = true;
        if (!pressed && down_) justReleased_ = true;
        down_ = pressed;
    }
    bool IsDown() const { return down_; }
    bool WasPressedThisFrame() const { return justPressed_; }
    bool WasReleasedThisFrame() const { return justReleased_; }
    bool IsUp() const { return !down_; }

    // Tracks how many frames the button has been held continuously.
    int HeldFrames() const { return heldFrames_; }
    void Tick() {
        if (down_) ++heldFrames_;
        else heldFrames_ = 0;
    }
    void EndFrame() {
        justPressed_ = false;
        justReleased_ = false;
    }
    void Reset() { down_ = false; justPressed_ = false; justReleased_ = false; heldFrames_ = 0; }

private:
    int buttonId_ = 0;
    std::string name_;
    int source_ = 0;
    bool down_ = false;
    bool justPressed_ = false;
    bool justReleased_ = false;
    int heldFrames_ = 0;
};

} // namespace bighero
