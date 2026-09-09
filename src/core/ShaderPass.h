#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// ShaderPass: describes a single programmable shader stage/pass for a
// rendering pipeline. Self-contained, std-lib only.
class ShaderPass {
public:
    enum class Stage : int { Vertex = 0, Fragment = 1, Geometry = 2, Compute = 3, TessControl = 4, TessEval = 5 };

    ShaderPass() = default;
    ShaderPass(Stage stage, std::string source, std::string entry = "main")
        : stage_(stage), source_(std::move(source)), entry_(std::move(entry)) {}

    void SetStage(Stage s) { stage_ = s; }
    Stage GetStage() const { return stage_; }
    void SetSource(std::string src) { source_ = std::move(src); }
    const std::string& Source() const { return source_; }
    void SetEntry(std::string e) { entry_ = std::move(e); }
    const std::string& Entry() const { return entry_; }

    void AddDefine(const std::string& key, const std::string& value = "1") {
        defines_.push_back(key + "=" + value);
    }
    const std::vector<std::string>& Defines() const { return defines_; }
    bool IsValid() const { return !source_.empty() && !entry_.empty(); }
    size_t SourceSize() const { return source_.size(); }

    static const char* StageName(Stage s) {
        switch (s) {
            case Stage::Vertex: return "Vertex";
            case Stage::Fragment: return "Fragment";
            case Stage::Geometry: return "Geometry";
            case Stage::Compute: return "Compute";
            case Stage::TessControl: return "TessControl";
            case Stage::TessEval: return "TessEval";
        }
        return "Unknown";
    }

private:
    Stage stage_ = Stage::Vertex;
    std::string source_;
    std::string entry_ = "main";
    std::vector<std::string> defines_;
};

} // namespace bighero
