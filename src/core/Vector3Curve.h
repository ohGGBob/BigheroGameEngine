#pragma once
#include "FloatCurve.h"
#include "CurveKey.h"

namespace bighero {

// Vector3Curve: a component-wise 3D vector animation curve built from three
// FloatCurve channels (x/y/z), with evaluation and lerp helpers.
// Self-contained, std-lib only.
class Vector3Curve {
public:
    void AddKey(float time, float vx, float vy, float vz) {
        x_.AddKey(CurveKey(time, vx));
        y_.AddKey(CurveKey(time, vy));
        z_.AddKey(CurveKey(time, vz));
    }
    void Clear() { x_.Clear(); y_.Clear(); z_.Clear(); }
    size_t KeyCount() const { return x_.KeyCount(); }
    bool IsEmpty() const { return x_.KeyCount() == 0; }

    // Evaluate the curve at time t, writing each component.
    void Evaluate(float t, float& vx, float& vy, float& vz) const {
        vx = x_.Evaluate(t);
        vy = y_.Evaluate(t);
        vz = z_.Evaluate(t);
    }
    // Component-wise lerp between two evaluated vectors.
    static void Lerp(float ax, float ay, float az,
                     float bx, float by, float bz, float t,
                     float& ox, float& oy, float& oz) {
        ox = ax + (bx - ax) * t;
        oy = ay + (by - ay) * t;
        oz = az + (bz - az) * t;
    }

private:
    FloatCurve x_, y_, z_;
};

} // namespace bighero
