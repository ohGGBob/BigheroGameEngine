#pragma once
#include <vector>
#include <cstdint>

namespace bighero {

// AudioBus: an audio routing bus that aggregates several input channels with
// a master volume/pan and sums them. Self-contained, std-lib only.
class AudioBus {
public:
    AudioBus() = default;
    explicit AudioBus(uint32_t id) : id_(id) {}

    void SetId(uint32_t id) { id_ = id; }
    uint32_t Id() const { return id_; }

    void SetVolume(float v) { volume_ = v < 0 ? 0 : v; }
    float Volume() const { return volume_; }
    void SetPan(float p) { pan_ = p < -1 ? -1 : (p > 1 ? 1 : p); }
    float Pan() const { return pan_; }

    void AddInput(uint32_t sourceId, float gain) {
        Input in; in.sourceId = sourceId; in.gain = gain;
        inputs_.push_back(in);
    }
    void ClearInputs() { inputs_.clear(); }
    size_t InputCount() const { return inputs_.size(); }
    uint32_t InputAt(size_t i) const {
        return i < inputs_.size() ? inputs_[i].sourceId : 0;
    }

    // Sum a set of per-input samples into a single bus output.
    float Mix(const float* inputSamples, size_t count) const {
        float sum = 0;
        for (size_t i = 0; i < count; ++i)
            sum += inputSamples[i] * inputs_[i].gain;
        return sum * volume_;
    }

private:
    struct Input { uint32_t sourceId = 0; float gain = 1.0f; };
    uint32_t id_ = 0;
    float volume_ = 1.0f;
    float pan_ = 0.0f;
    std::vector<Input> inputs_;
};

} // namespace bighero
