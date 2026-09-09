#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace bighero {

// BiomeMap: assigns discrete biome ids to cells based on (temperature,
// moisture, height) triples, with named biome lookup. Standard-library only,
// self-contained.
class BiomeMap {
public:
    enum class Biome { Ocean, Beach, Desert, Grassland, Forest, Tundra, Mountain, Snow, Swamp, Count };

    BiomeMap() {}

    // Classify a cell by its temperature/moisture/height (each in [0,1]).
    static Biome Classify(float temperature, float moisture, float height) {
        if (height < 0.15f) return Biome::Ocean;
        if (height > 0.85f) return Biome::Snow;
        if (height > 0.7f) return Biome::Mountain;
        if (temperature < 0.2f) return Biome::Tundra;
        if (temperature > 0.8f) {
            if (moisture < 0.35f) return Biome::Desert;
            if (moisture > 0.75f) return Biome::Swamp;
            return Biome::Grassland;
        }
        if (moisture > 0.7f) return Biome::Forest;
        if (moisture < 0.3f) return Biome::Desert;
        return Biome::Grassland;
    }

    static const char* Name(Biome b) {
        switch (b) {
            case Biome::Ocean: return "Ocean";
            case Biome::Beach: return "Beach";
            case Biome::Desert: return "Desert";
            case Biome::Grassland: return "Grassland";
            case Biome::Forest: return "Forest";
            case Biome::Tundra: return "Tundra";
            case Biome::Mountain: return "Mountain";
            case Biome::Snow: return "Snow";
            case Biome::Swamp: return "Swamp";
            default: return "Unknown";
        }
    }

    void Resize(std::size_t w, std::size_t h) {
        w_ = w < 1 ? 1 : w; h_ = h < 1 ? 1 : h;
        ids_.assign(w_ * h_, Biome::Grassland);
    }
    std::size_t Width() const { return w_; }
    std::size_t Height() const { return h_; }

    bool Set(std::size_t x, std::size_t y, Biome b) {
        if (x >= w_ || y >= h_) return false;
        ids_[y * w_ + x] = b;
        return true;
    }
    bool Get(std::size_t x, std::size_t y, Biome& b) const {
        if (x >= w_ || y >= h_) return false;
        b = ids_[y * w_ + x];
        return true;
    }

private:
    std::size_t w_=1, h_=1;
    std::vector<Biome> ids_;
};

} // namespace bighero
