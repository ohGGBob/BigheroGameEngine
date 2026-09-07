#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// A single render pass: binds a framebuffer + pipeline and issues draw
// batches. The atomic unit the backend submits to a command list.
class RenderPass {
public:
    enum class LoadOp { Load, Clear, DontCare };
    enum class StoreOp { Store, DontCare };

    struct Draw {
        uint64_t geometry;
        int count;
        int vertexOffset;
        int instanceCount;
    };

    RenderPass() {}
    RenderPass(const char* name, int framebuffer, int pipeline)
        : name_(name ? name : ""), framebuffer_(framebuffer), pipeline_(pipeline) {}

    void SetName(const char* n) { name_ = n ? n : ""; }
    const std::string& Name() const { return name_; }
    void SetFramebuffer(int fb) { framebuffer_ = fb; }
    int Framebuffer() const { return framebuffer_; }
    void SetPipeline(int p) { pipeline_ = p; }
    int Pipeline() const { return pipeline_; }

    void SetLoadOp(LoadOp op) { loadOp_ = op; }
    LoadOp Load() const { return loadOp_; }
    void SetStoreOp(StoreOp op) { storeOp_ = op; }
    StoreOp Store() const { return storeOp_; }

    void AddDraw(uint64_t geometry, int count, int vertexOffset = 0, int instanceCount = 1) {
        draws_.push_back({geometry, count, vertexOffset, instanceCount});
    }
    std::size_t DrawCount() const { return draws_.size(); }
    bool Empty() const { return draws_.empty(); }
    const Draw& At(std::size_t i) const { return draws_[i]; }
    void ClearDraws() { draws_.clear(); }

private:
    std::string name_;
    int framebuffer_ = 0;
    int pipeline_ = 0;
    LoadOp loadOp_ = LoadOp::Clear;
    StoreOp storeOp_ = StoreOp::Store;
    std::vector<Draw> draws_;
};

} // namespace bighero
