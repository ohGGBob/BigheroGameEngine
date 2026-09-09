#pragma once
#include <string>
#include <cstddef>

namespace bighero {

// System base: a phase-ordered update hook over the entities of scene.
// Provides lifecycle callbacks that a systems pipeline calls each frame.
class SystemBase {
public:
    enum class Phase { Early, Update, Late };

    SystemBase() {}
    virtual ~SystemBase() = default;

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }
    void SetPhase(Phase p) { phase_ = p; }
    Phase PhaseOrder() const { return phase_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Called once when the system is added to the world.
    virtual void OnStart() {}
    // Called each frame; `dt` is delta time in seconds.
    virtual void Update(float dt) { (void)dt; }
    // Called when the system is removed.
    virtual void OnDestroy() {}

    virtual const char* TypeName() const { return "SystemBase"; }

private:
    std::string name_;
    Phase phase_ = Phase::Update;
    bool enabled_ = true;
};

} // namespace bighero
