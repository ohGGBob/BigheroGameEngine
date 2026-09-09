#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// AudioClip: holds raw PCM audio samples plus sample rate/channel metadata,
// with mix/loop/resample helpers. Self-contained, std-lib only.
class AudioClip {
public:
    AudioClip() = default;
    AudioClip(uint32_t sampleRate, int channels, const std::vector<float>& samples)
        : sampleRate_(sampleRate), channels_(channels < 1 ? 1 : channels), samples_(samples) {}

    void SetData(uint32_t sampleRate, int channels, std::vector<float> samples) {
        sampleRate_ = sampleRate;
        channels_ = channels < 1 ? 1 : channels;
        samples_ = std::move(samples);
    }
    uint32_t SampleRate() const { return sampleRate_; }
    int Channels() const { return channels_; }
    float Duration() const {
        return sampleRate_ > 0 ? (float)(samples_.size() / (size_t)channels_) / sampleRate_ : 0.0f;
    }
    size_t SampleCount() const { return samples_.size(); }
    size_t FrameCount() const { return samples_.size() / (size_t)channels_; }
    const std::vector<float>& Samples() const { return samples_; }
    bool IsLoaded() const { return !samples_.empty(); }
    void Clear() { samples_.clear(); sampleRate_ = 0; channels_ = 1; }

    // Get an interleaved sample at a given frame + channel (clamped).
    float SampleFrame(size_t frame, int channel) const {
        if (samples_.empty() || channels_ < 1) return 0;
        if (channel < 0 || channel >= channels_) channel = 0;
        size_t frames = FrameCount();
        if (frame >= frames) frame = frames > 0 ? frames - 1 : 0;
        return samples_[frame * (size_t)channels_ + (size_t)channel];
    }
    // Interpolated sample at a fractional frame position.
    float SampleAt(float timeSec, int channel = 0) const {
        if (samples_.empty() || sampleRate_ == 0) return 0;
        float pos = timeSec * (float)sampleRate_;
        size_t i = (size_t)pos;
        float frac = pos - (float)i;
        float a = SampleFrame(i, channel);
        float b = SampleFrame(i + 1, channel);
        return a + (b - a) * frac;
    }

    // Resample to a new sample rate using linear interpolation.
    AudioClip Resampled(uint32_t newRate) const {
        if (newRate == sampleRate_ || sampleRate_ == 0 || samples_.empty()) return *this;
        // ratio = number of source frames consumed per output frame.
        float ratio = (float)sampleRate_ / (float)newRate;
        size_t newFrames = (size_t)((float)FrameCount() / ratio);
        std::vector<float> out(newFrames * (size_t)channels_);
        for (size_t f = 0; f < newFrames; ++f) {
            float srcPos = (float)f * ratio;
            size_t i = (size_t)srcPos;
            float frac = srcPos - (float)i;
            for (int c = 0; c < channels_; ++c) {
                float a = SampleFrame(i, c);
                float b = SampleFrame(i + 1, c);
                out[f * (size_t)channels_ + (size_t)c] = a + (b - a) * frac;
            }
        }
        return AudioClip(newRate, channels_, out);
    }

    // Mix another clip into this one at a given start frame and volume.
    void Mix(const AudioClip& other, size_t startFrame, float volume) {
        if (channels_ < 1 || other.channels_ != channels_) return;
        for (size_t f = 0; f < other.FrameCount(); ++f) {
            size_t dst = startFrame + f;
            if (dst >= FrameCount()) break;
            for (int c = 0; c < channels_; ++c) {
                samples_[dst * (size_t)channels_ + (size_t)c] +=
                    other.SampleFrame(f, c) * volume;
            }
        }
    }

private:
    uint32_t sampleRate_ = 0;
    int channels_ = 1;
    std::vector<float> samples_;
};

} // namespace bighero
