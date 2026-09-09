#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// PhysicsScene: a CPU-side registry of rigid bodies and colliders plus the
// contact list produced by the last physics step. Self-contained aggregation
// container used by tests and the solver backend.
class PhysicsScene {
public:
    struct BodyRecord {
        std::size_t id;
        float mass;
    };
    struct ColliderRecord {
        std::size_t id;
        std::size_t bodyId;
        bool trigger;
    };
    struct ContactRecord {
        std::size_t a, b;
        float depth;
    };

    PhysicsScene() {}

    void SetGravity(float g) { gravity_ = g; }
    float Gravity() const { return gravity_; }
    void SetSolverIterations(int n) { iterations_ = n < 1 ? 1 : n; }
    int SolverIterations() const { return iterations_; }

    std::size_t AddBody(std::size_t id, float mass) {
        bodies_.push_back({id, mass});
        return bodies_.size() - 1;
    }
    std::size_t BodyCount() const { return bodies_.size(); }
    bool GetBody(std::size_t i, BodyRecord& out) const {
        if (i >= bodies_.size()) return false;
        out = bodies_[i]; return true;
    }

    std::size_t AddCollider(std::size_t id, std::size_t bodyId, bool trigger) {
        colliders_.push_back({id, bodyId, trigger});
        return colliders_.size() - 1;
    }
    std::size_t ColliderCount() const { return colliders_.size(); }

    // Clear and rebuild the contact list after a step.
    void BeginContacts() { contacts_.clear(); }
    void AddContact(std::size_t a, std::size_t b, float depth) {
        contacts_.push_back({a, b, depth});
    }
    std::size_t ContactCount() const { return contacts_.size(); }
    bool GetContact(std::size_t i, ContactRecord& out) const {
        if (i >= contacts_.size()) return false;
        out = contacts_[i]; return true;
    }

    void Clear() { bodies_.clear(); colliders_.clear(); contacts_.clear(); }
    std::size_t TotalObjects() const { return bodies_.size() + colliders_.size(); }

private:
    float gravity_ = -9.81f;
    int iterations_ = 4;
    std::vector<BodyRecord> bodies_;
    std::vector<ColliderRecord> colliders_;
    std::vector<ContactRecord> contacts_;
};

} // namespace bighero
