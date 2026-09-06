#pragma once
// CRC32（IEEE 802.3 标准多项式 0xEDB88320）：纯标准库、仅头文件。
// 用于资源/存档完整性校验、缓存键、快速内容哈希。
//
// 提供逐字节增量更新（Append）与一次性 Compute；预先生成小端查找表（查表法提速）。
//
// 商业化价值：商业引擎下载/存档/资源校验、网络包校验、内容缓存键的稳定校验和。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace BigHero::Core
{
class Crc32
{
  public:
    using Value = uint32_t;

    // 计算整个缓冲区（从 seed 开始的 CRC）。
    static Value Compute(const void* data, size_t len, Value seed = 0) noexcept
    {
        Value crc = seed ^ 0xFFFFFFFFu;
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i)
            crc = Table()[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
    static Value Compute(std::string_view s, Value seed = 0) noexcept
    {
        return Compute(s.data(), s.size(), seed);
    }

    // 增量式：先 Begin，再多次 Append，最后 Get()。
    void Begin(Value seed = 0) noexcept { crc_ = seed ^ 0xFFFFFFFFu; }
    void Append(const void* data, size_t len) noexcept
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i)
            crc_ = Table()[(crc_ ^ p[i]) & 0xFFu] ^ (crc_ >> 8);
    }
    void Append(std::string_view s) noexcept { Append(s.data(), s.size()); }
    // 取值：返回从 Begin 到当前所有 Append 的 CRC。
    [[nodiscard]] Value Get() const noexcept { return crc_ ^ 0xFFFFFFFFu; }

  private:
    static const std::array<uint32_t, 256>& Table() noexcept
    {
        static const std::array<uint32_t, 256> table = [] {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        }();
        return table;
    }

    uint32_t crc_ = 0;
};
} // namespace BigHero::Core
