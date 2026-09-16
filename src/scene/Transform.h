#pragma once
// 组件化场景变换层级（Transform Hierarchy）。
// 纯 CPU、仅依赖 glm 头文件，可离线运行与单元测试（不触碰 Vulkan/GPU）。
//
// 约定：
//   - 右撇 Y-up 坐标系，弧度制（工程已定义 GLM_FORCE_RADIANS）。
//   - 局部->世界矩阵组合顺序：M_local = T * R * S（先缩放、再旋转、最后平移），
//     与世界矩阵相乘 obj->world 的列向量变换一致：world = parent_world * M_local。
//   - 层级以“扁平数组 + parent 索引”组织（面向未来 glTF/ECS 的骨架/场景树）。

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <vector>

namespace BigHero::Scene
{
// 单个变换组件：局部空间 TRS + 可选父节点索引。
// parent == kNoParent 表示根节点（无父）。
struct Transform
{
    static constexpr int32_t kNoParent = -1;

    glm::vec3 translation{0.0f};                // 相对父级的平移
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // 相对父级的旋转（单位四元数）
    glm::vec3 scale{1.0f};                      // 相对父级的非均匀缩放（避免零：调用方保证）
    int32_t parent = kNoParent;                 // 父节点在数组中的索引，-1=根

    [[nodiscard]] bool IsRoot() const noexcept { return parent == kNoParent; }
};

// 由欧拉角（度）构造旋转四元数（XYZ 顺序，工程常用绕Y自转场景）。
[[nodiscard]] inline glm::quat RotationEulerDeg(float pitchDeg, float yawDeg, float rollDeg)
{
    return glm::quat(glm::vec3(glm::radians(pitchDeg), glm::radians(yawDeg), glm::radians(rollDeg)));
}

// 计算局部 TRS 矩阵（列主序）：T * R * S。
[[nodiscard]] inline glm::mat4 LocalToMatrix(const Transform& t)
{
    glm::mat4 m(1.0f);
    m = glm::translate(m, t.translation);
    m *= glm::mat4_cast(t.rotation); // 四元数 -> 3x3旋转矩阵（嵌入4x4）
    m = glm::scale(m, t.scale);
    return m;
}

// 沿父链向上求局部->世界矩阵。
// transforms 为扁平变换数组；t 为其中某元素（或其拷贝）。
// 若节点无父则返回其局部矩阵；否则级联父世界 * 子局部。
[[nodiscard]] inline glm::mat4 LocalToWorldMatrix(const Transform& t, const std::vector<Transform>& transforms)
{
    const glm::mat4 local = LocalToMatrix(t);
    if (t.IsRoot())
        return local;
    return LocalToWorldMatrix(transforms[static_cast<size_t>(t.parent)], transforms) * local;
}

// 世界空间平移（层级各节点 translation 经旋转/缩放/父链累加后的绝对位置）。
[[nodiscard]] inline glm::vec3 WorldPosition(const Transform& t, const std::vector<Transform>& transforms)
{
    const glm::mat4 w = LocalToWorldMatrix(t, transforms);
    return glm::vec3(w[3]); // 平移列
}

// 世界空间 AABB：给定模型局部 AABB（aabbMin/aabbMax，可非均匀缩放），
// 计算其在 world 矩阵下的轴对齐包围盒（8 角点变换后取 min/max）。
// 严格保守（对任意旋转/非均匀缩放都正确）。
[[nodiscard]] inline std::array<glm::vec3, 2> WorldAabb(const glm::mat4& world, const glm::vec3& aabbMin,
                                                        const glm::vec3& aabbMax)
{
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(aabbMin.x, aabbMin.y, aabbMin.z), glm::vec3(aabbMax.x, aabbMin.y, aabbMin.z),
        glm::vec3(aabbMin.x, aabbMax.y, aabbMin.z), glm::vec3(aabbMax.x, aabbMax.y, aabbMin.z),
        glm::vec3(aabbMin.x, aabbMin.y, aabbMax.z), glm::vec3(aabbMax.x, aabbMin.y, aabbMax.z),
        glm::vec3(aabbMin.x, aabbMax.y, aabbMax.z), glm::vec3(aabbMax.x, aabbMax.y, aabbMax.z)};
    for (const glm::vec3& c : corners)
    {
        const glm::vec4 p = world * glm::vec4(c, 1.0f);
        lo = glm::min(lo, glm::vec3(p));
        hi = glm::max(hi, glm::vec3(p));
    }
    return {lo, hi};
}

