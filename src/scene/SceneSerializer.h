#pragma once
// 场景序列化器：将运行时场景状态（物体/光照/点光源/相机）序列化为 JSON 文本，
// 支持保存到文件与从文件加载。纯 CPU、仅依赖 glm + 标准库，可离线单元测试。
//
// 设计原则：
//   - 数据结构与运行时类型解耦：SerializableLight / SerializablePointLight 为纯数据，
//     Application 层负责与 LightParams / PointLightParams 互转，避免 scene 依赖 editor。
//   - JSON 为手写最小实现（writer + recursive-descent reader），不引入外部库。
//   - 版本号字段保证前向兼容：加载时校验 version，未知字段可忽略。

#include "Scene.h"

#include <cstdint>
#include <fstream>
#include <glm/glm.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace BigHero::Scene
{
// ---- 可序列化数据结构 ----

struct SerializableLight
{
    glm::vec3 direction{0.5f, -1.0f, -0.35f};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity = 3.0f;
    float ambient = 0.15f;
    float shadowStrength = 1.0f;
    float shadowBias = 0.0022f;
    float iblStrength = 1.0f;
    float exposure = 1.0f;
};

struct SerializablePointLight
{
    glm::vec3 position{0.0f, 2.5f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 30.0f;
    float radius = 9.0f;
    bool castsShadow = false;
};

struct SceneData
{
    uint32_t version = 1;
    float cameraFov = 60.0f;
    SerializableLight light;
    std::vector<SerializablePointLight> pointLights;
    std::vector<SceneObject> objects;
};

// ---- 最小 JSON 写入器 ----

namespace detail
{
inline void WriteIndent(std::string& out, int depth)
{
    for (int i = 0; i < depth; ++i)
        out += "  ";
}

inline void WriteString(std::string& out, const std::string& s)
{
    out += '"';
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    out += '"';
}

inline void WriteFloat(std::string& out, float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    out += buf;
}

inline void WriteVec3(std::string& out, const glm::vec3& v)
{
    out += '[';
    WriteFloat(out, v.x);
    out += ", ";
    WriteFloat(out, v.y);
    out += ", ";
    WriteFloat(out, v.z);
    out += ']';
}

inline void WriteBool(std::string& out, bool b)
{
    out += b ? "true" : "false";
}
} // namespace detail

// ---- 序列化 ----

inline std::string SerializeScene(const SceneData& data)
{
    using namespace detail;
    std::string out;
    out += "{\n";
    WriteIndent(out, 1);
    out += "\"version\": " + std::to_string(data.version) + ",\n";

    // camera
    WriteIndent(out, 1);
    out += "\"camera\": { \"fov\": ";
    WriteFloat(out, data.cameraFov);
    out += " },\n";

    // light
    WriteIndent(out, 1);
    out += "\"light\": {\n";
    WriteIndent(out, 2);
    out += "\"direction\": ";
    WriteVec3(out, data.light.direction);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"color\": ";
    WriteVec3(out, data.light.color);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"intensity\": ";
    WriteFloat(out, data.light.intensity);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"ambient\": ";
    WriteFloat(out, data.light.ambient);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"shadowStrength\": ";
    WriteFloat(out, data.light.shadowStrength);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"shadowBias\": ";
    WriteFloat(out, data.light.shadowBias);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"iblStrength\": ";
    WriteFloat(out, data.light.iblStrength);
    out += ",\n";
    WriteIndent(out, 2);
    out += "\"exposure\": ";
    WriteFloat(out, data.light.exposure);
    out += "\n";
    WriteIndent(out, 1);
    out += "},\n";

    // pointLights
    WriteIndent(out, 1);
    out += "\"pointLights\": [";
    if (!data.pointLights.empty())
    {
        out += "\n";
        for (size_t i = 0; i < data.pointLights.size(); ++i)
        {
            const auto& pl = data.pointLights[i];
            WriteIndent(out, 2);
            out += "{ \"position\": ";
            WriteVec3(out, pl.position);
            out += ", \"color\": ";
            WriteVec3(out, pl.color);
            out += ", \"intensity\": ";
            WriteFloat(out, pl.intensity);
            out += ", \"radius\": ";
            WriteFloat(out, pl.radius);
            out += ", \"castsShadow\": ";
            WriteBool(out, pl.castsShadow);
            out += " }";
            if (i + 1 < data.pointLights.size())
                out += ",";
            out += "\n";
        }
        WriteIndent(out, 1);
    }
    out += "],\n";

    // objects
    WriteIndent(out, 1);
    out += "\"objects\": [";
    if (!data.objects.empty())
    {
        out += "\n";
        for (size_t i = 0; i < data.objects.size(); ++i)
        {
            const auto& obj = data.objects[i];
            WriteIndent(out, 2);
            out += "{ \"position\": ";
            WriteVec3(out, obj.position);
            out += ", \"scale\": ";
            WriteFloat(out, obj.scale);
            out += ", \"tint\": ";
            WriteVec3(out, obj.tint);
            out += ", \"spinSpeed\": ";
            WriteFloat(out, obj.spinSpeed);
            out += ", \"phase\": ";
            WriteFloat(out, obj.phase);
            out += ", \"meshId\": " + std::to_string(obj.meshId);
            out += ", \"metallic\": ";
            WriteFloat(out, obj.metallic);
            out += ", \"roughness\": ";
            WriteFloat(out, obj.roughness);
            out += ", \"rotation\": ";
            WriteVec3(out, obj.rotation);
            out += " }";
            if (i + 1 < data.objects.size())
                out += ",";
            out += "\n";
        }
        WriteIndent(out, 1);
    }
    out += "]\n";

    out += "}\n";
    return out;
}

// ---- 最小 JSON 读取器（recursive descent） ----

namespace detail
{
class JsonReader
{
  public:
    explicit JsonReader(const std::string& text) : s_(text) {}

    void SkipWS()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    [[nodiscard]] bool Peek(char c)
    {
        SkipWS();
        return pos_ < s_.size() && s_[pos_] == c;
    }

    bool Expect(char c)
    {
        SkipWS();
        if (pos_ >= s_.size() || s_[pos_] != c)
            throw std::runtime_error(std::string("JSON: expected '") + c + "' at pos " + std::to_string(pos_));
        ++pos_;
        return true;
    }

    std::string ReadString()
    {
        Expect('"');
        std::string result;
        while (pos_ < s_.size() && s_[pos_] != '"')
        {
            if (s_[pos_] == '\\' && pos_ + 1 < s_.size())
            {
                ++pos_;
                switch (s_[pos_])
                {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += s_[pos_];
                }
            }
            else
            {
                result += s_[pos_];
            }
            ++pos_;
        }
        Expect('"');
        return result;
    }

    float ReadFloat()
    {
        SkipWS();
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+'))
            ++pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                    s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '-' || s_[pos_] == '+'))
            ++pos_;
        if (pos_ == start)
            throw std::runtime_error("JSON: expected number at pos " + std::to_string(pos_));
        return std::stof(s_.substr(start, pos_ - start));
    }

    bool ReadBool()
    {
        SkipWS();
        if (pos_ + 4 <= s_.size() && s_.compare(pos_, 4, "true") == 0)
        {
            pos_ += 4;
            return true;
        }
        if (pos_ + 5 <= s_.size() && s_.compare(pos_, 5, "false") == 0)
        {
            pos_ += 5;
            return false;
        }
        throw std::runtime_error("JSON: expected bool at pos " + std::to_string(pos_));
    }

    glm::vec3 ReadVec3()
    {
        Expect('[');
        glm::vec3 v;
        v.x = ReadFloat();
        Expect(',');
        v.y = ReadFloat();
        Expect(',');
        v.z = ReadFloat();
        Expect(']');
        return v;
    }

    // 读取一个对象的所有键值对，对每个 key 调用 handler(key)，handler 内部读取 value
    template<typename Handler> void ReadObject(Handler&& handler)
    {
        Expect('{');
        SkipWS();
        if (Peek('}'))
        {
            Expect('}');
            return;
        }
        while (true)
        {
            std::string key = ReadString();
            Expect(':');
            handler(key);
            SkipWS();
            if (Peek(','))
            {
                Expect(',');
                continue;
            }
            break;
        }
        Expect('}');
    }

    template<typename ElementHandler> void ReadArray(ElementHandler&& handler)
    {
        Expect('[');
        SkipWS();
        if (Peek(']'))
        {
            Expect(']');
            return;
        }
        while (true)
        {
            handler();
            SkipWS();
            if (Peek(','))
            {
                Expect(',');
                continue;
            }
            break;
        }
        Expect(']');
    }

  private:
    const std::string& s_;
    size_t pos_ = 0;
};
} // namespace detail

