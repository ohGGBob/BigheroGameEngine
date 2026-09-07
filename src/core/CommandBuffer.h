#pragma once
#include <vector>
#include <functional>

namespace bighero {

// CPU-side command recorder: bake draw/state commands into a buffer and
// replay them later. A building-block for a render command queue.
class CommandBuffer {
public:
    enum class Op {
        BindProgram,
        SetUniform,
        BindTexture,
        Draw,
        Present
    };

    struct Command {
        Op op;
        int a = 0, b = 0;         // opaque payload (e.g. program/ptr indices)
        float fx = 0, fy = 0, fz = 0, fw = 0;
    };

    void Clear() { cmds_.clear(); }
    bool Empty() const { return cmds_.empty(); }
    std::size_t Size() const { return cmds_.size(); }

    void BindProgram(int program) { cmds_.push_back({Op::BindProgram, program, 0}); }
    void SetUniform(int slot, float x, float y, float z, float w) {
        cmds_.push_back({Op::SetUniform, slot, 0, x, y, z, w});
    }
    void BindTexture(int tex) { cmds_.push_back({Op::BindTexture, tex, 0}); }
    void Draw(int vao, int count) { cmds_.push_back({Op::Draw, vao, count}); }
    void Present() { cmds_.push_back({Op::Present, 0, 0}); }

    const Command& At(std::size_t i) const { return cmds_[i]; }

    // Simple replay: invoke a callback for each command.
    template <class Fn>
    void ForEach(Fn&& fn) const {
        for (const auto& c : cmds_) fn(c);
    }

private:
    std::vector<Command> cmds_;
};

} // namespace bighero