// 便捷重载：从数组 + 索引直接算世界 AABB。
[[nodiscard]] inline std::array<glm::vec3, 2> WorldAabb(const Transform& t, const std::vector<Transform>& transforms,
                                                        const glm::vec3& aabbMin, const glm::vec3& aabbMax)
{
    return WorldAabb(LocalToWorldMatrix(t, transforms), aabbMin, aabbMax);
}

// 批量计算整层的局部->世界矩阵（每帧场景/骨骼求值主路径）。
//
// 相对逐节点调用 LocalToWorldMatrix 的 O(n·深度) 递归，本接口为单趟 O(n)：
// 每个节点的世界矩阵仅由「父世界 × 子局部」一次矩阵乘得到，结果缓存供后续节点复用。
// 支持任意节点存储顺序（父可在子之后），内部按父链先解析祖先再记忆化填充。
[[nodiscard]] inline std::vector<glm::mat4> ComputeAllWorldMatrices(const std::vector<Transform>& transforms)
{
    const size_t n = transforms.size();
    std::vector<glm::mat4> world(n, glm::mat4(1.0f));
    std::vector<uint8_t> done(n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        if (done[i])
            continue;
        // 收集当前节点到根的索引链（根在末尾）。
        std::vector<size_t> chain;
        size_t cur = i;
        bool valid = true;
        while (!done[cur])
        {
            chain.push_back(cur);
            const int32_t p = transforms[cur].parent;
            if (p == Transform::kNoParent)
                break;
            if (p < 0 || static_cast<size_t>(p) >= n)
            {
                valid = false; // 悬空父索引：跳出并回退为局部矩阵
                break;
            }
            cur = static_cast<size_t>(p);
        }
        if (!valid)
        {
            for (const size_t idx : chain)
            {
                if (!done[idx])
                {
                    world[idx] = LocalToMatrix(transforms[idx]);
                    done[idx] = 1;
                }
            }
            continue;
        }
        // 自根向下级联：world[child] = world[parent] * local[child]。
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const size_t idx = *it;
            const Transform& t = transforms[idx];
            const glm::mat4 local = LocalToMatrix(t);
            world[idx] = t.IsRoot() ? local : world[static_cast<size_t>(t.parent)] * local;
            done[idx] = 1;
        }
    }
    return world;
}
// ---------------------------------------------------------------------------
// 变换层级脏标记缓存（Transform Dirty-Flag Cache）。
//
// 动机：ComputeAllWorldMatrices 每帧对整层做单趟 O(n) 级联；当一帧内只有少数节点
// 发生局部变化（最常见的运行时形态）时，全量重算是浪费。本类维护「每个节点的局部 TRS +
// 世界矩阵缓存 + 脏子树标记」，仅重算发生变化的节点及其子树，未受影响的分支直接复用缓存：
//   - 空闲帧（无任何局部修改）：UpdateWorld() 为 O(1)，不做任何矩阵运算；
//   - 修改帧：仅 O(受影响子树节点数) 次矩阵乘，而非 O(n)。
//
// 语义与 ComputeAllWorldMatrices 完全一致（相同 T*R*S 组合、相同父级级联、相同悬空父
// 索引回退为局部矩阵），因此两者可互为「前后对照」基准。
//
// 约束：层级须为有向森林（不支持环）。父索引越界视为根。
class TransformHierarchy
{
  public:
    TransformHierarchy() = default;
    explicit TransformHierarchy(std::vector<Transform> transforms) { Reset(std::move(transforms)); }

    // 以给定局部变换重建整层，并标记全部节点待重算（首次 UpdateWorld 会全量计算）。
    void Reset(std::vector<Transform> transforms)
    {
        matrixMode_ = false;
        local_ = std::move(transforms);
        const size_t n = local_.size();
        world_.assign(n, glm::mat4(1.0f));
        valid_.assign(n, 0);
        dirtyRoot_.assign(n, 0);
        stamp_.assign(n, 0);
        dirtyRoots_.clear();
        gen_ = 0;
        allDirty_ = true;
        updateCount_ = 0;
        BuildChildren();
    }

    [[nodiscard]] size_t Size() const noexcept { return count(); }
    [[nodiscard]] bool Empty() const noexcept { return count() == 0; }

