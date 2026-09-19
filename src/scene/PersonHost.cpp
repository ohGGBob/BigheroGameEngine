#include "scene/PersonHost.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace BigHero::Scene
{
namespace
{
// 由参数派生部件尺寸（比例以身高为基准，Q 版大头短身）
Dims ComputeDims(const PersonParams& p)
{
    Dims d;
    const float h = p.height;
    d.bodyScale = 0.30f * h * p.bodyWidth;              // 躯干胶囊总高 1.7*scale，半径 0.5*scale
    d.headScale = 0.27f * h * p.headScale;              // 头球直径 1.0*scale（Q 版大头）
    d.limbScale = 0.11f * h * p.limbWidth;              // 四肢胶囊
    d.eyeScale = 0.028f * h;
    d.hairScale = 0.13f * h * p.headScale;              // 头顶发球
    d.bodyCenterY = 1.7f * d.limbScale + 0.85f * d.bodyScale; // 腿高 + 躯干半高
    return d;
}

// 部件局部位置（相对父）。返回与 kPartCount 对齐的数组，根使用世界坐标。
std::array<glm::vec3, PersonHost::kPartCount> ComputeLocalPositions(const PersonParams& p, const Dims& d)
{
    std::array<glm::vec3, PersonHost::kPartCount> pos{};
    const glm::vec3 zero{0.0f};
    pos[PersonHost::kBody] = p.position + glm::vec3(0.0f, d.bodyCenterY, 0.0f); // 根：世界（脚底贴地）
    pos[PersonHost::kHead] = glm::vec3(0.0f, 0.85f * d.bodyScale + 0.42f * d.headScale, 0.0f); // 悬于躯干顶
    pos[PersonHost::kHair] = glm::vec3(0.0f, 0.62f * d.headScale, -0.45f * d.headScale);        // 顶后方
    const float eyeZ = 0.44f * d.headScale;
    pos[PersonHost::kEyeL] = glm::vec3(-0.34f * d.headScale, 0.14f * d.headScale, eyeZ);
    pos[PersonHost::kEyeR] = glm::vec3(+0.34f * d.headScale, 0.14f * d.headScale, eyeZ);
    const float armX = 0.50f * d.bodyScale + 0.5f * d.limbScale; // 肩宽 = 躯干半径 + 臂半径
    pos[PersonHost::kArmL] = glm::vec3(-armX, 0.85f * (d.bodyScale - d.limbScale), 0.0f);
    pos[PersonHost::kArmR] = glm::vec3(+armX, 0.85f * (d.bodyScale - d.limbScale), 0.0f);
    const float legX = 0.45f * d.bodyScale;
    pos[PersonHost::kLegL] = glm::vec3(-legX, -0.85f * (d.bodyScale + d.limbScale), 0.0f);
    pos[PersonHost::kLegR] = glm::vec3(+legX, -0.85f * (d.bodyScale + d.limbScale), 0.0f);
    return pos;
}

// 组装一个部件 SceneObject（meshId 3=球 / 4=胶囊；无物理；不参与自转）
SceneObject MakePart(uint32_t meshId, const glm::vec3& pos, float scale, const glm::vec3& tint,
                     int32_t parentIndex)
{
    SceneObject o;
    o.position = pos;
    o.scale = scale;
    o.tint = tint;
    o.meshId = meshId;
    o.metallic = 0.0f;
    o.roughness = 0.65f;
    o.parentIndex = parentIndex;
    o.physicsType = Physics::BodyType::None; // 人物部件不参与物理碰撞
    return o;
}
} // namespace

int PersonHost::SpawnPerson(const PersonParams& params)
{
    const Dims d = ComputeDims(params);
    const auto pos = ComputeLocalPositions(params, d);

    Person person;
    person.params = params;

    // 创建顺序保证父先于子：body(根) → head → hair/eyes → arms/legs
    // 先记住父实体的稳定序下标（绝对下标），给子部件挂接用
    // meshId：球=3，胶囊=4
    const size_t bodyAbs = scene_.ObjectCount();
    person.parts[kBody] = scene_.CreateObject(MakePart(4, pos[kBody], d.bodyScale, params.clothTint, -1));
    const size_t headAbs = scene_.ObjectCount();
    person.parts[kHead] = scene_.CreateObject(
        MakePart(3, pos[kHead], d.headScale, params.skinTint, static_cast<int32_t>(bodyAbs)));
    person.parts[kHair] = scene_.CreateObject(
        MakePart(3, pos[kHair], d.hairScale, params.hairTint, static_cast<int32_t>(headAbs)));
    person.parts[kEyeL] = scene_.CreateObject(
        MakePart(3, pos[kEyeL], d.eyeScale, params.eyeTint, static_cast<int32_t>(headAbs)));
    person.parts[kEyeR] = scene_.CreateObject(
        MakePart(3, pos[kEyeR], d.eyeScale, params.eyeTint, static_cast<int32_t>(headAbs)));
    person.parts[kArmL] = scene_.CreateObject(
        MakePart(4, pos[kArmL], d.limbScale, params.clothTint * 0.85f, static_cast<int32_t>(bodyAbs)));
    person.parts[kArmR] = scene_.CreateObject(
        MakePart(4, pos[kArmR], d.limbScale, params.clothTint * 0.85f, static_cast<int32_t>(bodyAbs)));
    person.parts[kLegL] = scene_.CreateObject(
        MakePart(4, pos[kLegL], d.limbScale, params.clothTint * 0.9f, static_cast<int32_t>(bodyAbs)));
    person.parts[kLegR] = scene_.CreateObject(
        MakePart(4, pos[kLegR], d.limbScale, params.clothTint * 0.9f, static_cast<int32_t>(bodyAbs)));

    // 人物根返回后整体是稳定序（父下标引用在 CreateObject 内已解析）
    person.currentPose = params.pose;
    person.targetPose = params.pose;
    person.poseBlend = 1.0f;
    person.poseTransition = 0.3f;
    persons_.push_back(person);
    return static_cast<int>(persons_.size()) - 1;
}

void PersonHost::RemovePerson(int personIdx)
{
    if (personIdx < 0 || personIdx >= static_cast<int>(persons_.size()))
        return;
    const auto indices = PartOrderIndices(personIdx); // 已按当前稳定序重查
    // 从大到小销毁：DestroyAt 压缩下标，降序删除防止漂移错删
    std::vector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (const int orderIdx : sorted)
        scene_.DestroyAt(static_cast<size_t>(orderIdx));
    persons_.erase(persons_.begin() + personIdx);
}

std::array<float, PersonHost::kPartCount> PersonHost::ComputeJointRotations(const Person& p, float dt) const
{
    std::array<float, kPartCount> rot{}; // 部件局部绕 X 轴角度（度）；body 的 [1] 槽表示绕 Y（转身）
    (void)dt;
    switch (p.params.pose)
    {
    case PersonPose::Standing:
        // 自然站立：微收臂，绝无抖动
        rot[kArmL] = -4.0f;
        rot[kArmR] = -4.0f;
        break;
    case PersonPose::Walking:
    {
        // 行走：对侧摆臂 + 抬膝，正弦驱动
        const float swing = std::sin(p.animTime) * 45.0f;
        const float arm = std::sin(p.animTime) * 32.0f;
        rot[kLegL] = swing;
        rot[kLegR] = -swing;
        rot[kArmL] = -arm;
        rot[kArmR] = arm;
        rot[kBody] = 6.0f; // 微前倾
        break;
    }
    case PersonPose::Wave:
        // 抬手：右臂侧平举并小幅度挥舞
        rot[kArmR] = 88.0f + std::sin(p.animTime * 2.0f) * 14.0f;
        rot[kArmL] = -4.0f;
        break;
    case PersonPose::Turn:
        // 转身：绕 Y 在 Update 内累积到成员 yaw，绕 X 置零
        break;
    case PersonPose::Squat:
        // 蹲坐：躯干前倾、腿前屈、臂前伸，squatAmount 缓动插值
        rot[kBody] = 30.0f * p.squatAmount;
        rot[kLegL] = 65.0f * p.squatAmount;
        rot[kLegR] = 65.0f * p.squatAmount;
        rot[kArmL] = 60.0f * p.squatAmount;
        rot[kArmR] = 60.0f * p.squatAmount;
        break;
    }
    return rot;
}

void PersonHost::Update(float dt)
{
    for (Person& p : persons_)
    {
        // 动画相位推进（行走步频/挥手频率，与 poseSpeed 联动）
        p.animTime += dt * 2.4f * p.params.poseSpeed;
        if (p.animTime > 2.0f * glm::pi<float>())
            p.animTime -= 2.0f * glm::pi<float>();

        // 姿态间缓动（蹲坐进入/退出用）
        const float targetSquat = (p.params.pose == PersonPose::Squat) ? 1.0f : 0.0f;
        const float step = std::min(1.0f, dt * 6.0f);
        p.squatAmount += (targetSquat - p.squatAmount) * step;
        if (p.squatAmount < 0.002f && targetSquat == 0.0f)
            p.squatAmount = 0.0f;

        // 转身姿态：持续绕 Y 自转
        if (p.params.pose == PersonPose::Turn)
            p.yaw += dt * 150.0f * p.params.poseSpeed;
        if (p.yaw >= 360.0f)
            p.yaw -= 360.0f;

        const auto rotX = ComputeJointRotations(p, dt);
        const Dims d = ComputeDims(p.params);
        const auto pos = ComputeLocalPositions(p.params, d);

        // 蹲坐重心下移：整体压 low（根位置继续写回）
        glm::vec3 bodyPos = pos[kBody];
        bodyPos.y -= 0.55f * p.params.height * p.squatAmount;

        const int bodyIdx = OrderOf(p.parts[kBody]);
        if (bodyIdx < 0)
            continue; // 实体缺失（读档/重建后）——由 RemovePerson 路径清理
        const int headIdx = OrderOf(p.parts[kHead]);
        const int hairIdx = OrderOf(p.parts[kHair]);
        const int eyeLIdx = OrderOf(p.parts[kEyeL]);
        const int eyeRIdx = OrderOf(p.parts[kEyeR]);
        const int armLIdx = OrderOf(p.parts[kArmL]);
        const int armRIdx = OrderOf(p.parts[kArmR]);
        const int legLIdx = OrderOf(p.parts[kLegL]);
        const int legRIdx = OrderOf(p.parts[kLegR]);

        // 写回根与世界位置（含蹲降）+ Y 轴转身
        scene_.SetObjectPosition(static_cast<size_t>(bodyIdx), bodyPos);
        glm::vec3 bodyRot{rotX[kBody], p.yaw, 0.0f};
        // 旋转顺序为 XYZ 欧拉（ComputeObjectModelMatrix 内 R.x→R.y→R.z），转身放 Y
        scene_.SetObjectRotation(static_cast<size_t>(bodyIdx), bodyRot);
        scene_.SetObjectRotation(static_cast<size_t>(headIdx), glm::vec3(0.0f, 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(hairIdx), glm::vec3(0.0f, 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(eyeLIdx), glm::vec3(0.0f, 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(eyeRIdx), glm::vec3(0.0f, 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(armLIdx), glm::vec3(rotX[kArmL], 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(armRIdx), glm::vec3(rotX[kArmR], 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(legLIdx), glm::vec3(rotX[kLegL], 0.0f, 0.0f));
        scene_.SetObjectRotation(static_cast<size_t>(legRIdx), glm::vec3(rotX[kLegR], 0.0f, 0.0f));
    }

    // 失效人物清理：部件实体已不在场景中（读档/撤销重建）→ 从宿主移除
    for (int i = static_cast<int>(persons_.size()) - 1; i >= 0; --i)
    {
        bool alive = true;
        for (const Core::Entity e : persons_[static_cast<size_t>(i)].parts)
        {
            if (OrderOf(e) < 0)
            {
                alive = false;
                break;
            }
        }
        if (!alive)
            persons_.erase(persons_.begin() + i);
    }
}

void PersonHost::ApplyParams(int personIdx, const PersonParams& params)
{
    if (personIdx < 0 || personIdx >= static_cast<int>(persons_.size()))
        return;
    Person& p = persons_[static_cast<size_t>(personIdx)];
    const bool scaleChanged = (params.height != p.params.height || params.headScale != p.params.headScale ||
                               params.bodyWidth != p.params.bodyWidth || params.limbWidth != p.params.limbWidth);
    p.params = params;

    if (!scaleChanged)
        return; // 仅外观/姿态变化：旋转/位置由 Update 维护，无需重建布局

    // 体型变化：重建部件局部布局（实体不变，只改 TRS/材质）
    const Dims d = ComputeDims(params);
    const auto pos = ComputeLocalPositions(params, d);
    const int bodyIdx = OrderOf(p.parts[kBody]);
    if (bodyIdx < 0)
        return;
    scene_.SetObjectPosition(static_cast<size_t>(bodyIdx), pos[kBody]);
    scene_.SetObjectScale(static_cast<size_t>(bodyIdx), d.bodyScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kHead])), pos[kHead]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kHead])), d.headScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kHair])), pos[kHair]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kHair])), d.hairScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kEyeL])), pos[kEyeL]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kEyeL])), d.eyeScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kEyeR])), pos[kEyeR]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kEyeR])), d.eyeScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kArmL])), pos[kArmL]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kArmL])), d.limbScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kArmR])), pos[kArmR]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kArmR])), d.limbScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kLegL])), pos[kLegL]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kLegL])), d.limbScale);
    scene_.SetObjectPosition(static_cast<size_t>(OrderOf(p.parts[kLegR])), pos[kLegR]);
    scene_.SetObjectScale(static_cast<size_t>(OrderOf(p.parts[kLegR])), d.limbScale);
}

int PersonHost::RootOrderIndex(int personIdx) const
{
    if (personIdx < 0 || personIdx >= static_cast<int>(persons_.size()))
        return -1;
    return OrderOf(persons_[static_cast<size_t>(personIdx)].parts[kBody]);
}

std::vector<int> PersonHost::PartOrderIndices(int personIdx) const
{
    std::vector<int> out;
    if (personIdx < 0 || personIdx >= static_cast<int>(persons_.size()))
        return out;
    out.reserve(kPartCount);
    for (const Core::Entity e : persons_[static_cast<size_t>(personIdx)].parts)
    {
        const int idx = OrderOf(e);
        if (idx >= 0)
            out.push_back(idx);
    }
    return out;
}

int PersonHost::OrderOf(Core::Entity e) const
{
    const auto& order = scene_.Order();
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == e)
            return static_cast<int>(i);
    return -1;
}
} // namespace BigHero::Scene