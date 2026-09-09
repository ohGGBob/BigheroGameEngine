#pragma once
#include <cstdint>
#include <cmath>
#include "FrustumAABB_v2.h"
#include "PlaneFrustum_v2.h"

namespace bighero {

// FrustumIntersection: a convenience helper that tests objects against either
// a PlaneFrustum or an FrustumAABB and reports visibility (inside/intersect).
// Self-contained, std-lib only.
class FrustumIntersection {
public:
    enum class Result : int { Outside = 0, Intersecting = 1, Inside = 2 };

    FrustumIntersection() = default;

    Result TestBox(const PlaneFrustum& frustum, const FrustumAABB& box) const {
        if (frustum.ContainsBox(box)) return Result::Inside;
        if (frustum.IntersectsBox(box)) return Result::Intersecting;
        return Result::Outside;
    }
    Result TestBox(const FrustumAABB& view, const FrustumAABB& box) const {
        if (view.ContainsBox(box)) return Result::Inside;
        if (view.Overlaps(box)) return Result::Intersecting;
        return Result::Outside;
    }

    static const char* ToString(Result r) {
        switch (r) {
            case Result::Outside: return "Outside";
            case Result::Intersecting: return "Intersecting";
            case Result::Inside: return "Inside";
        }
        return "Unknown";
    }
};

} // namespace bighero