// ---- 反序列化 ----

inline bool DeserializeScene(const std::string& text, SceneData& out)
{
    try
    {
        detail::JsonReader r(text);
        r.ReadObject(
            [&](const std::string& key)
            {
                if (key == "version")
                    out.version = static_cast<uint32_t>(r.ReadFloat());
                else if (key == "camera")
                    r.ReadObject(
                        [&](const std::string& k)
                        {
                            if (k == "fov")
                                out.cameraFov = r.ReadFloat();
                            else
                                r.ReadFloat(); // 忽略未知字段
                        });
                else if (key == "light")
                    r.ReadObject(
                        [&](const std::string& k)
                        {
                            if (k == "direction")
                                out.light.direction = r.ReadVec3();
                            else if (k == "color")
                                out.light.color = r.ReadVec3();
                            else if (k == "intensity")
                                out.light.intensity = r.ReadFloat();
                            else if (k == "ambient")
                                out.light.ambient = r.ReadFloat();
                            else if (k == "shadowStrength")
                                out.light.shadowStrength = r.ReadFloat();
                            else if (k == "shadowBias")
                                out.light.shadowBias = r.ReadFloat();
                            else if (k == "iblStrength")
                                out.light.iblStrength = r.ReadFloat();
                            else if (k == "exposure")
                                out.light.exposure = r.ReadFloat();
                            else
                                r.ReadFloat();
                        });
                else if (key == "pointLights")
                    r.ReadArray(
                        [&]()
                        {
                            SerializablePointLight pl;
                            r.ReadObject(
                                [&](const std::string& k)
                                {
                                    if (k == "position")
                                        pl.position = r.ReadVec3();
                                    else if (k == "color")
                                        pl.color = r.ReadVec3();
                                    else if (k == "intensity")
                                        pl.intensity = r.ReadFloat();
                                    else if (k == "radius")
                                        pl.radius = r.ReadFloat();
                                    else if (k == "castsShadow")
                                        pl.castsShadow = r.ReadBool();
                                    else
                                        r.ReadFloat();
                                });
                            out.pointLights.push_back(pl);
                        });
                else if (key == "objects")
                    r.ReadArray(
                        [&]()
                        {
                            SceneObject obj;
                            r.ReadObject(
                                [&](const std::string& k)
                                {
                                    if (k == "position")
                                        obj.position = r.ReadVec3();
                                    else if (k == "scale")
                                        obj.scale = r.ReadFloat();
                                    else if (k == "tint")
                                        obj.tint = r.ReadVec3();
                                    else if (k == "spinSpeed")
                                        obj.spinSpeed = r.ReadFloat();
                                    else if (k == "phase")
                                        obj.phase = r.ReadFloat();
                                    else if (k == "meshId")
                                        obj.meshId = static_cast<uint32_t>(r.ReadFloat());
                                    else if (k == "metallic")
                                        obj.metallic = r.ReadFloat();
                                    else if (k == "roughness")
                                        obj.roughness = r.ReadFloat();
                                    else if (k == "rotation")
                                        obj.rotation = r.ReadVec3();
                                    else
                                        r.ReadFloat();
                                });
                            out.objects.push_back(obj);
                        });
                else
                    r.ReadFloat(); // 忽略未知顶层字段
            });
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// ---- 文件 I/O ----

inline bool SaveSceneToFile(const SceneData& data, const std::string& path)
{
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open())
        return false;
    ofs << SerializeScene(data);
    return ofs.good();
}

inline bool LoadSceneFromFile(const std::string& path, SceneData& out)
{
    std::ifstream ifs(path, std::ios::in);
    if (!ifs.is_open())
        return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return DeserializeScene(ss.str(), out);
}
} // namespace BigHero::Scene
