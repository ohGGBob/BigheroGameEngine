#pragma once
// 高效字符串构建器（StringBuilder）：批量拼接时减少中间临时字符串分配。
// 纯标准库、仅头文件。
//
// 商业化价值：日志行构造、序列化输出、调试信息拼接等"高频字符串拼接"场景，
// 一次性 reserve 后多次 Append，减少堆分配与拷贝。
//
// 提供：Append(各种基本类型 + 字符串 + string_view)、AppendLine、ToString、Clear、Str()、
//   Reserve、Size、Length。

#include <string>
#include <string_view>
#include <sstream>
#include <utility>

namespace BigHero::Core
{
class StringBuilder
{
  public:
    StringBuilder() = default;
    explicit StringBuilder(std::string_view initial) { Append(initial); }

    StringBuilder& Append(std::string_view s)
    {
        buf_.append(s.data(), s.size());
        return *this;
    }
    StringBuilder& Append(const char* s)
    {
        if (s)
            buf_.append(s);
        return *this;
    }
    StringBuilder& Append(char c)
    {
        buf_.push_back(c);
        return *this;
    }
    StringBuilder& Append(int v) { return AppendNumber(v); }
    StringBuilder& Append(unsigned v) { return AppendNumber(v); }
    StringBuilder& Append(long v) { return AppendNumber(v); }
    StringBuilder& Append(unsigned long v) { return AppendNumber(v); }
    StringBuilder& Append(long long v) { return AppendNumber(v); }
    StringBuilder& Append(unsigned long long v) { return AppendNumber(v); }
    StringBuilder& Append(float v) { return AppendNumber(v); }
    StringBuilder& Append(double v) { return AppendNumber(v); }
    StringBuilder& Append(bool v) { buf_.append(v ? "true" : "false"); return *this; }

    StringBuilder& AppendLine(std::string_view s) { Append(s); buf_.push_back('\n'); return *this; }
    StringBuilder& AppendLine() { buf_.push_back('\n'); return *this; }

    // 追加用分隔符连接的两个元素（如 "key", "=", "value"）。
    StringBuilder& Append(const char* key, char sep, std::string_view val)
    {
        Append(key);
        Append(sep);
        Append(val);
        return *this;
    }

    // 重复追加 n 次 s。
    StringBuilder& AppendRepeated(std::string_view s, size_t n)
    {
        buf_.reserve(buf_.size() + s.size() * n);
        for (size_t i = 0; i < n; ++i)
            buf_.append(s.data(), s.size());
        return *this;
    }

    const std::string& Str() const { return buf_; }
    std::string ToString() const { return buf_; }
    void Clear() { buf_.clear(); }
    void Reserve(size_t n) { buf_.reserve(n); }
    [[nodiscard]] size_t Size() const { return buf_.size(); }
    [[nodiscard]] size_t Length() const { return buf_.size(); }
    [[nodiscard]] bool Empty() const { return buf_.empty(); }

    // 就地取可变引用（便于标准库函数复用）。
    std::string& MutBuf() { return buf_; }

  private:
    template<typename V> StringBuilder& AppendNumber(V v)
    {
        // 用 ostringstream 做数字到字符串转换（跨平台安全），后续可优化为 itoa。
        std::ostringstream os;
        os << v;
        buf_.append(os.str());
        return *this;
    }

    std::string buf_;
};
} // namespace BigHero::Core
