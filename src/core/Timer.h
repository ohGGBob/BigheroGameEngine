#pragma once
#include <cstdint>
#include <functional>

namespace bighero {

// Reusable timer: counts down, supports one-shot and repeating modes.
struct Timer {
    enum Mode { OneShot, Repeat };

    float duration = 1.0f;
    float elapsed = 0.0f;
    Mode mode = OneShot;
    bool running = false;
    std::function<void()> callback;

    void Start(float d, bool repeat = false) {
        duration = d > 0 ? d : 1.0f;
        elapsed = 0;
        running = true;
        mode = repeat ? Repeat : OneShot;
    }
    void Stop() { running = false; elapsed = 0; }
    void Pause() { running = false; }

    // Advance by dt; invokes callback when (re)triggered. Returns times fired.
    int Update(float dt) {
        if (!running) return 0;
        elapsed += dt;
        int fired = 0;
        while (running && elapsed >= duration) {
            elapsed -= duration;
            if (callback) callback();
            ++fired;
            if (mode == OneShot) { running = false; break; }
        }
        return fired;
    }

    float Remaining() const { return running ? (duration - elapsed > 0 ? duration - elapsed : 0) : 0; }
    float Progress() const { return duration > 0 ? (elapsed / duration > 1 ? 1 : elapsed / duration) : 0; }
    bool IsRunning() const { return running; }
};

} // namespace bighero
