#pragma once
// 字节缓冲（Buffer）：可增长的字节容器，带读/写位置指针。
// 纯标准库、仅头文件。
//
// 商业化价值：网络包、序列化流、文件 I/O、解压输出的通用字节容器；
// 提供连续的写入与读取，便于做流式解析。
//
// 提供：Write(字节/字面量)/Read(读 offset 处)/Append/Resize/Clear/Data/Size
//   /SetWritePos/SetReadPos/Remaining。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace BigHero::Core
{
class Buffer
{
  public:
    Buffer() = default;
    explicit Buffer(size_t reserveSize) { data_.reserve(reserveSize); }

    void Write(const void* src, size_t n)
    {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        size_t writeEnd = writePos_ + n;
        if (writeEnd > data_.size())
        {
            data_.resize(writeEnd);
        }
        std::memcpy(data_.data() + writePos_, p, n);
        writePos_ = writeEnd;
        if (writePos_ > size_) size_ = writePos_;
    }

    void WriteByte(uint8_t b) { Write(&b, 1); }
    void WriteU32(uint32_t v) { Write(&v, sizeof(v)); }
    void WriteString(const std::string& s) { Write(s.data(), s.size()); }

    // 从 readPos_ 读取 n 字节到 dst；返回是否读取成功。
    bool Read(void* dst, size_t n)
    {
        if (readPos_ + n > data_.size())
            return false;
        std::memcpy(dst, data_.data() + readPos_, n);
        readPos_ += n;
        return true;
    }
    bool ReadU8(uint8_t& out) { return Read(&out, 1); }
    bool ReadU32(uint32_t& out) { return Read(&out, 4); }

    // 追加到末尾（不影响写指针）。
    void Append(const void* src, size_t n)
    {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        data_.insert(data_.end(), p, p + n);
        size_ = data_.size();
    }
    void Append(const std::string& s) { Append(s.data(), s.size()); }

    void Resize(size_t n) { data_.resize(n); size_ = n; if (writePos_ > n) writePos_ = n; if (readPos_ > n) readPos_ = n; }
    void Clear() { data_.clear(); size_ = 0; writePos_ = 0; readPos_ = 0; }

    [[nodiscard]] const uint8_t* Data() const { return data_.data(); }
    [[nodiscard]] uint8_t* Data() { return data_.data(); }
    [[nodiscard]] size_t Size() const { return size_; }
    [[nodiscard]] size_t Capacity() const { return data_.capacity(); }

    void SetWritePos(size_t p) { writePos_ = p > data_.size() ? data_.size() : p; }
    void SetReadPos(size_t p) { readPos_ = p > data_.size() ? data_.size() : p; }
    [[nodiscard]] size_t WritePos() const { return writePos_; }
    [[nodiscard]] size_t ReadPos() const { return readPos_; }
    [[nodiscard]] size_t Remaining() const { return data_.size() - readPos_; }

  private:
    std::vector<uint8_t> data_;
    size_t size_ = 0;
    size_t writePos_ = 0;
    size_t readPos_ = 0;
};
} // namespace BigHero::Core
