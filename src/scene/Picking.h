#pragma once
#include "scene/Scene.h"
#include <glm/glm.hpp>
#include <limits>
#include <vector>

namespace BigHero::Scene
{
// 射线-AABB相交测试（slab法）。命中返回沿射线的最近距离t（>0），未命中返回false
inline bool RayAabb(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& aabbMin, const glm::vec3& aabbMax,
                    float& outT)
{
    // 分量为0时inv为无穷大，slab逻辑仍正确（除非射线平行且在板外，t区间为空）
    const glm::vec3 inv = 1.0f / dir;
    const glm::vec3 t0s = (aabbMin - origin) * inv;
    const glm::vec3 t1s = (aabbMax - origin) * inv;

    const glm::vec3 tsmaller = glm::min(t0s, t1s);
    const glm::vec3 tbigger = glm::max(t0s, t1s);

    const float tmin = glm::max(glm::max(tsmaller.x, tsmaller.y), tsmaller.z);
    const float tmax = glm::min(glm::min(tbigger.x, tbigger.y), tbigger.z);

    if (tmax < tmin || tmax < 0.0f)
        return false;

    outT = (tmin > 0.0f) ? tmin : tmax;
    return true;
}

// 物体包围盒半边长：立方体0.5x0.5x0.5；圆环体按主半径+管半径近似
inline glm::vec3 ObjectHalfExtent(const SceneObject& obj)
{
    if (obj.meshId == 0)
        return glm::vec3(0.5f) * obj.scale;
    return glm::vec3(1.45f, 0.5f, 1.45f) * obj.scale;
}

// 从场景中拾取最近物体，未命中返回-1
inline int PickObject(const glm::vec3& origin, const glm::vec3& dir, const std::vector<SceneObject>& scene)
{
    int best = -1;
    float bestT = std::numeric_limits<float>::max();
    for (size_t i = 0; i < scene.size(); ++i)
    {
        const glm::vec3 half = ObjectHalfExtent(scene[i]);
        float t = 0.0f;
        if (RayAabb(origin, dir, scene[i].position - half, scene[i].position + half, t))
        {
            if (t < bestT)
            {
                bestT = t;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}
} // namespace BigHero::Scene
