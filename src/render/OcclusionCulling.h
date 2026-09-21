#pragma once
// 烘焙式遮挡剔除（PVS, Potentially Visible Set）：把「从场景的某一小块空间能看见哪些物体」
// 离线算成位图，运行时按相机所在格查一次表就完成剔除（U2-R2）。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan 对象；可离线单测。
//
// 背景与动机：
//   视锥剔除只能剔除「不在视野里」的物体；城市/室内/洞穴里真正昂贵的是那些**在视野内
//   却被墙挡住**的东西——一栋楼背后的 300 个道具照样要走完整条绘制路径。运行时做遮挡
//   查询（如硬件 occlusion query）会有 1~2 帧延迟与读回 stall；而把可见性**离线烘焙**成
//   位集（Quake 的 PVS、Unity 的 Occlusion Culling 均为此路）则运行时零成本、零延迟，
//   代价只是磁盘上一点数据和一次离线烘焙。本模块即这套机制的完整实现。
//
// 算法（保守烘焙，宁可多画也不错剔）：
//   1. 把场景切成均匀网格（cell）。
//   2. 对每个 cell × 每个被剔除对象：从 cell 内若干采样点向该对象表面若干采样点连射线，
//      只要**任意一条射线**不被任何遮挡体挡住，就标记「潜在可见」。
//      —— 保守性由「任意一条通即算可见」保证：射线采样只会漏掉极窄缝里的可见性，
//         表现为少剔除一些；绝不会出现「明明看得见却被剔掉」的穿帮。
//   3. 对象与相机同格（或包围盒与格相邻/相交）时直接判可见，避免自遮挡误剔。
//   4. 结果按 cell 存成 bitset（每个对象 1 bit）：cell 数 × 对象数 / 8 字节。
//
// 契约：
//   - 遮挡体与被剔除对象一律用世界空间 AABB 描述（建筑、墙体、大件道具足够）。
//   - 射线起点/终点落在某个遮挡体内部时**跳过该遮挡体**：既避免自遮挡，
//     也避免相机贴墙时把自己所在的那面墙算成遮挡。
//   - 烘焙是 O(cells × objects × rays × occluders)，属离线成本；运行时查询是 O(1)。
//   - 未做线程同步：Bake 为写、查询为读，约定分别离线与运行时执行。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Render
{
// 世界空间轴对齐包围盒（遮挡体与被剔除对象共用）。
struct Bounds3
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    [[nodiscard]] glm::vec3 Center() const { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec3 Extent() const { return max - min; }
    [[nodiscard]] bool Contains(const glm::vec3& p) const
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    // 膨胀 inflate 后是否相交（用于「贴着格子就算可见」的容差判定）。
    [[nodiscard]] bool IntersectsInflated(const Bounds3& o, float inflate) const
    {
        return (max.x + inflate >= o.min.x) && (min.x - inflate <= o.max.x) && (max.y + inflate >= o.min.y) &&
               (min.y - inflate <= o.max.y) && (max.z + inflate >= o.min.z) && (min.z - inflate <= o.max.z);
    }
};

// 射线 vs AABB（slab 法）。dir 不必归一化；返回是否命中，命中距离写入 tHit。
// 起点在盒内时返回 true 且 tHit = 0（调用方通常据此跳过该盒，见文件头契约）。
[[nodiscard]] inline bool RayHitsBox(const glm::vec3& origin, const glm::vec3& dir, const Bounds3& box, float tMax,
                                     float& tHit)
{
    if (box.Contains(origin))
    {
        tHit = 0.0f;
        return true;
    }
    float tMin = 0.0f;
    float tOut = tMax;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float o = origin[axis];
        const float d = dir[axis];
        const float lo = box.min[axis];
        const float hi = box.max[axis];
        if (std::fabs(d) < 1e-9f)
        {
            if (o < lo || o > hi)
                return false; // 平行且在 slab 外
            continue;
        }
        const float inv = 1.0f / d;
        float t1 = (lo - o) * inv;
        float t2 = (hi - o) * inv;
        if (t1 > t2)
            std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tOut = std::min(tOut, t2);
        if (tMin > tOut)
            return false;
    }
    tHit = tMin;
    return true;
}

