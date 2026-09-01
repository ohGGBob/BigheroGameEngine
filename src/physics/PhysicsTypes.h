#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace BigHero::Physics
{
// 刚体类型：无物理 / 静态（不可动）/ 动态（受物理驱动）/ 运动学（由代码驱动，可推动动态体）
enum class BodyType : uint8_t
{
    None = 0,
    Static = 1,
    Dynamic = 2,
    Kinematic = 3
};

// 碰撞形状类型：盒 / 球 / 胶囊
enum class ShapeType : uint8_t
{
    Box = 0,
    Sphere = 1,
    Capsule = 2
};

// 刚体创建配置
struct BodyConfig
{
    BodyType type = BodyType::None;
    ShapeType shape = ShapeType::Box;
    float mass = 1.0f;                       // 动态体质量（kg）
    float friction = 0.5f;                   // 摩擦系数 0无摩擦~1高摩擦
    float restitution = 0.0f;                // 弹性系数 0完全非弹性~1完全弹性
    glm::vec3 halfExtents{0.5f, 0.5f, 0.5f}; // 盒半尺寸（世界空间，已含缩放）
    float radius = 0.5f;                     // 球/胶囊半径
    float capsuleHeight = 1.0f;              // 胶囊圆柱段高度（不含两端半球）
};

// 调试线段（世界空间起点/终点 + 颜色）
struct DebugLine
{
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 color;
};
} // namespace BigHero::Physics
