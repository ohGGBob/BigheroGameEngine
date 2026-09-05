#pragma once
// 蒙皮网格（SkinnedMesh）：端到端骨骼动画求值管线（纯 CPU，可离线单测）。
//
// 职责：把动画采样与骨骼蒙皮串成一条完整链路：
//   AnimationPlayer（按时间采样节点局部 TRS）
//     -> Skeleton::ComputeSkinMatricesWithPose（节点全局矩阵 * 逆绑定 = 皮肤矩阵）
//       -> Skeleton::SkinVerticesWith（逐顶点 4 关节加权蒙皮）
//
// 约定：
//   - 构造时绑定 GltfModel 并缓存原始（绑定姿态）位置/法线，避免每帧回填。
//   - 无骨骼或无逐顶点权重时，Evaluate 回退到绑定姿态（等价静态网格），保证通用性。
//   - 输出为蒙皮后的位置与法线，可直接上传 GPU 或用于 CPU 侧校验/预览。
//   - 骨骼沿父链级联，故动画驱动父节点会自然带动子节点（层级动画）。

#include "scene/Animation.h"
#include "scene/Skeleton.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace BigHero::Scene
{
// 蒙皮网格：持有绑定姿态几何 + 骨骼，可按时/姿态求值得蒙皮结果。
class SkinnedMesh
{
  public:
    explicit SkinnedMesh(const GltfModel& model) : model_(&model), skeleton_(model)
    {
        // 缓存绑定姿态几何（原始顶点位置/法线）
        positions_.reserve(model.vertices.size());
        normals_.reserve(model.vertices.size());
        for (const Vertex& v : model.vertices)
        {
            positions_.push_back(v.pos);
            normals_.push_back(v.normal);
        }
    }

    [[nodiscard]] bool HasSkeleton() const noexcept { return skeleton_.HasSkin(); }
    [[nodiscard]] size_t AnimationCount() const noexcept { return model_->animations.size(); }
    [[nodiscard]] size_t VertexCount() const noexcept { return positions_.size(); }
    [[nodiscard]] const Skeleton& GetSkeleton() const noexcept { return skeleton_; }

    // 绑定姿态：有骨骼时按模型默认 TRS 蒙皮；否则原样输出（静态网格）。
    void EvaluateBind(std::vector<glm::vec3>& outPos, std::vector<glm::vec3>& outNormal) const
    {
        if (!CanSkin())
        {
            outPos = positions_;
            outNormal = normals_;
            return;
        }
        std::vector<glm::mat4> skin;
        skeleton_.ComputeSkinMatrices(skin);
        ApplySkin(skin, outPos, outNormal);
    }

    // 按动画下标 + 时间求值：输出蒙皮后的位置/法线。
    // 无骨骼/无权重/动画下标越界时回退到绑定姿态。
    void Evaluate(size_t animIndex, float time, bool loop, std::vector<glm::vec3>& outPos,
                  std::vector<glm::vec3>& outNormal) const
    {
        if (!CanSkin() || animIndex >= model_->animations.size())
        {
            EvaluateBind(outPos, outNormal);
            return;
        }
        AnimationPlayer player(*model_, animIndex);
        std::vector<glm::vec3> t, s;
        std::vector<glm::quat> r;
        player.Sample(time, loop, t, r, s);
        EvaluatePose(t, r, s, outPos, outNormal);
    }

    // 用外部姿态求值（供 AnimationBlender 混合结果或自定义姿态驱动）。
    void EvaluatePose(const std::vector<glm::vec3>& t, const std::vector<glm::quat>& r, const std::vector<glm::vec3>& s,
                      std::vector<glm::vec3>& outPos, std::vector<glm::vec3>& outNormal) const
    {
        if (!CanSkin())
        {
            EvaluateBind(outPos, outNormal);
            return;
        }
        std::vector<glm::mat4> skin;
        skeleton_.ComputeSkinMatricesWithPose(t, r, s, skin);
        ApplySkin(skin, outPos, outNormal);
    }

    // 取得指定动画时刻的皮肤矩阵（可直接喂给 GPU 骨骼调色板 SkinningPalette 上传）。
    // 无骨骼或动画下标越界时，返回绑定姿态的皮肤矩阵。
    void GetSkinMatrices(size_t animIndex, float time, bool loop, std::vector<glm::mat4>& outSkin) const
    {
        if (!CanSkin() || animIndex >= model_->animations.size())
        {
            skeleton_.ComputeSkinMatrices(outSkin);
            return;
        }
        AnimationPlayer player(*model_, animIndex);
        std::vector<glm::vec3> t, s;
        std::vector<glm::quat> r;
        player.Sample(time, loop, t, r, s);
        skeleton_.ComputeSkinMatricesWithPose(t, r, s, outSkin);
    }

  private:
    // 需同时具备骨骼关节与逐顶点蒙皮权重，才能真正蒙皮
    [[nodiscard]] bool CanSkin() const noexcept { return skeleton_.HasSkin() && !model_->jointIndices.empty(); }

    void ApplySkin(const std::vector<glm::mat4>& skin, std::vector<glm::vec3>& outPos,
                   std::vector<glm::vec3>& outNormal) const
    {
        skeleton_.SkinVerticesWith(skin, model_->jointIndices, model_->jointWeights, positions_, normals_, outPos,
                                   outNormal);
    }

    const GltfModel* model_ = nullptr;
    Skeleton skeleton_;
    std::vector<glm::vec3> positions_; // 绑定姿态位置（缓存）
    std::vector<glm::vec3> normals_;   // 绑定姿态法线（缓存）
};
} // namespace BigHero::Scene
