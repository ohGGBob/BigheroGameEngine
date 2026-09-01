#pragma once
// 屏幕空间 Gizmo 纯逻辑数学（不依赖 ImGui / Vulkan，可离线单测）。
// 选中物体后，将其世界坐标投影到屏幕，沿世界轴绘制手柄并换算鼠标拖拽为
// 世界空间平移/旋转量。所有函数均为纯函数，便于单元测试覆盖。
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace BigHero::Editor
{
// 操作模式：无 / 平移 / 旋转
enum class GizmoMode : int
{
    None = 0,
    Translate = 1,
    Rotate = 2
};

// 手柄轴：世界 X / Y / Z / 无
enum class GizmoAxis : int
{
    X = 0,
    Y = 1,
    Z = 2,
    None = 3
};

// 轴 -> 世界单位方向向量
inline glm::vec3 GizmoAxisVector(GizmoAxis axis)
{
    switch (axis)
    {
    case GizmoAxis::X:
        return {1.0f, 0.0f, 0.0f};
    case GizmoAxis::Y:
        return {0.0f, 1.0f, 0.0f};
    case GizmoAxis::Z:
        return {0.0f, 0.0f, 1.0f};
    default:
        return {0.0f, 0.0f, 0.0f};
    }
}

// 世界坐标 -> 屏幕像素（左上原点，ImGui 约定：y 向下）。
// 点在相机后方（clip.w <= 0）或超出视锥时返回 { -1e9, -1e9 } 表示无效。
inline glm::vec2 ProjectWorldToScreen(const glm::vec3& world, const glm::mat4& viewProj, const glm::vec2& viewport)
{
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-6f)
        return glm::vec2(-1e9f, -1e9f);
    const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
    // NDC [-1,1] -> [0,width]x[0,height]，y 翻转（屏幕上 = ndc.y=+1）
    return glm::vec2((ndc.x * 0.5f + 0.5f) * viewport.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * viewport.y);
}

// 轴拾取：屏幕鼠标 mouse 距三条世界轴（origin 出发）线段最近者小于 thresholdPx 时返回该轴，
// 否则返回 None。armPx 为手柄臂长（像素）。
inline GizmoAxis PickAxis(const glm::vec3& originWorld, const glm::mat4& viewProj, const glm::vec2& viewport,
                          const glm::vec2& mouse, float thresholdPx, float armPx = 80.0f)
{
    const glm::vec2 o = ProjectWorldToScreen(originWorld, viewProj, viewport);
    if (o.x < -1e8f)
        return GizmoAxis::None;
    const GizmoAxis axes[3] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    GizmoAxis best = GizmoAxis::None;
    float bestDist = thresholdPx;
    for (GizmoAxis ax : axes)
    {
        const glm::vec2 tip = ProjectWorldToScreen(originWorld + GizmoAxisVector(ax), viewProj, viewport);
        if (tip.x < -1e8f)
            continue;
        const glm::vec2 dir = glm::normalize(tip - o);
        const glm::vec2 rel = mouse - o;
        // 投影鼠标到轴线段 [0, armPx]
        const float t = glm::clamp(glm::dot(rel, dir), 0.0f, armPx);
        const glm::vec2 proj = o + dir * t;
        const float d = glm::length(mouse - proj);
        if (d < bestDist)
        {
            bestDist = d;
            best = ax;
        }
    }
    return best;
}

// 平移拖拽：屏幕鼠标位移 mouseDeltaPx 沿指定世界轴对应的屏幕方向，换算为世界位移量（带符号）。
// 当轴在屏幕上近乎垂直于视线（投影长度过小）时返回 0，避免数值不稳定。
inline float TranslateDragDelta(const glm::vec3& originWorld, GizmoAxis axis, const glm::mat4& viewProj,
                                const glm::vec2& viewport, const glm::vec2& mouseDeltaPx)
{
    if (axis == GizmoAxis::None)
        return 0.0f;
    const glm::vec3 axisDir = GizmoAxisVector(axis);
    const glm::vec2 o = ProjectWorldToScreen(originWorld, viewProj, viewport);
    const glm::vec2 tip = ProjectWorldToScreen(originWorld + axisDir, viewProj, viewport);
    const glm::vec2 axisScreen = tip - o;
    const float pxPerUnit = glm::length(axisScreen);
    if (pxPerUnit < 1e-3f)
        return 0.0f;
    const glm::vec2 dir = axisScreen / pxPerUnit; // 屏幕方向（含符号）
    return glm::dot(mouseDeltaPx, dir) / pxPerUnit;
}

// 旋转拖拽：给定手柄中心（屏幕）、上一帧鼠标、当前鼠标，返回绕中心扫过的带符号角度（弧度）。
// 符号由两向量叉积 z 分量决定（屏幕 y 向下，顺时针为正）。
inline float RotateDragAngle(const glm::vec2& centerScreen, const glm::vec2& fromMouse, const glm::vec2& toMouse)
{
    const glm::vec2 a = fromMouse - centerScreen;
    const glm::vec2 b = toMouse - centerScreen;
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la < 1e-4f || lb < 1e-4f)
        return 0.0f;
    const float cosA = glm::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f);
    float ang = std::acos(cosA);
    const float cross = a.x * b.y - a.y * b.x;
    if (cross < 0.0f)
        ang = -ang;
    return ang;
}
} // namespace BigHero::Editor
