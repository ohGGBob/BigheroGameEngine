#pragma once
#include <cstdint>

namespace bighero {

// PresentMode: describes how a swapchain presents images to the display.
// Self-contained, std-lib only.
class PresentMode {
public:
    enum class Mode : uint8_t {
        Immediate = 0,      // copy immediately, may tear
        Mailbox = 1,        // always queue one present, no tearing
        Fifo = 2,           // strict queue (vsync)
        FifoRelaxed = 3     // vsync without tearing when no tearing occurs
    };

    PresentMode() = default;
    explicit PresentMode(Mode m) : mode_(m) {}

    void SetMode(Mode m) { mode_ = m; }
    Mode GetMode() const { return mode_; }

    bool SupportsTearing() const { return mode_ == Mode::Immediate; }
    bool IsVsync() const { return mode_ == Mode::Fifo || mode_ == Mode::FifoRelaxed; }
    bool IsLowLatency() const { return mode_ == Mode::Immediate || mode_ == Mode::Mailbox; }
    bool IsTripleBuffered() const { return mode_ == Mode::Mailbox; }

    static const char* Name(Mode m) {
        switch (m) {
            case Mode::Immediate: return "Immediate";
            case Mode::Mailbox: return "Mailbox";
            case Mode::Fifo: return "Fifo";
            case Mode::FifoRelaxed: return "FifoRelaxed";
        }
        return "Unknown";
    }

private:
    Mode mode_ = Mode::Fifo;
};

} // namespace bighero
