#include "render/HdrImage.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace BigHero
{
namespace
{
constexpr size_t kMaxPixels = 1024ull * 1024ull * 512ull; // 内存/栈保护上限（~20亿像素）

// 扫描线 RLE 解码：最多读入 width 像素的 RGBE。
// cur/end 为数据游标；out 须有 width*4 字节。结束后 cur 前进到下一扫描线起点。
void DecodeRleScanline(const uint8_t*& cur, const uint8_t* end, uint8_t* out, uint32_t width)
{
    if (cur + 4 > end)
        throw std::runtime_error("HdrImage: 扫描线头截断");
    const uint8_t c0 = cur[0], c1 = cur[1], c2 = cur[2], c3 = cur[3];
    // 标准 RLE 头：0x02 0x02 hi lo（且跨度==行宽，行宽在 [8,32767]）
    const bool isRle =
        (c0 == 0x02 && c1 == 0x02 && width >= 8 && width <= 0x7fff && ((static_cast<size_t>(c2) << 8) | c3) == width);
    if (!isRle)
    {
        // 旧式未压缩格式：前 4 字节为第 0 个像素，其后 (width-1)*4 字节为其余像素
        const uint8_t pixel[4] = {c0, c1, c2, c3};
        std::memcpy(out, pixel, 4);
        cur += 4;
        std::memcpy(out + 4, cur, static_cast<size_t>(width - 1) * 4);
        cur += static_cast<size_t>(width - 1) * 4;
        if (cur > end)
            throw std::runtime_error("HdrImage: 扫描线原始数据越界");
        return;
    }
    const size_t span = width;
    cur += 4;

    // 逐通道（R,G,B,E）独立 run-length 解码，各通道写回 out 的第 ch 个分量
    // （每像素4字节，故通道内相邻像素间隔4字节）。
    for (int ch = 0; ch < 4; ++ch)
    {
        uint8_t* dst = out + ch;
        size_t remain = span;
        while (remain > 0)
        {
            if (cur >= end)
                throw std::runtime_error("HdrImage: RLE通道数据截断");
            uint8_t code = *cur++;
            if (code > 128)
            {
                // 重复段：下一个字节重复 (code-128) 次
                const size_t rep = code - 128;
                if (cur >= end)
                    throw std::runtime_error("HdrImage: RLE重复段数据缺失");
                const uint8_t v = *cur++;
                if (rep > remain)
                    throw std::runtime_error("HdrImage: RLE重复段越过行尾");
                for (size_t k = 0; k < rep; ++k)
                {
                    *dst = v;
                    dst += 4; // 跨像素
                }
                remain -= rep;
            }
            else
            {
                // 字面段：后续 code 字节为原始值
                if (cur + code > end)
                    throw std::runtime_error("HdrImage: RLE字面段数据越界");
                if (code > remain)
                    throw std::runtime_error("HdrImage: RLE字面段越过行尾");
                for (size_t k = 0; k < code; ++k)
                {
                    *dst = *cur++;
                    dst += 4; // 跨像素
                }
                remain -= code;
            }
        }
    }
}
} // namespace

void HdrImage::LoadFromFile(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
        throw std::runtime_error("HdrImage: 无法打开文件 -> " + path);
    std::vector<uint8_t> data;
    try
    {
        std::fseek(f, 0, SEEK_END);
        const long len = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (len < 0)
            throw std::runtime_error("HdrImage: 无法获取文件大小 -> " + path);
        data.resize(static_cast<size_t>(len));
        if (len > 0 && std::fread(data.data(), 1, static_cast<size_t>(len), f) != static_cast<size_t>(len))
            throw std::runtime_error("HdrImage: 读取文件失败 -> " + path);
    }
    catch (...)
    {
        std::fclose(f);
        throw;
    }
    std::fclose(f);
    LoadFromMemory(std::move(data));
}

void HdrImage::Reset(uint32_t width, uint32_t height, std::vector<glm::vec4> rgba)
{
    if (width == 0 || height == 0)
        throw std::runtime_error("HdrImage::Reset: 宽高必须>0");
    const size_t expect = static_cast<size_t>(width) * height;
    if (rgba.size() != expect)
        throw std::runtime_error("HdrImage::Reset: 像素数量与宽高不符");
    width_ = width;
    height_ = height;
    exposure_ = 1.0f;
    pixels_ = std::move(rgba);
}

size_t HdrImage::ParseHeader(const uint8_t* data, size_t size)
{
    if (size < 16)
        throw std::runtime_error("HdrImage: 文件过短");
    // 魔数（忽略大小写 BOM）
    if (std::memcmp(data, "#?RADIANCE", 10) != 0)
        throw std::runtime_error("HdrImage: 缺少 #?RADIANCE 魔数（非Radiance HDR）");

    // 逐行读文本头：变量段（FORMAT/EXPOSURE/注释）以空行结束，
    // 空行后紧跟分辨率行 "-Y <height> +X <width>"。两者都读到后才算头结束。
    size_t pos = 10;
    uint32_t w = 0, h = 0;
    bool sawBlank = false; // 是否已跨过空行（变量段结束）
    while (true)
    {
        if (pos >= size)
            throw std::runtime_error("HdrImage: 头未以空行结束");
        // 找行尾
        size_t eol = pos;
        while (eol < size && data[eol] != '\n')
            ++eol;
        if (eol >= size)
            throw std::runtime_error("HdrImage: 头文本截断");

        std::string line(reinterpret_cast<const char*>(data + pos), eol - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        pos = eol + 1; // 跳过 '\n'

        if (line.empty())
        {
            sawBlank = true;
            continue; // 空行：变量段结束标志，继续找分辨率行
        }

        if (line.starts_with("FORMAT="))
        {
            if (line.find("32-bit_rle_rgbe") == std::string::npos)
                throw std::runtime_error("HdrImage: 不支持的 FORMAT（仅支持 32-bit_rle_rgbe）");
        }
        else if (line.starts_with("EXPOSURE="))
        {
            exposure_ = std::strtof(line.c_str() + 9, nullptr);
            if (!(exposure_ > 0.0f) || !std::isfinite(exposure_))
                exposure_ = 1.0f;
        }
        else if (line.starts_with("-Y ") && line.find(" +X ") != std::string::npos)
        {
            // 分辨率：-Y <height> +X <width>
            w = static_cast<uint32_t>(std::strtoul(line.c_str() + line.find(" +X ") + 4, nullptr, 10));
            h = static_cast<uint32_t>(std::strtoul(line.c_str() + 3, nullptr, 10));
            break; // 分辨率行结束，头解析完成
        }
        else if (sawBlank)
        {
            // 空行之后出现未知非注释行，说明缺少合法分辨率行
            throw std::runtime_error("HdrImage: 空行后缺少 -Y +X 分辨率行");
        }
    }

    if (w == 0 || h == 0)
        throw std::runtime_error("HdrImage: 分辨率声明缺少/非法（应为 -Y <h> +X <w>）");
    if (static_cast<uint64_t>(w) * h > kMaxPixels)
        throw std::runtime_error("HdrImage: 像素数超过安全上限");
    width_ = w;
    height_ = h;
    return pos;
}

void HdrImage::DecodeScanline(const uint8_t*& cur, const uint8_t* end, uint8_t* out, uint32_t width)
{
    DecodeRleScanline(cur, end, out, width);
}

void HdrImage::LoadFromMemory(std::vector<uint8_t> data)
{
    const size_t pos = ParseHeader(data.data(), data.size());
    const uint8_t* cur = data.data() + pos;
    const uint8_t* end = data.data() + data.size();
    const size_t pixelCount = static_cast<size_t>(width_) * height_;

    // 逐扫描线解码为临时 RGBE 缓冲，再转 float RGBA
    std::vector<uint8_t> rgbe(pixelCount * 4);
    for (uint32_t y = 0; y < height_; ++y)
    {
        uint8_t* row = rgbe.data() + static_cast<size_t>(y) * width_ * 4;
        DecodeScanline(cur, end, row, width_);
        if (cur > end)
            throw std::runtime_error("HdrImage: 扫描线数据越界");
    }

    pixels_.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const glm::vec3 rgb = RGBEToLinear(rgbe.data() + i * 4);
        pixels_[i] = glm::vec4(rgb * exposure_, 1.0f);
    }
}

glm::vec3 HdrImage::RGBEToLinear(const uint8_t rgbe[4])
{
    if (rgbe[3] == 0)
        return glm::vec3(0.0f);
    const float scale = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - 128 - 8);
    return glm::vec3(rgbe[0], rgbe[1], rgbe[2]) * scale;
}

