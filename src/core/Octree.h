#pragma once
#include <vector>
#include <memory>
#include <cstddef>
#include <cmath>

namespace bighero {

// Octree: a 3D spatial subdivision tree for point querying. Each stored item
// carries (id, x, y, z) so splits relocate ids without external bookkeeping.
// Standard-library only, self-contained.
class Octree {
public:
    Octree() { Initialize(0, 0, 0, 1.0f, 6); }
    Octree(float cx, float cy, float cz, float halfExtent, int maxDepth = 6)
        { Initialize(cx, cy, cz, halfExtent, maxDepth); }

    void Initialize(float cx, float cy, float cz, float half, int maxDepth = 6) {
        maxDepth_ = maxDepth < 0 ? 0 : maxDepth;
        cx_=cx; cy_=cy; cz_=cz; half_ = half > 0 ? half : 1.0f;
        root_ = std::make_shared<Node>();
        root_->cx=cx_; root_->cy=cy_; root_->cz=cz_; root_->half=half_;
    }

    bool Insert(unsigned id, float x, float y, float z) {
        float dx=x-cx_, dy=y-cy_, dz=z-cz_;
        if (std::fabs(dx)>half_||std::fabs(dy)>half_||std::fabs(dz)>half_) return false;
        InsertNode(root_, id, x, y, z, 0);
        return true;
    }

    void QuerySphere(float cx, float cy, float cz, float r, std::vector<unsigned>& out) const {
        out.clear();
        QueryNode(root_, cx, cy, cz, r, out);
    }

private:
    struct Item { unsigned id; float x, y, z; };
    struct Node {
        float cx=0, cy=0, cz=0, half=0;
        std::vector<Item> items;
        std::shared_ptr<Node> child[8];
    };

    static int ChildIndex(float px, float py, float pz, float cx, float cy, float cz) {
        int ix = px >= cx ? 1 : 0;
        int iy = py >= cy ? 2 : 0;
        int iz = pz >= cz ? 4 : 0;
        return ix | iy | iz;
    }

    void InsertNode(const std::shared_ptr<Node>& n, unsigned id, float x, float y, float z, int depth) {
        if (!n->child[0] && n->items.size() >= capacity_ && depth < maxDepth_) {
            Split(n);
        }
        if (n->child[0]) {
            int ci = ChildIndex(x,y,z, n->cx,n->cy,n->cz);
            InsertNode(n->child[ci], id, x, y, z, depth+1);
        } else {
            n->items.push_back({id, x, y, z});
        }
    }

    void Split(const std::shared_ptr<Node>& n) {
        float h = n->half * 0.5f;
        for (int i=0;i<8;++i) {
            auto c = std::make_shared<Node>();
            float ox = (i&1)?h:-h, oy=(i&2)?h:-h, oz=(i&4)?h:-h;
            c->cx=n->cx+ox; c->cy=n->cy+oy; c->cz=n->cz+oz; c->half=h;
            n->child[i]=c;
        }
        auto items = n->items;
        n->items.clear();
        for (auto& it : items) {
            int ci = ChildIndex(it.x,it.y,it.z, n->cx,n->cy,n->cz);
            n->child[ci]->items.push_back(it);
        }
    }

    void QueryNode(const std::shared_ptr<Node>& n, float cx, float cy, float cz, float r,
                   std::vector<unsigned>& out) const {
        if (!n) return;
        float dx = std::max(std::fabs(cx-n->cx)-n->half, 0.0f);
        float dy = std::max(std::fabs(cy-n->cy)-n->half, 0.0f);
        float dz = std::max(std::fabs(cz-n->cz)-n->half, 0.0f);
        if (dx*dx+dy*dy+dz*dz > r*r) return;
        for (auto& it : n->items) {
            float ex=it.x-cx, ey=it.y-cy, ez=it.z-cz;
            if (ex*ex+ey*ey+ez*ez <= r*r) out.push_back(it.id);
        }
        for (int i=0;i<8;++i) QueryNode(n->child[i], cx, cy, cz, r, out);
    }

    std::shared_ptr<Node> root_;
    float cx_=0, cy_=0, cz_=0, half_=1;
    int maxDepth_=6;
    std::size_t capacity_=8;
};

} // namespace bighero
