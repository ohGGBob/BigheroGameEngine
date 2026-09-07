#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// Simple damped spring between two point masses in 2D.
struct SpringJoint {
    float ax, ay, bx, by;
    float restLength;
    float stiffness;
    float damping; // 0..1

    SpringJoint() : ax(0), ay(0), bx(1), by(0), restLength(1.0f), stiffness(0.5f), damping(0.1f) {}
    SpringJoint(float ax_, float ay_, float bx_, float by_, float rest,
                float k, float d)
        : ax(ax_), ay(ay_), bx(bx_), by(by_), restLength(rest), stiffness(k), damping(d) {}

    // Apply spring force to both endpoints given inverse masses (0 = immovable).
    // Returns the current length.
    float Apply(float invMassA, float invMassB) {
        float dx = bx - ax, dy = by - ay;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-10f) return 0.0f;
        float ux = dx / dist, uy = dy / dist;
        float relV = 0.0f; // relative velocity projection (simple)
        float force = stiffness * (dist - restLength) + damping * relV;
        float fx = force * ux, fy = force * uy;
        ax += fx * invMassA; ay += fy * invMassA;
        bx -= fx * invMassB; by -= fy * invMassB;
        return dist;
    }

    float CurrentLength() const {
        return std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    }
};

} // namespace bighero
