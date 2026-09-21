// Avatar 骨骼重定向（scene/AvatarRetarget.h）单元测试：纯逻辑、零 GPU，可离线运行。
// 覆盖名称规范化与别名 / 跨命名体系自动匹配 / 静止姿态不变式 / 旋转偏转量搬运 /
// 平移按骨骼长度缩放 / 根骨骼按身高比例 / 未映射骨骼保静止 / 手工映射 / 短输入兜底。
#include "framework/test_common.h"
#include "scene/AvatarRetarget.h"

using namespace BigHero;
using BigHero::Scene::AvatarBone;
using BigHero::Scene::AvatarDefinition;
using BigHero::Scene::AvatarRetarget;

namespace
{
// 来源骨架：Mixamo 命名（"mixamorig:Hips" 形态），身高 1.0，腿较短。
AvatarDefinition MakeMixamo()
{
    AvatarDefinition a;
    a.AddBone("mixamorig:Hips", -1, glm::vec3(0.0f, 1.0f, 0.0f));
    a.AddBone("mixamorig:Spine", 0, glm::vec3(0.0f, 0.2f, 0.0f));
    a.AddBone("mixamorig:LeftUpLeg", 0, glm::vec3(0.1f, -0.1f, 0.0f));
    a.AddBone("mixamorig:LeftLeg", 2, glm::vec3(0.0f, -0.4f, 0.0f));
    a.AddBone("mixamorig:RightUpLeg", 0, glm::vec3(-0.1f, -0.1f, 0.0f));
    a.AddBone("mixamorig:RightLeg", 4, glm::vec3(0.0f, -0.4f, 0.0f));
    a.AddBone("mixamorig:Head", 1, glm::vec3(0.0f, 0.5f, 0.0f));
    return a;
}

// 目标骨架：Unreal 风格命名（"thigh_l" / "spine_01"），身高 1.2，腿更长。
AvatarDefinition MakeUnreal()
{
    AvatarDefinition a;
    a.AddBone("pelvis", -1, glm::vec3(0.0f, 1.2f, 0.0f));
    a.AddBone("spine_01", 0, glm::vec3(0.0f, 0.3f, 0.0f));
    a.AddBone("thigh_l", 0, glm::vec3(0.15f, -0.15f, 0.0f));
    a.AddBone("calf_l", 2, glm::vec3(0.0f, -0.8f, 0.0f));
    a.AddBone("thigh_r", 0, glm::vec3(-0.15f, -0.15f, 0.0f));
    a.AddBone("calf_r", 4, glm::vec3(0.0f, -0.8f, 0.0f));
    a.AddBone("head", 1, glm::vec3(0.0f, 0.6f, 0.0f));
    return a;
}

// 取某骨架的静止姿态数组。
void RestPose(const AvatarDefinition& a, std::vector<glm::vec3>& t, std::vector<glm::quat>& r,
              std::vector<glm::vec3>& s)
{
    t.clear();
    r.clear();
    s.clear();
    for (size_t i = 0; i < a.BoneCount(); ++i)
    {
        t.push_back(a.Bone(i).restTranslation);
        r.push_back(a.Bone(i).restRotation);
        s.push_back(a.Bone(i).restScale);
    }
}

// 比较两个四元数对同一向量的作用（避开 q 与 -q 的双覆盖等价问题）。
void CheckSameRotation(const glm::quat& lhs, const glm::quat& rhs)
{
    const glm::vec3 v(1.0f, 0.35f, -0.2f);
    const glm::vec3 a = lhs * v;
    const glm::vec3 b = rhs * v;
    CHECK_NEAR(a.x, b.x, 1.0e-4f);
    CHECK_NEAR(a.y, b.y, 1.0e-4f);
    CHECK_NEAR(a.z, b.z, 1.0e-4f);
}
} // namespace

