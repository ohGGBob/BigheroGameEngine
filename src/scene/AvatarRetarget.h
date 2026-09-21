#pragma once
// Avatar 骨骼重定向（Retargeting）：把一套骨骼上的动画搬到另一套骨骼上（U2-N1 剩余部分）。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan 对象；可离线单测。
//
// 背景与动机：
//   动画资源是引擎里最贵的一类资产，而市面上的动画（Mixamo、各类动作库、美术自建骨架）
//   几乎不可能与项目自己的骨架完全一致：骨骼命名五花八门（Hips / pelvis / Bip01_Pelvis），
//   层级深浅不同（有无人锁骨）、比例也不同（长腿短腿）。若要求「骨骼必须完全一致才能
//   复用动画」，动画库就完全无法跨项目流通——这正是 Unity Humanoid / Unreal IK Rig 存在的
//   理由。本模块提供这套映射与重定向的最小可用实现。
//
// 重定向原理（业界通行做法，也是 Unity Humanoid 的核心）：
//   1. **匹配**：把两端骨骼名规范化后按别名表映射到同一套规范名，得到 src↔dst 骨骼对应。
//   2. **旋转**：动画带来的其实是「相对静止姿态的偏转量」，所以重定向搬运的是这个偏转量：
//          dstR_i = (srcR_j · inverse(srcRestR_j)) · dstRestR_i
//      这样即使两套骨骼的静止朝向完全不同（Mixamo 的 T-pose vs A-pose），动作方向也正确。
//   3. **平移**：按骨骼长度比例缩放（长腿角色迈的步子更大）：
//          dstT_i = dstRestT_i + (|dstRestT_i| / |srcRestT_j|) · (srcT_j - srcRestT_j)
//      根骨骼（无父节点）没有「骨骼长度」可比，改用整体身高比例。
//   4. **缩放**：逐分量取相对缩放 srcS_j / srcRestS_j，乘到 dst 的静止缩放上。
//   5. 未匹配到的目标骨骼保持其静止姿态（宁可不动，也不乱动）。
//
// 契约：
//   - 骨骼数组的父索引必须指向更早的下标（父先于子），与 Skeleton 的约定一致。
//   - 输入的 src 姿态数组短于骨骼数时按单位值兜底，绝不越界读。
//   - 映射是 dst→src 的单向函数；一个 src 骨骼可被多个 dst 骨骼引用（如共享手指根），
//     但一个 dst 骨骼最多对应一个 src 骨骼。
//   - 未做线程同步：Build 为写、RetargetPose 为读（只读映射与静止姿态），可并发调用。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace BigHero::Scene
{
// 一根骨骼（静止姿态 + 层级）。
struct AvatarBone
{
    std::string name;
    int parent = -1;
    glm::vec3 restTranslation{0.0f};
    glm::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 restScale{1.0f};
};

// 一套骨架（静止姿态的权威来源）。
class AvatarDefinition
{
  public:
    // 追加一根骨骼；parent 必须已存在于本骨架（或 -1 表示根）。返回其下标，-1 表示拒绝。
    int AddBone(const AvatarBone& bone)
    {
        if (bone.parent >= static_cast<int>(bones_.size()))
            return -1;
        if (bone.parent < -1)
            return -1;
        bones_.push_back(bone);
        return static_cast<int>(bones_.size()) - 1;
    }
    int AddBone(std::string name, int parent, const glm::vec3& t,
                const glm::quat& r = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), const glm::vec3& s = glm::vec3(1.0f))
    {
        AvatarBone b;
        b.name = std::move(name);
        b.parent = parent;
        b.restTranslation = t;
        b.restRotation = r;
        b.restScale = s;
        return AddBone(b);
    }

    [[nodiscard]] size_t BoneCount() const { return bones_.size(); }
    [[nodiscard]] const std::vector<AvatarBone>& Bones() const { return bones_; }
    [[nodiscard]] const AvatarBone& Bone(size_t i) const { return bones_[i]; }
    [[nodiscard]] int Parent(size_t i) const { return bones_[i].parent; }
    [[nodiscard]] bool IsRoot(size_t i) const { return bones_[i].parent < 0; }

    // 按原始名查找（大小写敏感，精确匹配）。
    [[nodiscard]] int FindBone(const std::string& name) const
    {
        for (size_t i = 0; i < bones_.size(); ++i)
            if (bones_[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    // 骨骼静止长度（到父骨骼的距离）；根骨骼返回 0（没有「骨骼长度」概念）。
    [[nodiscard]] float RestBoneLength(size_t i) const { return glm::length(bones_[i].restTranslation); }

    // ---- 名称规范化 ----
    // 1) 去掉命名空间前缀（"mixamorig:Hips" → "Hips"，"a/b/Spine" → "Spine"）
    // 2) 转小写并剔除分隔符（_ - 空格 .）
    // 3) 去掉常见前缀词（bip01 / bip / jnt / joint / bone / val / chr）
    // 注意：第 3 步只在去掉后仍有内容时才生效，避免把骨骼名整个吃掉。
    [[nodiscard]] static std::string NormalizeBoneName(const std::string& raw)
    {
        std::string s = raw;
        const size_t sep = s.find_last_of(":/|");
        if (sep != std::string::npos)
            s = s.substr(sep + 1);

        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            const bool isSep = (c == '_' || c == '-' || c == ' ' || c == '.');
            if (!isSep)
                out.push_back(c);
        }

        static const char* kPrefixes[] = {"mixamorig", "bip01", "bip", "jnt", "joint", "bone", "val", "chr"};
        for (const char* p : kPrefixes)
        {
            const std::string pre(p);
            if (out.size() > pre.size() && out.compare(0, pre.size(), pre) == 0)
            {
                out = out.substr(pre.size());
                break; // 只剥一层
            }
        }
        return out;
    }

    // 别名表：把各家命名收敛到同一套规范名（左右肢使用 left/right 前缀）。
    // 未收录的名称原样返回——精确匹配仍然生效，别名只是提高跨来源命中率。
    [[nodiscard]] static std::string CanonicalBoneName(const std::string& normalized)
    {
        static const std::unordered_map<std::string, std::string> kAlias = {
            // 躯干
            {"pelvis", "hips"},
            {"root", "hips"},
            {"hip", "hips"},
            {"hipmaster", "hips"},
            {"spine1", "spine"},
            {"spine01", "spine"},
            {"torso", "spine"},
            {"spine03", "spine"},
            {"spine2", "chest"},
            {"spine02", "chest"},
            {"upperchest", "chest"},
            {"spine04", "chest"},
            {"neck1", "neck"},
            {"neck01", "neck"},
            {"head1", "head"},
            {"headtop", "head"},
            // 左臂
            {"claviclel", "leftshoulder"},
            {"leftclavicle", "leftshoulder"},
            {"lclavicle", "leftshoulder"},
            {"shoulderl", "leftshoulder"},
            {"shoulderleft", "leftshoulder"},
            {"upperarml", "leftupperarm"},
            {"lupperarm", "leftupperarm"},
            {"upperarmleft", "leftupperarm"},
            {"leftarm", "leftupperarm"},
            {"arml", "leftupperarm"},
            {"lowerarml", "leftlowerarm"},
            {"llowerarm", "leftlowerarm"},
            {"lowerarmleft", "leftlowerarm"},
            {"forearml", "leftlowerarm"},
            {"leftforearm", "leftlowerarm"},
            {"lforearm", "leftlowerarm"},
            {"handl", "lefthand"},
            {"lhand", "lefthand"},
            // 右臂
            {"clavicler", "rightshoulder"},
            {"rightclavicle", "rightshoulder"},
            {"rclavicle", "rightshoulder"},
            {"shoulderr", "rightshoulder"},
            {"shoulderright", "rightshoulder"},
            {"upperarmr", "rightupperarm"},
            {"rupperarm", "rightupperarm"},
            {"upperarmright", "rightupperarm"},
            {"rightarm", "rightupperarm"},
            {"armr", "rightupperarm"},
            {"lowerarmr", "rightlowerarm"},
            {"rlowerarm", "rightlowerarm"},
            {"lowerarmright", "rightlowerarm"},
            {"forearmr", "rightlowerarm"},
            {"rightforearm", "rightlowerarm"},
            {"rforearm", "rightlowerarm"},
            {"handr", "righthand"},
            {"rhand", "righthand"},
            // 左腿
            {"upperlegl", "leftupperleg"},
            {"lupperleg", "leftupperleg"},
            {"upperlegleft", "leftupperleg"},
            {"leftupleg", "leftupperleg"},
            {"thighl", "leftupperleg"},
            {"leftthigh", "leftupperleg"},
            {"lowerlegl", "leftlowerleg"},
            {"llowerleg", "leftlowerleg"},
            {"lowerlegleft", "leftlowerleg"},
            {"calfl", "leftlowerleg"},
            {"leftcalf", "leftlowerleg"},
            {"shinl", "leftlowerleg"},
            {"leftleg", "leftlowerleg"},
            {"legl", "leftlowerleg"},
            {"footl", "leftfoot"},
            {"lfoot", "leftfoot"},
            {"toel", "lefttoe"},
            {"lefttoebase", "lefttoe"},
            {"lefttoeend", "lefttoe"},
            {"ltoe", "lefttoe"},
            // 右腿
            {"upperlegr", "rightupperleg"},
            {"rupperleg", "rightupperleg"},
            {"upperlegright", "rightupperleg"},
            {"rightupleg", "rightupperleg"},
            {"thighr", "rightupperleg"},
            {"rightthigh", "rightupperleg"},
            {"lowerlegr", "rightlowerleg"},
            {"rlowerleg", "rightlowerleg"},
            {"lowerlegright", "rightlowerleg"},
            {"calfr", "rightlowerleg"},
            {"rightcalf", "rightlowerleg"},
            {"shinr", "rightlowerleg"},
            {"rightleg", "rightlowerleg"},
            {"legr", "rightlowerleg"},
            {"footr", "rightfoot"},
            {"rfoot", "rightfoot"},
            {"toer", "righttoe"},
            {"righttoebase", "righttoe"},
            {"righttoeend", "righttoe"},
            {"rtoe", "righttoe"},
        };
        const auto it = kAlias.find(normalized);
        return it != kAlias.end() ? it->second : normalized;
    }

  private:
    std::vector<AvatarBone> bones_;
};

// 两套骨架之间的骨骼映射 + 姿态重定向。
class AvatarRetarget
{
  public:
    AvatarRetarget() = default;

    // 自动构建映射：两端骨骼名各自规范化 + 别名收敛后做精确匹配。
    // 同一规范名出现多次时取**先出现**的那根（层级较浅者通常在前），保证确定性。
    // extraMappings 可传入手工修正（dst 骨骼名 → src 骨骼名），优先级最高。
    static AvatarRetarget Build(const AvatarDefinition& src, const AvatarDefinition& dst,
                                const std::vector<std::pair<std::string, std::string>>& extraMappings = {})
    {
        AvatarRetarget r;
        r.srcToDst_.assign(src.BoneCount(), -1);
        r.dstToSrc_.assign(dst.BoneCount(), -1);

        std::unordered_map<std::string, int> srcByCanonical;
        for (size_t i = 0; i < src.BoneCount(); ++i)
        {
            const std::string key =
                AvatarDefinition::CanonicalBoneName(AvatarDefinition::NormalizeBoneName(src.Bone(i).name));
            srcByCanonical.emplace(key, static_cast<int>(i)); // 先出现者优先
        }

        for (size_t i = 0; i < dst.BoneCount(); ++i)
        {
            const std::string key =
                AvatarDefinition::CanonicalBoneName(AvatarDefinition::NormalizeBoneName(dst.Bone(i).name));
            const auto it = srcByCanonical.find(key);
            if (it != srcByCanonical.end())
                r.Link(static_cast<int>(i), it->second);
        }

        for (const auto& [dstName, srcName] : extraMappings)
        {
            const int d = dst.FindBone(dstName);
            const int s = src.FindBone(srcName);
            if (d >= 0 && s >= 0)
                r.Link(d, s);
            else if (d >= 0 && s < 0)
                r.Unlink(d);
        }
        return r;
    }

    // ---- 映射访问 ----
    // dst 骨骼 → src 骨骼（-1 = 无对应）；下标为 dst 骨骼下标。
    [[nodiscard]] const std::vector<int>& DestToSource() const { return dstToSrc_; }
    // src 骨骼 → 引用它的 dst 骨骼（-1 = 无人引用）。
    [[nodiscard]] const std::vector<int>& SourceToDest() const { return srcToDst_; }
    [[nodiscard]] int SourceFor(size_t dstBone) const { return dstBone < dstToSrc_.size() ? dstToSrc_[dstBone] : -1; }

    // 手工覆盖 / 清除某根 dst 骨骼的映射。
    void Link(int dstBone, int srcBone)
    {
        if (dstBone < 0 || dstBone >= static_cast<int>(dstToSrc_.size()))
            return;
        Unlink(dstBone);
        dstToSrc_[static_cast<size_t>(dstBone)] = srcBone;
        if (srcBone >= 0 && srcBone < static_cast<int>(srcToDst_.size()) && srcToDst_[static_cast<size_t>(srcBone)] < 0)
            srcToDst_[static_cast<size_t>(srcBone)] = dstBone;
    }
    void Unlink(int dstBone)
    {
        if (dstBone < 0 || dstBone >= static_cast<int>(dstToSrc_.size()))
            return;
        const int old = dstToSrc_[static_cast<size_t>(dstBone)];
        if (old >= 0 && old < static_cast<int>(srcToDst_.size()) && srcToDst_[static_cast<size_t>(old)] == dstBone)
            srcToDst_[static_cast<size_t>(old)] = -1;
        dstToSrc_[static_cast<size_t>(dstBone)] = -1;
    }

    // 映射覆盖率：目标骨架中有对应源骨骼的比例 ∈ [0, 1]。
    // 低于 ~0.6 通常意味着命名体系差异太大，需要补别名或手工映射。
    [[nodiscard]] double Coverage() const
    {
        if (dstToSrc_.empty())
            return 0.0;
        size_t n = 0;
        for (int s : dstToSrc_)
            if (s >= 0)
                ++n;
        return static_cast<double>(n) / static_cast<double>(dstToSrc_.size());
    }

    // 整体身高比例（dst / src）：根骨骼平移缩放用。
    // 用「根骨骼静止高度」做代理（角色 root 通常放在脚底或髋部，比例一致即可）。
    [[nodiscard]] static float HeightRatio(const AvatarDefinition& src, const AvatarDefinition& dst)
    {
        const float sh = RootHeight(src);
        const float dh = RootHeight(dst);
        if (sh < 1e-5f || dh < 1e-5f)
            return 1.0f;
        return dh / sh;
    }

    // ---- 姿态重定向 ----
    // 把 src 的**局部** TRS 姿态数组搬到 dst 的**局部** TRS 数组。
    // 输出数组大小恒等于 dst 骨骼数；未映射的骨骼保持静止姿态。
    // 输入数组不足时按单位值兜底（与 Skeleton 的约定一致）。
    void RetargetPose(const AvatarDefinition& src, const AvatarDefinition& dst, const std::vector<glm::vec3>& srcT,
                      const std::vector<glm::quat>& srcR, const std::vector<glm::vec3>& srcS,
                      std::vector<glm::vec3>& outT, std::vector<glm::quat>& outR, std::vector<glm::vec3>& outS) const
    {
        const size_t n = dst.BoneCount();
        outT.assign(n, glm::vec3(0.0f));
        outR.assign(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        outS.assign(n, glm::vec3(1.0f));

        const float rootScale = HeightRatio(src, dst);

        for (size_t i = 0; i < n; ++i)
        {
            const AvatarBone& d = dst.Bone(i);
            const int j = SourceFor(i);
            if (j < 0)
            {
                // 无对应源骨骼：保持静止姿态（宁可不动，也不乱动）
                outT[i] = d.restTranslation;
                outR[i] = d.restRotation;
                outS[i] = d.restScale;
                continue;
            }
            const size_t sj = static_cast<size_t>(j);
            const AvatarBone& s = src.Bone(sj);

            const glm::vec3 st = sj < srcT.size() ? srcT[sj] : s.restTranslation;
            const glm::quat sr = sj < srcR.size() ? srcR[sj] : s.restRotation;
            const glm::vec3 ss = sj < srcS.size() ? srcS[sj] : s.restScale;

            // 旋转：搬运「相对静止姿态的偏转量」，因此两端 T-pose/A-pose 不同也能对齐。
            const glm::quat delta = sr * glm::conjugate(s.restRotation);
            outR[i] = delta * d.restRotation;

            // 平移：按骨骼长度比例缩放，长腿角色迈更大的步子。
            const float srcLen = glm::length(s.restTranslation);
            const float dstLen = glm::length(d.restTranslation);
            float scale = 1.0f;
            if (dst.IsRoot(i))
                scale = rootScale; // 根骨骼没有「骨骼长度」可比，用整体身高比例
            else if (srcLen > 1e-5f && dstLen > 1e-5f)
                scale = dstLen / srcLen;
            outT[i] = d.restTranslation + scale * (st - s.restTranslation);

            // 缩放：取相对缩放（逐分量），兜底避免除零。
            glm::vec3 relScale(1.0f);
            for (int k = 0; k < 3; ++k)
            {
                const float denom = s.restScale[k];
                relScale[k] = (std::fabs(denom) > 1e-6f) ? (ss[k] / denom) : 1.0f;
            }
            outS[i] = d.restScale * relScale;
        }
    }

  private:
    [[nodiscard]] static float RootHeight(const AvatarDefinition& avatar)
    {
        // 取所有根骨骼中静止高度最大的一个作为身高代理（多根骨架也稳定）。
        float best = 0.0f;
        for (size_t i = 0; i < avatar.BoneCount(); ++i)
        {
            if (!avatar.IsRoot(i))
                continue;
            best = std::max(best, avatar.Bone(i).restTranslation.y);
        }
        return best;
    }

    std::vector<int> dstToSrc_;
    std::vector<int> srcToDst_;
};
} // namespace BigHero::Scene
