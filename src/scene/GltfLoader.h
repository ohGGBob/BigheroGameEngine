#pragma once
// 极简 glTF 2.0 加载器（纯 CPU、仅标准库、可离线单测）。
//
// 定位：与 ObjModel 并列的网格几何加载入口，聚焦几何数据（顶点/索引/子网格）。
// 不依赖外部 JSON 库，自带一个精简 JSON 解析器（JsonValue 变体树），
// 支持 glTF 2.0 静态网格核心字段：
//   - asset.version（校验 2.x）
//   - buffers[]（data URI base64 内嵌 或 相对外部 .bin 文件）
//   - bufferViews[]（byteOffset/byteLength/byteStride，索引到具体 buffer）
//   - accessors[]（componentType/type/count/byteOffset）
//   - meshes[].primitives[]（attributes: POSITION/NORMAL/TEXCOORD_0/COLOR_0/TANGENT，
//     indices，mode=4 TRIANGLES）
//   - materials[]（pbrMetallicRoughness.baseColorFactor）
//
// 语义约定：
//   - 仅支持 mode=4（TRIANGLES）；稀疏 accessor（sparse）暂不支持（遇到报错）。
//   - 支持静态网格 + 骨骼蒙皮数据提取（nodes/skins/JOINTS_0/WEIGHTS_0/逆绑定矩阵）。
//   - 支持动画数据提取（animations[]：通道 target.node/path + 采样器 input/output/interpolation），
//     供 AnimationPlayer（Animation.h）做 LINEAR/STEP 插值采样驱动骨骼动画。
//   - 每个 primitive 的顶点按 POSITION 索引去重后追加到模型，并记录子网格区间。
//   - 缺失法线回退 +Y、缺失 UV 用 0、缺失顶点色用 1（白）、缺失切线用 +X，与 OBJ 加载器一致。
//   - base64 解码支持标准与 URL-safe 两种字符集。

#include "scene/CubeMesh.h" // Vertex
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BigHero::Scene
{
namespace detail
{
// ---- 精简 JSON 变体树 ----
// 自引用结构：对象成员直接存为 (key, JsonValue) 对，std::vector<JsonValue>
// 在 C++17 起支持不完整类型作为成员声明，故可递归自包含。
struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;                         // Array
    std::vector<std::pair<std::string, JsonValue>> obj; // Object

    const JsonValue* Find(const std::string& key) const
    {
        if (type != Type::Object)
            return nullptr;
        for (const auto& m : obj)
            if (m.first == key)
                return &m.second;
        return nullptr;
    }
    double AsNumber(double dflt = 0.0) const { return type == Type::Number ? num : dflt; }
    int AsInt(int dflt = 0) const { return type == Type::Number ? static_cast<int>(std::llround(num)) : dflt; }
    std::string AsString() const { return type == Type::String ? str : std::string(); }
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string& text) : s_(text) {}
    JsonValue Parse()
    {
        SkipWs();
        JsonValue v = ParseValue();
        SkipWs();
        if (pos_ != s_.size())
            throw std::runtime_error("JsonParser: 尾随内容");
        return v;
    }

  private:
    const std::string& s_;
    size_t pos_ = 0;

    void SkipWs()
    {
        while (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }
    [[noreturn]] void Fail(const std::string& msg) const
    {
        throw std::runtime_error("JsonParser: " + msg + " @ " + std::to_string(pos_));
    }
    JsonValue ParseValue()
    {
        if (pos_ >= s_.size())
            Fail("意外的文件结尾");
        const char c = s_[pos_];
        if (c == '{')
            return ParseObject();
        if (c == '[')
            return ParseArray();
        if (c == '"')
        {
            JsonValue v;
            v.type = JsonValue::Type::String;
            v.str = ParseString();
            return v;
        }
        if (c == 't')
        {
            Expect("true");
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.b = true;
            return v;
        }
        if (c == 'f')
        {
            Expect("false");
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.b = false;
            return v;
        }
        if (c == 'n')
        {
            Expect("null");
            return JsonValue{};
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return ParseNumber();
        Fail(std::string("非法字符 '") + c + "'");
    }
    void Expect(const char* literal)
    {
        const size_t len = std::char_traits<char>::length(literal);
        if (pos_ + len > s_.size() || s_.compare(pos_, len, literal) != 0)
            Fail("期望 " + std::string(literal));
        pos_ += len;
    }
    JsonValue ParseNumber()
    {
        const size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-')
            ++pos_;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9')
            ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.')
        {
            ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9')
                ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-'))
                ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9')
                ++pos_;
        }
        JsonValue v;
        v.type = JsonValue::Type::Number;
        v.num = std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr);
        return v;
    }
    std::string ParseString()
    {
        if (pos_ >= s_.size() || s_[pos_] != '"')
            Fail("期望字符串起始");
        ++pos_;
        std::string out;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_++];
            if (c == '"')
                return out;
            if (c == '\\')
            {
                if (pos_ >= s_.size())
                    break;
                const char e = s_[pos_++];
                switch (e)
                {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u':
                {
                    if (pos_ + 4 > s_.size())
                        Fail("非法 \\u 转义");
                    const unsigned cp = std::strtoul(s_.substr(pos_, 4).c_str(), nullptr, 16);
                    pos_ += 4;
                    if (cp >= 0x80)
                    {
                        if (cp < 0x800)
                        {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                    }
                    else
                        out += static_cast<char>(cp);
                    break;
                }
                default:
                    out += e;
                    break;
                }
            }
            else
                out += c;
        }
        Fail("字符串未闭合");
    }
    JsonValue ParseArray()
    {
        ++pos_;
        JsonValue v;
        v.type = JsonValue::Type::Array;
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']')
        {
            ++pos_;
            return v;
        }
        while (true)
        {
            SkipWs();
            v.arr.push_back(ParseValue());
            SkipWs();
            if (pos_ >= s_.size())
                Fail("数组未闭合");
            if (s_[pos_] == ']')
            {
                ++pos_;
                return v;
            }
            if (s_[pos_] != ',')
                Fail("期望 ','");
            ++pos_;
        }
    }
    JsonValue ParseObject()
    {
        ++pos_;
        JsonValue v;
        v.type = JsonValue::Type::Object;
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}')
        {
            ++pos_;
            return v;
        }
        while (true)
        {
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"')
                Fail("期望对象键");
            const std::string key = ParseString();
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':')
                Fail("期望 ':'");
            ++pos_;
            SkipWs();
            v.obj.push_back(std::make_pair(key, ParseValue()));
            SkipWs();
            if (pos_ >= s_.size())
                Fail("对象未闭合");
            if (s_[pos_] == '}')
            {
                ++pos_;
                return v;
            }
            if (s_[pos_] != ',')
                Fail("期望 ','");
            ++pos_;
        }
    }
};

