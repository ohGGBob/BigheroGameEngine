#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// Frame render statistics: per-frame counters for draw calls, triangles,
// and memory usage. Building block for a debug/HUD overlay.
class RenderStats {
public:
    void Reset() { draws_=0; triangles_=0; vertices_=0; shaderBinds_=0; textureBinds_=0; }

    void DrawCall() { ++draws_; }
    void AddTriangles(std::size_t n) { triangles_ += n; }
    void AddVertices(std::size_t n) { vertices_ += n; }
    void ShaderBind() { ++shaderBinds_; }
    void TextureBind() { ++textureBinds_; }

    std::size_t DrawCalls() const { return draws_; }
    std::size_t Triangles() const { return triangles_; }
    std::size_t Vertices() const { return vertices_; }
    std::size_t ShaderBinds() const { return shaderBinds_; }
    std::size_t TextureBinds() const { return textureBinds_; }

    // Streaming (accumulating) counters for average fps / total.
    void TickFrame() { ++frames_; totalTris_ += triangles_; }
    std::size_t Frames() const { return frames_; }
    std::size_t TotalTriangles() const { return totalTris_; }
    double AvgTrianglesPerFrame() const {
        return frames_ ? (double)totalTris_ / frames_ : 0.0;
    }

    void SetFrameTimeMs(double ms) { frameTimeMs_ = ms; }
    double FrameTimeMs() const { return frameTimeMs_; }
    double Fps() const { return frameTimeMs_ > 0 ? 1000.0 / frameTimeMs_ : 0.0; }

private:
    std::size_t draws_ = 0, triangles_ = 0, vertices_ = 0;
    std::size_t shaderBinds_ = 0, textureBinds_ = 0;
    std::size_t frames_ = 0, totalTris_ = 0;
    double frameTimeMs_ = 0.0;
};

} // namespace bighero
