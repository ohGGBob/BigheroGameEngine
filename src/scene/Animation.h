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
//   - A1 动画事件：AnimationEventTrack 事件表由外部注入 AnimationEventPlayer
//     （GltfLoader 不感知事件，保持数据结构最小改动），Advance 返回本帧触发列表。
//   - A3 二维混合树：BlendSpace2D 两轴参数 + 采样点网格双线性插值，
//     输出 (动画下标, 权重) 列表供 AnimationBlender / AnimationStateMachine 消费。
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

// ---- A1 动画事件：事件表由外部注入播放层，不侵入 GltfLoader ----
// 单个事件：事件名 + 触发时刻（clip 本地时间，秒）+ 可选浮点参数。
struct AnimationEvent
{
    std::string name;
    float time = 0.0f;  // 触发时刻（秒）
    float param = 0.0f; // 可选浮点参数（如伤害值/音量/混合系数）
};

// 事件轨：绑定一条 clip（clipIndex 必填，clipName 可选用于校验），
// 由外部注入 AnimationEventPlayer。事件按 time 升序存放（AddEvent 自动稳定排序，
// 同一时刻的事件保持插入顺序，触发时按该顺序返回）。
struct AnimationEventTrack
{
    size_t clipIndex = 0; // 绑定的动画（clip）下标
    std::string clipName; // 绑定的 clip 名称（空串 = 不校验名称）
    std::vector<AnimationEvent> events;

    // 追加事件并保持按 time 稳定升序（同一时刻保持插入顺序）。
    void AddEvent(std::string name, float time, float param = 0.0f)
    {
        events.push_back(AnimationEvent{std::move(name), time, param});
        std::stable_sort(events.begin(), events.end(),
                         [](const AnimationEvent& a, const AnimationEvent& b) { return a.time < b.time; });
    }

    // 收集播放从 prev 跨越到 cur 期间触发的事件，按跨越顺序追加到 out（纯逻辑核心，可独立单测）。
    // 约定：
    //   - prev/cur 位于 [0, duration]；loop=true 时 cur 允许为未回绕的绝对时间（可超出或为负），
    //     prev 为上一帧的归一化时间。前进跨越区间为 (prev, cur]，倒退为 [cur, prev)。
    //   - loop=true 时事件时刻按周期取模（time=duration 等价 0）；非循环用原始时刻，
    //     time > duration 的事件不可达、永不触发。
    //   - 事件正好位于 prev 不触发（上一帧已触发或起始位置）；正好位于 cur 触发（刚到达）。
    //   - 一帧内每个事件至多追加一次（dt 跨多个周期也不重复），多事件按跨越先后排序。
    void CollectEvents(float prev, float cur, float duration, bool loop, std::vector<AnimationEvent>& out) const
    {
        if (events.empty() || duration <= 0.0f || cur == prev)
            return;

        auto wrapT = [duration](float t)
        {
            float r = std::fmod(t, duration);
            return (r < 0.0f) ? r + duration : r;
        };

        struct Hit
        {
            float time;   // 跨越时刻
            size_t index; // 事件下标（稳定排序保序用）
        };
        std::vector<Hit> hits;

        if (cur > prev) // 前进
        {
            if (!loop)
            {
                for (size_t i = 0; i < events.size(); ++i)
                    if (events[i].time > prev && events[i].time <= cur)
                        hits.push_back({events[i].time, i});
            }
            else
            {
                const float prevW = wrapT(prev);
                for (size_t i = 0; i < events.size(); ++i)
                {
                    const float e = wrapT(events[i].time);
                    // 首次跨越时刻 = prev + m（m ∈ (0, duration]，事件正好位于 prev 视为一个周期后）
                    float m = std::fmod(e - prevW, duration);
                    if (m <= 0.0f)
                        m += duration;
                    const float t1 = prev + m;
                    if (t1 <= cur)
                        hits.push_back({t1, i});
                }
            }
        }
        else // 倒退
        {
            if (!loop)
            {
                for (size_t i = 0; i < events.size(); ++i)
                    if (events[i].time >= cur && events[i].time < prev)
                        hits.push_back({events[i].time, i});
            }
            else
            {
                const float prevW = wrapT(prev);
                for (size_t i = 0; i < events.size(); ++i)
                {
                    const float e = wrapT(events[i].time);
                    float m = std::fmod(prevW - e, duration);
                    if (m <= 0.0f)
                        m += duration;
                    const float t1 = prev - m;
                    if (t1 >= cur)
                        hits.push_back({t1, i});
                }
            }
        }

        if (hits.empty())
            return;
        // 按跨越顺序输出（前进升序 / 倒退降序）；stable_sort 保持同一时刻事件的插入顺序
        if (cur > prev)
            std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; });
        else
            std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time > b.time; });
        for (const Hit& h : hits)
            out.push_back(events[h.index]);
    }
};