// ---- base64 解码（标准 + URL-safe） ----
inline int Base64Val(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+' || c == '-')
        return 62;
    if (c == '/' || c == '_')
        return 63;
    return -1;
}
inline std::vector<unsigned char> Base64Decode(const std::string& input)
{
    std::vector<unsigned char> out;
    int buffer = 0, bits = 0;
    for (const char c : input)
    {
        if (c == '=')
            break;
        const int v = Base64Val(c);
        if (v < 0)
            continue;
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}
inline bool DecodeDataUri(const std::string& uri, std::vector<unsigned char>& out)
{
    const std::string prefix = "data:";
    if (uri.compare(0, prefix.size(), prefix) != 0)
        return false;
    const size_t comma = uri.find(',');
    if (comma == std::string::npos)
        return false;
    const std::string meta = uri.substr(0, comma);
    if (meta.find("base64") == std::string::npos)
        return false;
    out = Base64Decode(uri.substr(comma + 1));
    return true;
}

// ---- glTF 数值工具 ----
inline uint32_t ComponentSize(int componentType)
{
    switch (componentType)
    {
    case 5120:
        return 1; // BYTE
    case 5121:
        return 1; // UBYTE
    case 5122:
        return 2; // SHORT
    case 5123:
        return 2; // USHORT
    case 5125:
        return 4; // UINT
    case 5126:
        return 4; // FLOAT
    default:
        throw std::runtime_error("glTF: 未知 componentType " + std::to_string(componentType));
    }
}
inline uint32_t TypeComponents(const std::string& type)
{
    if (type == "SCALAR")
        return 1;
    if (type == "VEC2")
        return 2;
    if (type == "VEC3")
        return 3;
    if (type == "VEC4")
        return 4;
    throw std::runtime_error("glTF: 未知 accessor.type " + type);
}

// 已解析的 bufferView：指向哪个 buffer + 相对偏移 + 元素步长
struct ViewInfo
{
    int bufferIndex = 0;
    size_t byteOffset = 0;
    size_t byteStride = 0; // 0 = 紧密打包（由 accessor 自己决定）
};
} // namespace detail

// ---- glTF 子网格（对应 primitive） ----
struct GltfPrimitive
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t materialIndex = -1; // -1 表示未指定材质
};

// ---- glTF 材质（抽取 PBR 基础色） ----
struct GltfMaterial
{
    std::string name;
    glm::vec4 baseColorFactor = glm::vec4(1.0f); // RGBA
};

