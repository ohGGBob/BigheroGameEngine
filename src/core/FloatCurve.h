#pragma once
#include "CurveKey.h"
#include <algorithm>
#include <vector>

namespace bighero
{

// FloatCurve: a scalar animation curve built from CurveKey samples, providing
// sorted insertion, clamping, and interpolation-based evaluation.
// Self-contained, std-lib only.
class FloatCurve
{
  public:
    FloatCurve() = default;
    ~FloatCurve() = default;

    void AddKey(const CurveKey& key)
    {
        keys_.push_back(key);
        Sort();
    }
    void AddKey(float time, float value, CurveKey::Mode mode = CurveKey::Mode::Smooth)
    {
        keys_.push_back(CurveKey(time, value, mode));
        Sort();
    }
    void Clear() { keys_.clear(); }
    size_t KeyCount() const { return keys_.size(); }
    const CurveKey& GetKey(size_t i) const { return keys_[i]; }

    float Start() const { return keys_.empty() ? 0 : keys_.front().time; }
    float End() const { return keys_.empty() ? 0 : keys_.back().time; }

    // Evaluate the curve at time t.
    float Evaluate(float t) const
    {
        if (keys_.empty())
            return 0;
        if (keys_.size() == 1)
            return keys_[0].value;
        if (t <= keys_.front().time)
            return keys_.front().value;
        if (t >= keys_.back().time)
            return keys_.back().value;
        for (size_t i = 0; i + 1 < keys_.size(); ++i)
        {
            if (t >= keys_[i].time && t <= keys_[i + 1].time)
            {
                float span = keys_[i + 1].time - keys_[i].time;
                float local = span > 1e-9f ? (t - keys_[i].time) / span : 0.0f;
                return keys_[i].Evaluate(keys_[i + 1].value, local);
            }
        }
        return keys_.back().value;
    }

    // Approximate the curve's integral over [a,b] via trapezoidal sampling.
    float Integrate(float a, float b, int samples = 32) const
    {
        if (samples < 2)
            samples = 2;
        float s = 0;
        float h = (b - a) / (samples - 1);
        for (int i = 0; i < samples - 1; ++i)
        {
            float ta = a + i * h, tb = ta + h;
            s += (Evaluate(ta) + Evaluate(tb)) * 0.5f * h;
        }
        return s;
    }

  private:
    void Sort()
    {
        for (size_t i = 0; i < keys_.size(); ++i)
            for (size_t j = i + 1; j < keys_.size(); ++j)
                if (keys_[j].time < keys_[i].time)
                {
                    CurveKey t = keys_[i];
                    keys_[i] = keys_[j];
                    keys_[j] = t;
                }
    }
    std::vector<CurveKey> keys_;
};

} // namespace bighero
