#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace bighero {

// ComputeShader: descriptor for a compute shader program — path/hash, a set
// of buffer bindings, and the thread group size. Pure config container the
// backend uses to dispatch compute work.
class ComputeShader {
public:
    struct Binding {
        std::string name;
        std::uint64_t bufferId;
        int bindingSlot;
    };

    ComputeShader() {}
    explicit ComputeShader(const std::string& path) : path_(path) {}

    void SetPath(const std::string& p) { path_ = p; }
    const std::string& Path() const { return path_; }
    void SetEntry(const std::string& e) { entry_ = e; }
    const std::string& Entry() const { return entry_; }
    void SetGroupSize(int x, int y, int z) {
        gx_ = x < 1 ? 1 : x; gy_ = y < 1 ? 1 : y; gz_ = z < 1 ? 1 : z;
    }
    void GroupSize(int& x, int& y, int& z) const { x=gx_; y=gy_; z=gz_; }

    void Bind(const std::string& name, std::uint64_t bufferId, int slot) {
        bindings_.push_back({name, bufferId, slot});
    }
    std::size_t BindingCount() const { return bindings_.size(); }
    bool GetBinding(std::size_t i, Binding& out) const {
        if (i >= bindings_.size()) return false;
        out = bindings_[i]; return true;
    }

    void SetDispatchX(int n) { dx_ = n < 1 ? 1 : n; }
    int DispatchX() const { return dx_; }
    void SetDispatchY(int n) { dy_ = n < 1 ? 1 : n; }
    int DispatchY() const { return dy_; }
    void SetDispatchZ(int n) { dz_ = n < 1 ? 1 : n; }
    int DispatchZ() const { return dz_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    std::string path_;
    std::string entry_ = "main";
    int gx_ = 8, gy_ = 8, gz_ = 1;
    int dx_ = 1, dy_ = 1, dz_ = 1;
    bool enabled_ = true;
    std::vector<Binding> bindings_;
};

} // namespace bighero
