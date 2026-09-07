#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Lightweight event bus: register callbacks for an event id, dispatch to all.
class EventBus {
public:
    using Handler = std::function<void(int)>;

    // Subscribe a handler for an event id; returns an opaque subscription id.
    int Subscribe(int eventId, Handler h) {
        handlers_.push_back({eventId, std::move(h), ++nextId_});
        subscribed_.push_back((int)handlers_.size() - 1);
        return nextId_;
    }

    // Unsubscribe by subscription id. Returns false if not found or already dead.
    bool Unsubscribe(int subId) {
        for (std::size_t i = 0; i < handlers_.size(); ++i) {
            if (handlers_[i].subId == subId) {
                if (!handlers_[i].handler) return false; // already removed
                handlers_[i].handler = nullptr; // mark dead
                return true;
            }
        }
        return false;
    }

    // Dispatch an event to all matching handlers.
    void Dispatch(int eventId, int payload = 0) {
        for (auto& h : handlers_) {
            if (h.eventId == eventId && h.handler) h.handler(payload);
        }
    }

    // Remove all dead (unsubscribed) handlers.
    void Compact() {
        std::vector<Entry> live;
        for (auto& h : handlers_) if (h.handler) live.push_back(h);
        handlers_.swap(live);
    }

    void Clear() { handlers_.clear(); subscribed_.clear(); }
    std::size_t Count() const { return handlers_.size(); }

private:
    struct Entry { int eventId; Handler handler; int subId; };
    std::vector<Entry> handlers_;
    std::vector<int> subscribed_;
    int nextId_ = 0;
};

} // namespace bighero
