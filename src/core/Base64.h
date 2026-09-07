#pragma once
// Base64 编解码。
// 纯标准库、仅头文件。
//
// 商业化价值：资源嵌入（data URI、纹理/网格内联）、网络载荷编码、
// 配置文件/序列化数据可读表示的通用编码原语。

#include <cstdint>
#include <string>
#include <vector>

namespace BigHero::Core
{
namespace Base64
{
    inline std::string Encode(const unsigned char* data, size_t len)
    {
        static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        size_t i = 0;
        while (i + 3 <= len)
        {
            uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out += tbl[(v >> 18) & 63];
            out += tbl[(v >> 12) & 63];
            out += tbl[(v >> 6) & 63];
            out += tbl[v & 63];
            i += 3;
        }
        if (i < len)
        {
            uint32_t v = data[i] << 16;
            bool has2 = (i + 1 < len);
            if (has2) v |= data[i + 1] << 8;
            out += tbl[(v >> 18) & 63];
            out += tbl[(v >> 12) & 63];
            out += has2 ? tbl[(v >> 6) & 63] : '=';
            out += '=';
        }
        return out;
    }

    inline std::string Encode(const std::string& s) { return Encode((const unsigned char*)s.data(), s.size()); }

    // 解码，忽略空白；非法字符终止并返回已解码部分。返回 false 表示输入非法/截断。
    inline bool Decode(const std::string& in, std::vector<uint8_t>& out)
    {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        out.clear();
        int accum = 0, accbits = 0;
        for (char c : in)
        {
            if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
                continue;
            if (c == '=')
                break; // padding 结束
            int v = val(c);
            if (v < 0)
                return false;
            accum = (accum << 6) | v;
            accbits += 6;
            if (accbits >= 8)
            {
                accbits -= 8;
                out.push_back((uint8_t)((accum >> accbits) & 0xFF));
            }
        }
        return true;
    }

    // 返回字节串的解码结果版本。
    inline bool Decode(const std::string& in, std::string& out)
    {
        std::vector<uint8_t> bytes;
        if (!Decode(in, bytes))
            return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }
} // namespace Base64
} // namespace BigHero::Core
