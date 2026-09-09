#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// TrailRenderer: records a series of trail points (world positions) as a
// moving object passes, so the backend can render a fading ribbon. Tracks
// a fixed-length ring buffer of points. Pure data container.
class TrailRenderer {
public:
    struct Point { float x, y, z; float life; };

    explicit TrailRenderer(std::size_t capacity = 128)
        : capacity_(capacity < 2 ? 2 : capacity) {
        buf_.resize(capacity_);
    }

    void Resize(std::size_t n) {
        capacity_ = n < 2 ? 2 : n;
        buf_.resize(capacity_);
    }
    std::size_t Capacity() const { return capacity_; }

    void AddPoint(float x, float y, float z, float life = 1.0f) {
        buf_[head_] = {x, y, z, life};
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) ++count_;
        // advance tail when ring wraps
        if (head_ == tail_ && count_ == capacity_) tail_ = (tail_ + 1) % capacity_;
        // simpler: if buffer full, advance tail
        if (count_ == capacity_) tail_ = (tail_ + 1) % capacity_;
    }

    // Decay all point lives by dt, dropping expired ones (advance tail).
    void Update(float dt) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            Point& p = buf_[i];
            if (p.life > 0) p.life -= dt;
        }
        while (count_ > 0 && buf_[tail_].life <= 0) {
            tail_ = (tail_ + 1) % capacity_;
            --count_;
        }
    }

    std::size_t Count() const { return count_; }
    // Get point `i` (0 = oldest) in ring order.
    bool Get(std::size_t i, float& x, float& y, float& z) const {
        if (i >= count_) return false;
        std::size_t idx = (tail_ + i) % capacity_;
        x = buf_[idx].x; y = buf_[idx].y; z = buf_[idx].z;
        return true;
    }

    void SetLife(float l) { life_ = l; }
    float Life() const { return life_; }
    void SetColor(float r, float g, float b, float a = 1.0f) { r_=r; g_=g; b_=b; a_=a; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void Clear() { count_ = 0; head_ = 0; tail_ = 0; for (auto& p : buf_) p.life = 0; }

private:
    std::vector<Point> buf_;
    std::size_t capacity_;
    std::size_t head_ = 0, tail_ = 0, count_ = 0;
    float life_ = 3.0f;
    float r_=1, g_=1, b_=1, a_=1;
    bool enabled_ = true;
};

} // namespace bighero
