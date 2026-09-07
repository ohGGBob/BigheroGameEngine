#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// A chain of post-processing passes applied to the final render target.
// Each pass names a shader/technique plus input/output target and any params.
class PostProcessChain {
public:
    struct Pass {
        std::string name;
        std::string shader;      // shader/technique key
        int inputTarget = 0;     // 0 = previous output
        int outputTarget = 0;    // 0 = final swapchain
        int order = 0;
        float a = 0, b = 0, c = 0; // generic params (e.g. bloom threshold)
        bool enabled = true;
    };

    void AddPass(const char* name, const char* shader, int order = 0) {
        passes_.push_back({name ? name : "", shader ? shader : "", 0, 0, order, 0,0,0, true});
    }
    void SetInput(int index, int target) { passes_[(std::size_t)index].inputTarget = target; }
    void SetOutput(int index, int target) { passes_[(std::size_t)index].outputTarget = target; }
    void SetParam(int index, float a, float b, float c) {
        passes_[(std::size_t)index].a = a; passes_[(std::size_t)index].b = b; passes_[(std::size_t)index].c = c;
    }
    void SetEnabled(int index, bool enable) { passes_[(std::size_t)index].enabled = enable; }
    bool IsEnabled(int index) const { return passes_[(std::size_t)index].enabled; }

    std::size_t Size() const { return passes_.size(); }
    bool Empty() const { return passes_.empty(); }
    const Pass& At(std::size_t i) const { return passes_[i]; }
    void Clear() { passes_.clear(); }

    // Number of passes that are currently enabled.
    std::size_t EnabledCount() const {
        std::size_t n = 0;
        for (const auto& p : passes_) if (p.enabled) ++n;
        return n;
    }

private:
    std::vector<Pass> passes_;
};

} // namespace bighero
