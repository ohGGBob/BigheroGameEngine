#pragma once
// 错误处理结果类型（Result）：类似 Rust Result / C++23 expected 的轻量实现。
// 纯标准库、仅头文件。
//
// 商业化价值：资源加载、系统调用、解析器等"可能失败"操作的显式错误传播；
// 替代裸异常或（bool, out）双参返回，调用方必须显式处理错误，增强健壮性。
//
// 设计：
//   - Ok(v)：成功（持有 T）。
//   - Err(e)：失败（持有 E）。
//   - IsOk/IsErr：状态查询。
//   - Value()/Error()：取出（失败时访问 Value 抛错，成功时访问 Error 抛错）。
//   - ValueOr(def)/ErrorOr(def)：带默认值取回。
//   - operator bool：提供显式检查，防止误吞错误。

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace BigHero::Core
{
template<typename T, typename E> class Result
{
  public:
    Result(const T& v) : ok_(true), val_(v) {}
    Result(T&& v) : ok_(true), val_(std::move(v)) {}
    Result(const E& e) : ok_(false), err_(e) {}
    Result(E&& e) : ok_(false), err_(std::move(e)) {}

    // 工厂：Ok / Err
    static Result Ok(const T& v) { return Result(v); }
    static Result Ok(T&& v) { return Result(std::move(v)); }
    static Result Err(const E& e) { return Result(e); }
    static Result Err(E&& e) { return Result(std::move(e)); }

    [[nodiscard]] bool IsOk() const { return ok_; }
    [[nodiscard]] bool IsErr() const { return !ok_; }
    explicit operator bool() const { return ok_; }

    const T& Value() const
    {
        if (!ok_)
            throw std::logic_error("Result::Value called on Err");
        return val_;
    }
    T& Value()
    {
        if (!ok_)
            throw std::logic_error("Result::Value called on Err");
        return val_;
    }
    const E& Error() const
    {
        if (ok_)
            throw std::logic_error("Result::Error called on Ok");
        return err_;
    }

    template<typename U> T ValueOr(U&& def) const { return ok_ ? val_ : std::forward<U>(def); }
    template<typename U> E ErrorOr(U&& def) const { return ok_ ? std::forward<U>(def) : err_; }

    // 若成功返回 T*，失败返回 nullptr（无需抛异常的安全访问）。
    const T* TryValue() const { return ok_ ? &val_ : nullptr; }
    T* TryValue() { return ok_ ? &val_ : nullptr; }

  private:
    bool ok_;
    T val_;
    E err_;
};
} // namespace BigHero::Core
