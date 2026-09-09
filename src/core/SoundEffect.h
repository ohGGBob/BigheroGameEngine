#pragma once
#include <string>
#include <memory>
#include <cstddef>

namespace bighero {

// Sound effect: a one-shot (or looping) audio handle bound to a clip. Wraps
// shared sample data and per-instance playback state.
class SoundEffect {
public:
    explicit SoundEffect(const std::string& clip = "") : clip_(clip) {}

    void SetClip(const std::string& clip) { clip_ = clip; }
    const std::string& Clip() const { return clip_; }

    void SetVolume(float v) { volume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
    float Volume() const { return volume_; }
    void SetPitch(float p) { pitch_ = p <= 0 ? 1 : p; }
    float Pitch() const { return pitch_; }

    void SetLoop(bool loop) { loop_ = loop; }
    bool Loop() const { return loop_; }
    void SetSpatial(bool s) { spatial_ = s; }
    bool Spatial() const { return spatial_; }
    void SetMaxDistance(float d) { maxDist_ = d < 0 ? 0 : d; }
    float MaxDistance() const { return maxDist_; }

    // Per-instance playback state.
    void Play() { playing_ = true; time_ = 0; }
    void Stop() { playing_ = false; time_ = 0; }
    bool IsPlaying() const { return playing_; }
    void SetTime(float t) { time_ = t < 0 ? 0 : t; }
    float Time() const { return time_; }

    bool IsValid() const { return !clip_.empty(); }

private:
    std::string clip_;
    float volume_ = 1.0f;
    float pitch_ = 1.0f;
    bool loop_ = false;
    bool spatial_ = false;
    float maxDist_ = 100.0f;
    bool playing_ = false;
    float time_ = 0.0f;
};

} // namespace bighero
