#pragma once
// 栈式/线性内存竞技场（MemoryArena）：一次性大块分配，线性前进，整体释放。
// 纯标准库、仅头文件、可离线单测。
//
// 商业化价值：渲染命令缓冲、粒子更新、UI 布局等"帧内临时数据"的分配；
// 用一个 arena 按帧分配上千个小对象而零系统调用、零碎片；帧末 Reset() 一次回收，
// 是商业引擎"临时分配器/帧分配器"的标准实现。
//
// 设计：
//   - 持有一连串 Chunk（每块默认 64KB），线性 bump 分配；当前块不足时新开一块，
//     块之间用 next 指针串成链表，避免大块 realloc 搬移。
//   - Allocate(size, align)：返回对齐后的内存指针；总大小需满足对齐。
//   - Reset()：把所有块的 offset 归零（复用内存），对象在下次分配时被覆盖（调用方自行管理析构）。
//   - 允许在大块上"压栈"与"回滚"——SaveMark/Rewind 用于帧内多阶段临时分配。

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>

namespace BigHero::Core
{
class MemoryArena
{
  public:
    // 内存块（链表节点）。定义为 public 以避免嵌套类型访问冲突。
    struct Chunk
    {
        char* data;
        size_t size;
        size_t offset;
        Chunk* next;
    };
    explicit MemoryArena(size_t chunkSize = 64 * 1024) noexcept
        : chunkSize_(chunkSize > kMinChunk ? chunkSize : kMinChunk)
    {
    }
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    ~MemoryArena() noexcept { FreeAll(); }

    // 分配未对齐内存（按 default alignment 对齐到 min(alignof(max_align_t), 16)）。
    void* Allocate(size_t size)
    {
        return AllocateAligned(size, kDefaultAlign);
    }

    // 对齐分配。
    void* AllocateAligned(size_t size, size_t alignment)
    {
        if (size == 0)
            size = 1;
        if (alignment < kDefaultAlign)
            alignment = kDefaultAlign;
        // 确保 alignment 是 2 的幂且合理
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            alignment = kDefaultAlign;

        // 尝试当前块
        if (cur_ != nullptr)
        {
            const size_t aligned = AlignUp(cur_->offset, alignment);
            if (aligned + size <= cur_->size)
            {
                char* p = cur_->data + aligned;
                cur_->offset = aligned + size;
                return p;
            }
        }
        // 新开一块（保证容纳本次请求）
        const size_t blockSize = size + alignment > chunkSize_ ? size + alignment : chunkSize_;
        AllocateChunk(blockSize);
        const size_t aligned = AlignUp(cur_->offset, alignment);
        char* p = cur_->data + aligned;
        cur_->offset = aligned + size;
        return p;
    }

    // 返回"标记"：可回滚到该位置的偏移（用于帧内多阶段临时分配）。
    struct Mark
    {
        Chunk* chunk;
        size_t offset;
    };
    [[nodiscard]] Mark SaveMark() const noexcept { return Mark{cur_, cur_ ? cur_->offset : 0}; }

    // 回滚到标记位置（释放此后所有分配，块本身保留复用）。
    void Rewind(Mark mark) noexcept
    {
        // 若标记所在块不再位于链表，向前找到它并裁剪后续块。
        Chunk* target = mark.chunk;
        Chunk* prev = nullptr;
        Chunk* c = head_;
        while (c != nullptr && c != target)
        {
            prev = c;
            c = c->next;
        }
        if (c == target)
        {
            // 释放 target 之后的块
            FreeFrom(c->next);
            c->next = nullptr;
            cur_ = c;
            cur_->offset = mark.offset;
        }
        else
        {
            // 标记块已不存在：重置到头部
            Reset();
        }
    }

    // 帧末一次性回收（所有块 offset 归零，复用内存；不返回给 OS）。
    void Reset() noexcept
    {
        for (Chunk* c = head_; c != nullptr; c = c->next)
            c->offset = 0;
        cur_ = head_;
    }

    template<typename T, typename... Args> T* Emplace(Args&&... args)
    {
        void* mem = AllocateAligned(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    [[nodiscard]] size_t TotalAllocated() const noexcept
    {
        size_t t = 0;
        for (Chunk* c = head_; c != nullptr; c = c->next)
            t += c->size;
        return t;
    }
    [[nodiscard]] size_t ChunkCount() const noexcept
    {
        size_t n = 0;
        for (Chunk* c = head_; c != nullptr; c = c->next)
            ++n;
        return n;
    }

  private:
    static constexpr size_t kMinChunk = 4096;
    static constexpr size_t kDefaultAlign = alignof(std::max_align_t);

    static size_t AlignUp(size_t v, size_t a) noexcept { return (v + a - 1) & ~(a - 1); }

    void AllocateChunk(size_t size)
    {
        // 手动分配以保持对齐（用 aligned 分配，确保数据指针对齐到 kDefaultAlign）。
        Chunk* c = static_cast<Chunk*>(::operator new(sizeof(Chunk) + size + kDefaultAlign, std::nothrow));
        if (!c)
            throw std::bad_alloc();
        char* base = reinterpret_cast<char*>(c + 1);
        uintptr_t raw = reinterpret_cast<uintptr_t>(base);
        uintptr_t aligned = (raw + kDefaultAlign - 1) & ~(uintptr_t(kDefaultAlign) - 1);
        c->data = reinterpret_cast<char*>(aligned);
        c->size = size;
        c->offset = 0;
        c->next = nullptr;
        // 串到链表（新块作为当前块，且追加末尾）。
        if (head_ == nullptr)
            head_ = c;
        else
            cur_->next = c;
        cur_ = c;
    }

    void FreeFrom(Chunk* c) noexcept
    {
        while (c != nullptr)
        {
            Chunk* next = c->next;
            ::operator delete(c);
            c = next;
        }
    }
    void FreeAll() noexcept { FreeFrom(head_); head_ = nullptr; cur_ = nullptr; }

    size_t chunkSize_;
    Chunk* head_ = nullptr;
    Chunk* cur_ = nullptr;
};
} // namespace BigHero::Core
