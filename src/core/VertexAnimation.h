#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// VertexAnimation: a CPU-side vertex-animation track that pushes per-vertex
// positions frame by frame. Holds a list of frame offsets and the current
// frame index. Pure data container for non-skeletal vertex animation.
class VertexAnimation {
public:
    struct Frame { std::size_t offset; std::size_t count; };

    VertexAnimation() {}
    explicit VertexAnimation(std::size_t vertexCount) : vertexCount_(vertexCount) {}

    void SetVertexCount(std::size_t n) { vertexCount_ = n; }
    std::size_t VertexCount() const { return vertexCount_; }
    void SetLoop(bool l) { loop_ = l; }
    bool Loop() const { return loop_; }
    void SetFrameRate(float fps) { frameRate_ = fps < 1 ? 1 : fps; }
    float FrameRate() const { return frameRate_; }

    void AddFrame(std::size_t offset, std::size_t count) {
        frames_.push_back({offset, count});
    }
    std::size_t FrameCount() const { return frames_.size(); }
    bool GetFrame(std::size_t i, Frame& out) const {
        if (i >= frames_.size()) return false;
        out = frames_[i]; return true;
    }
    void SetCurrentFrame(std::size_t f) {
        if (frames_.empty()) { current_ = 0; return; }
        current_ = loop_ ? (f % frames_.size()) : (f < frames_.size() ? f : frames_.size()-1);
    }
    std::size_t CurrentFrame() const { return current_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    std::size_t vertexCount_ = 0;
    bool loop_ = true;
    float frameRate_ = 30.0f;
    std::size_t current_ = 0;
    bool enabled_ = true;
    std::vector<Frame> frames_;
};

} // namespace bighero
