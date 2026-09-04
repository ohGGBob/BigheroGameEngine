#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace BigHero
{
// 纯 CPU 的 Radiance .hdr (RGBE) 高动态范围图像加载器。
// 不依赖 stb_image / Vulkan，可离线运行与单元测试。
//
// .hdr 格式要点：
//   - 文本头以 "#?RADIANCE" 开头，含 FORMAT=32-bit_rle_rgbe 与可选 EXPOSURE=...
//   - 空行后接 4 字节：A=宽度编码高字节、B=高度编码高字节、C=宽低字节、D=高低字节
//     （当且仅当两行都以 " -Y H +X W" 形式给出时 A==2）；用 A-128 取宽的指数，
//     宽度 = ((C << 8) | D) << (A - 128)。高度同理。
//   - 之后是逐扫描线 RGBE 数据：每行先 4 字节 RLE 控制头 [2,2,hi,lo]，
//     随后每个通道（R,G,B,E）按 run-length 编码（高位字节&0x80 表示重复段）。
//   - 像素存储为 RGBE（每像素4字节：R,G,B,E），E 为共享指数。
//     转 float：f = ld_exp(通道字节, E - 128 - 8)，若 E==0 则为纯黑。
class HdrImage
{
  public:
    HdrImage() = default;

    // 从文件加载 .hdr。失败抛 std::runtime_error（路径不存在/头非法/数据损坏）。
    void LoadFromFile(const std::string& path);

    // 从内存字节解析 .hdr（供测试与嵌入式资源使用）。
    // 解析成功后占用 data（移入），失败抛 std::runtime_error。
    void LoadFromMemory(std::vector<uint8_t> data);

    // 显式从已解码像素构造（宽高校验一致），供程序化生成/测试直接使用。
    void Reset(uint32_t width, uint32_t height, std::vector<glm::vec4> rgba);

    [[nodiscard]] uint32_t Width() const noexcept { return width_; }
    [[nodiscard]] uint32_t Height() const noexcept { return height_; }
    [[nodiscard]] bool IsValid() const noexcept { return !pixels_.empty() && width_ > 0 && height_ > 0; }

    // 线性 float RGBA 像素（row-major，A=1）。RGBA16F/R32G32B32A32_SFLOAT 上传源。
    [[nodiscard]] const std::vector<glm::vec4>& Pixels() const noexcept { return pixels_; }

    // 图像曝光乘数（默认 1.0）。解析到 EXPOSURE 头时自动计入像素。
    [[nodiscard]] float Exposure() const noexcept { return exposure_; }

    // ---- 静态工具：供单元测试与外部独立使用的纯逻辑 ----
    // RGBE(4字节) -> 线性 float RGB（E==0 时为黑）。
    [[nodiscard]] static glm::vec3 RGBEToLinear(const uint8_t rgbe[4]);

    // 等距柱状投影(2:1) 采样函数：dir 为世界方向（已归一化，y 向上）。
    // 从 width x height 的 float RGB 数据中双线性采样（clamp 边缘）。
    [[nodiscard]] static glm::vec3 SampleEquirect(const glm::vec3& dir, const glm::vec3* rgba, uint32_t width,
                                                  uint32_t height);

    // 等距柱状投影 -> 立方图（6 面，每面 size x size float RGB）。
    // 面序与 Vulkan 立方图一致：+X -X +Y -Y +Z -Z。
    // 返回 6 个 size*size 的 RGB 三通道数组（每个 face 单独 vector），
    // 方向映射自洽（同 EnvironmentLighting 的 kCubeFaces 约定）。
    [[nodiscard]] static std::array<std::vector<glm::vec3>, 6> EquirectToCube(const glm::vec3* rgba, uint32_t width,
                                                                              uint32_t height, uint32_t size);

  private:
    // 解析文本头，返回扫描线起始偏移；失败抛异常。
    size_t ParseHeader(const uint8_t* data, size_t size);
    // 解码单条扫描线（width 像素）的 RGBE 数据，写入 out（宽度处停止）。
    void DecodeScanline(const uint8_t*& cur, const uint8_t* end, uint8_t* out, uint32_t width);

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    float exposure_ = 1.0f;
    std::vector<glm::vec4> pixels_; // row-major RGBA float
};
} // namespace BigHero

