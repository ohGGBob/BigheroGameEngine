#pragma once
#include <vector>
#include <utility>
#include <cstddef>

namespace bighero {

// A timeline of events (time -> action index) plus a cursor; playback APIs
// let animations schedule callbacks at fixed times. Building block for
// sequence/animation timelines.
class FrameTimeline {
public:
    struct Event { float time; int action; };

    // Add an event at time t with an action id (not necessarily unique).
    void AddEvent(float t, int action) {
        events_.push_back({t, action});
        for (std::size_t i = events_.size() - 1; i > 0 && events_[i].time < events_[i-1].time; --i)
            std::swap(events_[i], events_[i-1]);
    }

    // Fire all events with time <= `t` and return the list of action ids.
    std::vector<int> Update(float t) {
        std::vector<int> fired;
        while (cursor_ < events_.size() && events_[cursor_].time <= t) {
            fired.push_back(events_[cursor_].action);
            ++cursor_;
        }
        return fired;
    }

    void Reset() { cursor_ = 0; }
    void Clear() { events_.clear(); cursor_ = 0; }
    std::size_t Size() const { return events_.size(); }
    bool HasFiredAll() const { return cursor_ >= events_.size(); }

private:
    std::vector<Event> events_;
    std::size_t cursor_ = 0;
};

} // namespace bighero