// ---- glTF 动画通道（target node + path） ----
struct GltfAnimationChannel
{
    int32_t targetNode = -1; // 目标节点下标
    std::string path;        // "translation" | "rotation" | "scale"
    int sampler = -1;        // 采样器下标
};

// ---- glTF 动画采样器（时间 -> 关键帧值） ----
struct GltfAnimationSampler
{
    std::string interpolation = "LINEAR"; // LINEAR | STEP | CUBICSPLINE
    std::vector<float> times;             // 输入（SCALAR FLOAT）
    std::vector<glm::vec4> values;        // 输出（VEC3 平移/缩放，VEC4 旋转）
};

// ---- glTF 动画 ----
struct GltfAnimation
{
    std::string name;
    std::vector<GltfAnimationChannel> channels;
    std::vector<GltfAnimationSampler> samplers;
};

// ---- glTF 模型结果 ----
struct GltfModel
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfMaterial> materials;

    // ---- 骨骼/蒙皮（可选，无 skin 时为空，向后兼容） ----
    // 节点层级（扁平数组，index 即 node index）
    std::vector<int32_t> nodeParents;        // 父节点索引，-1=根
    std::vector<glm::vec3> nodeTranslations; // 局部平移
    std::vector<glm::quat> nodeRotations;    // 局部旋转（四元数）
    std::vector<glm::vec3> nodeScales;       // 局部缩放

    // 皮肤：关节（节点索引）+ 逆绑定矩阵（每个关节一个）
    std::vector<int32_t> jointNodes;            // 关节对应的节点索引
    std::vector<glm::mat4> inverseBindMatrices; // 与 jointNodes 一一对应

    // 逐顶点蒙皮权重（可选，与 vertices 一一对应，各至多 4 关节）
    std::vector<glm::u8vec4> jointIndices; // 每顶点 0~4 个关节（u8，值为 jointNodes 下标）
    std::vector<glm::vec4> jointWeights;   // 每顶点 0~4 个权重（和为1）

    // 动画（可选，无 animations 时为空）
    std::vector<GltfAnimation> animations;
};

namespace detail
{
// 读 accessor 第 elem 个元素的第 comp 个分量，映射为 float
inline float AccessorComponent(const std::vector<std::vector<unsigned char>>& buffers,
                               const std::vector<ViewInfo>& views, const JsonValue& acc, uint64_t elem, uint32_t comp)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    const uint32_t compSize = ComponentSize(componentType);
    const std::string type = acc.Find("type") ? acc.Find("type")->AsString() : std::string();
    const uint32_t numComp = TypeComponents(type);
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;

    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: accessor 缺少/越界 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    if (view.bufferIndex < 0 || view.bufferIndex >= static_cast<int>(buffers.size()))
        throw std::runtime_error("glTF: bufferView.buffer 越界");

    // 元素步长：bufferView 显式 byteStride 优先，否则紧密打包 numComp*compSize
    const size_t elemStride = view.byteStride ? view.byteStride : static_cast<size_t>(numComp) * compSize;
    const size_t srcOffset = view.byteOffset + accByteOffset + elem * elemStride + comp * compSize;

    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];
    if (srcOffset + compSize > buf.size())
        throw std::runtime_error("glTF: accessor 越界 buffer");

    uint32_t raw = 0;
    if (compSize == 1)
        raw = buf[srcOffset];
    else if (compSize == 2)
    {
        std::memcpy(&raw, &buf[srcOffset], 2);
        raw &= 0xFFFF;
    }
    else
        std::memcpy(&raw, &buf[srcOffset], 4);

    if (componentType == 5126)
        return reinterpret_cast<const float&>(raw);
    switch (componentType)
    {
    case 5120:
        return static_cast<float>(static_cast<int8_t>(raw & 0xFF)) / 127.0f;
    case 5121:
        return static_cast<float>(raw & 0xFF) / 255.0f;
    case 5122:
        return static_cast<float>(static_cast<int16_t>(raw & 0xFFFF)) / 32767.0f;
    case 5123:
        return static_cast<float>(raw & 0xFFFF) / 65535.0f;
    case 5125:
        return static_cast<float>(raw);
    default:
        throw std::runtime_error("glTF: 非法 componentType");
    }
}

