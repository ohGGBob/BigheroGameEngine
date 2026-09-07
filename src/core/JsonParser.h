#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cmath>

namespace bighero {

// Minimal self-contained JSON value + parser (no external deps).
namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    // object stored as parallel-key/val arrays to preserve order & avoid map dep
    std::vector<std::string> keys;
    std::vector<Value> vals;

    bool IsNull() const { return type == Null; }
    bool IsBool() const { return type == Bool; }
    bool IsNumber() const { return type == Number; }
    bool IsString() const { return type == String; }
    bool IsArray() const { return type == Array; }
    bool IsObject() const { return type == Object; }

    int Size() const { return type == Array ? (int)arr.size() : (type == Object ? (int)keys.size() : 0); }

    const Value& operator[](std::size_t i) const { return arr[i]; }
    const Value& Get(const std::string& key) const {
        static Value dummy;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return vals[i];
        return dummy;
    }
    bool Has(const std::string& key) const {
        for (auto& k : keys) if (k == key) return true;
        return false;
    }
    double AsNumber() const { return type == Number ? num : 0.0; }
    std::string AsString() const { return type == String ? str : std::string(); }
};

class Parser {
public:
    // Returns true and fills out on success; false on syntax error.
    static bool Parse(const std::string& text, Value& out) {
        Parser p(text.data(), text.size());
        if (!p.SkipWs()) return false;
        if (!p.ParseValue(out)) return false;
        p.SkipWs();
        return p.pos_ == p.size_; // must consume whole input
    }

private:
    Parser(const char* data, std::size_t size) : data_(data), size_(size), pos_(0) {}

    bool SkipWs() {
        while (pos_ < size_) {
            char c = data_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
        return true;
    }
    bool ParseValue(Value& out) {
        if (pos_ >= size_) return false;
        char c = data_[pos_];
        if (c == '{') { out.type = Value::Object; return ParseObject(out); }
        if (c == '[') { out.type = Value::Array; return ParseArray(out); }
        if (c == '"') { out.type = Value::String; return ParseString(out.str); }
        if (c == 't' || c == 'f') { out.type = Value::Bool; return ParseBool(out.b); }
        if (c == 'n') { out.type = Value::Null; return ParseNull(); }
        if (c == '-' || (c >= '0' && c <= '9')) { out.type = Value::Number; return ParseNumber(out.num); }
        return false;
    }
    bool ParseObject(Value& out) {
        ++pos_; // {
        if (!SkipWs()) return false;
        if (pos_ < size_ && data_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            if (!SkipWs()) return false;
            if (pos_ >= size_ || data_[pos_] != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            if (!SkipWs()) return false;
            if (pos_ >= size_ || data_[pos_] != ':') return false;
            ++pos_;
            if (!SkipWs()) return false;
            Value v;
            if (!ParseValue(v)) return false;
            out.keys.push_back(key);
            out.vals.push_back(v);
            if (!SkipWs()) return false;
            if (pos_ >= size_) return false;
            if (data_[pos_] == ',') { ++pos_; continue; }
            if (data_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }
    bool ParseArray(Value& out) {
        ++pos_; // [
        if (!SkipWs()) return false;
        if (pos_ < size_ && data_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            if (!SkipWs()) return false;
            Value v;
            if (!ParseValue(v)) return false;
            out.arr.push_back(v);
            if (!SkipWs()) return false;
            if (pos_ >= size_) return false;
            if (data_[pos_] == ',') { ++pos_; continue; }
            if (data_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }
    bool ParseString(std::string& out) {
        if (pos_ >= size_ || data_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < size_) {
            char c = data_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= size_) return false;
                char e = data_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    default: return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
    bool ParseBool(bool& out) {
        if (pos_ + 4 <= size_ && std::string(data_ + pos_, 4) == "true") { out = true; pos_ += 4; return true; }
        if (pos_ + 5 <= size_ && std::string(data_ + pos_, 5) == "false") { out = false; pos_ += 5; return true; }
        return false;
    }
    bool ParseNull() {
        if (pos_ + 4 <= size_ && std::string(data_ + pos_, 4) == "null") { pos_ += 4; return true; }
        return false;
    }
    bool ParseNumber(double& out) {
        std::string tok;
        if (pos_ < size_ && data_[pos_] == '-') { tok += '-'; ++pos_; }
        int digits = 0;
        while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') { tok += data_[pos_++]; ++digits; }
        if (digits == 0) return false;
        if (pos_ < size_ && data_[pos_] == '.') {
            tok += '.'; ++pos_;
            int frac = 0;
            while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') { tok += data_[pos_++]; ++frac; }
            if (frac == 0) return false;
        }
        if (pos_ < size_ && (data_[pos_] == 'e' || data_[pos_] == 'E')) {
            tok += data_[pos_++];
            if (pos_ < size_ && (data_[pos_] == '+' || data_[pos_] == '-')) tok += data_[pos_++];
            int exp = 0;
            while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') { tok += data_[pos_++]; ++exp; }
            if (exp == 0) return false;
        }
        out = std::strtod(tok.c_str(), nullptr);
        return true;
    }

    const char* data_;
    std::size_t size_, pos_;
};

} // namespace json

} // namespace bighero
