#pragma once
#include <vector>
#include <cstddef>
#include <cmath>
#include <unordered_map>

namespace bighero {

// MeshOptimizer: static helpers to post-process triangle meshes — weld
// coincident vertices, remove degenerate triangles, and strip unused vertices.
// Standard-library only, self-contained. Works on an interleaved
// (x,y,z)*vertexCount float buffer with an index buffer.
class MeshOptimizer {
public:
    MeshOptimizer() {}

    // Weld vertices within `tolerance` using a spatial hash of rounded coords.
    // Rewrites positions + indices in place.
    static void Weld(std::vector<float>& positions, std::vector<unsigned>& indices,
                     float tolerance = 1e-4f) {
        float keyScale = tolerance > 1e-9f ? 1.0f/tolerance : 1e9f;
        std::unordered_map<long long, unsigned> hash;
        std::vector<float> newPos;
        std::vector<unsigned> remap(positions.size()/3, (unsigned)-1);
        auto keyOf = [&](float x, float y, float z) {
            long long ix = (long long)std::llround((double)x*keyScale);
            long long iy = (long long)std::llround((double)y*keyScale);
            long long iz = (long long)std::llround((double)z*keyScale);
            return (ix * 73856093LL) ^ (iy * 19349663LL) ^ (iz * 83492791LL);
        };
        for (std::size_t v=0; v<positions.size()/3; ++v) {
            float x=positions[v*3], y=positions[v*3+1], z=positions[v*3+2];
            long long k = keyOf(x,y,z);
            // find an existing entry within tolerance
            unsigned match = (unsigned)-1;
            for (auto& kv : hash) {
                if (kv.first != k) continue;  // quick key match; coarse
                match = kv.second; break;
            }
            if (match == (unsigned)-1) {
                match = (unsigned)(newPos.size()/3);
                newPos.push_back(x); newPos.push_back(y); newPos.push_back(z);
                hash[k] = match;
            }
            remap[v] = match;
        }
        // rebuild indices through remap
        for (auto& idx : indices) if (idx < remap.size()) idx = remap[idx];
        positions.swap(newPos);
    }

    // Remove triangles whose area is ~0 (degenerate / repeated indices).
    static void RemoveDegenerate(std::vector<float>& positions, std::vector<unsigned>& indices) {
        std::vector<unsigned> out;
        out.reserve(indices.size());
        for (std::size_t i=0; i+2 < indices.size(); i += 3) {
            unsigned a=indices[i], b=indices[i+1], c=indices[i+2];
            if (a==b || b==c || a==c) continue;
            float ax=positions[(std::size_t)a*3], ay=positions[(std::size_t)a*3+1], az=positions[(std::size_t)a*3+2];
            float bx=positions[(std::size_t)b*3], by=positions[(std::size_t)b*3+1], bz=positions[(std::size_t)b*3+2];
            float cx=positions[(std::size_t)c*3], cy=positions[(std::size_t)c*3+1], cz=positions[(std::size_t)c*3+2];
            float ux=bx-ax, uy=by-ay, uz=bz-az;
            float vx=cx-ax, vy=cy-ay, vz=cz-az;
            float crossx=uy*vz-uz*vy, crossy=uz*vx-ux*vz, crossz=ux*vy-uy*vx;
            float area2 = crossx*crossx+crossy*crossy+crossz*crossz;
            if (area2 > 1e-12f) { out.push_back(a); out.push_back(b); out.push_back(c); }
        }
        indices.swap(out);
    }

    // Move vertices not referenced by any index to the end (compact them).
    static void StripUnused(std::vector<float>& positions, std::vector<unsigned>& indices) {
        std::vector<char> used(positions.size()/3, 0);
        for (unsigned idx : indices) if (idx < used.size()) used[idx]=1;
        std::vector<float> compact;
        std::vector<unsigned> remap(used.size(), (unsigned)-1);
        for (std::size_t v=0; v<used.size(); ++v) if (used[v]) {
            remap[v]=(unsigned)(compact.size()/3);
            compact.push_back(positions[v*3]);
            compact.push_back(positions[v*3+1]);
            compact.push_back(positions[v*3+2]);
        }
        for (auto& idx : indices) if (idx < remap.size()) idx = remap[idx];
        positions.swap(compact);
    }
};

} // namespace bighero
