#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// LightProbeGroup: a cluster of light probes occupying a region. Holds probe
// slots (each with a LightProbe id and position) and provides a simple
// nearest-probe lookup. Pure CPU-side aggregation container.
class LightProbeGroup {
public:
    struct Probe { std::uint64_t probeId; float x, y, z; };

    LightProbeGroup() {}
    explicit LightProbeGroup(std::uint64_t id) : id_(id) {}

    void SetId(std::uint64_t id) { id_ = id; }
    std::uint64_t Id() const { return id_; }
    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_.c_str(); }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void AddProbe(std::uint64_t probeId, float x, float y, float z) {
        probes_.push_back({probeId, x, y, z});
    }
    std::size_t ProbeCount() const { return probes_.size(); }
    bool GetProbe(std::size_t i, Probe& out) const {
        if (i >= probes_.size()) return false;
        out = probes_[i]; return true;
    }

    // Nearest probe by squared distance (returns false if group empty).
    bool FindNearest(float x, float y, float z, Probe& out) const {
        if (probes_.empty()) return false;
        std::size_t best = 0; float bestD = DistSq(probes_[0], x, y, z);
        for (std::size_t i = 1; i < probes_.size(); ++i) {
            float d = DistSq(probes_[i], x, y, z);
            if (d < bestD) { bestD = d; best = i; }
        }
        out = probes_[best]; return true;
    }

    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void Clear() { probes_.clear(); }

private:
    static float DistSq(const Probe& p, float x, float y, float z) {
        float dx = p.x-x, dy = p.y-y, dz = p.z-z;
        return dx*dx + dy*dy + dz*dz;
    }
    std::uint64_t id_ = 0;
    std::string name_;
    bool enabled_ = true;
    float intensity_ = 1.0f;
    std::vector<Probe> probes_;
};

} // namespace bighero
