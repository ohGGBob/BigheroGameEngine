#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// VersionInfo: holds engine/product version as semantic version parts plus a
// build tag. Self-contained, std-lib only.
class VersionInfo {
public:
    VersionInfo() = default;
    VersionInfo(int major, int minor, int patch)
        : major_(major), minor_(minor), patch_(patch) {}

    void Set(int major, int minor, int patch, const char* tag = nullptr) {
        major_ = major; minor_ = minor; patch_ = patch;
        tag_ = tag ? tag : "";
    }
    int Major() const { return major_; }
    int Minor() const { return minor_; }
    int Patch() const { return patch_; }
    const std::string& BuildTag() const { return tag_; }
    void SetBuildTag(const char* tag) { tag_ = tag ? tag : ""; }

    std::string ToString() const {
        std::string s = std::to_string(major_) + "." + std::to_string(minor_) + "." + std::to_string(patch_);
        if (!tag_.empty()) { s += "-"; s += tag_; }
        return s;
    }

    bool IsNewerThan(const VersionInfo& o) const {
        if (major_ != o.major_) return major_ > o.major_;
        if (minor_ != o.minor_) return minor_ > o.minor_;
        return patch_ > o.patch_;
    }
    bool operator==(const VersionInfo& o) const {
        return major_ == o.major_ && minor_ == o.minor_ && patch_ == o.patch_;
    }
    bool operator!=(const VersionInfo& o) const { return !(*this == o); }

    static VersionInfo Parse(const char* str) {
        VersionInfo v;
        if (!str) return v;
        int m = 0, mi = 0, p = 0;
        // Parse "MAJOR.MINOR.PATCH[-tag]"
        const char* c = str;
        auto readNum = [&]() -> int {
            int n = 0;
            while (*c >= '0' && *c <= '9') { n = n*10 + (*c - '0'); ++c; }
            return n;
        };
        m = readNum();
        if (*c == '.') { ++c; mi = readNum(); }
        if (*c == '.') { ++c; p = readNum(); }
        std::string tag;
        if (*c == '-') { ++c; while (*c) { tag += *c++; } }
        else { while (*c) ++c; }
        v.Set(m, mi, p, tag.empty() ? nullptr : tag.c_str());
        return v;
    }

private:
    int major_ = 0, minor_ = 0, patch_ = 0;
    std::string tag_;
};

} // namespace bighero