    // 以「局部矩阵 + 父索引」重建整层（矩阵局部模式）。供已经算好局部模型矩阵、需按实体
    // 父子关系级联世界的调用方使用（如 ECS 渲染路径）：避免重复的欧拉/四元数往返，且与调用
    // 方既有的 ComputeEntityModelMatrix 逐位一致。父索引为 -1 或越界视为根。
    void ResetMatrices(std::vector<glm::mat4> locals, std::vector<int32_t> parents)
    {
        matrixMode_ = true;
        mLocal_ = std::move(locals);
        mParent_ = std::move(parents);
        const size_t n = count();
        world_.assign(n, glm::mat4(1.0f));
        valid_.assign(n, 0);
        dirtyRoot_.assign(n, 0);
        stamp_.assign(n, 0);
        dirtyRoots_.clear();
        gen_ = 0;
        allDirty_ = true;
        updateCount_ = 0;
        BuildChildren();
    }

    [[nodiscard]] const std::vector<Transform>& Locals() const noexcept { return local_; }
    [[nodiscard]] const Transform& Local(size_t i) const { return local_[i]; }
    [[nodiscard]] bool IsValid(size_t i) const { return valid_[i] != 0; }
    // 需在 UpdateWorld() 之后读取；返回缓存的世界矩阵。
    [[nodiscard]] const glm::mat4& World(size_t i) const { return world_[i]; }
    [[nodiscard]] const std::vector<glm::mat4>& WorldMatrices() const noexcept { return world_; }
    [[nodiscard]] glm::vec3 WorldPositionOf(size_t i) const { return glm::vec3(world_[i][3]); }

    // 就地设置局部变换：标记该节点及其子树待重算（层级拓扑不变，无需重建子表）。
    void SetLocal(size_t i, const Transform& t)
    {
        local_[i] = t;
        MarkDirty(i);
    }
    void SetTranslation(size_t i, const glm::vec3& v)
    {
        local_[i].translation = v;
        MarkDirty(i);
    }
    void SetRotation(size_t i, const glm::quat& q)
    {
        local_[i].rotation = q;
        MarkDirty(i);
    }
    void SetScale(size_t i, const glm::vec3& v)
    {
        local_[i].scale = v;
        MarkDirty(i);
    }

    // 重设父节点会改变层级拓扑，需重建子表；随后标记新子树待重算。
    void SetParent(size_t i, int32_t parent)
    {
        local_[i].parent = parent;
        BuildChildren();
        MarkDirty(i);
    }

    // 矩阵局部模式：就地更新节点局部矩阵，仅标记该子树待重算（拓扑不变）。
    void SetLocalMatrix(size_t i, const glm::mat4& m)
    {
        mLocal_[i] = m;
        MarkDirty(i);
    }

    // 矩阵局部模式：重设父索引（拓扑变更，重建子表后标记子树待重算）。
    void SetParentIndex(size_t i, int32_t parent)
    {
        mParent_[i] = parent;
        BuildChildren();
        MarkDirty(i);
    }

    // 仅重算脏子树；返回本次实际重算的节点数（空闲帧为 0）。
    size_t UpdateWorld()
    {
        if (allDirty_)
        {
            ForceRecomputeAll();
            allDirty_ = false;
            return updateCount_;
        }
        updateCount_ = 0;
        if (dirtyRoots_.empty())
            return 0;
        ++gen_;
        for (const size_t r : dirtyRoots_)
            RecomputeSubtree(r, updateCount_);
        for (const size_t r : dirtyRoots_)
            dirtyRoot_[r] = 0;
        dirtyRoots_.clear();
        return updateCount_;
    }

