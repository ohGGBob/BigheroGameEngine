#pragma once
// 骨骼蒙皮（Skeleton）：基于 glTF 骨骼数据的 CPU 姿态计算器。
// 纯 CPU、仅依赖 glm 头文件，可离线运行与单元测试（不触碰 Vulkan/GPU）。
//
// 职责：
//   - 从 GltfModel（GltfLoader.h 解析）提取节点层级与关节/逆绑定矩阵。
//   - 计算每个节点的全局矩阵（局部 TRS 沿父链级联，复用 Transform 的 T*R*S 约定）。
//   - 计算关节的皮肤矩阵 = 全局矩阵 * 逆绑定矩阵，供 CPU 蒙皮顶点（预览/校验）。
//
// 约定：
//   - 右撇 Y-up，弧度制。局部矩阵 M_local = T * R * S；全局 = 父全局 * 子局部。
//   - 皮肤矩阵 skinned = globalJoint * inverseBindMatrix，顶点 p' = skin * p（齐次）。
//   - 关节权重 JOINTS_0/WEIGHTS_0 由 GltfModel 提供，逐顶点至多 4 关节。

#include "scene/GltfLoader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace BigHero::Scene
{
    // 骨骼：持有节点层级（TRS + parent）与关节（节点下标 + 逆绑定矩阵）。
    class Skeleton
    {
    public:
        Skeleton() = default;

        // 从 glTF 模型提取骨骼数据；无 skin 时 HasSkin()==false。
        explicit Skeleton(const GltfModel& model)
        {
            // 节点层级（含非关节的中间节点，用于正确级联）
            parents_ = model.nodeParents;
            translations_ = model.nodeTranslations;
            rotations_ = model.nodeRotations;
            scales_ = model.nodeScales;
            // 关节
            jointNodes_ = model.jointNodes;
            inverseBind_ = model.inverseBindMatrices;
        }

        [[nodiscard]] bool HasSkin() const noexcept { return !jointNodes_.empty(); }
        [[nodiscard]] size_t JointCount() const noexcept { return jointNodes_.size(); }

        // 计算所有节点（含中间节点）的全局矩阵，索引为节点下标。
        // 用递归沿父链计算并记忆化，保证父先于子，不受节点数组顺序影响。
        void ComputeGlobalNodeMatrices(std::vector<glm::mat4>& outGlobal) const
        {
            const size_t n = parents_.size();
            outGlobal.assign(n, glm::mat4(1.0f));
            std::vector<bool> computed(n, false);
            std::function<glm::mat4(size_t)> global = [&](size_t i) -> glm::mat4
            {
                if (computed[i])
                    return outGlobal[i];
                glm::mat4 local(1.0f);
                local = glm::translate(local, translations_[i]);
                local *= glm::mat4_cast(rotations_[i]);
                local = glm::scale(local, scales_[i]);
                const int32_t p = parents_[i];
                outGlobal[i] = (p < 0) ? local : global(static_cast<size_t>(p)) * local;
                computed[i] = true;
                return outGlobal[i];
            };
            for (size_t i = 0; i < n; ++i)
                global(i);
        }

        // 计算关节的全局矩阵（仅关节，按 jointNodes 顺序，索引即关节下标）。
        void ComputeGlobalJointMatrices(std::vector<glm::mat4>& outGlobal) const
        {
            std::vector<glm::mat4> nodeGlobal;
            ComputeGlobalNodeMatrices(nodeGlobal);
            outGlobal.resize(jointNodes_.size());
            for (size_t j = 0; j < jointNodes_.size(); ++j)
            {
                const int32_t node = jointNodes_[j];
                outGlobal[j] = (node >= 0 && node < static_cast<int32_t>(nodeGlobal.size()))
                    ? nodeGlobal[static_cast<size_t>(node)] : glm::mat4(1.0f);
            }
        }

        // 计算皮肤矩阵 = 全局关节矩阵 * 逆绑定矩阵（索引即关节下标）。
        // 逆绑定矩阵数量不足时按单位矩阵补足（容错）。
        void ComputeSkinMatrices(std::vector<glm::mat4>& outSkin) const
        {
            std::vector<glm::mat4> global;
            ComputeGlobalJointMatrices(global);
            outSkin.resize(jointNodes_.size());
            for (size_t j = 0; j < jointNodes_.size(); ++j)
            {
                const glm::mat4 ibm = (j < inverseBind_.size()) ? inverseBind_[j] : glm::mat4(1.0f);
                outSkin[j] = global[j] * ibm;
            }
        }

        // CPU 蒙皮：对逐顶点位置/法线应用皮肤矩阵（最多 4 关节，权重归一化）。
        // vertices 与 jointIndices/jointWeights 一一对应。
        void SkinVertices(
            const std::vector<glm::u8vec4>& jointIndices,
            const std::vector<glm::vec4>& jointWeights,
            const std::vector<glm::vec3>& positionsIn,
            const std::vector<glm::vec3>& normalsIn,
            std::vector<glm::vec3>& positionsOut,
            std::vector<glm::vec3>& normalsOut) const
        {
            std::vector<glm::mat4> skin;
            ComputeSkinMatrices(skin);
            const size_t count = positionsIn.size();
            positionsOut.resize(count);
            normalsOut.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                const glm::u8vec4 j = (i < jointIndices.size()) ? jointIndices[i] : glm::u8vec4(0);
                const glm::vec4 w = (i < jointWeights.size()) ? jointWeights[i] : glm::vec4(1, 0, 0, 0);
                glm::vec3 p(0.0f), n(0.0f);
                for (int k = 0; k < 4; ++k)
                {
                    const size_t jIdx = static_cast<size_t>(j[k]);
                    const glm::mat4& sk = (jIdx < skin.size()) ? skin[jIdx] : glm::mat4(1.0f);
                    const glm::vec3 skinnedP = glm::vec3(sk * glm::vec4(positionsIn[i], 1.0f));
                    const glm::mat3 normalMat = glm::mat3(sk);
                    const glm::vec3 skinnedN = glm::normalize(normalMat * normalsIn[i]);
                    p += skinnedP * w[k];
                    n += skinnedN * w[k];
                }
                positionsOut[i] = p;
                normalsOut[i] = (glm::dot(n, n) > 1e-12f) ? glm::normalize(n) : normalsIn[i];
            }
        }

    private:
        std::vector<int32_t> parents_;
        std::vector<glm::vec3> translations_;
        std::vector<glm::quat> rotations_;
        std::vector<glm::vec3> scales_;
        std::vector<int32_t> jointNodes_;
        std::vector<glm::mat4> inverseBind_;
    };
} // namespace BigHero::Scene
