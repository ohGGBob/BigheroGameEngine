#pragma once
// 动画播放器（AnimationPlayer）：glTF 动画采样器，纯 CPU、仅依赖 glm 头文件，可离线单测。
//
// 职责：
//   - 对 GltfModel（GltfLoader.h 解析）的某条动画，在给定时间 t 处按通道求值，
//     输出每个节点的局部 TRS（平移/旋转/缩放）。
//   - 插值模式：LINEAR（平移/缩放 lerp、旋转 slerp）、STEP（取左端点关键帧）。
//     CUBICSPLINE 暂不支持（glTF 需三次 Hermite 采样，超出当前范围，遇到回退 STEP）。
//   - 与 Skeleton 协同：先 Sample 得到各节点动画后的局部 TRS，再交给
//     Skeleton::ComputeSkinMatricesWithPose 计算皮肤矩阵，从而驱动蒙皮。
//   - AnimationState 负责播放状态（时间推进/速度/循环），
//     AnimationBlender 负责多动画加权混合（crossfade 过渡）。
//
// 约定：
//   - 时间轴由采样器 input（SCALAR FLOAT）定义，output（VEC3 平移/缩放，VEC4 旋转）一一对应。
//   - 通道 target.path ∈ {"translation","rotation","scale"}，target.node 是节点下标。
//   - 未命中的节点保持模型默认 TRS；t 超出区间时按 loop 决定是否回绕。