TEST_CASE("Avatar.NameNormalization")
{
    // 命名空间前缀（Mixamo 的 "mixamorig:"）被剥掉
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("mixamorig:Hips"), std::string("hips"));
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("a/b/Spine"), std::string("spine"));
    // 大小写与分隔符归一
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("Left_Up-Leg"), std::string("leftupleg"));
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("  Head "), std::string("head"));
    // 常见前缀词剥除
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("Bip01_Pelvis"), std::string("pelvis"));
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("Jnt_Spine"), std::string("spine"));
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("bone_root"), std::string("root"));
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("Bip01 Pelvis"), std::string("pelvis"));
    // 剥除后为空则保留原名（不把骨骼名整个吃掉）
    CHECK_EQ(AvatarDefinition::NormalizeBoneName("bone"), std::string("bone"));

    // 别名收敛到同一规范名
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("hips"), std::string("hips"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("pelvis"), std::string("hips"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("root"), std::string("hips"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("spine1"), std::string("spine"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("spine2"), std::string("chest"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("leftupleg"), std::string("leftupperleg"));
    // 注意：CanonicalBoneName 的输入是「已规范化」的名称（先 NormalizeBoneName 再查别名）
    CHECK_EQ(AvatarDefinition::CanonicalBoneName(AvatarDefinition::NormalizeBoneName("thigh_l")),
             std::string("leftupperleg"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName(AvatarDefinition::NormalizeBoneName("calf_r")),
             std::string("rightlowerleg"));
    CHECK_EQ(AvatarDefinition::CanonicalBoneName(AvatarDefinition::NormalizeBoneName("clavicle_l")),
             std::string("leftshoulder"));
    // 未收录的名称原样返回（精确匹配仍然生效）
    CHECK_EQ(AvatarDefinition::CanonicalBoneName("tail01"), std::string("tail01"));
}

TEST_CASE("Avatar.AutoMatchAcrossNaming")
{
    const AvatarDefinition src = MakeMixamo();
    const AvatarDefinition dst = MakeUnreal();

    const AvatarRetarget r = AvatarRetarget::Build(src, dst);
    CHECK_NEAR(r.Coverage(), 1.0, 1.0e-9); // 7/7 全中

    // 逐根核对映射（规范名对应）
    CHECK_EQ(r.SourceFor(0), 0); // pelvis ↔ Hips
    CHECK_EQ(r.SourceFor(1), 1); // spine_01 ↔ Spine
    CHECK_EQ(r.SourceFor(2), 2); // thigh_l ↔ LeftUpLeg
    CHECK_EQ(r.SourceFor(3), 3); // calf_l ↔ LeftLeg
    CHECK_EQ(r.SourceFor(4), 4); // thigh_r ↔ RightUpLeg
    CHECK_EQ(r.SourceFor(5), 5); // calf_r ↔ RightLeg
    CHECK_EQ(r.SourceFor(6), 6); // head ↔ Head

    // 反向映射与正向一致
    for (size_t i = 0; i < dst.BoneCount(); ++i)
        CHECK_EQ(r.SourceToDest()[static_cast<size_t>(r.SourceFor(i))], static_cast<int>(i));

    // 目标骨架多一根源骨架没有的骨骼（尾巴）→ 覆盖率下降但未映射骨骼不得崩
    AvatarDefinition withTail = MakeUnreal();
    withTail.AddBone("tail_01", 0, glm::vec3(0.0f, -0.1f, -0.3f));
    const AvatarRetarget r2 = AvatarRetarget::Build(src, withTail);
    CHECK_LT(r2.Coverage(), 1.0);
    CHECK_GT(r2.Coverage(), 0.5);
    CHECK_EQ(r2.SourceFor(7), -1); // tail_01 无对应
}

TEST_CASE("Avatar.RestPoseMapsToRestPose")
{
    // 最核心的不变量：喂进来源的静止姿态，输出必须是目标的静止姿态。
    // 这一条挂了，说明重定向引入了系统性的姿态偏移。
    const AvatarDefinition src = MakeMixamo();
    const AvatarDefinition dst = MakeUnreal();
    const AvatarRetarget r = AvatarRetarget::Build(src, dst);

    std::vector<glm::vec3> st, ss;
    std::vector<glm::quat> sr;
    RestPose(src, st, sr, ss);

    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);

    CHECK_EQ(dt.size(), dst.BoneCount());
    CHECK_EQ(dr.size(), dst.BoneCount());
    CHECK_EQ(ds.size(), dst.BoneCount());
    for (size_t i = 0; i < dst.BoneCount(); ++i)
    {
        CHECK_NEAR(dt[i].x, dst.Bone(i).restTranslation.x, 1.0e-5f);
        CHECK_NEAR(dt[i].y, dst.Bone(i).restTranslation.y, 1.0e-5f);
        CHECK_NEAR(dt[i].z, dst.Bone(i).restTranslation.z, 1.0e-5f);
        CheckSameRotation(dr[i], dst.Bone(i).restRotation);
        CHECK_NEAR(ds[i].x, dst.Bone(i).restScale.x, 1.0e-5f);
    }
}

TEST_CASE("Avatar.RotationCarriesDelta")
{
    // 重定向搬运的是「相对静止姿态的偏转量」：
    //   目标姿态相对目标静止的偏转 == 来源姿态相对来源静止的偏转。
    // 这条性质使得 T-pose 骨架的动画能正确驱动 A-pose 骨架。
    // 让两端的静止朝向完全不同（来源 T-pose、目标 A-pose：腿张开 0.6 rad）
    const glm::quat srcRest = glm::angleAxis(0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::quat dstRest = glm::angleAxis(0.6f, glm::vec3(0.0f, 0.0f, 1.0f));
    AvatarDefinition s2;
    s2.AddBone("mixamorig:Hips", -1, glm::vec3(0.0f, 1.0f, 0.0f), srcRest);
    s2.AddBone("mixamorig:LeftUpLeg", 0, glm::vec3(0.1f, -0.1f, 0.0f), srcRest);
    AvatarDefinition d2;
    d2.AddBone("pelvis", -1, glm::vec3(0.0f, 1.2f, 0.0f), dstRest);
    d2.AddBone("thigh_l", 0, glm::vec3(0.15f, -0.15f, 0.0f), dstRest);

    const AvatarRetarget r = AvatarRetarget::Build(s2, d2);
    CHECK_NEAR(r.Coverage(), 1.0, 1.0e-9);

    // 来源：抬起左大腿（绕 X 轴转 -1.1 rad，叠加在静止姿态上）
    const glm::quat lift = glm::angleAxis(-1.1f, glm::vec3(1.0f, 0.0f, 0.0f));
    std::vector<glm::quat> sr{s2.Bone(0).restRotation, s2.Bone(1).restRotation * lift};
    std::vector<glm::vec3> st{s2.Bone(0).restTranslation, s2.Bone(1).restTranslation};
    std::vector<glm::vec3> ss{glm::vec3(1.0f), glm::vec3(1.0f)};

    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(s2, d2, st, sr, ss, dt, dr, ds);

    // 目标大腿「相对自己静止姿态」的偏转，应等于来源的偏转量
    CheckSameRotation(dr[1] * glm::conjugate(d2.Bone(1).restRotation), sr[1] * glm::conjugate(s2.Bone(1).restRotation));
    // 而绝对朝向必须带着目标自己的 A-pose 静止朝向（不是照抄来源的 T-pose）
    CheckSameRotation(dr[1], (sr[1] * glm::conjugate(s2.Bone(1).restRotation)) * d2.Bone(1).restRotation);
    // 根骨骼无偏转 → 保持目标静止姿态
    CheckSameRotation(dr[0], d2.Bone(0).restRotation);
}

TEST_CASE("Avatar.TranslationScalesByBoneLength")
{
    const AvatarDefinition src = MakeMixamo();
    const AvatarDefinition dst = MakeUnreal();
    const AvatarRetarget r = AvatarRetarget::Build(src, dst);

    std::vector<glm::vec3> st, ss;
    std::vector<glm::quat> sr;
    RestPose(src, st, sr, ss);

    // 来源大腿（静止长度 |(0.1,-0.1,0)| ≈ 0.1414）位移 +0.1
    st[2] = src.Bone(2).restTranslation + glm::vec3(0.1f, 0.0f, 0.0f);
    // 目标大腿静止长度 |(0.15,-0.15,0)| ≈ 0.2121 → 比例 1.5 → 位移 +0.15
    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);

    CHECK_NEAR(dt[2].x, dst.Bone(2).restTranslation.x + 0.15f, 1.0e-4f);
    CHECK_NEAR(dt[2].y, dst.Bone(2).restTranslation.y, 1.0e-5f);

    // 小腿：来源 0.4 → 目标 0.8，比例 2
    st[3] = src.Bone(3).restTranslation + glm::vec3(0.0f, -0.05f, 0.0f);
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);
    CHECK_NEAR(dt[3].y, dst.Bone(3).restTranslation.y - 0.1f, 1.0e-4f);

    // 未动的骨骼保持静止
    CHECK_NEAR(dt[5].y, dst.Bone(5).restTranslation.y, 1.0e-5f);
}

TEST_CASE("Avatar.RootUsesHeightRatio")
{
    // 根骨骼没有「骨骼长度」可比，改用整体身高比例（1.2 / 1.0 = 1.2）。
    const AvatarDefinition src = MakeMixamo();
    const AvatarDefinition dst = MakeUnreal();
    const AvatarRetarget r = AvatarRetarget::Build(src, dst);
    CHECK_NEAR(AvatarRetarget::HeightRatio(src, dst), 1.2f, 1.0e-5f);

    std::vector<glm::vec3> st, ss;
    std::vector<glm::quat> sr;
    RestPose(src, st, sr, ss);
    st[0] = src.Bone(0).restTranslation + glm::vec3(0.0f, 0.25f, 0.0f); // 跳起 0.25

    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);
    CHECK_NEAR(dt[0].y, dst.Bone(0).restTranslation.y + 0.25f * 1.2f, 1.0e-4f);

    // 零高度（根骨骼静止高度为 0）时比例退化为 1，不产生 NaN/Inf
    AvatarDefinition flat;
    flat.AddBone("hips", -1, glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK_NEAR(AvatarRetarget::HeightRatio(flat, dst), 1.0f, 1.0e-6f);
    CHECK_NEAR(AvatarRetarget::HeightRatio(src, flat), 1.0f, 1.0e-6f);
}

TEST_CASE("Avatar.UnmappedBoneKeepsRest")
{
    // 目标有、来源没有的骨骼必须保持静止姿态——宁可不动，也不乱动。
    const AvatarDefinition src = MakeMixamo();
    AvatarDefinition dst = MakeUnreal();
    const int tail = dst.AddBone("tail_01", 0, glm::vec3(0.0f, -0.1f, -0.3f));
    CHECK_GE(tail, 0);

    const AvatarRetarget r = AvatarRetarget::Build(src, dst);
    CHECK_EQ(r.SourceFor(static_cast<size_t>(tail)), -1);

    std::vector<glm::vec3> st, ss;
    std::vector<glm::quat> sr;
    RestPose(src, st, sr, ss);
    st[0] += glm::vec3(1.0f, 2.0f, 3.0f); // 整体大幅运动，尾巴也不该跟着乱飞

    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);
    CHECK_NEAR(dt[static_cast<size_t>(tail)].x, dst.Bone(static_cast<size_t>(tail)).restTranslation.x, 1.0e-5f);
    CHECK_NEAR(dt[static_cast<size_t>(tail)].y, dst.Bone(static_cast<size_t>(tail)).restTranslation.y, 1.0e-5f);
    CHECK_NEAR(dt[static_cast<size_t>(tail)].z, dst.Bone(static_cast<size_t>(tail)).restTranslation.z, 1.0e-5f);
}

TEST_CASE("Avatar.ManualMappingOverride")
{
    const AvatarDefinition src = MakeMixamo();
    const AvatarDefinition dst = MakeUnreal();

    // 构造期传入手工修正：让 head 改去跟 Spine（荒谬但能验证优先级）
    const AvatarRetarget r = AvatarRetarget::Build(src, dst, {{"head", "mixamorig:Spine"}, {"tail_01", "nonexistent"}});
    CHECK_EQ(r.SourceFor(6), 1); // head → Spine（覆盖自动匹配的 6）

    // 运行期 Link / Unlink
    AvatarRetarget m = AvatarRetarget::Build(src, dst);
    CHECK_EQ(m.SourceFor(0), 0);
    m.Unlink(0);
    CHECK_EQ(m.SourceFor(0), -1);
    CHECK_EQ(m.SourceToDest()[0], -1); // 反向也应断开
    // 反向表只记「首个引用该 src 骨骼的 dst」：验证前先把 src 3 让出来
    m.Unlink(0);
    m.Unlink(3);
    CHECK_EQ(m.SourceToDest()[3], -1); // src 3（LeftLeg）已无人引用
    m.Link(3, 0);                      // calf_l 改去跟 Hips
    CHECK_EQ(m.SourceFor(3), 0);
    CHECK_EQ(m.SourceToDest()[0], 3); // 反向随之建立

    // 越界操作被安全忽略
    m.Link(999, 0);
    m.Link(-1, 0);
    m.Unlink(999);
    CHECK_EQ(m.DestToSource().size(), dst.BoneCount());
    CHECK_EQ(m.SourceToDest().size(), src.BoneCount());

    // 空骨架
    const AvatarDefinition empty;
    const AvatarRetarget e = AvatarRetarget::Build(empty, empty);
    CHECK_NEAR(e.Coverage(), 0.0, 1.0e-9);
    CHECK_EQ(e.SourceFor(0), -1); // 越界查询返回 -1
}

TEST_CASE("Avatar.DefinitionValidation")
{
    AvatarDefinition a;
    // 父索引必须指向已存在的骨骼（或 -1）
    CHECK_EQ(a.AddBone(AvatarBone{"orphan", 3, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)}),
             -1);
    CHECK_EQ(a.AddBone(AvatarBone{"badneg", -2, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)}),
             -1);
    CHECK_EQ(a.BoneCount(), 0u);

    // 注意：CHECK_GE 等关系宏会把实参求值两次，带副作用的调用必须先落到变量里再断言。
    const int rootIdx = a.AddBone("root", -1, glm::vec3(0.0f, 1.0f, 0.0f));
    const int childIdx = a.AddBone("child", 0, glm::vec3(0.0f, 0.5f, 0.0f));
    CHECK_GE(rootIdx, 0);
    CHECK_GE(childIdx, 0);
    CHECK_EQ(a.BoneCount(), 2u);
    CHECK(a.IsRoot(0));
    CHECK(!a.IsRoot(1));
    CHECK_EQ(a.Parent(1), 0);
    CHECK_EQ(a.FindBone("child"), 1);
    CHECK_EQ(a.FindBone("nope"), -1);
    CHECK_NEAR(a.RestBoneLength(1), 0.5f, 1.0e-6f);
}

