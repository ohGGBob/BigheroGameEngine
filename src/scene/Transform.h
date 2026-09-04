#pragma once
// 组件化场景变换层级（Transform Hierarchy）。
// 纯 CPU、仅依赖 glm 头文件，可离线运行与单元测试（不触碰 Vulkan/GPU）。
//
// 约定：
//   - 右撇 Y-up 坐标系，弧度制（工程已定义 GLM_FORCE_RADIANS）。
//   - 局部->世界矩阵组合顺序：M_local = T * R * S（先缩放、再旋转、最后平移），
//     与世界矩阵相乘 obj->world 的列向量变换一致：world = parent_world * M_local。
//   - 层级以“扁平数组 + parent 索引”组织（面向未来 glTF/ECS 的骨架/场景树）。

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
} // namespace BigHero::Scene
