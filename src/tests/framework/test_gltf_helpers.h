#pragma once
// glTF 测试辅助：二进制缓冲构造（base64 内嵌编码 + 小端字节追加），
// 供 glTF 加载器 / 动画 / 蒙皮系测试用例复用。

#include <cstdint>
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
