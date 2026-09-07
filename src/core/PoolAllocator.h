#pragma once
// 固定块内存池（PoolAllocator）：预分配固定大小块，O(1) 申请/释放。
// 纯标准库、仅头文件。
//
// 商业化价值：粒子、投射物、节点等"大量同构短生命周期对象"的高频分配场景；
// 消除堆碎片与慢速 malloc，适合游戏主循环内的临时对象复用。
//
// 实现：一次性分配 blockCount*blockSize 字节的连续缓冲，空槽串成 free 链表；
//   - Allocate：取头空槽返回其指针（O(1)）。
//   - Free(p)：把块归还到 free 链表头（O(1)），需调用方保证指针来自本池且未被重复释放。
//   - Capacity/Allocated/FreeCount：查询。
//   - 不提供构造函数调用（仅原始字节），调用方负责对象生命周期（placement new / 显式析构）。

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cstring>

namespace BigHero::Core
{
class PoolAllocator
{
  public:
    PoolAllocator(size_t blockSize, size_t blockCount)
        : blockSize_(blockSize), blockCount_(blockCount)
    {
        if (blockSize == 0 || blockCount == 0)
            return;
        // 块需容纳一个 next 指针（用于 free 链表）。
        if (blockSize < sizeof(void*))
            blockSize_ = sizeof(void*);
        buffer_.resize(blockSize_ * blockCount_);
        // 构建 free 链表：每个块头存下一空块索引。
        freeHead_ = 0;
        for (size_t i = 0; i < blockCount_; ++i)
        {
            void* block = RawBlock(i);
            *reinterpret_cast<size_t*>(block) = (i + 1 < blockCount_) ? i + 1 : kNull;
        }
        freeCount_ = blockCount_;
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* Allocate()
    {
        if (freeHead_ == kNull || freeCount_ == 0)
            return nullptr;
        size_t idx = freeHead_;
        void* block = RawBlock(idx);
        freeHead_ = *reinterpret_cast<size_t*>(block);
        --freeCount_;
        // 清零块头，避免残留 next 指针（可选）。
        std::memset(block, 0, blockSize_);
        return block;
    }

    void Free(void* p)
    {
        if (!p)
            return;
        // 把块放回 free 链表头（需 p 在本池内——由调用方保证）。
        size_t idx = static_cast<size_t>((static_cast<uint8_t*>(p) - buffer_.data())) / blockSize_;
        *reinterpret_cast<size_t*>(p) = freeHead_;
        freeHead_ = idx;
        ++freeCount_;
    }

    [[nodiscard]] size_t BlockSize() const { return blockSize_; }
    [[nodiscard]] size_t Capacity() const { return blockCount_; }
    [[nodiscard]] size_t FreeCount() const { return freeCount_; }
    [[nodiscard]] size_t AllocatedCount() const { return blockCount_ - freeCount_; }
    [[nodiscard]] bool Empty() const { return freeCount_ == blockCount_; }
    [[nodiscard]] bool Full() const { return freeCount_ == 0; }

  private:
    static constexpr size_t kNull = static_cast<size_t>(-1);
    uint8_t* RawBlock(size_t i) { return buffer_.data() + i * blockSize_; }

    size_t blockSize_ = 0;
    size_t blockCount_ = 0;
    size_t freeHead_ = kNull;
    size_t freeCount_ = 0;
    std::vector<uint8_t> buffer_;
};
} // namespace BigHero::Core
