#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// In-memory PCM audio buffer: interleaved float samples with a channel count
// and sample rate. Building block for procedural audio and sound effects.
class AudioBuffer {
public:
    enum class Format { Mono, Stereo, MultiChannel };

    AudioBuffer() {}
    AudioBuffer(int channels, int sampleRate, std::size_t frames)
        : channels_(channels), sampleRate_(sampleRate),
          frames_(frames), samples_((std::size_t)channels * frames, 0.0f) {}

    int Channels() const { return channels_; }
    int SampleRate() const { return sampleRate_; }
    std::size_t Frames() const { return frames_; }
    std::size_t SampleCount() const { return samples_.size(); }

    void Resize(int channels, int sampleRate, std::size_t frames) {
        channels_ = channels; sampleRate_ = sampleRate; frames_ = frames;
        samples_.assign((std::size_t)channels * frames, 0.0f);
    }

    // Interleaved access.
    float& Sample(int channel, std::size_t frame) {
        return samples_[(std::size_t)frame * channels_ + channel];
    }
    float Sample(int channel, std::size_t frame) const {
        return samples_[(std::size_t)frame * channels_ + channel];
    }

    // Mix a mono signal in with a gain (additive).
    void MixMono(const float* data, std::size_t count, float gain = 1.0f) {
        std::size_t n = count < frames_ ? count : frames_;
        for (std::size_t i = 0; i < n; ++i)
            for (int c = 0; c < channels_; ++c)
                Sample(c, i) += data[i] * gain;
    }

    // Fill with a sine tone of frequency `freq` and amplitude `amp`.
    void FillSine(float freq, float amp) {
        if (sampleRate_ <= 0) return;
        for (std::size_t i = 0; i < frames_; ++i) {
            float t = (float)i / (float)sampleRate_;
            float v = std::sin(2.0f * 3.14159265f * freq * t) * amp;
            for (int c = 0; c < channels_; ++c) Sample(c, i) = v;
        }
    }

    // Simple peak amplitude.
    float Peak() const {
        float p = 0;
        for (float s : samples_) { float a = std::fabs(s); if (a > p) p = a; }
        return p;
    }

    bool Empty() const { return samples_.empty(); }
    void Clear() { samples_.assign(samples_.size(), 0.0f); }

private:
    int channels_ = 1;
    int sampleRate_ = 44100;
    std::size_t frames_ = 0;
    std::vector<float> samples_;
};

} // namespace bighero
