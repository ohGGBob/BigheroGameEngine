#pragma once
#include <vector>
#include <string>
#include <cstddef>

namespace bighero {

// Fully-described render pipeline: shader stages, vertex layout, and the
// fixed-function state (blend/rasterizer/depth-stencil) bound together.
// This is the CPU-side description passed to the backend to create a real
// graphics pipeline object.
class RenderPipeline {
public:
    struct StageEntry {
        int shader = 0;         // Shader program handle
        std::string name;       // e.g. "vertex"/"fragment"
    };

    void AddStage(int shader, const char* entryName = "") {
        stages_.push_back({shader, std::string(entryName)});
    }
    void SetVertexLayout(std::size_t layoutId) { layout_ = layoutId; }
    std::size_t VertexLayout() const { return layout_; }

    // Wire fixed-function state via opaque handles.
    void SetBlendState(std::size_t handle) { blendState_ = handle; }
    std::size_t BlendState() const { return blendState_; }
    void SetRasterizerState(std::size_t handle) { rasterizer_ = handle; }
    std::size_t RasterizerState() const { return rasterizer_; }
    void SetDepthStencilState(std::size_t handle) { depthStencil_ = handle; }
    std::size_t DepthStencilState() const { return depthStencil_; }

    void AddColorAttachmentFormat(int format) { colorFormats_.push_back(format); }
    void SetDepthFormat(int format) { depthFormat_ = format; }
    int DepthFormat() const { return depthFormat_; }

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }
    bool IsComplete() const { return !stages_.empty(); }
    std::size_t StageCount() const { return stages_.size(); }
    std::size_t ColorFormatCount() const { return colorFormats_.size(); }

private:
    std::vector<StageEntry> stages_;
    std::size_t layout_ = 0;
    std::size_t blendState_ = 0;
    std::size_t rasterizer_ = 0;
    std::size_t depthStencil_ = 0;
    std::vector<int> colorFormats_;
    int depthFormat_ = 0;
    std::string name_;
};

} // namespace bighero
