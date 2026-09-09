#pragma once
#include <cstdint>

namespace bighero {

// ImageLayout: describes the memory layout (access pattern) of an image at a
// given point in a pipeline. Self-contained, std-lib only.
class ImageLayout {
public:
    enum class Layout : uint8_t {
        Undefined = 0,
        General = 1,
        ColorAttachment = 2,
        DepthStencilAttachment = 3,
        DepthStencilReadOnly = 4,
        ShaderReadOnly = 5,
        TransferSrc = 6,
        TransferDst = 7,
        Present = 8
    };

    ImageLayout() = default;
    ImageLayout(Layout layout, uint32_t baseMip = 0, uint32_t mipCount = 1,
                uint32_t baseLayer = 0, uint32_t layerCount = 1)
        : layout_(layout), baseMip_(baseMip), mipCount_(mipCount),
          baseLayer_(baseLayer), layerCount_(layerCount) {}

    void SetLayout(Layout l) { layout_ = l; }
    Layout GetLayout() const { return layout_; }
    void SetBaseMip(uint32_t m) { baseMip_ = m; }
    uint32_t BaseMip() const { return baseMip_; }
    void SetMipCount(uint32_t c) { mipCount_ = c; }
    uint32_t MipCount() const { return mipCount_; }
    void SetBaseLayer(uint32_t l) { baseLayer_ = l; }
    uint32_t BaseLayer() const { return baseLayer_; }
    void SetLayerCount(uint32_t c) { layerCount_ = c; }
    uint32_t LayerCount() const { return layerCount_; }

    bool IsReadOnly() const {
        return layout_ == Layout::ShaderReadOnly || layout_ == Layout::DepthStencilReadOnly;
    }
    bool IsAttachment() const {
        return layout_ == Layout::ColorAttachment || layout_ == Layout::DepthStencilAttachment;
    }
    bool IsPresentable() const { return layout_ == Layout::Present; }

    static const char* Name(Layout l) {
        switch (l) {
            case Layout::Undefined: return "Undefined";
            case Layout::General: return "General";
            case Layout::ColorAttachment: return "ColorAttachment";
            case Layout::DepthStencilAttachment: return "DepthStencilAttachment";
            case Layout::DepthStencilReadOnly: return "DepthStencilReadOnly";
            case Layout::ShaderReadOnly: return "ShaderReadOnly";
            case Layout::TransferSrc: return "TransferSrc";
            case Layout::TransferDst: return "TransferDst";
            case Layout::Present: return "Present";
        }
        return "Unknown";
    }

private:
    Layout layout_ = Layout::Undefined;
    uint32_t baseMip_ = 0, mipCount_ = 1;
    uint32_t baseLayer_ = 0, layerCount_ = 1;
};

} // namespace bighero
