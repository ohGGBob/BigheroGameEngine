#pragma once
#include <cstdint>
#include <vector>
#include "ShaderModule_v2.h"
#include "PipelineLayout_v2.h"
#include "VertexInputAttribute_v2.h"
#include "VertexInputBinding_v2.h"
#include "RasterState_v2.h"
#include "AttachmentDescription_v2.h"

namespace bighero {

// GraphicsPipelineDescriptor: a complete description of a graphics pipeline
// (shader stages, vertex input, raster state, color attachments).
// Self-contained, std-lib only.
class GraphicsPipelineDescriptor {
public:
    GraphicsPipelineDescriptor() = default;

    void AddShaderStage(ShaderModule shader) { stages_.push_back(shader); }
    size_t ShaderStageCount() const { return stages_.size(); }
    const ShaderModule& StageAt(size_t i) const { return stages_[i]; }

    void AddVertexAttribute(VertexInputAttribute attr) { attributes_.push_back(attr); }
    size_t VertexAttributeCount() const { return attributes_.size(); }
    void AddVertexBinding(VertexInputBinding binding) { bindings_.push_back(binding); }
    size_t VertexBindingCount() const { return bindings_.size(); }

    void SetLayout(PipelineLayout layout) { layout_ = layout; }
    const PipelineLayout& Layout() const { return layout_; }
    void SetRasterState(RasterState raster) { raster_ = raster; }
    const RasterState& Raster() const { return raster_; }

    void AddColorAttachment(const AttachmentDescription& a) { colorAttachments_.push_back(a); }
    size_t ColorAttachmentCount() const { return colorAttachments_.size(); }
    const AttachmentDescription& ColorAttachmentAt(size_t i) const { return colorAttachments_[i]; }

    void SetRenderPass(uint64_t renderPass) { renderPass_ = renderPass; }
    uint64_t RenderPass() const { return renderPass_; }
    void SetSubpass(uint32_t subpass) { subpass_ = subpass; }
    uint32_t Subpass() const { return subpass_; }

    bool IsValid() const {
        return !stages_.empty() && renderPass_ != 0;
    }

private:
    std::vector<ShaderModule> stages_;
    std::vector<VertexInputAttribute> attributes_;
    std::vector<VertexInputBinding> bindings_;
    PipelineLayout layout_;
    RasterState raster_;
    std::vector<AttachmentDescription> colorAttachments_;
    uint64_t renderPass_ = 0;
    uint32_t subpass_ = 0;
};

} // namespace bighero