glm::vec3 HdrImage::SampleEquirect(const glm::vec3& dir, const glm::vec3* rgba, uint32_t width, uint32_t height)
{
    // 等距柱状（标准约定）：u=经度[0,2π]沿+x->+z，v=纬度，v=0 对应 +Y(天顶)、v=1 对应 -Y(天底)。
    // 图像首行(top)=天顶，末行=天底。与 LearnOpenGL IBL 采样约定一致，保证与 GPU 卷积自洽。
    const glm::vec3 d = glm::normalize(dir);
    const float theta = std::atan2(d.z, d.x);                                   // [-π, π]
    const float v = std::acos(glm::clamp(d.y, -1.0f, 1.0f)) / glm::pi<float>(); // [0,1] 顶->底
    float u = (theta + glm::pi<float>()) / (2.0f * glm::pi<float>());
    u = glm::clamp(u, 0.0f, 1.0f);

    // 双线性采样（clamp 到边缘）
    const float fx = u * static_cast<float>(width) - 0.5f;
    const float fy = v * static_cast<float>(height) - 0.5f;
    const int x0 = glm::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(width) - 1);
    const int y0 = glm::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(height) - 1);
    const int x1 = glm::clamp(x0 + 1, 0, static_cast<int>(width) - 1);
    const int y1 = glm::clamp(y0 + 1, 0, static_cast<int>(height) - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    const glm::vec3& p00 = rgba[static_cast<size_t>(y0) * width + x0];
    const glm::vec3& p10 = rgba[static_cast<size_t>(y0) * width + x1];
    const glm::vec3& p01 = rgba[static_cast<size_t>(y1) * width + x0];
    const glm::vec3& p11 = rgba[static_cast<size_t>(y1) * width + x1];

    return glm::mix(glm::mix(p00, p10, tx), glm::mix(p01, p11, tx), ty);
}

