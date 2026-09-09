#pragma once
#include <cstdint>
#include "ShaderModule.h"
#include "PipelineLayout.h"
#include "ShaderStageFlag.h"

namespace bighero {

// ComputePipelineDescriptor: a description of a compute pipeline (shader,
// layout, base pipeline). Self-contained, std-lib only.
class ComputePipelineDescriptor {
public:
    ComputePipelineDescriptor() = default;

    void SetShader(ShaderModule shader) { shader_ = shader; }
    const ShaderModule& Shader() const { return shader_; }
    void SetLayout(PipelineLayout layout) { layout_ = layout; }
    const PipelineLayout& Layout() const { return layout_; }

    void SetStageFlag(uint16_t stage) { stage_ = stage; }
    uint16_t StageFlag() const { return stage_; }

    void SetBasePipeline(uint64_t base) { basePipeline_ = base; }
    uint64_t BasePipeline() const { return basePipeline_; }
    void SetBasePipelineIndex(int32_t idx) { basePipeIndex_ = idx; }
    int32_t BasePipelineIndex() const { return basePipeIndex_; }

    bool IsValid() const {
        return shader_.IsValid() && shader_.GetStage() == ShaderModule::Stage::Compute;
    }
    static const char* StageName(uint16_t s) {
        if (s == ShaderStageFlag::Compute) return "Compute";
        return "Other";
    }

private:
    ShaderModule shader_;
    PipelineLayout layout_;
    uint16_t stage_ = ShaderStageFlag::Compute;
    uint64_t basePipeline_ = 0;
    int32_t basePipeIndex_ = -1;
};

} // namespace bighero
