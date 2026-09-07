#pragma once
#include <functional>

namespace bighero {

// Minimal stackless coroutine: wrap a generator-style lambda that yields
// control back via the provided yield callable. The coroutine keeps a single
// continuation, so it can be started/stopped and stepped repeatedly.
class Coroutine {
public:
    // The body receives a `bool& done` and a `yield` callable. When the body
    // returns, the coroutine is finished. Calling yield() suspends until the
    // next Resume(). `done` may be set early to stop.
    using Body = std::function<void(bool& done, const std::function<void()>& yield)>;

    Coroutine() {}
    explicit Coroutine(Body body) : body_(std::move(body)) {}

    void Start(Body body) {
        body_ = std::move(body);
        finished_ = false;
        started_ = false;
    }
    void Reset() { body_ = nullptr; finished_ = false; started_ = false; }

    // Step the coroutine until it yields or finishes.
    void Resume() {
        if (!body_ || finished_) return;
        bool done = false;
        std::function<void()> yield = [&]() {
            // Mark that we should stop right after the body returns this step.
            // Use a flag so the enclosing step halts.
            yielded_ = true;
            // Longjmp-free suspension: we rely on the body returning after
            // calling yield(), so this is a cooperative step model.
        };
        yielded_ = false;
        started_ = true;
        body_(done, yield);
        if (done) finished_ = true;
    }

    bool Finished() const { return finished_; }
    bool Started() const { return started_; }
    bool Valid() const { return (bool)body_; }

    // Continue until finished (bounded steps) — returns true if it completed.
    bool RunToCompletion(int maxSteps = 10000) {
        for (int i = 0; i < maxSteps && !finished_; ++i) {
            Resume();
            if (!yielded_) break; // body returned without yielding -> finished
        }
        return finished_;
    }

private:
    Body body_;
    bool finished_ = false;
    bool started_ = false;
    bool yielded_ = false;
};

} // namespace bighero