TEST_CASE("Avatar.ShortInputArraysFallBack")
{
    // 动画采样结果短于骨骼数时按静止值兜底，绝不越界读、绝不产生垃圾值。
    const AvatarDefinition src = MakeMixamo();
    const AvatarDefinition dst = MakeUnreal();
    const AvatarRetarget r = AvatarRetarget::Build(src, dst);

    const std::vector<glm::vec3> st{}; // 完全空
    const std::vector<glm::quat> sr{};
    const std::vector<glm::vec3> ss{};

    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);

    CHECK_EQ(dt.size(), dst.BoneCount());
    for (size_t i = 0; i < dst.BoneCount(); ++i)
    {
        CHECK(std::isfinite(dt[i].x) && std::isfinite(dt[i].y) && std::isfinite(dt[i].z));
        CHECK(std::isfinite(dr[i].w) && std::isfinite(dr[i].x));
        CHECK(std::isfinite(ds[i].x));
        CHECK_NEAR(dt[i].y, dst.Bone(i).restTranslation.y, 1.0e-5f); // 退化为静止
    }

    // 部分提供：前 2 根有值，其余兜底
    const std::vector<glm::vec3> st2{glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.0f, 0.2f, 0.0f)};
    std::vector<glm::quat> sr2{glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    std::vector<glm::vec3> ss2{glm::vec3(1.0f), glm::vec3(1.0f)};
    r.RetargetPose(src, dst, st2, sr2, ss2, dt, dr, ds);
    CHECK_NEAR(dt[0].y, 1.2f + (1.5f - 1.0f) * 1.2f, 1.0e-4f);   // 根骨骼按身高比例
    CHECK_NEAR(dt[6].y, dst.Bone(6).restTranslation.y, 1.0e-5f); // 未提供 → 静止
}

