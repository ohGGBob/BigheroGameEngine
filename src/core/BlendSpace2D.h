#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// BlendSpace2D: a 2-D blend space mapping a (x,y) parameter pair to up to
// three neighbouring animation clips via barycentric weights. Standard-library
// only, self-contained.
class BlendSpace2D {
public:
    struct Clip { float x, y; float weight; unsigned id; };

    BlendSpace2D() {}

    void Clear() { clips_.clear(); }
    void AddClip(float x, float y, unsigned id) { clips_.push_back({x, y, 0, id}); }
    std::size_t ClipCount() const { return clips_.size(); }

    // Compute weights for a point using the nearest triangle (barycentric)
    // among the clip anchors. If fewer than 3 clips, falls back to inverse-
    // distance weighting.
    void Sample(float x, float y, std::vector<unsigned>& ids,
                std::vector<float>& weights) const {
        ids.clear(); weights.clear();
        std::size_t n = clips_.size();
        if (n == 0) return;
        if (n == 1) { ids.push_back(clips_[0].id); weights.push_back(1.0f); return; }
        if (n == 2) {
            // Linear blend between the two, projected on the line.
            float x0=clips_[0].x, y0=clips_[0].y, x1=clips_[1].x, y1=clips_[1].y;
            float dx=x1-x0, dy=y1-y0;
            float len2 = dx*dx+dy*dy;
            float t = len2 > 1e-9f ? ((x-x0)*dx + (y-y0)*dy) / len2 : 0.5f;
            if (t<0)t=0;
            if(t>1)t=1;
            ids.push_back(clips_[0].id); weights.push_back(1.0f-t);
            ids.push_back(clips_[1].id); weights.push_back(t);
            return;
        }
        // Inverse-distance weighting to 3 nearest anchors.
        // Find 3 nearest.
        std::size_t best[3] = {0,0,0};
        float bestD[3] = {1e30f,1e30f,1e30f};
        for (std::size_t i=0;i<n;++i) {
            float ddx=clips_[i].x-x, ddy=clips_[i].y-y;
            float d = ddx*ddx+ddy*ddy;
            if (d < bestD[0]) { bestD[2]=bestD[1]; best[2]=best[1]; bestD[1]=bestD[0]; best[1]=best[0]; bestD[0]=d; best[0]=i; }
            else if (d < bestD[1]) { bestD[2]=bestD[1]; best[2]=best[1]; bestD[1]=d; best[1]=i; }
            else if (d < bestD[2]) { bestD[2]=d; best[2]=i; }
        }
        float wsum=0; float w[3]={0,0,0};
        for (int k=0;k<3;++k) {
            float d=std::sqrt(bestD[k]);
            float inv = d>1e-6f ? 1.0f/d : 1e6f;
            w[k]=inv; wsum+=inv;
        }
        for (int k=0;k<3;++k) {
            ids.push_back(clips_[best[k]].id);
            weights.push_back(w[k]/wsum);
        }
    }

private:
    std::vector<Clip> clips_;
};

} // namespace bighero
