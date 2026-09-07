#pragma once
// 通用唯一标识（Uuid）：128 位 UUID 生成与解析。
// 纯标准库、仅头文件。
//
// 商业化价值：运行时对象句柄、资源 ID、网络实体同步 ID、调试追踪的标准标识；
// 比裸 uint64 更大的命名空间，避免 ID 碰撞，适合跨系统/跨资产分发。
//
// 提供：Generate（v4 随机，从 random_device + mt19937_64 取 122 个随机位）、
//   FromString/ToString（8-4-4-4-12 标准格式）、IsNil、相等比较、哈希。

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <random>
#include <string>

namespace BigHero::Core
{
class Uuid
{
  public:
    // 标准的 16 字节表示。
    std::array<uint8_t, 16> bytes{};

    Uuid() = default; // nil
    static Uuid Nil() { return Uuid{}; }
    [[nodiscard]] bool IsNil() const
    {
        for (auto b : bytes)
            if (b != 0)
                return false;
        return true;
    }

    static Uuid Generate()
    {
        // v4：用 122 个随机位（剩余 6 位固定版本/变体位）。
        static thread_local std::mt19937_64 rng{ std::random_device{}() };
        std::uniform_int_distribution<uint64_t> dist;
        Uuid u;
        uint64_t a = dist(rng), b = dist(rng);
        for (int i = 0; i < 8; ++i) u.bytes[i] = (uint8_t)(a >> (i * 8));
        for (int i = 0; i < 8; ++i) u.bytes[8 + i] = (uint8_t)(b >> (i * 8));
        u.bytes[6] = (uint8_t)((u.bytes[6] & 0x0F) | 0x40); // version 4
        u.bytes[8] = (uint8_t)((u.bytes[8] & 0x3F) | 0x80); // variant 10xx
        return u;
    }

    [[nodiscard]] std::string ToString() const
    {
        std::ostringstream os;
        os << std::hex << std::setfill('0');
        for (int i = 0; i < 16; ++i)
        {
            os << std::setw(2) << (int)bytes[i];
            if (i == 3 || i == 5 || i == 7 || i == 9)
                os << '-';
        }
        return os.str();
    }

    // 解析 8-4-4-4-12 格式，忽略连字符；非法返回 nil。
    static Uuid FromString(const std::string& s)
    {
        Uuid u;
        std::string hex;
        for (char c : s)
            if (c != '-')
                hex += c;
        if (hex.size() != 32)
            return Uuid{};
        for (int i = 0; i < 16; ++i)
        {
            int hi = HexVal(hex[i * 2]), lo = HexVal(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return Uuid{};
            u.bytes[i] = (uint8_t)((hi << 4) | lo);
        }
        return u;
    }

    bool operator==(const Uuid& o) const { return bytes == o.bytes; }
    bool operator!=(const Uuid& o) const { return !(*this == o); }
    bool operator<(const Uuid& o) const { return bytes < o.bytes; }

  private:
    static int HexVal(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
};
} // namespace BigHero::Core
