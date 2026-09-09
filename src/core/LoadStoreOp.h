#pragma once
#include <cstdint>

namespace bighero {

// LoadStoreOp: describes load/store operations applied to a render-pass
// attachment at the beginning/end of a subpass. Self-contained.
class LoadStoreOp {
public:
    enum class Op : uint8_t { DontCare = 0, Load = 1, Clear = 2, Store = 3 };

    LoadStoreOp() = default;
    LoadStoreOp(Op load, Op store) : load_(load), store_(store) {}

    void SetLoad(Op o) { load_ = o; }
    Op GetLoad() const { return load_; }
    void SetStore(Op o) { store_ = o; }
    Op GetStore() const { return store_; }

    bool IsClear() const { return load_ == Op::Clear; }
    bool IsLoad() const { return load_ == Op::Load; }
    bool IsStore() const { return store_ == Op::Store; }
    bool IsDontCare() const { return load_ == Op::DontCare && store_ == Op::DontCare; }

    static const char* Name(Op o) {
        switch (o) {
            case Op::DontCare: return "DontCare";
            case Op::Load: return "Load";
            case Op::Clear: return "Clear";
            case Op::Store: return "Store";
        }
        return "Unknown";
    }

private:
    Op load_ = Op::DontCare;
    Op store_ = Op::DontCare;
};

} // namespace bighero
