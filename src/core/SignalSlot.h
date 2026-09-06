#pragma once
// 信号-槽（Signal/Slot）：类型安全、线程安全的信号通知机制。
// 纯标准库、仅头文件。
//
// 商业化价值：引擎 UI 回调、动画完成通知、状态变更通知、输入事件回调的标准模式；
// 比裸函数指针更安全（连接对象生命周期管理），比 std::function 集合更易管理连接/断连。
//
// 设计：
//   - Signal<Args...>：Connect 返回 Connection(句柄)，Disconnect/DisconnectAll。
//   - Emit：同步调用所有已连接槽（按连接顺序）。
//   - 线程安全：内部互斥锁保护槽集合；Emit 时拷贝快照。
//   - Connection 支持 move，析构自动断连（RAII，防悬挂）。
//   - 注意：Connection 持有 Signal* 裸指针（非拥有）。调用方必须保证 Signal 的生命周期
//     不短于其 Connection 对象；这是引擎内部惯例（类 Qt 的 QObject 父子管理）。

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <algorithm>
#include <vector>

namespace BigHero::Core
{
template<typename... Args> class Signal
{
  public:
    class Connection
    {
      public:
        Connection() = default;
        Connection(Signal* sig, uint64_t id) : sig_(sig), id_(id) {}
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& o) noexcept : sig_(o.sig_), id_(o.id_) { o.sig_ = nullptr; o.id_ = 0; }
        Connection& operator=(Connection&& o) noexcept
        {
            if (this != &o) { Disconnect(); sig_ = o.sig_; id_ = o.id_; o.sig_ = nullptr; o.id_ = 0; }
            return *this;
        }
        ~Connection() { Disconnect(); }
        void Disconnect()
        {
            if (sig_)
                sig_->Disconnect(id_);
            sig_ = nullptr; id_ = 0;
        }
        [[nodiscard]] bool Connected() const { return sig_ != nullptr && id_ != 0; }
      private:
        Signal* sig_ = nullptr;
        uint64_t id_ = 0;
    };

    Connection Connect(std::function<void(Args...)> slot)
    {
        std::lock_guard<std::mutex> lock(mx_);
        Slot s;
        s.slot = std::move(slot);
        s.id = ++nextId_;
        slots_.push_back(s);
        return Connection(this, s.id);
    }

    void Emit(Args... args)
    {
        std::vector<std::pair<std::function<void(Args...)>, uint64_t>> snapshot;
        {
            std::lock_guard<std::mutex> lock(mx_);
            snapshot.reserve(slots_.size());
            for (const auto& s : slots_)
                snapshot.emplace_back(s.slot, s.id);
        }
        for (auto& p : snapshot)
            p.first(args...);
    }

    void Disconnect(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mx_);
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
                                    [&](const Slot& s) { return s.id == id; }),
                     slots_.end());
    }

    void DisconnectAll()
    {
        std::lock_guard<std::mutex> lock(mx_);
        slots_.clear();
    }

  private:
    struct Slot { std::function<void(Args...)> slot; uint64_t id = 0; };
    std::vector<Slot> slots_;
    std::mutex mx_;
    uint64_t nextId_ = 0;
};
} // namespace BigHero::Core
