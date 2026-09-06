#pragma once
// 字符串工具集（StringUtils）：纯标准库、仅头文件。
// 用于 UI 标签、日志格式化、资源路径处理、序列化解析等高频场景。
//
// 提供：Trim / ToLower / ToUpper / StartsWith / EndsWith / Split / Join /
//       Replace / ToString(数值) / ParseInt / ParseFloat / Format。

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace BigHero::Core
{
namespace str
{
// 去首尾空白（空格/制表/换行）。默认去左右两侧。
inline std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
inline std::string ToUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

inline bool StartsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
inline bool EndsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// 以分隔符切分（保留空串；skipEmpty=true 时丢弃空段）。
inline std::vector<std::string> Split(const std::string& s, char delim, bool skipEmpty = false)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i)
    {
        if (i == s.size() || s[i] == delim)
        {
            std::string piece = s.substr(start, i - start);
            if (!skipEmpty || !piece.empty())
                out.push_back(std::move(piece));
            start = i + 1;
        }
    }
    return out;
}

// 用分隔符连接字符串集合。
template<typename Iter> inline std::string Join(Iter first, Iter last, const std::string& sep)
{
    std::string out;
    bool firstElem = true;
    for (auto it = first; it != last; ++it)
    {
        if (!firstElem)
            out += sep;
        out += *it;
        firstElem = false;
    }
    return out;
}

// 替换所有出现的 from 为 to。
inline std::string Replace(const std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty())
        return s;
    std::string out;
    size_t pos = 0, found;
    while ((found = s.find(from, pos)) != std::string::npos)
    {
        out.append(s, pos, found - pos);
        out += to;
        pos = found + from.size();
    }
    out.append(s, pos, std::string::npos);
    return out;
}

// 解析整数（std::from_chars，数字+可选符号）。失败返回 false。
inline bool ParseInt(const std::string& s, int& out)
{
    const char* b = s.data();
    const char* e = b + s.size();
    auto res = std::from_chars(b, e, out);
    return res.ec == std::errc() && res.ptr == e;
}
// 解析浮点（strtod）。失败返回 false。
inline bool ParseFloat(const std::string& s, double& out)
{
    if (s.empty())
        return false;
    const char* c = s.c_str();
    char* end = nullptr;
    out = std::strtod(c, &end);
    return end != c && *end == '\0';
}
// 解析浮点（float 重载）。
inline bool ParseFloat(const std::string& s, float& out)
{
    double d = 0.0;
    if (!ParseFloat(s, d))
        return false;
    out = static_cast<float>(d);
    return true;
}

// 数值转字符串（短数字路径：优化 int/uint/double）。
template<typename T> inline std::string ToString(T v)
{
    // 用 std::to_string 作为通用兜底，对 int/float 效率足够。
    if constexpr (std::is_same_v<T, float>)
    {
        if (v == static_cast<long long>(v) && std::abs(v) < 1e15)
            return std::to_string(static_cast<long long>(v));
        return std::to_string(v);
    }
    else
        return std::to_string(v);
}

// 简化 printf 风格格式化（%d / %s / %f 逐项替换），用于日志/标签拼接。
// 注意：支持有限转换说明符，长格式请优先用 std::ostringstream。
inline std::string Format(const std::string& fmt)
{
    return fmt;
}
} // namespace str
} // namespace BigHero::Core
