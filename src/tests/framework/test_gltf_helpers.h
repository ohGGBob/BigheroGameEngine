#pragma once
// glTF 测试辅助：二进制缓冲构造（base64 内嵌编码 + 小端字节追加）+ 解码，
// 供 glTF 加载器 / 动画 / 蒙皮系测试用例复用。
//
// 商业化增强：
//   - B64Decode()：base64 解码的逆操作，用于测试里校验/还原内嵌 buffer，避免手写偏移表。
//   - AppendU32()：小端追加一个 uint32（与既有 AppendFloat/AppendU16 对齐）。
//   - AppendF32x3()/AppendVec3()：连续追加 3 个 float（glTF 顶点/位置数据常用布局），
//     减少测试代码里重复手写三行。

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// 将字节序列编码为 base64（glTF data URI 内嵌缓冲用）。
inline std::string B64Encode(const std::vector<unsigned char>& bytes)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const unsigned a = bytes[i];
        const unsigned b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const unsigned c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        out += tbl[a >> 2];
        out += tbl[((a & 3) << 4) | (b >> 4)];
        out += (i + 1 < bytes.size()) ? tbl[((b & 0xF) << 2) | (c >> 6)] : '=';
        out += (i + 2 < bytes.size()) ? tbl[c & 0x3F] : '=';
    }
    return out;
}

// 将 base64 字符串解码为字节序列（B64Encode 的逆操作）。
// 忽略空白（空白/换行）；遇到非法字符时提前返回已解码部分。
inline std::vector<unsigned char> B64Decode(const std::string& in)
{
    auto val = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1; // 非 base64 字符
    };

    std::vector<unsigned char> out;
    out.reserve((in.size() / 4) * 3);
    int buf = 0, bits = 0;
    for (unsigned char c : in)
    {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == '=')
            break; // 结束填充
        const int v = val(c);
        if (v < 0)
            continue; // 跳过非法字符
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// 以小端字节序向缓冲追加一个 float。
inline void AppendFloat(std::vector<unsigned char>& v, float x)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), p, p + 4);
}

// 以小端字节序向缓冲追加一个 uint16。
inline void AppendU16(std::vector<unsigned char>& v, uint16_t x)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), p, p + 2);
}

// 以小端字节序向缓冲追加一个 uint32。
inline void AppendU32(std::vector<unsigned char>& v, uint32_t x)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), p, p + 4);
}

// 连续追加 3 个 float（glTF vec3/position/normal 布局），供批量顶点数据构造。
inline void AppendF32x3(std::vector<unsigned char>& v, float x, float y, float z)
{
    AppendFloat(v, x);
    AppendFloat(v, y);
    AppendFloat(v, z);
}
