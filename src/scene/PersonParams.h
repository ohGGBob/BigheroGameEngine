#pragma once
// 人物外观/体型/姿态参数（纯数据结构，EditorPanel 与 PersonHost 共用，无引擎依赖）。
#include <glm/glm.hpp>

namespace BigHero::Scene
{
// 人物基础姿态（Todo 3 姿态参数；Todo 4 动作状态机在其上叠加连贯演绎）
enum class PersonPose : int
{
    Standing = 0, // 站立
    Walking = 1,  // 行走（原地踏步摆臂）
    Wave = 2,     // 抬手（右手侧平举挥动）
    Turn = 3,     // 转身（原地绕 Y 自转）
    Squat = 4,    // 蹲坐
};

// 人物生成参数：外观（体型/肤色/衣色/发色/眼色）+ 姿态
struct PersonParams
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};     // 脚底世界位置（y≈0 贴地）
    float height = 1.55f;                     // 总身高（米）
    float headScale = 1.0f;                   // 头部比例（Q 版大头基值放大/缩小）
    float bodyWidth = 1.0f;                   // 躯干宽度
    float limbWidth = 1.0f;                   // 四肢粗细
    glm::vec3 skinTint{0.93f, 0.75f, 0.62f};  // 肤色
    glm::vec3 clothTint{0.30f, 0.52f, 0.80f}; // 衣色（躯干+裤）
    glm::vec3 hairTint{0.33f, 0.22f, 0.16f};  // 发色
    glm::vec3 eyeTint{0.10f, 0.10f, 0.13f};   // 眼色
    PersonPose pose = PersonPose::Standing;   // 当前姿态
    float poseSpeed = 1.0f;                   // 动作速度倍率（行走步频/挥手频率/转身角速度）

    // UI 辅助：初始化（重置为默认）
    static PersonParams Default() { return PersonParams{}; }
};
} // namespace BigHero::Scene
