#pragma once
#include <string>
#include <vector>
#include <cmath>

namespace bighero {

// Animation clip controller: plays named clips by time, with blending weight.
class AnimationController {
public:
    struct Clip {
        std::string name;
        float duration = 1.0f;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = false;
    };

    // Add a clip (or replace existing by name).
    void AddClip(const char* name, float duration, bool loop = true) {
        for (auto& c : clips_) {
            if (c.name == name) { c.duration = duration; c.loop = loop; return; }
        }
        Clip c; c.name = name; c.duration = duration; c.loop = loop;
        clips_.push_back(c);
    }

    // Start playing a named clip (resets its time).
    bool Play(const char* name) {
        Clip* c = Find(name);
        if (!c) return false;
        c->time = 0; c->playing = true;
        current_ = name;
        return true;
    }
    bool Stop(const char* name) {
        Clip* c = Find(name);
        if (!c) return false;
        c->playing = false;
        return true;
    }

    // Advance current clip by dt; wraps if looping.
    void Update(float dt) {
        for (auto& c : clips_) {
            if (!c.playing) continue;
            c.time += dt * c.speed;
            if (c.time >= c.duration) {
                if (c.loop) c.time = std::fmod(c.time, c.duration);
                else { c.time = c.duration; c.playing = false; }
            }
        }
    }

    float GetTime(const char* name) const {
        const Clip* c = Find(name);
        return c ? c->time : 0.0f;
    }
    bool IsPlaying(const char* name) const {
        const Clip* c = Find(name);
        return c && c->playing;
    }
    const std::string& Current() const { return current_; }
    int NumClips() const { return (int)clips_.size(); }

    // Normalized progress [0,1] of a clip.
    float NormalizedTime(const char* name) const {
        const Clip* c = Find(name);
        if (!c || c->duration <= 0) return 0.0f;
        return c->time / c->duration;
    }

private:
    Clip* Find(const char* name) {
        for (auto& c : clips_) if (c.name == name) return &c;
        return nullptr;
    }
    const Clip* Find(const char* name) const {
        for (auto& c : clips_) if (c.name == name) return &c;
        return nullptr;
    }
    std::vector<Clip> clips_;
    std::string current_;
};

} // namespace bighero
