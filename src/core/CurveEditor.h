#pragma once
#include "CurveKey.h"
#include <vector>

namespace bighero {

// CurveEditor: an editable keyframe container used by animation tooling to
// add/remove/modify CurveKey points before baking. Self-contained, std-lib.
class CurveEditor {
public:
    CurveEditor() = default;

    void AddKey(const CurveKey& k) { keys_.push_back(k); }
    void AddKey(float time, float value) { keys_.push_back(CurveKey(time, value)); }
    void RemoveKeyAt(size_t i) {
        if (i < keys_.size()) keys_.erase(keys_.begin() + i);
    }
    void RemoveKeyByTime(float time) {
        for (size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i].time == time) { keys_.erase(keys_.begin() + i); return; }
    }
    void Clear() { keys_.clear(); }
    size_t Count() const { return keys_.size(); }
    const CurveKey& At(size_t i) const {
        static CurveKey dummy;
        return i < keys_.size() ? keys_[i] : dummy;
    }
    CurveKey& MutableAt(size_t i) {
        static CurveKey dummy;
        return i < keys_.size() ? keys_[i] : dummy;
    }
    // Reorder keys by ascending time (stable-sorted by time).
    void SortByTime() {
        for (size_t i = 0; i < keys_.size(); ++i)
            for (size_t j = i + 1; j < keys_.size(); ++j)
                if (keys_[j].time < keys_[i].time) {
                    CurveKey t = keys_[i]; keys_[i] = keys_[j]; keys_[j] = t;
                }
    }

private:
    std::vector<CurveKey> keys_;
};

} // namespace bighero
