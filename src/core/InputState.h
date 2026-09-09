#pragma once
#include <string>
#include <cmath>

namespace bighero {

// InputState: a snapshot of raw input values for a single frame: axis
// values, and boolean key/mouse triggers. Pure data container consumed by
// the input system.
class InputState {
public:
    InputState() {}

    // --- Axis (analog) ---
    void SetAxis(const std::string& name, float value) {
        std::size_t i = IndexOf(name);
        if (i == npos) return;
        axes_[i] = value;
    }
    float GetAxis(const std::string& name) const {
        std::size_t i = IndexOf(name);
        return i == npos ? 0.0f : axes_[i];
    }
    void SetAxis(std::size_t index, float value) {
        if (index < axisCount_) axes_[index] = value;
    }
    float GetAxis(std::size_t index) const {
        return index < axisCount_ ? axes_[index] : 0.0f;
    }
    float GetAbsAxis(const std::string& name) const {
        return std::fabs(GetAxis(name));
    }
    bool IsAxisPositive(const std::string& name) const { return GetAxis(name) > 0.0f; }
    bool IsAxisNegative(const std::string& name) const { return GetAxis(name) < 0.0f; }

    // --- Buttons (digital) ---
    void SetKey(const std::string& name, bool down) {
        std::size_t i = IndexOfKey(name);
        if (i == npos) return;
        keys_[i] = down;
    }
    bool GetKey(const std::string& name) const {
        std::size_t i = IndexOfKey(name);
        return i == npos ? false : keys_[i];
    }

    void Reset() {
        for (std::size_t i = 0; i < axisCount_; ++i) axes_[i] = 0;
        for (std::size_t i = 0; i < keyCount_; ++i) keys_[i] = false;
    }

    static constexpr std::size_t npos = (std::size_t)-1;
    static constexpr std::size_t axisCount_ = 16;
    static constexpr std::size_t keyCount_ = 32;

private:
    static std::size_t IndexOf(const std::string& name) {
        // Well-known axes by first char hash fallback; keep simple: map to 0-15.
        std::size_t h = 0;
        for (char c : name) h = h * 31 + (unsigned char)c;
        return h % axisCount_;
    }
    static std::size_t IndexOfKey(const std::string& name) {
        std::size_t h = 0;
        for (char c : name) h = h * 31 + (unsigned char)c;
        return h % keyCount_;
    }

    float axes_[axisCount_] = {};
    bool keys_[keyCount_] = {};
};

} // namespace bighero