// 读取索引 accessor 的全部索引（componentType 5121/5123/5125 非归一化整型）
inline std::vector<uint32_t> ReadIndices(const std::vector<std::vector<unsigned char>>& buffers,
                                         const std::vector<ViewInfo>& views, const JsonValue& acc, int count)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    const uint32_t compSize = ComponentSize(componentType);
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;
    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: indices 缺少 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    const size_t elemStride = view.byteStride ? view.byteStride : compSize;
    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];

    std::vector<uint32_t> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const size_t off = view.byteOffset + accByteOffset + static_cast<size_t>(i) * elemStride;
        if (off + compSize > buf.size())
            throw std::runtime_error("glTF: indices 越界");
        if (compSize == 1)
            out.push_back(buf[off]);
        else if (compSize == 2)
        {
            uint16_t v;
            std::memcpy(&v, &buf[off], 2);
            out.push_back(v);
        }
        else
        {
            uint32_t v;
            std::memcpy(&v, &buf[off], 4);
            out.push_back(v);
        }
    }
    return out;
}

// 读取 MAT4 数组（逆绑定矩阵等）：列主序，每元素 16 个 FLOAT
inline std::vector<glm::mat4> ReadMatrices(const std::vector<std::vector<unsigned char>>& buffers,
                                           const std::vector<ViewInfo>& views, const JsonValue& acc, int count)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    if (componentType != 5126)
        throw std::runtime_error("glTF: MAT4 需 FLOAT componentType");
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;
    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: MAT4 缺少 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    const size_t elemStride = view.byteStride ? view.byteStride : 64; // 16 * 4
    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];

    std::vector<glm::mat4> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        glm::mat4 m(1.0f);
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const size_t off = view.byteOffset + accByteOffset + static_cast<size_t>(i) * elemStride +
                                   static_cast<size_t>((c * 4 + r) * 4);
                if (off + 4 > buf.size())
                    throw std::runtime_error("glTF: MAT4 越界");
                float v;
                std::memcpy(&v, &buf[off], 4);
                m[c][r] = v;
            }
        }
        out.push_back(m);
    }
    return out;
}

// 读取 VEC4 归一化 u8 关节索引（JOINTS_0）
inline std::vector<glm::u8vec4> ReadJointIndices(const std::vector<std::vector<unsigned char>>& buffers,
                                                 const std::vector<ViewInfo>& views, const JsonValue& acc, int count)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    const uint32_t compSize = ComponentSize(componentType);
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;
    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: JOINTS 缺少 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    const size_t elemStride = view.byteStride ? view.byteStride : compSize * 4;
    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];

    std::vector<glm::u8vec4> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        glm::u8vec4 j;
        for (int c = 0; c < 4; ++c)
        {
            const size_t off = view.byteOffset + accByteOffset + static_cast<size_t>(i) * elemStride +
                               static_cast<size_t>(c * compSize);
            if (off + compSize > buf.size())
                throw std::runtime_error("glTF: JOINTS 越界");
            uint32_t raw = 0;
            if (compSize == 1)
                raw = buf[off];
            else if (compSize == 2)
            {
                uint16_t v;
                std::memcpy(&v, &buf[off], 2);
                raw = v;
            }
            else
            {
                uint32_t v;
                std::memcpy(&v, &buf[off], 4);
                raw = v;
            }
            j[c] = static_cast<uint8_t>(raw);
        }
        out.push_back(j);
    }
    return out;
}

// 读取 VEC4 float 权重（WEIGHTS_0）
inline std::vector<glm::vec4> ReadWeights(const std::vector<std::vector<unsigned char>>& buffers,
                                          const std::vector<ViewInfo>& views, const JsonValue& acc, int count)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    if (componentType != 5126)
        throw std::runtime_error("glTF: WEIGHTS 需 FLOAT componentType");
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;
    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: WEIGHTS 缺少 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    const size_t elemStride = view.byteStride ? view.byteStride : 16;
    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];

    std::vector<glm::vec4> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        glm::vec4 w(0.0f);
        for (int c = 0; c < 4; ++c)
        {
            const size_t off =
                view.byteOffset + accByteOffset + static_cast<size_t>(i) * elemStride + static_cast<size_t>(c * 4);
            if (off + 4 > buf.size())
                throw std::runtime_error("glTF: WEIGHTS 越界");
            float v;
            std::memcpy(&v, &buf[off], 4);
            w[c] = v;
        }
        out.push_back(w);
    }
    return out;
}

// 读取 SCALAR float 数组（动画时间等）
inline std::vector<float> ReadFloats(const std::vector<std::vector<unsigned char>>& buffers,
                                     const std::vector<ViewInfo>& views, const JsonValue& acc, int count)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    if (componentType != 5126)
        throw std::runtime_error("glTF: SCALAR 时间需 FLOAT componentType");
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;
    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: SCALAR 缺少 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    const size_t elemStride = view.byteStride ? view.byteStride : 4;
    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];

    std::vector<float> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const size_t off = view.byteOffset + accByteOffset + static_cast<size_t>(i) * elemStride;
        if (off + 4 > buf.size())
            throw std::runtime_error("glTF: SCALAR 越界");
        float v;
        std::memcpy(&v, &buf[off], 4);
        out.push_back(v);
    }
    return out;
}

