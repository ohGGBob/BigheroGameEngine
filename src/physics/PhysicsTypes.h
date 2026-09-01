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
    uint32_t userTag = UINT32_MAX;           // 用户标签（射线命中时返回，用于关联场景物体）
};

// 射线命中结果
struct RaycastHit
{
    bool hit = false;
    uint32_t userTag = UINT32_MAX; // 命中刚体的 userTag
    glm::vec3 point{0.0f};         // 世界空间命中点
    glm::vec3 normal{0.0f};        // 世界空间命中法线
    float distance = 0.0f;         // 命中距离
};

// 关节类型
enum class JointType : uint8_t
{
    Fixed = 0,         // 固定关节：两物体完全绑定
    Hinge = 1,         // 铰链关节：绕轴旋转（门、摆锤）
    BallAndSocket = 2, // 球窝关节：任意方向旋转（肩膀、链条）
    Slider = 3         // 滑块关节：沿轴平移（抽屉、活塞）
};

// 关节创建配置
struct JointConfig
{
    JointType type = JointType::Fixed;
    uint32_t body1Id = UINT32_MAX;    // 第一个刚体 ID（PhysicsEngine 内部 ID）
    uint32_t body2Id = UINT32_MAX;    // 第二个刚体 ID
    glm::vec3 anchor{0.0f};           // 锚点（世界空间）
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // 旋转/滑动轴（世界空间，铰链/滑块用）
    bool collisionEnabled = false;    // 关节两物体是否互相碰撞（通常关闭）
};

// 关节运行时信息（用于调试渲染和编辑器查询）
struct JointInfo
{
    JointType type;
    uint32_t body1Id;
    uint32_t body2Id;
    glm::vec3 anchor;
    glm::vec3 axis;
};

// 场景级关节定义（引用场景物体索引，序列化用）
struct SceneJoint
{
    uint32_t objectA = UINT32_MAX; // 场景物体索引 A
    uint32_t objectB = UINT32_MAX; // 场景物体索引 B
    JointType type = JointType::Fixed;
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // 旋转/滑动轴（世界空间）
};

// 调试线段（世界空间起点/终点 + 颜色）
struct DebugLine
{
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 color;
};
} // namespace BigHero::Physics