// ---- A1 动画事件播放器：AnimationState 时间推进 + 事件触发 + 姿态采样 ----
// 组合 AnimationPlayer（采样）与 AnimationState（推进状态）；事件轨由外部注入
// （GltfLoader 不感知事件）。每帧 Advance 返回本帧触发的事件列表，消费方（引擎侧
// 脚音效/逻辑通知等）自行处理。暂停或 dt=0 时不推进、不触发。
class AnimationEventPlayer
{
  public:
    explicit AnimationEventPlayer(const GltfModel& model, size_t animIndex = 0)
        : player_(model, animIndex), animIndex_(animIndex),
          animName_(animIndex < model.animations.size() ? model.animations[animIndex].name : std::string()),
          duration_(player_.IsValid() ? player_.Duration() : 0.0f)
    {
    }

    [[nodiscard]] bool IsValid() const noexcept { return player_.IsValid(); }
    [[nodiscard]] float Duration() const noexcept { return duration_; }
    [[nodiscard]] float Time() const noexcept { return state_.time; }

    // 绑定事件轨（外部注入，生命周期由调用方保证）。轨须与播放器 clip 匹配：
    // clipIndex 相同，且 clipName 为空或与动画名一致；不匹配返回 false 且不生效（防串轨误触发）。
    // 传入 nullptr 解绑并返回 true。
    bool BindTrack(const AnimationEventTrack* track)
    {
        if (track == nullptr)
        {
            track_ = nullptr;
            return true;
        }
        if (track->clipIndex != animIndex_)
            return false;
        if (!track->clipName.empty() && track->clipName != animName_)
            return false;
        track_ = track;
        return true;
    }

    // ---- 播放控制（转发 AnimationState：speed<0 倒放、paused 冻结推进） ----
    void SetSpeed(float speed) { state_.speed = speed; }
    void SetLoop(bool loop) { state_.loop = loop; }
    void SetPaused(bool paused) { state_.playing = !paused; }
    [[nodiscard]] bool IsPaused() const noexcept { return !state_.playing; }
    [[nodiscard]] const AnimationState& State() const noexcept { return state_; }

    // seek：跳转到 t（循环回绕、非循环夹取到 [0, duration]）。
    // 与 Unity 对齐：seek 是重新定位而非播放跨越，被跨过的事件不触发；
    // 之后的 Advance 从新位置继续（只触发新位置之后跨越的事件）。
    void Seek(float t)
    {
        if (duration_ <= 0.0f)
        {
            state_.time = 0.0f;
            return;
        }
        t = std::max(t, 0.0f);
        state_.time = state_.loop ? WrapTime(t, duration_) : std::min(t, duration_);
    }

    // 每帧推进：按 AnimationState 语义推进时间（暂停/零步进不触发事件），
    // 返回本帧跨越触发的事件列表（按跨越顺序，一帧内每个事件至多一次）。
    std::vector<AnimationEvent> Advance(float dt)
    {
        std::vector<AnimationEvent> fired;
        const float prev = state_.time;
        state_.Advance(dt);
        const float cur = state_.time; // 未回绕/未夹取的推进结果
        if (duration_ > 0.0f)
        {
            if (track_ != nullptr)
            {
                if (state_.loop)
                    track_->CollectEvents(prev, cur, duration_, true, fired);
                else
                    track_->CollectEvents(prev, std::clamp(cur, 0.0f, duration_), duration_, false, fired);
            }
            // 归一化内部时间：循环回绕到 [0, duration)，非循环夹取到 [0, duration]
            state_.time = state_.loop ? WrapTime(cur, duration_) : std::clamp(cur, 0.0f, duration_);
        }
        return fired;
    }

