#pragma once
// 路径工具（PathUtils）：跨平台路径规范化/拼接/拆分。
// 纯标准库、仅头文件。
//
// 商业化价值：资源加载、资产打包、平台差异（\ 与 /）归一化的标准工具；
// 避免各子系统各自手写路径处理导致的不一致。
//
// 提供：
//   - Normalize：统一分隔符为 '/'，折叠 ./ 与连续 //，解析 ../（不解析符号链接）。
//   - Join：拼接多段路径（自动补/去多余分隔符）。
//   - GetFileName / GetBaseName / GetExtension：取文件名/基名/扩展名（含是否带点）。
//   - GetParentDir：取父目录路径。
//   - IsAbsolute / IsRelative：判断绝对/相对路径。
//   - ChangeExtension：替换扩展名。

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace BigHero::Core
{
inline std::string NormalizePath(std::string_view p)
{
    if (p.empty())
        return {};
    // 统一为 '/'（注意保留 Windows 盘符前缀 'C:'）
    std::string path(p);
    std::replace(path.begin(), path.end(), '\\', '/');

    bool isAbs = !path.empty() && path[0] == '/';
    // 保存盘符前缀
    std::string prefix;
    size_t start = 0;
    if (path.size() >= 2 && path[1] == ':')
    {
        prefix = path.substr(0, 2); // 如 "C:"
        start = 2;
        if (start < path.size() && path[start] == '/') { prefix += '/'; ++start; isAbs = true; }
    }
    else if (!path.empty() && path[0] == '/')
    {
        prefix = "/";
        start = 1;
        isAbs = true;
    }

    // 切分并按栈折叠 '.' '..'
    std::vector<std::string> parts;
    size_t i = start;
    while (i < path.size())
    {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        std::string tok = path.substr(i, j - i);
        if (tok == "." || tok.empty())
        {
            // skip
        }
        else if (tok == "..")
        {
            if (!parts.empty() && parts.back() != "..")
                parts.pop_back();
            else if (!isAbs)
                parts.push_back("..");
        }
        else
        {
            parts.push_back(tok);
        }
        i = j + 1;
    }

    std::string out = prefix;
    for (size_t k = 0; k < parts.size(); ++k)
    {
        if (k > 0)
            out += '/';
        out += parts[k];
    }
    if (out.empty())
        return isAbs ? prefix : std::string(".");
    return out;
}

inline std::string JoinPath(std::string_view a, std::string_view b)
{
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    std::string res(a);
    if (res.back() != '/') res += '/';
    res += b;
    return NormalizePath(res);
}

inline std::string GetFileName(std::string_view p)
{
    std::string n = NormalizePath(p);
    size_t pos = n.find_last_of('/');
    return pos == std::string::npos ? n : n.substr(pos + 1);
}

inline std::string GetBaseName(std::string_view p)
{
    std::string f = GetFileName(p);
    size_t dot = f.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? f : f.substr(0, dot);
}

inline std::string GetExtension(std::string_view p, bool withDot = true)
{
    std::string f = GetFileName(p);
    size_t dot = f.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return {};
    return withDot ? f.substr(dot) : f.substr(dot + 1);
}

inline std::string GetParentDir(std::string_view p)
{
    std::string n = NormalizePath(p);
    size_t pos = n.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return "/";
    return n.substr(0, pos);
}

inline bool IsAbsolutePath(std::string_view p)
{
    std::string n = NormalizePath(p);
    if (!n.empty() && n[0] == '/')
        return true;
    if (n.size() >= 3 && n[1] == ':' && (n[2] == '/' || n[2] == '\\'))
        return true;
    return false;
}

inline bool IsRelativePath(std::string_view p) { return !IsAbsolutePath(p); }

inline std::string ChangeExtension(std::string_view p, std::string_view newExt)
{
    std::string n = NormalizePath(p);
    size_t slash = n.find_last_of('/');
    size_t dot = n.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return n + std::string(newExt);
    return n.substr(0, dot) + std::string(newExt);
}
} // namespace BigHero::Core
