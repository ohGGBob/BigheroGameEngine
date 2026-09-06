#pragma once
// 二进制序列化读写（BinaryReader / BinaryWriter）：纯标准库、仅头文件。
// 用于存档、网络包、资源缓存、编辑器序列化的底层字节流。
//
// 商业化价值：商业引擎的存档/快照/网络协议都依赖高效、可跨平台端序的二进制读写。
//
// 设计：
//   - BinaryWriter：向后端 vector<unsigned char> 追加写入，小端字节序，可嵌套。
//   - BinaryReader：从内存字节流读取，小端解码，带边界校验防越界读。
//   - 支持基础类型 + string + vector<T> 便捷写入。
//   - 端序可用 SetBigEndian()/SetLittleEndian() 控制（默认小端）。

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace BigHero::Core
{
namespace bin
{
// ---- 小端/大端转换 ----
inline uint16_t ByteSwap16(uint16_t v) noexcept { return static_cast<uint16_t>((v << 8) | (v >> 8)); }
inline uint32_t ByteSwap32(uint32_t v) noexcept
{
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x000000FFu) << 24);
}
inline uint64_t ByteSwap64(uint64_t v) noexcept
{
    return (static_cast<uint64_t>(ByteSwap32(static_cast<uint32_t>(v))) << 32) | ByteSwap32(static_cast<uint32_t>(v >> 32));
}

// 检测主机是否小端。
inline bool IsLittleEndian() noexcept
{
    const uint16_t x = 0x0102;
    return *reinterpret_cast<const uint8_t*>(&x) == 0x02;
}
} // namespace bin

class BinaryWriter
{
  public:
    BinaryWriter(bool bigEndian = false) : bigEndian_(bigEndian) {}

    void SetBigEndian(bool on) noexcept { bigEndian_ = on; }
    [[nodiscard]] bool IsBigEndian() const noexcept { return bigEndian_; }

    [[nodiscard]] size_t Size() const noexcept { return data_.size(); }
    [[nodiscard]] const std::vector<unsigned char>& Data() const noexcept { return data_; }
    void Clear() noexcept { data_.clear(); }

    // ---- 基础类型 ----
    void WriteU8(uint8_t v) { data_.push_back(v); }
    void WriteI8(int8_t v) { WriteU8(static_cast<uint8_t>(v)); }
    void WriteU16(uint16_t v) { WriteRaw(v, sizeof(v)); }
    void WriteI16(int16_t v) { WriteU16(static_cast<uint16_t>(v)); }
    void WriteU32(uint32_t v) { WriteRaw(v, sizeof(v)); }
    void WriteI32(int32_t v) { WriteU32(static_cast<uint32_t>(v)); }
    void WriteU64(uint64_t v) { WriteRaw(v, sizeof(v)); }
    void WriteI64(int64_t v) { WriteU64(static_cast<uint64_t>(v)); }
    void WriteFloat(float v) { WriteU32(std::bit_cast<uint32_t>(v)); }
    void WriteDouble(double v) { WriteU64(std::bit_cast<uint64_t>(v)); }
    void WriteBool(bool v) { WriteU8(v ? 1 : 0); }

    // ---- string / byte buffer ----
    void WriteString(const std::string& s)
    {
        WriteU32(static_cast<uint32_t>(s.size()));
        data_.insert(data_.end(), s.begin(), s.end());
    }

    // ---- vector<T>（T 为可写基础类型） ----
    template<typename T> void WriteVector(const std::vector<T>& vec)
    {
        WriteU32(static_cast<uint32_t>(vec.size()));
        for (const T& v : vec)
        {
            using U = std::make_unsigned_t<T>;
            const U uv = static_cast<U>(v);
            WriteRaw(uv, sizeof(T));
        }
    }

