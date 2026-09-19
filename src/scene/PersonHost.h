#pragma once
// PersonHost：场景人物宿主（Todo 3）。
// 职责：把「Q 版人物」作为一组球/胶囊部件实体挂成 ECS 父子骨骼树，
//       每帧按姿态参数把各部件的局部旋转/位置写回 ECS（Transform 局部值），
//       支持按人物索引删除整组部件。纯 CPU、仅标准库 + glm + EcsScene，可离线单元测试。
//
// 部件布局（人物 = 1 根 + 7 子部件，局部坐标，Y 向上，脚底在 position.y）：
//   body  —— 胶囊（躯干，根节点，世界位置=position+躯干中心抬升）
//   head  —— 球（挂 body 顶部）
//   hair  —— 球（挂 head，顶部后方，Q 版呆毛感）
//   eyeL/eyeR —— 小球（挂 head 前侧）
//   armL/armR —— 胶囊（挂 body 肩部两侧）
//   legL/legR —— 胶囊（挂 body 底侧，脚底贴地）
#include "scene/EcsScene.h"
#include "scene/PersonParams.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

namespace BigHero::Scene
{
class PersonHost
{
  public:
    // 部件稳定序（与 Person 内 parts_ 数组下标一致；根在 0 位）
    enum PartKind : int
    {
        kBody = 0,
        kHead,
        kHair,
        kEyeL,
        kEyeR,
        kArmL,
        kArmR,
        kLegL,
        kLegR,
        kPartCount = 9
    };

    explicit PersonHost(EcsScene& scene) : scene_(scene) {}

    // 在场景稳定序末尾生成一个新人物。返回该人物的索引（personList 下标），失败返回 -1。
    [[nodiscard]] int SpawnPerson(const PersonParams& params);

    // 删除第 personIdx 个人物：整组部件按稳定序下标从大到小销毁（防下标漂移）。
    void RemovePerson(int personIdx);

    // 每帧推进：按姿态参数把各部件局部旋转/位置写回 ECS 并推进动画相位。
    void Update(float dt);

    // 更新某人物参数（面板滑块）：重建尺度/颜色（局部布局）并保持姿态。
    void ApplyParams(int personIdx, const PersonParams& params);

    // 请求切换目标姿态（状态机驱动）：从当前渲染姿态平滑跨过渡到目标姿态，
    // transitionSec 为跨过渡时长（秒）。若目标已是当前姿态且过渡完成则忽略。
    void RequestPose(int personIdx, PersonPose pose, float transitionSec = 0.3f);

    // 读取某人物当前参数（选中回显用；越界返回默认）。
    [[nodiscard]] PersonParams GetParams(int personIdx) const;

    // 删除场景中所有人物（场景全量重建/读档时调用，order_ 内实体已不存在）。
    void Clear() { persons_.clear(); }

    [[nodiscard]] size_t Count() const { return persons_.size(); }
    // 第 personIdx 个人物根节点在场景稳定序中的下标（选中/高亮用），-1=不存在
    [[nodiscard]] int RootOrderIndex(int personIdx) const;
    // 第 personIdx 个人物的全部部件稳定序下标（删除整组用，未命中返回空）
    [[nodiscard]] std::vector<int> PartOrderIndices(int personIdx) const;

private:
    struct Person
    {
        std::array<Core::Entity, kPartCount> parts{}; // 各部件实体
        PersonParams params;
        float animTime = 0.0f;    // 动画相位（行走/挥手用）
        float yaw = 0.0f;         // 转身姿态累积（度）
        float squatAmount = 0.0f; // 蹲坐插值（0..1，缓入缓出）

        // 姿态状态机（Todo 2）：从 currentPose 平滑过渡到 targetPose
        PersonPose currentPose = PersonPose::Standing; // 过渡起点（渲染当前）
        PersonPose targetPose = PersonPose::Standing;  // 过渡目标（= params.pose）
        float poseBlend = 1.0f;                        // 过渡进度 0..1（1=已就位）
        float poseTransition = 0.3f;                   // 过渡时长（秒）
    };

    // 计算单个姿态下的关节旋转（度）；与状态机无关，供过渡混合采样
    [[nodiscard]] static std::array<float, kPartCount> ComputePoseRotations(PersonPose pose, const Person& p);
    [[nodiscard]] std::array<float, kPartCount> ComputeJointRotations(const Person& p, float dt) const;
    [[nodiscard]] int OrderOf(Core::Entity e) const;

    EcsScene& scene_;
    std::vector<Person> persons_;
};

// 部件局部尺寸（半径/半高），由 params 派生；高度以 params.height 为基准
struct Dims
{
    float bodyScale = 0.35f;  // 躯干胶囊 scale（胶囊单位：半径0.5 柱高0.7，总高1.7*scale）
    float headScale = 0.5f;   // 头球 scale（直径 1.0*scale）
    float limbScale = 0.22f;  // 四肢胶囊 scale（半径 0.5*scale）
    float eyeScale = 0.045f;  // 眼球 scale
    float hairScale = 0.18f;  // 头发球 scale
    float bodyCenterY = 0.9f; // 躯干中心相对脚底的高度（身高归一，乘 height）
};
} // namespace BigHero::Scene