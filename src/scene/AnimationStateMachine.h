#pragma once
// 动画状态机（AnimationStateMachine）：纯 CPU、仅依赖 glm + GltfLoader，可离线单测。
//
// 对标 Unity Animator / Unreal Animation Blueprint 的核心子集：
//   - 状态（State）：每个状态绑定一条 glTF 动画（animationIndex），含播放速度/循环
//   - 过渡（Transition）：状态间切换，支持 crossfade 混合时长、退出时间、条件
//   - 参数（Parameter）：Float / Bool / Trigger 三种，驱动过渡条件
//   - 任意状态过渡（Any State）：from = -1 表示从任意状态可进入
//
// 用法：
//   1. AddState / AddTransition 构建状态图
//   2. 每帧 SetFloat/SetBool/SetTrigger 设置参数
//   3. Update(dt) 推进时间并评估过渡
//   4. SamplePose(model, outT, outR, outS) 输出混合后的骨骼姿态
//
// 约定：
//   - animationIndex = -1 表示绑定姿态（无动画），用于占位或尚未分配动画的状态
//   - Trigger 参数在被过渡消费后自动重置
//   - crossfade 期间同时采样前后两条动画，按时间权重混合
//   - 时间单位为秒，过渡时长单位为秒

#include "scene/Animation.h"
#include "scene/GltfLoader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace BigHero::Scene
{
// 过渡条件类型
enum class AnimConditionType : uint8_t
{
    FloatGreater = 0, // param > threshold
    FloatLess = 1,    // param < threshold
    BoolTrue = 2,     // param == true
    BoolFalse = 3,    // param == false
    Trigger = 4       // param trigger 被设置
};

// 单个过渡条件
struct AnimCondition
{
    std::string param;
    AnimConditionType type = AnimConditionType::FloatGreater;
    float threshold = 0.0f;
};

// 动画状态
struct AnimState
{
    std::string name;
    int animationIndex = -1; // -1 = 绑定姿态
    float speed = 1.0f;
    bool loop = true;
};

// 状态间过渡
struct AnimTransition
{
    int from = -1; // -1 = Any State（任意状态可进入）
    int to = -1;
    float duration = 0.25f;          // crossfade 时长（秒）
    bool hasExitTime = false;        // 是否要求达到退出时间
    float exitNormalizedTime = 0.9f; // 退出时间（归一化 0~1）
    std::vector<AnimCondition> conditions;
};

// 动画状态机
class AnimationStateMachine
{
  public:
    AnimationStateMachine() = default;

    // ---- 构建状态图 ----
    int AddState(const std::string& name, int animationIndex = -1, float speed = 1.0f, bool loop = true)
    {
        states_.push_back(AnimState{name, animationIndex, speed, loop});
        return static_cast<int>(states_.size() - 1);
    }

    int AddTransition(int from, int to, float duration = 0.25f, std::vector<AnimCondition> conditions = {})
    {
        transitions_.push_back(AnimTransition{from, to, duration, false, 0.9f, std::move(conditions)});
        return static_cast<int>(transitions_.size() - 1);
    }

    int AddTransitionWithExit(int from, int to, float duration, float exitNormalizedTime,
                              std::vector<AnimCondition> conditions = {})
    {
        transitions_.push_back(AnimTransition{from, to, duration, true, exitNormalizedTime, std::move(conditions)});
        return static_cast<int>(transitions_.size() - 1);
    }

    // ---- 参数设置 ----
    void SetFloat(const std::string& name, float value) { floats_[name] = value; }
    void SetBool(const std::string& name, bool value) { bools_[name] = value; }
    void SetTrigger(const std::string& name) { triggers_[name] = true; }

    [[nodiscard]] float GetFloat(const std::string& name) const
    {
        auto it = floats_.find(name);
        return it != floats_.end() ? it->second : 0.0f;
    }
    [[nodiscard]] bool GetBool(const std::string& name) const
    {
        auto it = bools_.find(name);
        return it != bools_.end() ? it->second : false;
    }

    // ---- 运行时 ----
    // 设置初始状态（不触发过渡）
    void SetInitialState(int stateIndex)
    {
        if (stateIndex >= 0 && stateIndex < static_cast<int>(states_.size()))
        {
            currentState_ = stateIndex;
            currentTime_ = 0.0f;
        }
    }

    // 绑定模型引用（用于查询动画时长，需保证模型生命周期长于状态机）
    void BindModel(const GltfModel* model) { modelRef_ = model; }

    // 每帧更新：推进时间、评估过渡、执行 crossfade
    void Update(float dt)
    {
        if (currentState_ < 0 || states_.empty())
            return;

        // 推进当前状态时间
        const AnimState& cur = states_[static_cast<size_t>(currentState_)];
        const float dur = StateDuration(cur);
        currentTime_ += dt * cur.speed;
        if (cur.loop && dur > 0.0f)
            currentTime_ = std::fmod(currentTime_, dur);
        else if (!cur.loop && dur > 0.0f)
            currentTime_ = std::min(currentTime_, dur);

        // 推进 crossfade
        if (transitioning_)
        {
            transitionElapsed_ += dt;
            if (transitionElapsed_ >= transitionDuration_)
            {
                // 过渡结束
                transitioning_ = false;
                currentState_ = nextState_;
                currentTime_ = 0.0f; // 新状态从头开始
                nextState_ = -1;
            }
        }

        // 评估过渡（仅在非过渡中时评估，避免过渡中再次切换）
        if (!transitioning_)
            EvaluateTransitions();

        // 消费所有 trigger（无论是否被使用，每帧结束后重置）
        triggers_.clear();
    }

    // 输出混合后的骨骼姿态（当前动画 + crossfade 中的前一动画）
    void SamplePose(const GltfModel& model, std::vector<glm::vec3>& outT, std::vector<glm::quat>& outR,
                    std::vector<glm::vec3>& outS) const
    {
        if (currentState_ < 0 || states_.empty())
        {
            // 无状态：输出绑定姿态
            outT = model.nodeTranslations;
            outR = model.nodeRotations;
            outS = model.nodeScales;
            return;
        }

        const AnimState& cur = states_[static_cast<size_t>(currentState_)];

        if (!transitioning_)
        {
            SampleState(model, cur, currentTime_, outT, outR, outS);
            return;
        }

        // crossfade：混合前一状态和下一状态
        const float t = transitionDuration_ > 0.0f ? transitionElapsed_ / transitionDuration_ : 1.0f;
        const float easeT = t * t * (3.0f - 2.0f * t); // smoothstep

        std::vector<glm::vec3> t1, s1, t2, s2;
        std::vector<glm::quat> r1, r2;
        const AnimState& prev = states_[static_cast<size_t>(prevState_)];
        const AnimState& next = states_[static_cast<size_t>(nextState_)];

        SampleState(model, prev, prevTime_, t1, r1, s1);
        SampleState(model, next, transitionElapsed_, t2, r2, s2);

        const size_t n = model.nodeTranslations.size();
        outT.resize(n);
        outR.resize(n);
        outS.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            outT[i] = glm::mix(t1[i], t2[i], easeT);
            outS[i] = glm::mix(s1[i], s2[i], easeT);
            // 四元数短弧 slerp
            glm::quat a = r1[i], b = r2[i];
            if (glm::dot(a, b) < 0.0f)
                b = glm::quat(-b.w, -b.x, -b.y, -b.z);
            outR[i] = glm::normalize(glm::slerp(a, b, easeT));
        }
    }

    // ---- 查询 ----
    [[nodiscard]] int CurrentState() const noexcept { return currentState_; }
    [[nodiscard]] const char* CurrentStateName() const
    {
        return (currentState_ >= 0 && currentState_ < static_cast<int>(states_.size()))
                   ? states_[static_cast<size_t>(currentState_)].name.c_str()
                   : "(none)";
    }
    [[nodiscard]] float CurrentTime() const noexcept { return currentTime_; }
    [[nodiscard]] bool IsTransitioning() const noexcept { return transitioning_; }
    [[nodiscard]] float TransitionProgress() const
    {
        return transitioning_ && transitionDuration_ > 0.0f ? transitionElapsed_ / transitionDuration_ : 0.0f;
    }
    [[nodiscard]] size_t StateCount() const noexcept { return states_.size(); }
    [[nodiscard]] size_t TransitionCount() const noexcept { return transitions_.size(); }
    [[nodiscard]] const AnimState& GetState(int index) const { return states_[static_cast<size_t>(index)]; }
    [[nodiscard]] const std::vector<AnimTransition>& Transitions() const noexcept { return transitions_; }
    [[nodiscard]] const std::unordered_map<std::string, float>& FloatParams() const noexcept { return floats_; }
    [[nodiscard]] const std::unordered_map<std::string, bool>& BoolParams() const noexcept { return bools_; }

  private:
    // 获取状态动画时长（秒），-1 动画返回 0
    [[nodiscard]] float StateDuration(const AnimState& state) const
    {
        if (state.animationIndex < 0 || modelRef_ == nullptr)
            return 0.0f;
        if (static_cast<size_t>(state.animationIndex) >= modelRef_->animations.size())
            return 0.0f;
        float maxT = 0.0f;
        const auto& anim = modelRef_->animations[static_cast<size_t>(state.animationIndex)];
        for (const auto& s : anim.samplers)
            if (!s.times.empty())
                maxT = std::max(maxT, s.times.back());
        return maxT;
    }

    // 采样单个状态的姿态
    static void SampleState(const GltfModel& model, const AnimState& state, float time, std::vector<glm::vec3>& outT,
                            std::vector<glm::quat>& outR, std::vector<glm::vec3>& outS)
    {
        if (state.animationIndex < 0 || static_cast<size_t>(state.animationIndex) >= model.animations.size())
        {
            outT = model.nodeTranslations;
            outR = model.nodeRotations;
            outS = model.nodeScales;
            return;
        }
        AnimationPlayer player(model, static_cast<size_t>(state.animationIndex));
        player.Sample(time, state.loop, outT, outR, outS);
    }

    // 评估所有过渡条件，触发第一个满足的过渡
    void EvaluateTransitions()
    {
        const AnimState& cur = states_[static_cast<size_t>(currentState_)];
        const float dur = StateDuration(cur);
        const float normTime = (dur > 0.0f) ? std::fmod(currentTime_, dur) / dur : 0.0f;

        for (const AnimTransition& tr : transitions_)
        {
            // from 匹配：-1 = Any State，或等于当前状态
            if (tr.from != -1 && tr.from != currentState_)
                continue;
            if (tr.to < 0 || tr.to >= static_cast<int>(states_.size()))
                continue;
            if (tr.to == currentState_)
                continue; // 不过渡到自身

            // 退出时间检查
            if (tr.hasExitTime && normTime < tr.exitNormalizedTime)
                continue;

            // 条件检查
            if (!CheckConditions(tr.conditions))
                continue;

            // 触发过渡
            StartTransition(tr.to, tr.duration);
            return;
        }
    }

    // 检查一组条件（全部满足才返回 true）
    [[nodiscard]] bool CheckConditions(const std::vector<AnimCondition>& conditions) const
    {
        for (const AnimCondition& c : conditions)
        {
            switch (c.type)
            {
            case AnimConditionType::FloatGreater:
            {
                auto it = floats_.find(c.param);
                const float v = it != floats_.end() ? it->second : 0.0f;
                if (!(v > c.threshold))
                    return false;
                break;
            }
            case AnimConditionType::FloatLess:
            {
                auto it = floats_.find(c.param);
                const float v = it != floats_.end() ? it->second : 0.0f;
                if (!(v < c.threshold))
                    return false;
                break;
            }
            case AnimConditionType::BoolTrue:
            {
                auto it = bools_.find(c.param);
                if (!(it != bools_.end() && it->second))
                    return false;
                break;
            }
            case AnimConditionType::BoolFalse:
            {
                auto it = bools_.find(c.param);
                if (it != bools_.end() && it->second)
                    return false;
                break;
            }
            case AnimConditionType::Trigger:
            {
                auto it = triggers_.find(c.param);
                if (!(it != triggers_.end() && it->second))
                    return false;
                break;
            }
            }
        }
        return true;
    }

    // 开始 crossfade 过渡
    void StartTransition(int toState, float duration)
    {
        prevState_ = currentState_;
        prevTime_ = currentTime_;
        nextState_ = toState;
        transitionElapsed_ = 0.0f;
        transitionDuration_ = std::max(duration, 0.001f);
        transitioning_ = true;
    }

    std::vector<AnimState> states_;
    std::vector<AnimTransition> transitions_;
    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, bool> bools_;
    std::unordered_map<std::string, bool> triggers_;

    int currentState_ = -1;
    float currentTime_ = 0.0f;

    // crossfade 状态
    bool transitioning_ = false;
    int prevState_ = -1;
    float prevTime_ = 0.0f;
    int nextState_ = -1;
    float transitionElapsed_ = 0.0f;
    float transitionDuration_ = 0.25f;

    // 可选：绑定的模型引用（用于时长查询）
    const GltfModel* modelRef_ = nullptr;
};
} // namespace BigHero::Scene

