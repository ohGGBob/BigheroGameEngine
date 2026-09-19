#pragma once
// 精简 JSON 解析器（JsonValue 变体树 + 递归下降解析）。
// 纯标准库、仅头文件、可离线单测。
//
// 商业化价值：资产管线（glTF 场景/材质描述）、配置文件、编辑器序列化等
// 通用 JSON 读取原语；不依赖第三方 JSON 库。
//
// 能力范围：
//   - null / bool / number(double) / string / array / object
//   - 字符串转义（\" \\ \/ \b \f \n \r \t \uXXXX -> UTF-8）
//   - 标准 JSON 数字（负号 / 小数 / 指数）
//   - 严格模式：尾随内容、非法字符、未闭合容器、非法转义均抛 std::runtime_error
// 不支持：注释、NaN/Infinity、64 位整数精确表示（统一 double）。

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace BigHero::Core
{
// ---- JSON 变体树 ----
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
} // namespace BigHero::Core