// 读取 VEC3/VEC4 float 数组（动画采样值），统一存为 vec4
inline std::vector<glm::vec4> ReadVecValues(const std::vector<std::vector<unsigned char>>& buffers,
                                            const std::vector<ViewInfo>& views, const JsonValue& acc, int count)
{
    const int componentType = acc.Find("componentType") ? acc.Find("componentType")->AsInt(0) : 0;
    if (componentType != 5126)
        throw std::runtime_error("glTF: 采样值需 FLOAT componentType");
    const std::string type = acc.Find("type") ? acc.Find("type")->AsString() : std::string();
    const uint32_t numComp = TypeComponents(type); // 3 或 4
    const size_t accByteOffset = acc.Find("byteOffset") ? static_cast<size_t>(acc.Find("byteOffset")->AsInt(0)) : 0;
    const JsonValue* bvNode = acc.Find("bufferView");
    const int viewIdx = bvNode ? bvNode->AsInt(-1) : -1;
    if (viewIdx < 0 || viewIdx >= static_cast<int>(views.size()))
        throw std::runtime_error("glTF: 采样值缺少 bufferView");
    const ViewInfo& view = views[static_cast<size_t>(viewIdx)];
    const size_t elemStride = view.byteStride ? view.byteStride : static_cast<size_t>(numComp) * 4;
    const std::vector<unsigned char>& buf = buffers[static_cast<size_t>(view.bufferIndex)];

    std::vector<glm::vec4> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        glm::vec4 v(0.0f);
        for (uint32_t c = 0; c < numComp; ++c)
        {
            const size_t off =
                view.byteOffset + accByteOffset + static_cast<size_t>(i) * elemStride + static_cast<size_t>(c * 4);
            if (off + 4 > buf.size())
                throw std::runtime_error("glTF: 采样值越界");
            float f;
            std::memcpy(&f, &buf[off], 4);
            v[c] = f;
        }
        out.push_back(v);
    }
    return out;
}
} // namespace detail

