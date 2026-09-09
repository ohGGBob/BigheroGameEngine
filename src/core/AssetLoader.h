#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace bighero {

// AssetLoader: a lightweight synchronous loader shim with typed callbacks.
// Self-contained / std-lib only. Storage backend is intentionally abstracted;
// the caller supplies a read callback returning raw bytes.
class AssetLoader {
public:
    using ReadFn = std::function<bool(const std::string& path, std::vector<uint8_t>& outBytes)>;
    using LoadCallback = std::function<void(const std::string& path, bool ok)>;

    void SetReader(ReadFn fn) { reader_ = std::move(fn); }
    bool HasReader() const { return static_cast<bool>(reader_); }

    // Attempt to load raw bytes for a path via the reader. Returns false if no
    // reader is set or the reader reports failure.
    bool LoadRaw(const std::string& path, std::vector<uint8_t>& outBytes) const {
        if (!reader_) return false;
        return reader_(path, outBytes);
    }

    bool LoadText(const std::string& path, std::string& outText) const {
        std::vector<uint8_t> bytes;
        if (!LoadRaw(path, bytes)) return false;
        outText.assign(bytes.begin(), bytes.end());
        return true;
    }

    void SetOnLoad(LoadCallback cb) { onLoad_ = std::move(cb); }
    void NotifyLoaded(const std::string& path, bool ok) const {
        if (onLoad_) onLoad_(path, ok);
    }

    void SetBasePath(std::string p) { basePath_ = std::move(p); }
    const std::string& BasePath() const { return basePath_; }
    std::string Resolve(const std::string& rel) const {
        if (basePath_.empty()) return rel;
        if (basePath_.back() == '/' || basePath_.back() == '\\') return basePath_ + rel;
        return basePath_ + "/" + rel;
    }

private:
    ReadFn reader_;
    LoadCallback onLoad_;
    std::string basePath_;
};

} // namespace bighero