TEST_CASE("Avatar.ScaleCarriesRelativeScale")
{
    // 缩放取「相对静止的倍率」：来源放大 2 倍 → 目标也相对自己放大 2 倍。
    AvatarDefinition src, dst;
    src.AddBone("hips", -1, glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    src.AddBone("spine", 0, glm::vec3(0.0f, 0.2f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(2.0f));
    dst.AddBone("pelvis", -1, glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.5f));
    dst.AddBone("spine_01", 0, glm::vec3(0.0f, 0.3f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(3.0f));

    const AvatarRetarget r = AvatarRetarget::Build(src, dst);
    CHECK_NEAR(r.Coverage(), 1.0, 1.0e-9);

    const std::vector<glm::vec3> st{glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.2f, 0.0f)};
    const std::vector<glm::quat> sr{glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    const std::vector<glm::vec3> ss{glm::vec3(2.0f), glm::vec3(6.0f)}; // 相对静止：×2 与 ×3

    std::vector<glm::vec3> dt, ds;
    std::vector<glm::quat> dr;
    r.RetargetPose(src, dst, st, sr, ss, dt, dr, ds);
    CHECK_NEAR(ds[0].x, 1.0f, 1.0e-5f); // 0.5 × 2
    CHECK_NEAR(ds[1].x, 9.0f, 1.0e-5f); // 3.0 × 3

    // 静止缩放为 0 的分量退化为 1，不产生 NaN/Inf
    AvatarDefinition degenerate;
    degenerate.AddBone("hips", -1, glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    const AvatarRetarget r2 = AvatarRetarget::Build(src, degenerate);
    const std::vector<glm::vec3> ss3{glm::vec3(5.0f)};
    std::vector<glm::vec3> dt2, ds2;
    std::vector<glm::quat> dr2;
    r2.RetargetPose(src, degenerate, std::vector<glm::vec3>{glm::vec3(0.0f, 1.0f, 0.0f)},
                    std::vector<glm::quat>{glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}, ss3, dt2, dr2, ds2);
    CHECK(std::isfinite(ds2[0].x));
    CHECK_NEAR(ds2[0].x, 0.0f, 1.0e-6f); // 0 × 1（退化分量按 1 处理，结果仍为静止的 0）
}
