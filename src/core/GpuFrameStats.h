#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// GpuFrameStats: per-frame aggregate GPU statistics — draw calls, triangles,
// vertices, state changes, and timing. The backend fills these counters each
// frame and the profiler/ui layer reads them. Pure aggregate data container.
class GpuFrameStats {
public:
    GpuFrameStats() {}

    void Reset() {
        drawCalls_ = 0; triangles_ = 0; vertices_ = 0; indices_ = 0;
        stateChanges_ = 0; shaderBinds_ = 0; textureBinds_ = 0; bufferBinds_ = 0;
        frames_ = 0; cpuMs_ = 0.0; gpuMs_ = 0.0;
    }

    void BeginFrame() { ++frames_; }
    std::size_t Frames() const { return frames_; }

    void AddDrawCall(std::size_t tri, std::size_t vert, std::size_t idx) {
        ++drawCalls_; triangles_ += tri; vertices_ += vert; indices_ += idx;
    }
    void AddStateChange() { ++stateChanges_; }
    void AddShaderBind() { ++shaderBinds_; }
    void AddTextureBind() { ++textureBinds_; }
    void AddBufferBind() { ++bufferBinds_; }

    void SetCpuMs(double ms) { cpuMs_ = ms; }
    double CpuMs() const { return cpuMs_; }
    void SetGpuMs(double ms) { gpuMs_ = ms; }
    double GpuMs() const { return gpuMs_; }

    std::size_t DrawCalls() const { return drawCalls_; }
    std::size_t Triangles() const { return triangles_; }
    std::size_t Vertices() const { return vertices_; }
    std::size_t Indices() const { return indices_; }
    std::size_t StateChanges() const { return stateChanges_; }
    std::size_t ShaderBinds() const { return shaderBinds_; }
    std::size_t TextureBinds() const { return textureBinds_; }
    std::size_t BufferBinds() const { return bufferBinds_; }

    double AverageCpuMs() const { return frames_ ? cpuMs_ / (double)frames_ : 0.0; }
    double AverageGpuMs() const { return frames_ ? gpuMs_ / (double)frames_ : 0.0; }

private:
    std::size_t drawCalls_ = 0, triangles_ = 0, vertices_ = 0, indices_ = 0;
    std::size_t stateChanges_ = 0, shaderBinds_ = 0, textureBinds_ = 0, bufferBinds_ = 0;
    std::size_t frames_ = 0;
    double cpuMs_ = 0.0, gpuMs_ = 0.0;
};

} // namespace bighero
