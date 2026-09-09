#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// ShaderVariant: describes a compiled shader permutation — a base shader id,
// a set of enabled defines/features, a render queue, and keyword mask. The
// backend key on this to fetch the precompiled program.
class ShaderVariant {
public:
    ShaderVariant() {}
    explicit ShaderVariant(const std::string& base) : base_(base) {}
    ShaderVariant(const std::string& base, const std::vector<std::string>& keywords)
        : base_(base), keywords_(keywords) {}

    void SetBase(const std::string& b) { base_ = b; }
    const std::string& Base() const { return base_; }

    void AddKeyword(const std::string& k) {
        for (auto& existing : keywords_) if (existing == k) return;
        keywords_.push_back(k);
    }
    void RemoveKeyword(const std::string& k) {
        for (auto it = keywords_.begin(); it != keywords_.end(); ++it) {
            if (*it == k) { keywords_.erase(it); return; }
        }
    }
    bool HasKeyword(const std::string& k) const {
        for (auto& existing : keywords_) if (existing == k) return true;
        return false;
    }
    std::size_t KeywordCount() const { return keywords_.size(); }

    bool IsValid() const { return !base_.empty(); }

    // Stable key string used by the backend as a cache key.
    std::string CacheKey() const {
        std::string k = base_;
        for (auto& w : keywords_) k += "#" + w;
        return k;
    }

    // Does this variant satisfy a required set of keywords?
    bool Satisfies(const std::vector<std::string>& required) const {
        for (auto& r : required) if (!HasKeyword(r)) return false;
        return true;
    }

private:
    std::string base_;
    std::vector<std::string> keywords_;
};

} // namespace bighero
