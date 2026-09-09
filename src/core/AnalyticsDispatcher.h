#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// AnalyticsDispatcher: collects lightweight analytics events (name + value +
// count) into an in-memory buffer for periodic flush to a backend. Pure
// stdlib container with basic aggregation.
class AnalyticsDispatcher {
public:
    struct Event {
        std::string name;
        float value;
        int count;
    };

    AnalyticsDispatcher() {}

    void Track(const std::string& name, float value = 1.0f) {
        total_ += value;
        ++totalCount_;
        for (auto& e : events_) {
            if (e.name == name) { e.value += value; ++e.count; return; }
        }
        events_.push_back({name, value, 1});
    }

    std::size_t UniqueEvents() const { return events_.size(); }
    float TotalValue() const { return total_; }
    int TotalCount() const { return totalCount_; }

    bool Get(const std::string& name, float& value, int& count) const {
        for (auto& e : events_) {
            if (e.name == name) { value = e.value; count = e.count; return true; }
        }
        value = 0; count = 0; return false;
    }

    // Clear the aggregated buffer (simulating a flush).
    void Flush() {
        events_.clear();
        total_ = 0; totalCount_ = 0; flushed_++;
    }
    int FlushCount() const { return flushed_; }

    void SetSession(const std::string& s) { session_ = s; }
    const std::string& Session() const { return session_; }

private:
    std::vector<Event> events_;
    float total_ = 0;
    int totalCount_ = 0;
    int flushed_ = 0;
    std::string session_;
};

} // namespace bighero