std::array<std::vector<glm::vec3>, 6> HdrImage::EquirectToCube(const glm::vec3* rgba, uint32_t width, uint32_t height,
                                                               uint32_t size)
{
    if (rgba == nullptr || width == 0 || height == 0 || size == 0)
        throw std::runtime_error("HdrImage::EquirectToCube: 非法参数");

    // 面序与方向：+X -X +Y -Y +Z -Z（与 EnvironmentLighting::kCubeFaces 自洽）
    const std::array<glm::vec3, 6> majors = {glm::vec3(1, 0, 0),  glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                             glm::vec3(0, -1, 0), glm::vec3(0, 0, 1),  glm::vec3(0, 0, -1)};
    const std::array<glm::vec3, 6> sVecs = {glm::vec3(0, 0, -1), glm::vec3(0, 0, 1), glm::vec3(1, 0, 0),
                                            glm::vec3(1, 0, 0),  glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0)};
    const std::array<glm::vec3, 6> tVecs = {glm::vec3(0, -1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1),
                                            glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), glm::vec3(0, -1, 0)};

    std::array<std::vector<glm::vec3>, 6> faces;
    const size_t faceCount = static_cast<size_t>(size) * size;
    for (auto& f : faces)
        f.resize(faceCount);

    for (uint32_t face = 0; face < 6; ++face)
    {
        const glm::vec3& major = majors[face];
        const glm::vec3& sVec = sVecs[face];
        const glm::vec3& tVec = tVecs[face];
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const float s = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                const glm::vec3 dir = glm::normalize(major + sVec * (2.0f * s - 1.0f) + tVec * (2.0f * t - 1.0f));
                faces[face][static_cast<size_t>(y) * size + x] = SampleEquirect(dir, rgba, width, height);
            }
        }
    }
    return faces;
}
} // namespace BigHero