// 从内存 glTF JSON 文档加载（支持 data URI base64 内嵌缓冲）
inline GltfModel LoadGltfFromMemory(const std::string& jsonText)
{
    using namespace detail;

    const JsonValue root = JsonParser(jsonText).Parse();

    // asset.version 必须 2.x
    if (const JsonValue* asset = root.Find("asset"))
    {
        const std::string ver = asset->Find("version") ? asset->Find("version")->AsString() : std::string();
        if (ver.size() < 1 || ver[0] != '2')
            throw std::runtime_error("glTF: 仅支持 asset.version 2.x，得到 '" + ver + "'");
    }
    else
    {
        throw std::runtime_error("glTF: 缺少 asset");
    }

    // buffers：解析 data URI base64（外部 .bin 由 LoadGltf 预先注入/解析）
    std::vector<std::vector<unsigned char>> buffers;
    if (const JsonValue* bufs = root.Find("buffers"))
    {
        buffers.reserve(bufs->arr.size());
        for (const JsonValue& b : bufs->arr)
        {
            const std::string uri = b.Find("uri") ? b.Find("uri")->AsString() : std::string();
            std::vector<unsigned char> bytes;
            if (!uri.empty() && DecodeDataUri(uri, bytes))
            {
                // 内嵌 base64 已解码
            }
            else if (!uri.empty())
            {
                throw std::runtime_error("glTF: 内存加载不支持外部 buffer uri（请用 LoadGltf 或改为 data URI）");
            }
            // 无 uri（纯 byteLength）视为空，实际数据由测试/外部填充
            buffers.push_back(std::move(bytes));
        }
    }

    // bufferViews
    std::vector<ViewInfo> views;
    if (const JsonValue* viewArr = root.Find("bufferViews"))
    {
        views.reserve(viewArr->arr.size());
        for (const JsonValue& v : viewArr->arr)
        {
            ViewInfo info;
            info.bufferIndex = v.Find("buffer") ? v.Find("buffer")->AsInt(0) : 0;
            info.byteOffset = v.Find("byteOffset") ? static_cast<size_t>(v.Find("byteOffset")->AsInt(0)) : 0;
            info.byteStride = v.Find("byteStride") ? static_cast<size_t>(v.Find("byteStride")->AsInt(0)) : 0;
            views.push_back(info);
        }
    }

    // accessors：attributes 中的值是 accessors[] 的下标，需先整体解析
    std::vector<JsonValue> accessors;
    if (const JsonValue* accArr = root.Find("accessors"))
        accessors = accArr->arr;
    // 按下标解析 accessor 对象
    const auto accByIndex = [&](const JsonValue* ref) -> const JsonValue*
    {
        if (!ref || ref->type != JsonValue::Type::Number)
            return nullptr;
        const int idx = ref->AsInt(-1);
        if (idx < 0 || idx >= static_cast<int>(accessors.size()))
            return nullptr;
        return &accessors[static_cast<size_t>(idx)];
    };

    // 材质
    GltfModel model;
    if (const JsonValue* mats = root.Find("materials"))
    {
        model.materials.reserve(mats->arr.size());
        for (const JsonValue& m : mats->arr)
        {
            GltfMaterial mat;
            mat.name = m.Find("name") ? m.Find("name")->AsString() : std::string();
            if (const JsonValue* pbr = m.Find("pbrMetallicRoughness"))
                if (const JsonValue* bcf = pbr->Find("baseColorFactor"))
                    if (bcf->type == JsonValue::Type::Array && bcf->arr.size() >= 4)
                    {
                        mat.baseColorFactor.r = static_cast<float>(bcf->arr[0].AsNumber(1.0));
                        mat.baseColorFactor.g = static_cast<float>(bcf->arr[1].AsNumber(1.0));
                        mat.baseColorFactor.b = static_cast<float>(bcf->arr[2].AsNumber(1.0));
                        mat.baseColorFactor.a = static_cast<float>(bcf->arr[3].AsNumber(1.0));
                    }
            model.materials.push_back(std::move(mat));
        }
    }

    // ---- 节点层级（nodes[]）：扁平数组，parent 由 children 反向构建 ----
    if (const JsonValue* nodes = root.Find("nodes"))
    {
        model.nodeParents.assign(nodes->arr.size(), -1);
        model.nodeTranslations.assign(nodes->arr.size(), glm::vec3(0.0f));
        model.nodeRotations.assign(nodes->arr.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        model.nodeScales.assign(nodes->arr.size(), glm::vec3(1.0f));

        for (size_t i = 0; i < nodes->arr.size(); ++i)
        {
            const JsonValue& n = nodes->arr[i];
            if (const JsonValue* t = n.Find("translation"))
                if (t->type == JsonValue::Type::Array && t->arr.size() >= 3)
                    model.nodeTranslations[i] = glm::vec3(static_cast<float>(t->arr[0].AsNumber(0.0)),
                                                          static_cast<float>(t->arr[1].AsNumber(0.0)),
                                                          static_cast<float>(t->arr[2].AsNumber(0.0)));
            if (const JsonValue* r = n.Find("rotation"))
                if (r->type == JsonValue::Type::Array && r->arr.size() >= 4)
                    model.nodeRotations[i] = glm::quat(static_cast<float>(r->arr[3].AsNumber(1.0)),  // w
                                                       static_cast<float>(r->arr[0].AsNumber(0.0)),  // x
                                                       static_cast<float>(r->arr[1].AsNumber(0.0)),  // y
                                                       static_cast<float>(r->arr[2].AsNumber(0.0))); // z
            if (const JsonValue* s = n.Find("scale"))
                if (s->type == JsonValue::Type::Array && s->arr.size() >= 3)
                    model.nodeScales[i] = glm::vec3(static_cast<float>(s->arr[0].AsNumber(1.0)),
                                                    static_cast<float>(s->arr[1].AsNumber(1.0)),
                                                    static_cast<float>(s->arr[2].AsNumber(1.0)));
            // 反向建立父子关系：本节点的 children 都以本节点为父
            if (const JsonValue* ch = n.Find("children"))
                for (const JsonValue& c : ch->arr)
                {
                    const int child = c.AsInt(-1);
                    if (child >= 0 && child < static_cast<int>(model.nodeParents.size()))
                        model.nodeParents[static_cast<size_t>(child)] = static_cast<int32_t>(i);
                }
        }
    }

    // ---- 皮肤（skins[]）：关节节点 + 逆绑定矩阵 ----
    if (const JsonValue* skins = root.Find("skins"))
    {
        if (!skins->arr.empty())
        {
            const JsonValue& skin = skins->arr[0]; // 取第一个皮肤
            if (const JsonValue* joints = skin.Find("joints"))
            {
                for (const JsonValue& j : joints->arr)
                    model.jointNodes.push_back(j.AsInt(-1));
            }
            if (const JsonValue* ibm = skin.Find("inverseBindMatrices"))
            {
                const JsonValue* ibmAcc = accByIndex(ibm);
                if (ibmAcc)
                {
                    const int count = ibmAcc->Find("count") ? ibmAcc->Find("count")->AsInt(0) : 0;
                    model.inverseBindMatrices = ReadMatrices(buffers, views, *ibmAcc, count);
                }
            }
        }
    }

    // ---- 动画（animations[]）：通道 + 采样器 ----
    if (const JsonValue* anims = root.Find("animations"))
    {
        model.animations.reserve(anims->arr.size());
        for (const JsonValue& anim : anims->arr)
        {
            GltfAnimation out;
            out.name = anim.Find("name") ? anim.Find("name")->AsString() : std::string();

            // 先解析 samplers（通道通过 sampler 下标引用）
            if (const JsonValue* sarr = anim.Find("samplers"))
            {
                out.samplers.reserve(sarr->arr.size());
                for (const JsonValue& s : sarr->arr)
                {
                    GltfAnimationSampler sp;
                    sp.interpolation =
                        s.Find("interpolation") ? s.Find("interpolation")->AsString() : std::string("LINEAR");
                    // 输入：SCALAR FLOAT 时间戳
                    const JsonValue* inRef = s.Find("input");
                    const JsonValue* inAcc = accByIndex(inRef);
                    if (inAcc)
                    {
                        const int cnt = inAcc->Find("count") ? inAcc->Find("count")->AsInt(0) : 0;
                        sp.times = ReadFloats(buffers, views, *inAcc, cnt);
                    }
                    // 输出：VEC3/VEC4 FLOAT 采样值（统一存 vec4）
                    const JsonValue* outRef = s.Find("output");
                    const JsonValue* outAcc = accByIndex(outRef);
                    if (outAcc)
                    {
                        const int cnt = outAcc->Find("count") ? outAcc->Find("count")->AsInt(0) : 0;
                        sp.values = ReadVecValues(buffers, views, *outAcc, cnt);
                    }
                    out.samplers.push_back(std::move(sp));
                }
            }
            // 通道
            if (const JsonValue* carr = anim.Find("channels"))
            {
                out.channels.reserve(carr->arr.size());
                for (const JsonValue& ch : carr->arr)
                {
                    GltfAnimationChannel c;
                    if (const JsonValue* tgt = ch.Find("target"))
                    {
                        c.targetNode = tgt->Find("node") ? tgt->Find("node")->AsInt(-1) : -1;
                        c.path = tgt->Find("path") ? tgt->Find("path")->AsString() : std::string();
                    }
                    c.sampler = ch.Find("sampler") ? ch.Find("sampler")->AsInt(-1) : -1;
                    out.channels.push_back(std::move(c));
                }
            }
            model.animations.push_back(std::move(out));
        }
    }

    const JsonValue* meshes = root.Find("meshes");
    if (!meshes)
        throw std::runtime_error("glTF: 缺少 meshes");

    for (const JsonValue& mesh : meshes->arr)
    {
        const JsonValue* prims = mesh.Find("primitives");
        if (!prims)
            continue;
        for (const JsonValue& prim : prims->arr)
        {
            const int mode = prim.Find("mode") ? prim.Find("mode")->AsInt(4) : 4;
            if (mode != 4)
                throw std::runtime_error("glTF: 仅支持 mode=4(TRIANGLES)，得到 " + std::to_string(mode));

            const JsonValue* attrs = prim.Find("attributes");
            if (!attrs)
                throw std::runtime_error("glTF: primitive 缺少 attributes");
            // attributes 的值是 accessors[] 下标，解析为 accessor 对象
            const JsonValue* posAcc = accByIndex(attrs->Find("POSITION"));
            if (!posAcc)
                throw std::runtime_error("glTF: primitive 缺少/非法 POSITION accessor");
            const JsonValue* norAcc = accByIndex(attrs->Find("NORMAL"));
            const JsonValue* uvAcc = accByIndex(attrs->Find("TEXCOORD_0"));
            const JsonValue* colAcc = accByIndex(attrs->Find("COLOR_0"));
            const JsonValue* tanAcc = accByIndex(attrs->Find("TANGENT"));

            const int posCount = posAcc->Find("count") ? posAcc->Find("count")->AsInt(0) : 0;

            // 读取索引（可选）
            std::vector<uint32_t> localIndices;
            const JsonValue* idxRef = prim.Find("indices");
            const JsonValue* idxAcc = accByIndex(idxRef);
            if (idxAcc)
            {
                const int count = idxAcc->Find("count") ? idxAcc->Find("count")->AsInt(0) : 0;
                localIndices = ReadIndices(buffers, views, *idxAcc, count);
            }

            // 蒙皮：JOINTS_0 / WEIGHTS_0（可选）
            const JsonValue* jntAcc = accByIndex(attrs->Find("JOINTS_0"));
            const JsonValue* wgtAcc = accByIndex(attrs->Find("WEIGHTS_0"));
            std::vector<glm::u8vec4> jntData;
            std::vector<glm::vec4> wgtData;
            if (jntAcc)
                jntData = ReadJointIndices(buffers, views, *jntAcc, posCount);
            if (wgtAcc)
                wgtData = ReadWeights(buffers, views, *wgtAcc, posCount);

            // 构建本 primitive 顶点（按 POSITION 索引逐一读取）
            const uint32_t vertexBase = static_cast<uint32_t>(model.vertices.size());
            std::vector<Vertex> primVerts;
            primVerts.reserve(static_cast<size_t>(posCount));
            const bool hasSkin = !jntData.empty();
            for (int i = 0; i < posCount; ++i)
            {
                const uint64_t e = static_cast<uint64_t>(i);
                Vertex v{};
                v.pos = glm::vec3(AccessorComponent(buffers, views, *posAcc, e, 0),
                                  AccessorComponent(buffers, views, *posAcc, e, 1),
                                  AccessorComponent(buffers, views, *posAcc, e, 2));
                if (norAcc)
                    v.normal = glm::vec3(AccessorComponent(buffers, views, *norAcc, e, 0),
                                         AccessorComponent(buffers, views, *norAcc, e, 1),
                                         AccessorComponent(buffers, views, *norAcc, e, 2));
                else
                    v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                if (uvAcc)
                    v.uv = glm::vec2(AccessorComponent(buffers, views, *uvAcc, e, 0),
                                     AccessorComponent(buffers, views, *uvAcc, e, 1));
                if (colAcc)
                    v.color = glm::vec3(AccessorComponent(buffers, views, *colAcc, e, 0),
                                        AccessorComponent(buffers, views, *colAcc, e, 1),
                                        AccessorComponent(buffers, views, *colAcc, e, 2));
                else
                    v.color = glm::vec3(1.0f);
                if (tanAcc)
                    v.tangent = glm::vec3(AccessorComponent(buffers, views, *tanAcc, e, 0),
                                          AccessorComponent(buffers, views, *tanAcc, e, 1),
                                          AccessorComponent(buffers, views, *tanAcc, e, 2));
                else
                    v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                primVerts.push_back(v);
            }

            // 追加本 primitive 蒙皮数据到模型（与 primVerts 一一对应）
            if (hasSkin)
            {
                model.jointIndices.insert(model.jointIndices.end(), jntData.begin(), jntData.end());
                model.jointWeights.insert(model.jointWeights.end(), wgtData.begin(), wgtData.end());
            }

            // 索引重映射到全局顶点
            std::vector<uint32_t> outIndices;
            if (idxAcc)
            {
                outIndices.reserve(localIndices.size());
                for (const uint32_t src : localIndices)
                {
                    if (src >= primVerts.size())
                        throw std::runtime_error("glTF: 索引越界顶点");
                    outIndices.push_back(vertexBase + src);
                }
            }
            else
            {
                outIndices.reserve(primVerts.size());
                for (uint32_t i = 0; i < primVerts.size(); ++i)
                    outIndices.push_back(vertexBase + i);
            }

            GltfPrimitive primInfo;
            primInfo.firstIndex = static_cast<uint32_t>(model.indices.size());
            primInfo.indexCount = static_cast<uint32_t>(outIndices.size());
            primInfo.materialIndex = prim.Find("material") ? prim.Find("material")->AsInt(-1) : -1;
            model.primitives.push_back(primInfo);

            model.vertices.insert(model.vertices.end(), primVerts.begin(), primVerts.end());
            model.indices.insert(model.indices.end(), outIndices.begin(), outIndices.end());
        }
    }

    if (model.vertices.empty())
        throw std::runtime_error("glTF: 没有可用顶点");
    return model;
}

// 从文件加载 glTF（支持内嵌 base64 data URI 的 buffer）。
// 外部相对 .bin buffer：为保持简单与健壮，暂不支持，建议导出为内嵌或后续接入时再扩展。
inline GltfModel LoadGltf(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("GltfLoader: 无法打开 " + path);
    std::string jsonText((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // 去除 UTF-8 BOM
    if (jsonText.size() >= 3 && static_cast<unsigned char>(jsonText[0]) == 0xEF &&
        static_cast<unsigned char>(jsonText[1]) == 0xBB && static_cast<unsigned char>(jsonText[2]) == 0xBF)
        jsonText.erase(0, 3);
    return LoadGltfFromMemory(jsonText);
}
} // namespace BigHero::Scene