    // 采样当前姿态（时间取内部状态、循环用状态开关；转发 AnimationPlayer::Sample）。
    void SamplePose(std::vector<glm::vec3>& outT, std::vector<glm::quat>& outR, std::vector<glm::vec3>& outS) const
    {
        player_.Sample(state_.time, state_.loop, outT, outR, outS);
    }

  private:
    static float WrapTime(float t, float duration)
    {
        float r = std::fmod(t, duration);
        return (r < 0.0f) ? r + duration : r;
    }

    AnimationPlayer player_;
    AnimationState state_;
    const AnimationEventTrack* track_ = nullptr;
    size_t animIndex_ = 0;
    std::string animName_;
    float duration_ = 0.0f;
};

// ---- A3 二维混合空间（Blend Space）：两轴参数 + 采样点网格 + 双线性插值权重 ----
// 权重项：动画下标 + 权重（同下标已合并、按 animIndex 升序），可逐项喂给
// AnimationBlender::AddLayer（blender 内部再做归一化）。
struct BlendWeightEntry
{
    size_t animIndex = 0;
    float weight = 0.0f;
};

// 采样点：参数坐标 + 关联动画下标 + 权重基线（该点被精确命中时的权重）。
struct BlendSpaceSample
{
    glm::vec2 params = glm::vec2(0.0f);
    size_t animIndex = 0;
    float weight = 1.0f;
};

// 二维混合空间：纯逻辑、无 IO，采样点支持增删。Evaluate 规则：
//   1. 参数先 clamp 到轴范围（未调用 SetAxisRange 时为采样点包围盒）；
//   2. 精确命中某采样点（距离 <= 1e-5）时直接返回其基线权重（同动画合并）；
//   3. 否则取两轴唯一坐标构成的包围单元做双线性插值：位于单元角点上的采样点
//      贡献 基线权重 × X系数 × Y系数；角点缺失（散点数据）贡献 0（可能得到空权重
//      或未归一化的部分权重，调用方应回退绑定姿态 / 交由 blender 归一化）；
//      单轴退化（所有采样点该轴同坐标）时该轴系数恒为 1（沿另一轴线性插值）；
//   4. 权重按动画下标合并升序输出，<= 0 的项跳过。
class BlendSpace2D
{
  public:
    // 添加采样点，返回其下标（RemoveSample 后其后点下标前移）。
    size_t AddSample(const glm::vec2& params, size_t animIndex, float weight = 1.0f)
    {
        samples_.push_back(BlendSpaceSample{params, animIndex, weight});
        return samples_.size() - 1;
    }

