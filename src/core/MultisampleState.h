#pragma once
#include <cstdint>
#include "SampleCount_v2.h"

namespace bighero {

// MultisampleState: describes the multisample antialiasing (MSAA) state for a
// graphics pipeline. Self-contained, std-lib only.
class MultisampleState {
public:
    MultisampleState() = default;
    explicit MultisampleState(SampleCount sampleCount) : samples_(sampleCount) {}

    void SetSampleCount(SampleCount s) { samples_ = s; }
    SampleCount SampleCountValue() const { return samples_; }
    uint8_t Samples() const { return samples_.Count(); }

    void EnableSampleShading(bool b) { sampleShading_ = b; }
    bool SampleShadingEnabled() const { return sampleShading_; }
    void SetMinSampleShading(float f) { minSampleShading_ = f; }
    float MinSampleShading() const { return minSampleShading_; }

    void EnableAlphaToCoverage(bool b) { alphaToCoverage_ = b; }
    bool AlphaToCoverage() const { return alphaToCoverage_; }
    void EnableAlphaToOne(bool b) { alphaToOne_ = b; }
    bool AlphaToOne() const { return alphaToOne_; }

    bool IsMSAA() const { return samples_.IsMSAA(); }
    bool IsValid() const { return samples_.IsValid(); }

private:
    SampleCount samples_;
    bool sampleShading_ = false;
    float minSampleShading_ = 1.0f;
    bool alphaToCoverage_ = false;
    bool alphaToOne_ = false;
};

} // namespace bighero
