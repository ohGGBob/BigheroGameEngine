#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// ShaderModule: a compiled shader module (SPIR-V / bytecode) with an entry
// point and one or more shader stage bindings. Self-contained.
class ShaderModule {
public:
    enum class Stage : uint8_t { Vertex = 0, Fragment = 1, Compute = 2, Geometry = 3 };

    ShaderModule() = default;
    ShaderModule(std::string name, Stage stage, std::vector<uint32_t> bytecode)
        : name_(std::move(name)), stage_(stage), bytecode_(std::move(bytecode)) {}

    const std::string& Name() const { return name_; }
    void SetName(std::string n) { name_ = std::move(n); }
    void SetStage(Stage s) { stage_ = s; }
    Stage GetStage() const { return stage_; }

    void SetBytecode(std::vector<uint32_t> code) { bytecode_ = std::move(code); }
    const std::vector<uint32_t>& Bytecode() const { return bytecode_; }
    size_t WordCount() const { return bytecode_.size(); }
    size_t SizeBytes() const { return bytecode_.size() * sizeof(uint32_t); }

    void SetEntryPoint(std::string e) { entry_ = std::move(e); }
    const std::string& EntryPoint() const { return entry_; }

    bool IsValid() const { return !bytecode_.empty() && !entry_.empty(); }
    static const char* StageName(Stage s) {
        switch (s) {
            case Stage::Vertex: return "Vertex";
            case Stage::Fragment: return "Fragment";
            case Stage::Compute: return "Compute";
            case Stage::Geometry: return "Geometry";
        }
        return "Unknown";
    }

private:
    std::string name_;
    Stage stage_ = Stage::Vertex;
    std::vector<uint32_t> bytecode_;
    std::string entry_ = "main";
};

} // namespace bighero
