#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// GPU texture handle descriptor for 2D/cube/3D and array textures.
// CPU-side bookkeeping; the backing image lives in the backend.
class GpuTexture {
public:
    enum class Kind { Tex2D, Tex3D, Cube, Tex2DArray };
    enum class Format { R8, RG8, RGB8, RGBA8, RGBA16F, RGBA32F, Depth24, Depth32F, DepthStencil };
    enum class MipMode { None, Generate, Full };

    GpuTexture() {}
    GpuTexture(int w, int h, Format format = Format::RGBA8,
               Kind kind = Kind::Tex2D)
        : w_(w), h_(h), format_(format), kind_(kind) {}

    void SetSize(int w, int h) { w_ = w; h_ = h; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    void SetFormat(Format f) { format_ = f; }
    Format FormatMode() const { return format_; }
    void SetKind(Kind k) { kind_ = k; }
    Kind KindMode() const { return kind_; }
    void SetHandle(std::uint64_t h) { handle_ = h; }
    std::uint64_t Handle() const { return handle_; }

    void SetMips(MipMode m) { mips_ = m; }
    MipMode Mips() const { return mips_; }
    void SetLayers(int l) { layers_ = l; }
    int Layers() const { return layers_; }

    bool IsValid() const { return handle_ != 0 && w_ > 0 && h_ > 0; }

private:
    int w_ = 0, h_ = 0;
    Format format_ = Format::RGBA8;
    Kind kind_ = Kind::Tex2D;
    std::uint64_t handle_ = 0;
    MipMode mips_ = MipMode::Generate;
    int layers_ = 1;
};

} // namespace bighero
