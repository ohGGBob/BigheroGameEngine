#pragma once
#include <cstdint>

namespace bighero {

// OcclusionQuery: represents a single GPU occlusion query that will hold the
// number of visible pixels/samples rendered for a draw. The backend records
// the query start/stop and later reads the result. Pure CPU-side descriptor.
class OcclusionQuery {
public:
    enum class State { Idle, Pending, Ready };

    OcclusionQuery() {}
    explicit OcclusionQuery(std::uint64_t id) : id_(id) {}

    void SetId(std::uint64_t id) { id_ = id; }
    std::uint64_t Id() const { return id_; }
    void Allocate(std::uint64_t id) { id_ = id; state_ = State::Idle; }

    void Begin() { state_ = State::Pending; }
    void End() { state_ = State::Pending; }
    bool IsPending() const { return state_ == State::Pending; }

    // Backend reports the query result when ready.
    void SetResult(std::uint64_t passCount) {
        result_ = passCount; state_ = State::Ready;
    }
    bool IsReady() const { return state_ == State::Ready; }
    std::uint64_t Result() const { return result_; }
    bool Visible() const { return state_ == State::Ready && result_ > 0; }

    void Reset() { state_ = State::Idle; result_ = 0; }

private:
    std::uint64_t id_ = 0;
    State state_ = State::Idle;
    std::uint64_t result_ = 0;
};

} // namespace bighero
