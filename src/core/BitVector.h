#pragma once
// 位向量（BitVector）：动态的二进制位序列，支持紧凑存储与位操作。
// 纯标准库、仅头文件。
//
// 商业化价值：布隆过滤器、可见性掩码、标签位掩码、稀疏集合索引进度的紧凑底层；
// 相比 vector<bool> 更可控，且有明确的 set/clear/test/resize 语义。

#include <cstddef>
#include <cstdint>
#include <bit>
#include <vector>
#include <algorithm>

namespace BigHero::Core
{
class BitVector
{
  public:
    BitVector() = default;
    explicit BitVector(size_t nBits) { Resize(nBits); }

    void Resize(size_t nBits)
    {
        words_.assign((nBits + 63) / 64, 0);
        size_ = nBits;
    }

    void Set(size_t i)
    {
        if (i >= size_) return;
        words_[i / 64] |= (uint64_t(1) << (i % 64));
    }
    void Clear(size_t i)
    {
        if (i >= size_) return;
        words_[i / 64] &= ~(uint64_t(1) << (i % 64));
    }
    bool Test(size_t i) const
    {
        if (i >= size_) return false;
        return (words_[i / 64] >> (i % 64)) & 1;
    }
    void Toggle(size_t i)
    {
        if (i >= size_) return;
        words_[i / 64] ^= (uint64_t(1) << (i % 64));
    }

    void ClearAll() { std::fill(words_.begin(), words_.end(), 0); }
    void SetAll()
    {
        for (auto& w : words_)
            w = ~uint64_t(0);
        // 清理最后 word 中超出 size 的高位（保持语义一致）。
        if (size_ % 64 != 0)
            words_.back() &= ((uint64_t(1) << (size_ % 64)) - 1);
    }

    [[nodiscard]] size_t Size() const { return size_; }
    [[nodiscard]] bool Empty() const { return size_ == 0; }
    [[nodiscard]] size_t WordCount() const { return words_.size(); }

    [[nodiscard]] size_t CountSetBits() const
    {
        size_t c = 0;
        for (auto w : words_)
            c += (size_t)std::popcount<uint64_t>(w);
        return c;
    }

    [[nodiscard]] bool Any() const
    {
        for (auto w : words_)
            if (w != 0) return true;
        return false;
    }
    [[nodiscard]] bool None() const { return !Any(); }

    const std::vector<uint64_t>& Words() const { return words_; }

  private:
    std::vector<uint64_t> words_;
    size_t size_ = 0;
};
} // namespace BigHero::Core
