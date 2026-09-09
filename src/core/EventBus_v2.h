#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>

namespace bighero {

// EventBus: a publish/subscribe event system. Handlers are registered by event
// type id and invoked synchronously on Publish. Self-contained, std-lib only.
class EventBus {
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Subscribe a handler to an event type. Returns a subscription id.
    int Subscribe(uint32_t eventType, std::function<void(const void*)> handler) {
        auto& subs = handlers_[eventType];
        int id = nextId_++;
        subs.push_back({id, std::move(handler)});
        return id;
    }

    // Unsubscribe by id. Returns true if removed.
    bool Unsubscribe(uint32_t eventType, int subId) {
        auto it = handlers_.find(eventType);
        if (it == handlers_.end()) return false;
        auto& subs = it->second;
        for (size_t i = 0; i < subs.size(); ++i) {
            if (subs[i].id == subId) { subs.erase(subs.begin()+i); return true; }
        }
        return false;
    }

    // Publish an event payload to all subscribers of eventType.
    void Publish(uint32_t eventType, const void* payload = nullptr) {
        auto it = handlers_.find(eventType);
        if (it == handlers_.end()) return;
        auto subs = it->second; // copy so handlers may unsubscribe safely
        for (auto& s : subs) if (s.handler) s.handler(payload);
    }

    int SubscriberCount(uint32_t eventType) const {
        auto it = handlers_.find(eventType);
        return it == handlers_.end() ? 0 : (int)it->second.size();
    }
    void Clear() { handlers_.clear(); nextId_ = 1; }

private:
    struct Sub { int id; std::function<void(const void*)> handler; };
    std::unordered_map<uint32_t, std::vector<Sub>> handlers_;
    int nextId_ = 1;
};

} // namespace bighero
