#pragma once
#include <vector>
#include <memory>
#include <cstddef>

namespace bighero {

// QuadTree: a 2D spatial subdivision tree for point querying. Each stored item
// carries (id, x, y) so splits relocate ids without external bookkeeping.
// Standard-library only, self-contained.
class QuadTree {
public:
    QuadTree() { Initialize(0, 0, 1, 1, 8); }
    QuadTree(float minX, float minY, float maxX, float maxY, int maxDepth = 8)
        { Initialize(minX, minY, maxX, maxY, maxDepth); }

    void Initialize(float minX, float minY, float maxX, float maxY, int maxDepth = 8) {
        minX_=minX; minY_=minY; maxX_=maxX; maxY_=maxY;
        maxDepth_ = maxDepth < 0 ? 0 : maxDepth;
        root_ = std::make_shared<Node>();
        root_->cx=(minX+maxX)*0.5f; root_->cy=(minY+maxY)*0.5f;
        root_->hw=(maxX-minX)*0.5f; root_->hh=(maxY-minY)*0.5f;
    }

    bool Insert(unsigned id, float x, float y) {
        if (x < minX_ || x > maxX_ || y < minY_ || y > maxY_) return false;
        InsertNode(root_, id, x, y, 0);
        return true;
    }

    // Range query: collect ids within box centred at (cx,cy) with half-extents.
    void Query(float cx, float cy, float hw, float hh, std::vector<unsigned>& out) const {
        out.clear();
        QueryNode(root_, cx, cy, hw, hh, out);
    }

private:
    struct Item { unsigned id; float x, y; };
    struct Node {
        float cx=0, cy=0, hw=0, hh=0;
        std::vector<Item> items;
        std::shared_ptr<Node> nw, ne, sw, se;
    };

    static void Split(const std::shared_ptr<Node>& n) {
        float hx = n->hw * 0.5f, hy = n->hh * 0.5f;
        auto mk = [&](float cx, float cy) {
            auto c = std::make_shared<Node>();
            c->cx=cx; c->cy=cy; c->hw=hx; c->hh=hy;
            return c;
        };
        n->nw = mk(n->cx-hx, n->cy-hy);
        n->ne = mk(n->cx+hx, n->cy-hy);
        n->sw = mk(n->cx-hx, n->cy+hy);
        n->se = mk(n->cx+hx, n->cy+hy);
        auto items = n->items;
        n->items.clear();
        for (auto& it : items) {
            auto& child = SelectChild(n, it.x, it.y);
            child->items.push_back(it);
        }
    }

    static std::shared_ptr<Node>& SelectChild(const std::shared_ptr<Node>& n, float x, float y) {
        bool left = x < n->cx, bottom = y < n->cy;
        if (left)  return bottom ? n->nw : n->sw;
        else       return bottom ? n->ne : n->se;
    }

    void InsertNode(const std::shared_ptr<Node>& n, unsigned id, float x, float y, int depth) {
        if (!n->nw) {
            n->items.push_back({id, x, y});
            if (n->items.size() > capacity_ && depth < maxDepth_)
                Split(n);
            return;
        }
        auto& child = SelectChild(n, x, y);
        InsertNode(child, id, x, y, depth+1);
    }

    void QueryNode(const std::shared_ptr<Node>& n, float cx, float cy, float hw, float hh,
                   std::vector<unsigned>& out) const {
        if (!n) return;
        if (cx+hw < n->cx-n->hw || cx-hw > n->cx+n->hw ||
            cy+hh < n->cy-n->hh || cy-hh > n->cy+n->hh) return;
        for (auto& it : n->items)
            if (it.x >= cx-hw && it.x <= cx+hw && it.y >= cy-hh && it.y <= cy+hh)
                out.push_back(it.id);
        QueryNode(n->nw, cx, cy, hw, hh, out);
        QueryNode(n->ne, cx, cy, hw, hh, out);
        QueryNode(n->sw, cx, cy, hw, hh, out);
        QueryNode(n->se, cx, cy, hw, hh, out);
    }

    std::shared_ptr<Node> root_;
    float minX_=0, minY_=0, maxX_=1, maxY_=1;
    int maxDepth_=8;
    std::size_t capacity_=8;
};

} // namespace bighero
