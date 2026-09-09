#pragma once
#include <cstdint>

namespace bighero {

// SampleCount: describes the number of samples per pixel for a render target
// (MSAA). Self-contained, std-lib only.
class SampleCount {
public:
    enum : uint8_t { S1 = 1, S2 = 2, S4 = 4, S8 = 8, S16 = 16 };

    SampleCount() = default;
    explicit SampleCount(uint8_t count) : count_(count) {}

    void SetCount(uint8_t c) { count_ = c; }
    uint8_t Count() const { return count_; }
    uint8_t CountValue() const { return count_; }
    bool Is1() const { return count_ == S1; }
    bool Is2() const { return count_ == S2; }
    bool Is4() const { return count_ == S4; }
    bool Is8() const { return count_ == S8; }
    bool Is16() const { return count_ == S16; }
    bool IsMSAA() const { return count_ > S1; }
    bool IsValid() const {
        return count_ == S1 || count_ == S2 || count_ == S4 || count_ == S8 || count_ == S16;
    }

    uint64_t TotalSamples(uint64_t pixelCount) const {
        return pixelCount * count_;
    }
    // Number of resolve steps needed for this sample count (log2).
    int Log2Samples() const {
        int n = 0; uint8_t c = count_;
        while (c > 1) { c >>= 1; ++n; }
        return n;
    }

private:
    uint8_t count_ = S1;
};

} // namespace bighero
