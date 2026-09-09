#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>

namespace bighero {

// Audio mixer: sums multiple input channels (mono float buffers weighted by
// gain) into a master output bus. Lightweight CPU-side mixing.
class AudioMixer {
public:
    explicit AudioMixer(int channels = 2) : channels_(channels < 1 ? 1 : channels) {}

    void SetGain(float g) { gain_ = g < 0 ? 0 : g; }
    float Gain() const { return gain_; }
    void SetMuted(bool m) { muted_ = m; }
    bool Muted() const { return muted_; }
    int Channels() const { return channels_; }

    // Mix a mono source into the output at `frame` with per-source gain.
    void AddMono(float* out, std::size_t frames, const float* src,
                 float srcGain = 1.0f) {
        for (std::size_t i = 0; i < frames; ++i) {
            float s = src[i] * srcGain * (muted_ ? 0.0f : gain_);
            for (int c = 0; c < channels_; ++c)
                out[(std::size_t)i * channels_ + c] += s;
        }
    }

    // Mix a stereo source (interleaved) into the output.
    void AddStereo(float* out, std::size_t frames, const float* src,
                   float srcGain = 1.0f) {
        if (channels_ < 2) return;
        for (std::size_t i = 0; i < frames; ++i) {
            float g = muted_ ? 0.0f : gain_ * srcGain;
            out[i * 2] += src[i * 2] * g;
            out[i * 2 + 1] += src[i * 2 + 1] * g;
        }
    }

    // Apply master gain + mute as final bus processing.
    void ApplyMaster(float* out, std::size_t frames) {
        float g = muted_ ? 0.0f : gain_;
        for (std::size_t i = 0; i < frames * (std::size_t)channels_; ++i)
            out[i] *= g;
    }

    void Clear(float* out, std::size_t frames) {
        std::fill(out, out + frames * (std::size_t)channels_, 0.0f);
    }

private:
    int channels_;
    float gain_ = 1.0f;
    bool muted_ = false;
};

} // namespace bighero
