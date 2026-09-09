#pragma once
#include <cstdint>

namespace bighero {

// AttachmentDescription: describes a single render-pass attachment (format,
// load/store ops, initial/final layout). Self-contained, std-lib only.
class AttachmentDescription {
public:
    enum class LoadOp : uint8_t { DontCare = 0, Load = 1, Clear = 2 };
    enum class StoreOp : uint8_t { DontCare = 0, Store = 1 };
    enum class Layout : uint8_t {
        Undefined = 0, General = 1, ColorAttachment = 2,
        DepthStencilAttachment = 3, ShaderReadOnly = 4, Present = 5
    };

    AttachmentDescription() = default;
    AttachmentDescription(uint32_t index, Layout initial, Layout final,
                          LoadOp load, StoreOp store)
        : index_(index), initialLayout_(initial), finalLayout_(final),
          loadOp_(load), storeOp_(store) {}

    void SetIndex(uint32_t i) { index_ = i; }
    uint32_t Index() const { return index_; }
    void SetInitialLayout(Layout l) { initialLayout_ = l; }
    Layout InitialLayout() const { return initialLayout_; }
    void SetFinalLayout(Layout l) { finalLayout_ = l; }
    Layout FinalLayout() const { return finalLayout_; }
    void SetLoadOp(LoadOp o) { loadOp_ = o; }
    LoadOp GetLoadOp() const { return loadOp_; }
    void SetStoreOp(StoreOp o) { storeOp_ = o; }
    StoreOp GetStoreOp() const { return storeOp_; }
    void SetStencilLoadOp(LoadOp o) { stencilLoad_ = o; }
    LoadOp StencilLoadOp() const { return stencilLoad_; }
    void SetStencilStoreOp(StoreOp o) { stencilStore_ = o; }
    StoreOp StencilStoreOp() const { return stencilStore_; }

    bool IsCleared() const { return loadOp_ == LoadOp::Clear || stencilLoad_ == LoadOp::Clear; }
    bool IsStored() const { return storeOp_ == StoreOp::Store; }
    bool IsDepthAttachment() const {
        return initialLayout_ == Layout::DepthStencilAttachment || finalLayout_ == Layout::DepthStencilAttachment;
    }

    static const char* LoadOpName(LoadOp o) {
        switch (o) { case LoadOp::DontCare: return "DontCare"; case LoadOp::Load: return "Load"; case LoadOp::Clear: return "Clear"; }
        return "Unknown";
    }

private:
    uint32_t index_ = 0;
    Layout initialLayout_ = Layout::Undefined;
    Layout finalLayout_ = Layout::Undefined;
    LoadOp loadOp_ = LoadOp::DontCare;
    StoreOp storeOp_ = StoreOp::DontCare;
    LoadOp stencilLoad_ = LoadOp::DontCare;
    StoreOp stencilStore_ = StoreOp::DontCare;
};

} // namespace bighero
