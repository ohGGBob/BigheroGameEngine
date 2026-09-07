#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace bighero {

// A single draw batch: the geometry handle plus material/program and the
// instance count, used to merge many draw commands into fewer submissions.
class DrawBatch {
public:
    DrawBatch() {}
    DrawBatch(uint64_t geometry, int material, int program, int count)
        : geometry_(geometry), material_(material), program_(program), count_(count) {}

    void SetGeometry(uint64_t g) { geometry_ = g; }
    uint64_t Geometry() const { return geometry_; }
    void SetMaterial(int m) { material_ = m; }
    int Material() const { return material_; }
    void SetProgram(int p) { program_ = p; }
    int Program() const { return program_; }
    void SetCount(int c) { count_ = c; }
    int Count() const { return count_; }
    bool IsEmpty() const { return count_ <= 0; }

    static DrawBatch Merge(const DrawBatch& a, const DrawBatch& b) {
        // Only merge if same geometry/material/program.
        if (a.geometry_ != b.geometry_ || a.material_ != b.material_ || a.program_ != b.program_)
            return a;
        return DrawBatch(a.geometry_, a.material_, a.program_, a.count_ + b.count_);
    }

private:
    uint64_t geometry_ = 0;
    int material_ = 0;
    int program_ = 0;
    int count_ = 0;
};

} // namespace bighero
