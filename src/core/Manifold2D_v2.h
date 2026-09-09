#pragma once
#include <cstdint>
#include <vector>
#include "Contact2D_v2.h"

namespace bighero {

// Manifold2D: a collision manifold holding one or more Contact2D points for a
// pair of colliding bodies. Self-contained, std-lib only.
class Manifold2D {
public:
    Manifold2D() = default;

    void SetBodyA(int id) { bodyA_ = id; }
    void SetBodyB(int id) { bodyB_ = id; }
    int BodyA() const { return bodyA_; }
    int BodyB() const { return bodyB_; }

    size_t ContactCount() const { return contacts_.size(); }
    const Contact2D& GetContact(size_t i) const { return contacts_[i]; }
    Contact2D& GetContact(size_t i) { return contacts_[i]; }

    void AddContact(const Contact2D& c) { contacts_.push_back(c); }
    void Clear() { contacts_.clear(); }
    void Reset() { contacts_.clear(); bodyA_=bodyB_=-1; }

    bool IsColliding() const { 
        for (const auto& c : contacts_) if (c.Penetration() > 0) return true;
        return !contacts_.empty();
    }

    // Deepest penetration among contacts.
    float MaxPenetration() const {
        float m = 0;
        for (const auto& c : contacts_) if (c.Penetration() > m) m = c.Penetration();
        return m;
    }

    // Total normal impulse accumulated.
    float TotalNormalImpulse() const {
        float s = 0;
        for (const auto& c : contacts_) s += c.NormalImpulse();
        return s;
    }

private:
    std::vector<Contact2D> contacts_;
    int bodyA_ = -1, bodyB_ = -1;
};

} // namespace bighero
