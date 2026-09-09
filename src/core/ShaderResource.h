#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// ShaderResource: describes a shader module, its entry point and stage.
// Self-contained / std-lib only.
class ShaderResource {
public:
    enum class Stage : uint8_t { Vertex=0, Fragment=1, Compute=2, Geometry=3, TessControl=4, TessEval=5 };

    ShaderResource() = default;
    ShaderResource(std::string name, Stage stage) : name_(std::move(name)), stage_(stage) {}

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetStage(Stage s) { stage_ = s; }
    Stage GetStage() const { return stage_; }
    void SetEntryPoint(std::string e) { entry_ = std::move(e); }
    const std::string& EntryPoint() const { return entry_; }
    void SetPath(std::string p) { path_ = std::move(p); }
    const std::string& Path() const { return path_; }

    void AddDefine(const std::string& d) { defines_.push_back(d); }
    const std::vector<std::string>& Defines() const { return defines_; }

    bool IsValid() const { return !name_.empty(); }
    bool IsCompute() const { return stage_ == Stage::Compute; }
    static const char* StageName(Stage s) {
        switch (s) {
            case Stage::Vertex: return "Vertex"; case Stage::Fragment: return "Fragment";
            case Stage::Compute: return "Compute"; case Stage::Geometry: return "Geometry";
            case Stage::TessControl: return "TessControl"; case Stage::TessEval: return "TessEval";
        }
        return "Unknown";
    }

private:
    std::string name_, entry_ = "main", path_;
    Stage stage_ = Stage::Vertex;
    std::vector<std::string> defines_;
};

} // namespace bighero
