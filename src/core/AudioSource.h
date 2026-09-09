#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// Audio source: playback metadata for a 3D/2D sound. Pure data holder used
// by the audio system to schedule and mix clips.
class AudioSource {
public:
    enum class Mode { Once, Loop, PingPong };
    enum class Spatial { Global, Positional };

    explicit AudioSource(const std::string& clip = "") : clip_(clip) {}

    void SetClip(const std::string& clip) { clip_ = clip; }
    const std::string& Clip() const { return clip_; }

    void SetMode(Mode m) { mode_ = m; }
    Mode PlaybackMode() const { return mode_; }
    void SetSpatial(Spatial s) { spatial_ = s; }
    Spatial SpatialMode() const { return spatial_; }

    void SetVolume(float v) { volume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
    float Volume() const { return volume_; }
    void SetPitch(float p) { pitch_ = p <= 0 ? 1 : p; }
    float Pitch() const { return pitch_; }
    void SetPan(float pan) { pan_ = pan < -1 ? -1 : (pan > 1 ? 1 : pan); }
    float Pan() const { return pan_; }
    void SetLoop(bool loop) { mode_ = loop ? Mode::Loop : Mode::Once; }
    bool IsLooping() const { return mode_ == Mode::Loop; }

    // Playback position in seconds.
    void SetTime(float t) { time_ = t < 0 ? 0 : t; }
    float Time() const { return time_; }
    void SetPaused(bool p) { paused_ = p; }
    bool Paused() const { return paused_; }

    void Play() { paused_ = false; playing_ = true; }
    void Stop() { playing_ = false; time_ = 0; }
    bool IsPlaying() const { return playing_ && !paused_; }

private:
    std::string clip_;
    Mode mode_ = Mode::Once;
    Spatial spatial_ = Spatial::Global;
    float volume_ = 1.0f;
    float pitch_ = 1.0f;
    float pan_ = 0.0f;
    float time_ = 0.0f;
    bool paused_ = false;
    bool playing_ = false;
};

} // namespace bighero