  private:
    template<typename T> void WriteRaw(const T& v, size_t n)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
        if ((!bigEndian_ && bin::IsLittleEndian()) || (bigEndian_ && !bin::IsLittleEndian()))
        {
            // 主机端序与目标一致：直接追加
            data_.insert(data_.end(), p, p + n);
        }
        else
        {
            // 需要翻转
            for (size_t i = 0; i < n; ++i)
                data_.push_back(p[n - 1 - i]);
        }
    }

    std::vector<unsigned char> data_;
    bool bigEndian_ = false;
};

class BinaryReader
{
  public:
    explicit BinaryReader(const std::vector<unsigned char>& data, bool bigEndian = false)
        : data_(data), bigEndian_(bigEndian) {}

    void SetBigEndian(bool on) noexcept { bigEndian_ = on; }
    // 已读取到的位置。
    [[nodiscard]] size_t Position() const noexcept { return pos_; }
    [[nodiscard]] size_t Remaining() const noexcept { return data_.size() - pos_; }
    [[nodiscard]] bool Good() const noexcept { return pos_ <= data_.size(); }
    void Seek(size_t pos) noexcept { pos_ = pos < data_.size() ? pos : data_.size(); }

    // ---- 基础类型 ----
    bool ReadU8(uint8_t& out) { return ReadRaw(out, 1); }
    bool ReadI8(int8_t& out) { return ReadRaw(out, 1); }
    bool ReadU16(uint16_t& out) { return ReadRaw(out, 2); }
    bool ReadI16(int16_t& out) { uint16_t v; if (!ReadRaw(v, 2)) return false; out = static_cast<int16_t>(v); return true; }
    bool ReadU32(uint32_t& out) { return ReadRaw(out, 4); }
    bool ReadI32(int32_t& out) { uint32_t v; if (!ReadRaw(v, 4)) return false; out = static_cast<int32_t>(v); return true; }
    bool ReadU64(uint64_t& out) { return ReadRaw(out, 8); }
    bool ReadI64(int64_t& out) { uint64_t v; if (!ReadRaw(v, 8)) return false; out = static_cast<int64_t>(v); return true; }
    bool ReadFloat(float& out) { uint32_t v; if (!ReadRaw(v, 4)) return false; out = std::bit_cast<float>(v); return true; }
    bool ReadDouble(double& out) { uint64_t v; if (!ReadRaw(v, 8)) return false; out = std::bit_cast<double>(v); return true; }
    bool ReadBool(bool& out) { uint8_t v; if (!ReadU8(v)) return false; out = v != 0; return true; }

    bool ReadString(std::string& out)
    {
        uint32_t len = 0;
        if (!ReadU32(len))
            return false;
        if (Remaining() < len)
            return false;
        out.assign(data_.begin() + static_cast<ptrdiff_t>(pos_), data_.begin() + static_cast<ptrdiff_t>(pos_ + len));
        pos_ += len;
        return true;
    }

    template<typename T> bool ReadVector(std::vector<T>& out)
    {
        uint32_t n = 0;
        if (!ReadU32(n))
            return false;
        if (Remaining() < static_cast<size_t>(n) * sizeof(T))
            return false;
        out.resize(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            T v;
            if (!ReadRaw(v, sizeof(T)))
                return false;
            out[i] = v;
        }
        return true;
    }

  private:
    template<typename T> bool ReadRaw(T& out, size_t n)
    {
        if (Remaining() < n)
            return false;
        unsigned char* p = reinterpret_cast<unsigned char*>(&out);
        if (!bigEndian_ && bin::IsLittleEndian() || bigEndian_ && !bin::IsLittleEndian())
        {
            std::memcpy(p, data_.data() + pos_, n);
        }
        else
        {
            for (size_t i = 0; i < n; ++i)
                p[i] = data_[pos_ + n - 1 - i];
        }
        pos_ += n;
        return true;
    }

    const std::vector<unsigned char>& data_;
    bool bigEndian_ = false;
    size_t pos_ = 0;
};
} // namespace BigHero::Core