#include "scene/GltfLoader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace BigHero::Scene
{
// 动画播放器：对一个 GltfModel 的指定动画按时间采样节点 TRS。
class AnimationPlayer
{
  public:
    // 绑定一条动画。若 model 无动画或 animIndex 越界，则 IsValid()==false。
    explicit AnimationPlayer(const GltfModel& model, size_t animIndex = 0) : model_(&model)
    {
        if (animIndex < model.animations.size())
            anim_ = &model.animations[animIndex];
    }

    [[nodiscard]] bool IsValid() const noexcept { return anim_ != nullptr; }
    [[nodiscard]] size_t AnimationCount() const noexcept { return model_->animations.size(); }

    // 动画总时长 = 所有采样器输入时间的最大值（至少 0）。
    [[nodiscard]] float Duration() const
    {
        if (!IsValid())
            return 0.0f;
        float maxT = 0.0f;
        for (const GltfAnimationSampler& s : anim_->samplers)
            if (!s.times.empty())
                maxT = std::max(maxT, s.times.back());
        return maxT;
    }

    // 在时刻 t 对动画求值，输出各节点的局部 TRS（索引为节点下标）。
    // outT/outR/outS 会重置为节点数量大小，并先填充模型默认值，
    // 再由命中的通道覆盖。loop=true 时 t 对 Duration() 取模回绕。
    void Sample(float t, bool loop, std::vector<glm::vec3>& outT, std::vector<glm::quat>& outR,
                std::vector<glm::vec3>& outS) const
    {
        const size_t n = model_->nodeTranslations.size();
        outT = model_->nodeTranslations;
        outR = model_->nodeRotations;
        outS = model_->nodeScales;
        if (n == 0 || !IsValid())
            return;

        const float dur = Duration();
        float time = t;
        if (loop && dur > 0.0f)
            time = std::fmod(t, dur);
        if (time < 0.0f)
            time = 0.0f;

        for (const GltfAnimationChannel& ch : anim_->channels)
        {
            const size_t node = static_cast<size_t>(ch.targetNode);
            if (node >= n)
                continue;
            if (ch.sampler < 0 || ch.sampler >= static_cast<int>(anim_->samplers.size()))
                continue;
            const GltfAnimationSampler& sp = anim_->samplers[static_cast<size_t>(ch.sampler)];
            if (sp.times.empty() || sp.values.empty())
                continue;

            if (ch.path == "translation")
                outT[node] = EvalVec3(sp, time);
            else if (ch.path == "scale")
                outS[node] = EvalVec3(sp, time);
            else if (ch.path == "rotation")
                outR[node] = EvalQuat(sp, time);
        }
    }

  private:
    // 找到采样区间左端点下标 k（times[k] <= time < times[k+1]），以及插值系数 u。
    static void Locate(const std::vector<float>& times, float time, int& k, float& u)
    {
        const int n = static_cast<int>(times.size());
        if (time <= times[0])
        {
            k = 0;
            u = 0.0f;
            return;
        }
        if (time >= times[n - 1])
        {
            k = n - 2;
            u = 1.0f;
            return;
        }
        int lo = 0, hi = n - 1;
        while (lo + 1 < hi)
        {
            const int mid = (lo + hi) / 2;
            if (times[mid] <= time)
                lo = mid;
            else
                hi = mid;
        }
        k = lo;
        const float span = times[lo + 1] - times[lo];
        u = (span > 1e-12f) ? (time - times[lo]) / span : 0.0f;
    }

    // VEC3 值（平移/缩放）：LINEAR lerp，STEP 取左端点。
    static glm::vec3 EvalVec3(const GltfAnimationSampler& sp, float time)
    {
        int k;
        float u;
        Locate(sp.times, time, k, u);
        const glm::vec3 a = glm::vec3(sp.values[static_cast<size_t>(k)]);
        const bool linear = sp.interpolation == "LINEAR";
        if (!linear || k + 1 >= static_cast<int>(sp.values.size()))
            return a; // STEP 或末段
        const glm::vec3 b = glm::vec3(sp.values[static_cast<size_t>(k + 1)]);
        return glm::mix(a, b, u);
    }

    // 旋转四元数：LINEAR slerp，STEP 取左端点。
    static glm::quat EvalQuat(const GltfAnimationSampler& sp, float time)
    {
        int k;
        float u;
        Locate(sp.times, time, k, u);
        const glm::quat a = QuatFromVec4(sp.values[static_cast<size_t>(k)]);
        const bool linear = sp.interpolation == "LINEAR";
        if (!linear || k + 1 >= static_cast<int>(sp.values.size()))
            return a;
        const glm::quat b = QuatFromVec4(sp.values[static_cast<size_t>(k + 1)]);
        return glm::normalize(glm::slerp(a, b, u));
    }

    // 采样值(vec4) 转四元数：glTF 存 (x,y,z,w)。
    static glm::quat QuatFromVec4(const glm::vec4& v) { return glm::quat(v.w, v.x, v.y, v.z); }

    const GltfModel* model_ = nullptr;
    const GltfAnimation* anim_ = nullptr;
};

// ---- 动画播放状态：时间推进、播放速度、循环控制 ----
struct AnimationState
{
    float time = 0.0f;   // 当前播放时间（秒）
    float speed = 1.0f;  // 播放速度倍率（负值可倒放）
    bool loop = true;    // 是否循环播放
    bool playing = true; // 是否播放中（false 时 Advance 不推进）

    void Advance(float dt)
    {
        if (playing)
            time += dt * speed;
    }
    void Reset() { time = 0.0f; }
};

// ---- 动画混合器：对同一模型的多个动画按权重混合节点 TRS ----
// 典型用法：crossfade 过渡时两条动画各自推进，权重随时间此消彼长。
class AnimationBlender
{
  public:
    explicit AnimationBlender(const GltfModel& model) : model_(&model) {}

    void Clear() { layers_.clear(); }

    // 添加一层：动画下标 + 权重 + 该层自身时间。权重 <=0 或越界下标被忽略。
    void AddLayer(size_t animIndex, float weight, float time = 0.0f)
    {
        if (weight <= 0.0f || animIndex >= model_->animations.size())
            return;
        layers_.push_back(Layer{animIndex, weight, time});
    }

    [[nodiscard]] size_t LayerCount() const noexcept { return layers_.size(); }

    // 混合输出各节点局部 TRS：权重归一化后平移/缩放加权求和，
    // 旋转以首层为符号参考做短弧累加再归一化（避免插值绕远路）。
    void Sample(bool loop, std::vector<glm::vec3>& outT, std::vector<glm::quat>& outR,
                std::vector<glm::vec3>& outS) const
    {
        const size_t n = model_->nodeTranslations.size();
        // 旋转累加器初始化为零四元数（非单位），逐层加权累加后统一归一化
        outT.assign(n, glm::vec3(0.0f));
        outR.assign(n, glm::quat(0.0f, 0.0f, 0.0f, 0.0f));
        outS.assign(n, glm::vec3(0.0f));
        if (n == 0 || layers_.empty())
            return;

        float wSum = 0.0f;
        for (const Layer& l : layers_)
            wSum += l.weight;
        if (wSum <= 0.0f)
            return;

        std::vector<glm::vec3> lt, ls;
        std::vector<glm::quat> lr;
        std::vector<glm::quat> ref; // 首层旋转作为符号参考

        for (size_t li = 0; li < layers_.size(); ++li)
        {
            const Layer& l = layers_[li];
            AnimationPlayer player(*model_, l.animIndex);
            player.Sample(l.time, loop, lt, lr, ls);
            const float w = l.weight / wSum;
            if (li == 0)
                ref = lr;
            for (size_t i = 0; i < n; ++i)
            {
                outT[i] += lt[i] * w;
                outS[i] += ls[i] * w;
                glm::quat q = lr[i];
                // 短弧：与首层反向时取共轭（q 与 -q 表示同一旋转）
                const glm::quat rf = (i < ref.size()) ? ref[i] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                if (li > 0 && Dot4(rf, q) < 0.0f)
                    q = glm::quat(-q.w, -q.x, -q.y, -q.z);
                outR[i].w += q.w * w;
                outR[i].x += q.x * w;
                outR[i].y += q.y * w;
                outR[i].z += q.z * w;
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            const float len = std::sqrt(Dot4(outR[i], outR[i]));
            outR[i] = (len > 1e-8f) ? glm::quat(outR[i].w / len, outR[i].x / len, outR[i].y / len, outR[i].z / len)
                                    : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }

  private:
    struct Layer
    {
        size_t animIndex = 0;
        float weight = 0.0f;
        float time = 0.0f;
    };

    // 四元数点积（glm::dot 对 qua 支持不稳定，此处手写保证可移植）
    static float Dot4(const glm::quat& a, const glm::quat& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }

    const GltfModel* model_ = nullptr;
    std::vector<Layer> layers_;
};
} // namespace BigHero::Scene

