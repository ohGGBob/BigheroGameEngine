#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// PipelineCache: an opaque cache for compiled graphics/compute pipelines,
// allowing shader recompilation results to be reused. Self-contained.
class PipelineCache {
public:
    PipelineCache() = default;

    void SetInitialData(const void* data, size_t size) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        data_.assign(p, p + size);
    }
    size_t Size() const { return data_.size(); }
    const std::vector<uint8_t>& Data() const { return data_; }

    void AddEntry(uint64_t key, size_t size) {
        entries_.push_back({key, size});
        totalSize_ += size;
    }
    size_t EntryCount() const { return entries_.size(); }
    uint64_t TotalSize() const { return totalSize_; }

    void Clear() { entries_.clear(); data_.clear(); totalSize_ = 0; }
    bool IsEmpty() const { return entries_.empty() && data_.empty(); }

    struct Entry { uint64_t key; size_t size; };
    const Entry& EntryAt(size_t i) const { return entries_[i]; }

private:
    std::vector<Entry> entries_;
    std::vector<uint8_t> data_;
    uint64_t totalSize_ = 0;
};

} // namespace bighero