    // 按下标移除采样点（其余点相对顺序与下标保持稳定）；越界忽略。
    void RemoveSample(size_t index)
    {
        if (index < samples_.size())
            samples_.erase(samples_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void Clear() noexcept { samples_.clear(); }
    [[nodiscard]] size_t SampleCount() const noexcept { return samples_.size(); }
    [[nodiscard]] const BlendSpaceSample& GetSample(size_t index) const { return samples_[index]; }

    // 参数轴范围（axis: 0=X, 1=Y），Evaluate 时越界 clamp；非法 axis 忽略。
    void SetAxisRange(int axis, float minValue, float maxValue)
    {
        if (axis != 0 && axis != 1)
            return;
        const size_t a = static_cast<size_t>(axis);
        hasRange_[a] = true;
        axisMin_[a] = std::min(minValue, maxValue);
        axisMax_[a] = std::max(minValue, maxValue);
    }
    [[nodiscard]] bool HasAxisRange(int axis) const
    {
        return (axis == 0 || axis == 1) && hasRange_[static_cast<size_t>(axis)];
    }

    // 给定参数点计算混合权重（规则见类注释），结果按 animIndex 升序。
    [[nodiscard]] std::vector<BlendWeightEntry> Evaluate(const glm::vec2& params) const
    {
        if (samples_.empty())
            return {};

        // 1) clamp 到轴范围（未设置时为采样点包围盒）
        glm::vec2 lo(0.0f), hi(0.0f);
        for (int a = 0; a < 2; ++a)
        {
            const size_t ai = static_cast<size_t>(a);
            if (hasRange_[ai])
            {
                lo[a] = axisMin_[ai];
                hi[a] = axisMax_[ai];
            }
            else
            {
                lo[a] = hi[a] = samples_.front().params[a];
                for (const BlendSpaceSample& s : samples_)
                {
                    lo[a] = std::min(lo[a], s.params[a]);
                    hi[a] = std::max(hi[a], s.params[a]);
                }
            }
        }
        const glm::vec2 p = glm::clamp(params, lo, hi);

        std::vector<BlendWeightEntry> out;
        // 2) 精确命中采样点：直接返回基线权重（同动画合并）
        bool exactHit = false;
        for (const BlendSpaceSample& s : samples_)
        {
            if (glm::distance(s.params, p) > kEps)
                continue;
            exactHit = true;
            MergeWeight(out, s.animIndex, s.weight);
        }
        if (!exactHit)
        {
            // 3) 包围单元双线性：两轴唯一坐标（升序）确定单元角点
            std::vector<float> xs, ys;
            for (const BlendSpaceSample& s : samples_)
            {
                if (std::find_if(xs.begin(), xs.end(), [&s](float v) { return Near(v, s.params.x); }) == xs.end())
                    xs.push_back(s.params.x);
                if (std::find_if(ys.begin(), ys.end(), [&s](float v) { return Near(v, s.params.y); }) == ys.end())
                    ys.push_back(s.params.y);
            }
            std::sort(xs.begin(), xs.end());
            std::sort(ys.begin(), ys.end());

            float x0, x1, tx, y0, y1, ty;
            Bracket(xs, p.x, x0, x1, tx);
            Bracket(ys, p.y, y0, y1, ty);

            for (const BlendSpaceSample& s : samples_)
            {
                // 采样点必须恰好位于单元角点（两轴坐标都在 eps 内匹配）才参与插值
                if ((!Near(s.params.x, x0) && !Near(s.params.x, x1))
                    || (!Near(s.params.y, y0) && !Near(s.params.y, y1)))
                    continue;
                const float bx = Near(x0, x1) ? 1.0f : (Near(s.params.x, x0) ? 1.0f - tx : tx);
                const float by = Near(y0, y1) ? 1.0f : (Near(s.params.y, y0) ? 1.0f - ty : ty);
                MergeWeight(out, s.animIndex, s.weight * bx * by);
            }
        }

        // 4) 跳过非正权重，按 animIndex 升序输出
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [](const BlendWeightEntry& e) { return e.weight <= 0.0f; }),
                  out.end());
        std::sort(out.begin(), out.end(),
                  [](const BlendWeightEntry& a, const BlendWeightEntry& b) { return a.animIndex < b.animIndex; });
        return out;
    }

  private:
    static constexpr float kEps = 1e-5f;

    static bool Near(float a, float b) { return std::fabs(a - b) <= kEps; }

    static void MergeWeight(std::vector<BlendWeightEntry>& out, size_t animIndex, float weight)
    {
        for (BlendWeightEntry& e : out)
            if (e.animIndex == animIndex)
            {
                e.weight += weight;
                return;
            }
        out.push_back(BlendWeightEntry{animIndex, weight});
    }

    // 在升序坐标表中求 p 的包围区间 [v0, v1] 与系数 t；p 在表外（或表退化）时取端点（v0==v1, t=0）
    static void Bracket(const std::vector<float>& vs, float p, float& v0, float& v1, float& t)
    {
        if (vs.size() == 1 || p <= vs.front())
        {
            v0 = v1 = vs.front();
            t = 0.0f;
            return;
        }
        if (p >= vs.back())
        {
            v0 = v1 = vs.back();
            t = 0.0f;
            return;
        }
        size_t i = 1;
        while (i < vs.size() && vs[i] <= p)
            ++i;
        v0 = vs[i - 1];
        v1 = vs[i];
        t = (v1 > v0) ? (p - v0) / (v1 - v0) : 0.0f;
    }

    std::vector<BlendSpaceSample> samples_;
    float axisMin_[2] = {0.0f, 0.0f};
    float axisMax_[2] = {0.0f, 0.0f};
    bool hasRange_[2] = {false, false};
};
} // namespace BigHero::Scene
