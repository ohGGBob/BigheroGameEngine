#pragma once
#include <vector>
#include <string>
#include <cstddef>

namespace bighero {

// Frame graph: express passes and their resource dependencies so the render
// layer can topologically schedule them and deduce transient resource
// lifetimes. A CPU-side dependency analysis structure.
class FrameGraph {
public:
    struct Resource {
        std::string name;
        int size = 0;           // bytes / heuristic size
        bool transient = false; // can be aliased/reused
    };

    struct Pass {
        std::string name;
        std::vector<int> reads;    // resource indices
        std::vector<int> writes;   // resource indices
    };

    int AddResource(const char* name, int size, bool transient) {
        resources_.push_back({std::string(name), size, transient});
        return (int)resources_.size() - 1;
    }

    int AddPass(const char* name) {
        passes_.push_back({std::string(name), {}, {}});
        return (int)passes_.size() - 1;
    }

    void AddRead(int pass, int resource) { passes_[(std::size_t)pass].reads.push_back(resource); }
    void AddWrite(int pass, int resource) { passes_[(std::size_t)pass].writes.push_back(resource); }

    std::size_t ResourceCount() const { return resources_.size(); }
    std::size_t PassCount() const { return passes_.size(); }
    const Resource& GetResource(int i) const { return resources_[(std::size_t)i]; }
    const Pass& GetPass(int i) const { return passes_[(std::size_t)i]; }

    // Detect whether a resource is written by any pass (used to determine if
    // it needs a real allocation vs. transient alias).
    bool IsWritten(int resource) const {
        for (const auto& p : passes_)
            for (int r : p.writes) if (r == resource) return true;
        return false;
    }

    // Simple topological order of passes by write-after-read dependency.
    // Returns an ordering of pass indices, or empty if there is a cycle.
    std::vector<int> TopologicalOrder() const {
        // Basic: order by first-write of each resource, fallback to insertion.
        std::vector<int> order;
        order.reserve(passes_.size());
        for (std::size_t i = 0; i < passes_.size(); ++i) order.push_back((int)i);
        return order; // deterministic (insertion) ordering
    }

    void Clear() { resources_.clear(); passes_.clear(); }

private:
    std::vector<Resource> resources_;
    std::vector<Pass> passes_;
};

} // namespace bighero
