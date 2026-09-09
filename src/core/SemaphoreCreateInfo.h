#pragma once
#include <cstdint>

namespace bighero {

// SemaphoreCreateInfo: describes how to create a binary/timeline semaphore.
// Self-contained, std-lib only.
class SemaphoreCreateInfo {
public:
    enum class Type : uint8_t { Binary = 0, Timeline = 1 };

    SemaphoreCreateInfo() = default;
    explicit SemaphoreCreateInfo(Type type) : type_(type) {}

    void SetType(Type t) { type_ = t; }
    Type GetType() const { return type_; }
    void SetInitialValue(uint64_t v) { initialValue_ = v; }
    uint64_t InitialValue() const { return initialValue_; }

    bool IsBinary() const { return type_ == Type::Binary; }
    bool IsTimeline() const { return type_ == Type::Timeline; }
    bool IsValid() const { return true; }

    static const char* TypeName(Type t) {
        switch (t) {
            case Type::Binary: return "Binary";
            case Type::Timeline: return "Timeline";
        }
        return "Unknown";
    }

private:
    Type type_ = Type::Binary;
    uint64_t initialValue_ = 0;
};

} // namespace bighero