    // 全量强制重算（对照基准 / 冷启动），返回重算节点数。
    // 自根向叶做 DFS，父先于子计算，故不依赖「父索引 < 子索引」的隐式约定。
    size_t ForceRecomputeAll()
    {
        const size_t n = count();
        updateCount_ = 0;
        ++gen_;
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t p = parentOf(i);
            if (p == Transform::kNoParent || p < 0 || static_cast<size_t>(p) >= n)
                RecomputeSubtree(i, updateCount_);
        }
        for (size_t i = 0; i < n; ++i)
        {
            valid_[i] = 1;
            dirtyRoot_[i] = 0;
        }
        dirtyRoots_.clear();
        return updateCount_;
    }

    [[nodiscard]] bool IsDirtyRoot(size_t i) const { return dirtyRoot_[i] != 0; }
    [[nodiscard]] size_t DirtyRootCount() const noexcept { return dirtyRoots_.size(); }
    [[nodiscard]] size_t LastRecomputed() const noexcept { return updateCount_; }

  private:
    // 依据父索引重建子节点邻接表（越界父索引视为根，不入任何子表）。
    void BuildChildren()
    {
        const size_t n = count();
        children_.assign(n, {});
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t p = parentOf(i);
            if (p != Transform::kNoParent && p >= 0 && static_cast<size_t>(p) < n)
                children_[static_cast<size_t>(p)].push_back(i);
        }
    }

    // 标记 i 为待重算子树根：若已有祖先根覆盖 i 则跳过；并移除 i 的后代根（被 i 覆盖）。
    // 保证 dirtyRoots_ 中的根两两不存在祖先/后代关系，从而各自子树互不相交。
    void MarkDirty(size_t i)
    {
        if (allDirty_ || dirtyRoot_[i])
            return;
        const size_t n = count();
        size_t guard = 0;
        for (int32_t p = parentOf(i); p != Transform::kNoParent && p >= 0 && static_cast<size_t>(p) < n;
             p = parentOf(static_cast<size_t>(p)))
        {
            if (dirtyRoot_[static_cast<size_t>(p)])
                return; // 祖先已是脏根，i 将在祖先重算时被覆盖
            if (++guard > n)
                break; // 防环兜底
        }
        RemoveDescendantRoots(i);
        dirtyRoot_[i] = 1;
        dirtyRoots_.push_back(i);
    }

    void RemoveDescendantRoots(size_t i, size_t depth = 0)
    {
        if (depth > count())
            return; // 防环兜底
        for (const size_t c : children_[i])
        {
            if (dirtyRoot_[c])
            {
                dirtyRoot_[c] = 0;
                const auto it = std::find(dirtyRoots_.begin(), dirtyRoots_.end(), c);
                if (it != dirtyRoots_.end())
                    dirtyRoots_.erase(it);
            }
            RemoveDescendantRoots(c, depth + 1);
        }
    }

    // 重算以 idx 为根的整个子树（父世界取自缓存或本趟已算好的新值）。
    void RecomputeSubtree(size_t idx, size_t& nRecomputed)
    {
        if (gen_ != 0 && stamp_[idx] == gen_)
            return; // 本趟已访问（互不相交子树下不会发生，仅作防环兜底）
        stamp_[idx] = gen_;
        const glm::mat4 local = localMatrixOf(idx);
        const int32_t p = parentOf(idx);
        const size_t n = count();
        if (p == Transform::kNoParent || p < 0 || static_cast<size_t>(p) >= n)
            world_[idx] = local; // 悬空父索引回退为局部矩阵（与 ComputeAllWorldMatrices 一致）
        else
            world_[idx] = world_[static_cast<size_t>(p)] * local;
        valid_[idx] = 1;
        ++nRecomputed;
        for (const size_t c : children_[idx])
            RecomputeSubtree(c, nRecomputed);
    }

    // TRS 局部模式（默认）与矩阵局部模式共用的节点数。
    [[nodiscard]] size_t count() const noexcept { return matrixMode_ ? mLocal_.size() : local_.size(); }
    // 父索引（两种模式统一读取）。
    [[nodiscard]] int32_t parentOf(size_t i) const noexcept { return matrixMode_ ? mParent_[i] : local_[i].parent; }
    // 局部矩阵（两种模式统一读取）。
    [[nodiscard]] glm::mat4 localMatrixOf(size_t i) const
    {
        return matrixMode_ ? mLocal_[i] : LocalToMatrix(local_[i]);
    }

    std::vector<Transform> local_;
    std::vector<glm::mat4> mLocal_; // 矩阵局部模式的局部矩阵
    std::vector<int32_t> mParent_;  // 矩阵局部模式的父索引
    bool matrixMode_ = false;
    std::vector<glm::mat4> world_;
    std::vector<uint8_t> valid_;
    std::vector<uint8_t> dirtyRoot_;
    std::vector<uint32_t> stamp_;
    std::vector<std::vector<size_t>> children_;
    std::vector<size_t> dirtyRoots_;
    uint32_t gen_ = 0;
    bool allDirty_ = true;
    size_t updateCount_ = 0;
};
} // namespace BigHero::Scene
