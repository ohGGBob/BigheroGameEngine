#pragma once
#include <cstdint>
#include <string>

namespace bighero
{

// AssetMetadata: describes a single asset's identity, source, type and load
// state. Self-contained / std-lib only.
class AssetMetadata
{
  public:
    enum class AssetType : uint8_t
    {
        Texture = 0,
        Mesh = 1,
        Shader = 2,
        Material = 3,
        AnimationClip = 4,
        Audio = 5,
        Font = 6,
        Scene = 7,
        Unknown = 8
    };
    enum class LoadState : uint8_t
    {
        Unloaded = 0,
        Loading = 1,
        Loaded = 2,
        Failed = 3
    };

    AssetMetadata() = default;
    AssetMetadata(std::string path, AssetType type) : path_(std::move(path)), type_(type) {}

    void SetPath(std::string p) { path_ = std::move(p); }
    const std::string& Path() const { return path_; }
    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetType(AssetType t) { type_ = t; }
    AssetType Type() const { return type_; }
    void SetLoadState(LoadState s) { state_ = s; }
    LoadState State() const { return state_; }
    void SetSizeBytes(uint64_t s) { sizeBytes_ = s; }
    uint64_t SizeBytes() const { return sizeBytes_; }
    void SetHash(uint64_t h) { hash_ = h; }
    uint64_t Hash() const { return hash_; }

    bool IsLoaded() const { return state_ == LoadState::Loaded; }
    bool IsFailed() const { return state_ == LoadState::Failed; }
    static const char* TypeName(AssetType t)
    {
        switch (t)
        {
        case AssetType::Texture:
            return "Texture";
        case AssetType::Mesh:
            return "Mesh";
        case AssetType::Shader:
            return "Shader";
        case AssetType::Material:
            return "Material";
        case AssetType::AnimationClip:
            return "AnimationClip";
        case AssetType::Audio:
            return "Audio";
        case AssetType::Font:
            return "Font";
        case AssetType::Scene:
            return "Scene";
        default:
            return "Unknown";
        }
    }

  private:
    std::string path_, name_;
    AssetType type_ = AssetType::Unknown;
    LoadState state_ = LoadState::Unloaded;
    uint64_t sizeBytes_ = 0, hash_ = 0;
};

} // namespace bighero
