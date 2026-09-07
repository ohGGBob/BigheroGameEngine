#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <climits>

namespace bighero {

// Simple spatial partition for 3D broad-phase queries over axis-aligned boxes.
class SpatialPartition3D {
public:
    struct Box { float minX,minY,minZ, maxX,maxY,maxZ; int id; };

    SpatialPartition3D() : cellSize_(1.0f) {}
    explicit SpatialPartition3D(float cellSize) : cellSize_(cellSize > 0 ? cellSize : 1.0f) {}

    void Build(const std::vector<Box>& boxes) {
        boxes_ = boxes;
        cells_.assign(0, {});
        minX_=minY_=minZ_=0; maxX_=maxY_=maxZ_=-1;
        if (boxes_.empty()) return;
        minX_=minY_=minZ_= INT_MAX; maxX_=maxY_=maxZ_= INT_MIN;
        for (auto& b : boxes_) {
            int cx0=CellX(b.minX), cy0=CellY(b.minY), cz0=CellZ(b.minZ);
            int cx1=CellX(b.maxX), cy1=CellY(b.maxY), cz1=CellZ(b.maxZ);
            if(cx0<minX_)minX_=cx0; if(cy0<minY_)minY_=cy0; if(cz0<minZ_)minZ_=cz0;
            if(cx1>maxX_)maxX_=cx1; if(cy1>maxY_)maxY_=cy1; if(cz1>maxZ_)maxZ_=cz1;
        }
        int gw=maxX_-minX_+1, gh=maxY_-minY_+1, gd=maxZ_-minZ_+1;
        if(gw<=0||gh<=0||gd<=0) return;
        cells_.assign((size_t)gw*gh*gd, {});
        for(size_t i=0;i<boxes_.size();++i){
            const Box& b=boxes_[i];
            int cx0=CellX(b.minX), cy0=CellY(b.minY), cz0=CellZ(b.minZ);
            int cx1=CellX(b.maxX), cy1=CellY(b.maxY), cz1=CellZ(b.maxZ);
            for(int cz=cz0;cz<=cz1;++cz)for(int cy=cy0;cy<=cy1;++cy)for(int cx=cx0;cx<=cx1;++cx)
                cells_[(size_t)(cz-minZ_)*gh*gw+(cy-minY_)*gw+(cx-minX_)].push_back((int)i);
        }
        gridW_=gw; gridH_=gh; gridD_=gd;
    }

    void Query(float minX,float minY,float minZ,float maxX,float maxY,float maxZ,
               std::vector<int>& out) const {
        out.clear();
        if(cells_.empty()) return;
        mark_.assign(boxes_.size(),0); ++stamp_;
        int cx0=CellX(minX),cy0=CellY(minY),cz0=CellZ(minZ);
        int cx1=CellX(maxX),cy1=CellY(maxY),cz1=CellZ(maxZ);
        for(int cz=cz0;cz<=cz1;++cz)for(int cy=cy0;cy<=cy1;++cy)for(int cx=cx0;cx<=cx1;++cx){
            const auto& cell=cells_[(size_t)(cz-minZ_)*gridH_*gridW_+(cy-minY_)*gridW_+(cx-minX_)];
            for(int id:cell)
                if(mark_[id]!=stamp_ && Overlaps(boxes_[id],minX,minY,minZ,maxX,maxY,maxZ)){
                    mark_[id]=stamp_; out.push_back(id);
                }
        }
    }

private:
    int CellX(float x) const { return (int)std::floor(x/cellSize_); }
    int CellY(float y) const { return (int)std::floor(y/cellSize_); }
    int CellZ(float z) const { return (int)std::floor(z/cellSize_); }
    static bool Overlaps(const Box& b,float minX,float minY,float minZ,float maxX,float maxY,float maxZ){
        return b.minX<maxX && minX<b.maxX && b.minY<maxY && minY<b.maxY && b.minZ<maxZ && minZ<b.maxZ;
    }
    float cellSize_;
    std::vector<Box> boxes_;
    std::vector<std::vector<int>> cells_;
    int minX_,minY_,minZ_,maxX_,maxY_,maxZ_;
    int gridW_=0,gridH_=0,gridD_=0;
    mutable std::vector<int> mark_;
    mutable int stamp_=0;
};

} // namespace bighero
