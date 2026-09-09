#pragma once
#include <cmath>
#include <cstddef>

namespace bighero {

// RadialGradient: a radial gradient evaluator producing interpolated RGBA at a
// normalized radial distance. Standard-library only, self-contained.
class RadialGradient {
public:
    RadialGradient() {}

    void SetCenter(float cx, float cy) { cx_=cx; cy_=cy; }
    void SetRadius(float r) { radius_ = r > 1e-6f ? r : 1e-6f; }
    float Radius() const { return radius_; }

    // Define stops (0..1) -> (r,g,b,a). Keep sorted by position.
    void SetStops(const float* positions, const float* rgba, std::size_t count) {
        nStops_ = count < 64 ? count : 64;
        for (std::size_t i=0;i<nStops_;++i) {
            pos_[i]=positions[i]; 
            for (int c=0;c<4;++c) col_[i][c]=rgba[i*4+c];
        }
        if (nStops_ > 0 && pos_[0] > 0) {} // leave
    }

    // Evaluate at (x,y); returns (r,g,b,a) in out[4].
    void Sample(float x, float y, float out[4]) const {
        float dx = x - cx_, dy = y - cy_;
        float t = std::sqrt(dx*dx + dy*dy) / radius_;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        if (nStops_ == 0) {
            out[0]=out[1]=out[2]=out[3]=0;
            return;
        }
        if (t <= pos_[0]) {
            for (int c=0;c<4;++c) out[c]=col_[0][c];
            return;
        }
        if (t >= pos_[nStops_-1]) {
            for (int c=0;c<4;++c) out[c]=col_[nStops_-1][c];
            return;
        }
        for (std::size_t i=0;i+1<nStops_;++i) {
            if (t >= pos_[i] && t <= pos_[i+1]) {
                float range = pos_[i+1]-pos_[i];
                float f = range > 0 ? (t-pos_[i])/range : 0.0f;
                for (int c=0;c<4;++c) out[c]=col_[i][c]+f*(col_[i+1][c]-col_[i][c]);
                return;
            }
        }
        for (int c=0;c<4;++c) out[c]=col_[nStops_-1][c];
    }

private:
    float cx_=0, cy_=0, radius_=1.0f;
    std::size_t nStops_=0;
    float pos_[64]{};
    float col_[64][4]{};
};

} // namespace bighero