// 烘焙参数。
struct OcclusionBakeParams
{
    // 每个 cell 内的射线起点数：1 = 只用格心；9 = 格心 + 8 个内缩角点。
    int originSamples = 9;
    // 每个对象的射线终点数：1 = 只用包围盒中心；9 = 8 角 + 中心；27 = 再加棱/面心。
    int targetSamples = 9;
    // 射线最大长度（超出该距离的对象一律视为可见，避免为极远物体白算射线）。
    float maxRayDistance = 500.0f;
    // 对象包围盒与 cell 相交判定时的膨胀量（贴边即算可见，防自遮挡误剔）。
    float cellInflate = 0.25f;
    // 是否跳过「包含射线起点或终点」的遮挡体（默认开，见文件头契约）。
    bool skipTouchingOccluders = true;
};

class OcclusionVolume
{
  public:
    // ---- 网格 ----
    bool Resize(const glm::ivec3& dims, const glm::vec3& origin, const glm::vec3& spacing)
    {
        if (dims.x < 1 || dims.y < 1 || dims.z < 1)
            return false;
        if (!(spacing.x > 0.0f) || !(spacing.y > 0.0f) || !(spacing.z > 0.0f))
            return false;
        dims_ = dims;
        origin_ = origin;
        spacing_ = spacing;
        const size_t cells = static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y) * static_cast<size_t>(dims.z);
        cells_ = cells;
        Invalidate();
        return true;
    }

    [[nodiscard]] size_t CellCount() const { return cells_; }
    [[nodiscard]] size_t ObjectCount() const { return objectCount_; }
    [[nodiscard]] const glm::ivec3& Dims() const { return dims_; }

    [[nodiscard]] glm::vec3 CellPosition(const glm::ivec3& idx) const { return origin_ + glm::vec3(idx) * spacing_; }
    [[nodiscard]] glm::vec3 CellCenter(const glm::ivec3& idx) const { return CellPosition(idx) + spacing_ * 0.5f; }
    [[nodiscard]] glm::vec3 CellExtent() const { return spacing_; }

    // 世界坐标 → 网格索引（越界按边界钳制）。
    [[nodiscard]] glm::ivec3 ClampedCell(const glm::vec3& world) const
    {
        const glm::vec3 g = (world - origin_) / spacing_;
        const glm::vec3 f = glm::floor(g);
        return glm::clamp(glm::ivec3(static_cast<int>(f.x), static_cast<int>(f.y), static_cast<int>(f.z)),
                          glm::ivec3(0), dims_ - glm::ivec3(1));
    }

    // ---- 烘焙 ----
    // occluders：遮挡体（墙、楼、山体）。cullables：可被剔除的对象（道具、装饰、小建筑）。
    // 返回 true 表示烘焙完成并已生成 PVS。空网格 / 空对象表都视为成功（退化为空 PVS）。
    bool Bake(const std::vector<Bounds3>& occluders, const std::vector<Bounds3>& cullables,
              const OcclusionBakeParams& params = {})
    {
        if (cells_ == 0)
            return false;
        objectCount_ = cullables.size();
        stride_ = (objectCount_ + 63u) / 64u;
        if (stride_ == 0)
            stride_ = 1; // 无对象时也保留一个字，保持布局一致
        pvs_.assign(cells_ * stride_, 0ull);
        baked_ = true;

        for (int z = 0; z < dims_.z; ++z)
        {
            for (int y = 0; y < dims_.y; ++y)
            {
                for (int x = 0; x < dims_.x; ++x)
                {
                    const glm::ivec3 cellIdx(x, y, z);
                    const size_t cell = CellIndex(cellIdx);
                    const Bounds3 cellBox = CellBounds(cellIdx);
                    const std::vector<glm::vec3> origins = CellSamplePoints(cellBox, params.originSamples);

                    for (size_t o = 0; o < cullables.size(); ++o)
                    {
                        const Bounds3& obj = cullables[o];
                        // 与 cell 相交（含容差）→ 必然可见：相机就在这东西旁边，谈遮挡无意义。
                        if (cellBox.IntersectsInflated(obj, params.cellInflate))
                        {
                            SetVisible(cell, o);
                            continue;
                        }
                        const std::vector<glm::vec3> targets = BoxSamplePoints(obj, params.targetSamples);
                        if (IsAnyRayVisible(origins, targets, occluders, params))
                            SetVisible(cell, o);
                    }
                }
            }
        }
        return true;
    }

    void Invalidate()
    {
        pvs_.clear();
        objectCount_ = 0;
        stride_ = 1;
        baked_ = false;
    }
    [[nodiscard]] bool IsBaked() const { return baked_; }

    // ---- 运行时查询 ----
    // 从 cellIdx 处能否看到第 obj 个对象。未烘焙 / 越界一律返回 true（保守：宁可多画）。
    [[nodiscard]] bool IsVisible(const glm::ivec3& cellIdx, size_t obj) const
    {
        if (!baked_ || obj >= objectCount_)
            return true;
        const size_t cell = CellIndex(cellIdx);
        if (cell >= cells_)
            return true;
        return (pvs_[cell * stride_ + obj / 64u] & (1ull << (obj % 64u))) != 0ull;
    }
    [[nodiscard]] bool IsVisibleAt(const glm::vec3& world, size_t obj) const
    {
        return IsVisible(ClampedCell(world), obj);
    }

    // 该 cell 的可见对象索引列表（按索引升序，便于确定性遍历）。
    [[nodiscard]] std::vector<uint32_t> VisibleObjects(const glm::ivec3& cellIdx) const
    {
        std::vector<uint32_t> out;
        if (!baked_)
            return out;
        const size_t cell = CellIndex(cellIdx);
        if (cell >= cells_)
            return out;
        const uint64_t* row = &pvs_[cell * stride_];
        for (size_t o = 0; o < objectCount_; ++o)
        {
            if ((row[o / 64u] & (1ull << (o % 64u))) != 0ull)
                out.push_back(static_cast<uint32_t>(o));
        }
        return out;
    }
    [[nodiscard]] std::vector<uint32_t> VisibleObjectsAt(const glm::vec3& world) const
    {
        return VisibleObjects(ClampedCell(world));
    }

    [[nodiscard]] size_t VisibleCount(const glm::ivec3& cellIdx) const
    {
        size_t n = 0;
        const size_t cell = CellIndex(cellIdx);
        if (!baked_ || cell >= cells_)
            return objectCount_; // 未烘焙 → 保守全可见
        const uint64_t* row = &pvs_[cell * stride_];
        for (size_t o = 0; o < objectCount_; ++o)
        {
            if ((row[o / 64u] & (1ull << (o % 64u))) != 0ull)
                ++n;
        }
        return n;
    }

    // 烘焙质量指标：全部 cell 的平均可见比例（越低说明剔除越有效）。
    // 用于烘焙后的自检与回归（"这版烘焙把可见集砍掉了多少"）。
    [[nodiscard]] double AverageVisibilityRatio() const
    {
        if (!baked_ || cells_ == 0 || objectCount_ == 0)
            return 1.0;
        double sum = 0.0;
        for (size_t c = 0; c < cells_; ++c)
        {
            size_t n = 0;
            const uint64_t* row = &pvs_[c * stride_];
            for (size_t o = 0; o < objectCount_; ++o)
            {
                if ((row[o / 64u] & (1ull << (o % 64u))) != 0ull)
                    ++n;
            }
            sum += static_cast<double>(n) / static_cast<double>(objectCount_);
        }
        return sum / static_cast<double>(cells_);
    }

    // PVS 位图字节数（烘焙产物体积，构建面板/内存预算用）。
    [[nodiscard]] size_t PvsBytes() const { return pvs_.size() * sizeof(uint64_t); }

  private:
    [[nodiscard]] size_t CellIndex(const glm::ivec3& idx) const
    {
        if (idx.x < 0 || idx.y < 0 || idx.z < 0 || idx.x >= dims_.x || idx.y >= dims_.y || idx.z >= dims_.z)
            return static_cast<size_t>(-1);
        return (static_cast<size_t>(idx.z) * static_cast<size_t>(dims_.y) + static_cast<size_t>(idx.y)) *
                   static_cast<size_t>(dims_.x) +
               static_cast<size_t>(idx.x);
    }

    [[nodiscard]] Bounds3 CellBounds(const glm::ivec3& idx) const
    {
        Bounds3 b;
        b.min = CellPosition(idx);
        b.max = b.min + spacing_;
        return b;
    }

    // cell 内射线起点：格心，或格心 + 8 个内缩角点（贴墙站立时也有一条射线能出去）。
    [[nodiscard]] static std::vector<glm::vec3> CellSamplePoints(const Bounds3& box, int requested)
    {
        std::vector<glm::vec3> out;
        out.push_back(box.Center());
        if (requested < 9)
            return out;
        const glm::vec3 e = box.Extent();
        const float inset = 0.15f; // 往里收一点，避免起点正好压在遮挡体表面
        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 p = box.min;
            p.x += ((i & 1) ? e.x - inset * e.x : inset * e.x);
            p.y += ((i & 2) ? e.y - inset * e.y : inset * e.y);
            p.z += ((i & 4) ? e.z - inset * e.z : inset * e.z);
            out.push_back(p);
        }
        return out;
    }

    // 对象表面采样点：中心，或 8 角 + 中心，或 27 点（再加棱中点与面心）。
    [[nodiscard]] static std::vector<glm::vec3> BoxSamplePoints(const Bounds3& box, int requested)
    {
        std::vector<glm::vec3> out;
        if (requested < 9)
        {
            out.push_back(box.Center());
            return out;
        }
        for (int i = 0; i < 8; ++i)
        {
            out.emplace_back((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                             (i & 4) ? box.max.z : box.min.z);
        }
        if (requested < 27)
        {
            out.push_back(box.Center());
            return out;
        }
        out.push_back(box.Center());
        const glm::vec3 c = box.Center();
        for (int axis = 0; axis < 3; ++axis)
        {
            for (int hi = 0; hi < 2; ++hi)
            {
                glm::vec3 p = c;
                p[axis] = hi ? box.max[axis] : box.min[axis];
                out.push_back(p); // 6 个面心
            }
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            const int a1 = (axis + 1) % 3;
            const int a2 = (axis + 2) % 3;
            for (int s1 = 0; s1 < 2; ++s1)
            {
                for (int s2 = 0; s2 < 2; ++s2)
                {
                    glm::vec3 p = c;
                    p[axis] = c[axis];
                    p[a1] = s1 ? box.max[a1] : box.min[a1];
                    p[a2] = s2 ? box.max[a2] : box.min[a2];
                    out.push_back(p); // 12 条棱中点
                }
            }
        }
        return out;
    }

    // 只要存在一条「起点→终点」未被遮挡的射线即判可见（保守：宁可多画）。
    [[nodiscard]] static bool IsAnyRayVisible(const std::vector<glm::vec3>& origins,
                                              const std::vector<glm::vec3>& targets,
                                              const std::vector<Bounds3>& occluders, const OcclusionBakeParams& params)
    {
        for (const glm::vec3& o : origins)
        {
            for (const glm::vec3& t : targets)
            {
                const glm::vec3 delta = t - o;
                const float dist = glm::length(delta);
                if (dist < 1e-5f)
                    return true;
                if (dist > params.maxRayDistance)
                    return true; // 过远：不再判定遮挡，直接放行
                const glm::vec3 dir = delta / dist;

                bool blocked = false;
                for (const Bounds3& occ : occluders)
                {
                    float tHit = 0.0f;
                    if (!RayHitsBox(o, dir, occ, dist, tHit))
                        continue;
                    // 起点或终点落在该遮挡体内 → 它是「自己」或相机正贴着它，不算遮挡。
                    if (params.skipTouchingOccluders && (occ.Contains(o) || occ.Contains(t)))
                        continue;
                    // 命中点必须在起点与终点之间，且不能正好压在终点上（贴面对象）。
                    if (tHit >= dist - 1e-4f)
                        continue;
                    blocked = true;
                    break;
                }
                if (!blocked)
                    return true;
            }
        }
        return false;
    }

    void SetVisible(size_t cell, size_t obj) { pvs_[cell * stride_ + obj / 64u] |= (1ull << (obj % 64u)); }

    glm::ivec3 dims_{0, 0, 0};
    glm::vec3 origin_{0.0f};
    glm::vec3 spacing_{1.0f};
    size_t cells_ = 0;
    size_t objectCount_ = 0;
    size_t stride_ = 1;
    std::vector<uint64_t> pvs_;
    bool baked_ = false;
};
} // namespace BigHero::Render
