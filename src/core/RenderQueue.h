#pragma once
#include <vector>
#include <algorithm>
#include <cstddef>

namespace bighero {

// Render queue: holds draw entries with a sort key and groups them for later
// submission. Entries are sorted front-to-back or back-to-front by depth.
class RenderQueue {
public:
    enum class Order { FrontToBack, BackToFront, Naive };

    struct Entry {
        int sortKey = 0;    // opaque sort key (e.g. depth or material id)
        int material = 0;
        uint64_t geometry = 0;
        int count = 0;
    };

    void Add(const Entry& e) { entries_.push_back(e); }
    void Clear() { entries_.clear(); }
    bool Empty() const { return entries_.empty(); }
    std::size_t Size() const { return entries_.size(); }

    // Sort by sortKey (ascending). FrontToBack typically uses ascending depth
    // for early-z; BackToFront (transparent) uses descending.
    void Sort(Order order = Order::FrontToBack) {
        if (order == Order::Naive) return;
        if (order == Order::FrontToBack) {
            std::sort(entries_.begin(), entries_.end(),
                      [](const Entry& a, const Entry& b){ return a.sortKey < b.sortKey; });
        } else {
            std::sort(entries_.begin(), entries_.end(),
                      [](const Entry& a, const Entry& b){ return a.sortKey > b.sortKey; });
        }
    }

    const Entry& At(std::size_t i) const { return entries_[i]; }

    template <class Fn>
    void ForEach(Fn&& fn) const { for (const auto& e : entries_) fn(e); }

private:
    std::vector<Entry> entries_;
};

} // namespace bighero
