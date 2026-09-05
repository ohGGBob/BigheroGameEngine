#pragma once
#include <array>
#include <cstddef>
#include <glm/glm.hpp>

namespace BigHero::Render
{
// 视锥体剔除：从视图投影矩阵提取 6 个平面，对物体包围球做相交测试，
// 完全在视锥外的物体可直接跳过绘制（典型前向剔除优化）。
//
// 注意：本引擎启用 GLM_FORCE_DEPTH_ZERO_TO_ONE，投影矩阵产出 Vulkan NDC，
// 其 z 取值范围为 [0, 1]（而非 OpenGL 的 [-1, 1]）。因此近平面方程只需取
// 矩阵的第 3 行（z >= 0），而非 OpenGL 的 (row3 + row2)。
struct Frustum
{
    // 6 个平面：xyz=单位法线（指向视锥内部），w=原点到平面的有符号距离（plane: dot(n, x) + w = 0）
    std::array<glm::vec4, 6> planes{};

    // 平面命名索引（便于单测与调试引用）
    enum : size_t
    {
        kLeft,
        kRight,
        kBottom,
        kTop,
        kNear,
        kFar
    };

    // 从视图投影矩阵（VP = proj * view，列主序 glm::mat4）提取 6 个平面
    static Frustum FromViewProj(const glm::mat4& m)
    {
        Frustum f;

        // glm mat4 列主序：m[col][row]；第 r 行 = (m[0][r], m[1][r], m[2][r], m[3][r])
        const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

        // 裁剪空间不等式（Vulkan NDC，cw > 0 表示在相机前方）：
        //   -cw <= cx <= cw  -> left:  cx + cw >= 0 ; right: cw - cx >= 0
        //   -cw <= cy <= cw  -> bottom: cy + cw >= 0 ; top:  cw - cy >= 0
        //    0  <= cz <= cw  -> near:  cz >= 0       ; far:  cw - cz >= 0
        f.planes[kLeft] = row3 + row0;
        f.planes[kRight] = row3 - row0;
        f.planes[kBottom] = row3 + row1;
        f.planes[kTop] = row3 - row1;
        f.planes[kNear] = row2; // Vulkan：近平面即 z >= 0
        f.planes[kFar] = row3 - row2;

        // 归一化（仅 xyz 长度，w 同步缩放以保持平面方程一致）
        for (glm::vec4& p : f.planes)
        {
            const float len = glm::length(glm::vec3(p));
            if (len > 1e-6f)
                p /= len;
        }
        return f;
    }

    // 球 (center, radius) 是否与视锥相交：任一平面有符号距离 < -radius 表示完全在该平面外侧，可剔除。
    // 返回 false 即完全在视锥外（确定的剔除）；返回 true 表示在内部或部分相交（必须绘制）。
    [[nodiscard]] bool IntersectsSphere(const glm::vec3& center, float radius) const
    {
        for (const glm::vec4& p : planes)
        {
            const float signedDist = glm::dot(glm::vec3(p), center) + p.w;
            if (signedDist < -radius)
                return false;
        }
        return true;
    }
};
} // namespace BigHero::Render
