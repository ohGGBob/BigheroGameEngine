#pragma once
// 粒子系统（CPU 模拟，纯逻辑，无 GPU/窗口依赖，可离线单测）。
//
// 设计：
//   - Particle：单个粒子状态（位置/速度/颜色/寿命/尺寸）。固定容量对象池，避免每帧分配。
//   - Emitter：发射配置（速率、位置、初速度、寿命、尺寸、颜色、扩散）。
//   - ParticleSystem：每帧 Update(dt) 推进——按累积器生成新粒子、对存活粒子做
//     显式欧拉积分（gravity + 阻尼）、寿命衰减、死亡回收；池满时环形覆盖最旧粒子。
//   - 渲染侧（ParticleRenderer，见升级 17-3）仅读取 GetParticles() 的只读视图上传 GPU。
//
// 该模块是"玩法+工具一轮"升级 17-2 / 17-3 的核心，AI 特效、爆炸、拖尾等均可复用。

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Game
{
// 单个粒子
struct Particle
{
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 color{1.0f};
    float life = 0.0f;    // 剩余寿命（秒），<=0 视为死亡
    float maxLife = 1.0f; // 初始寿命（用于渲染端做淡出比例）
    float size = 1.0f;    // 点精灵尺寸（世界/屏幕单位，由渲染端解释）
    bool active = false;  // 是否在对象池中存活
};

// 发射器配置
struct Emitter
{
    float rate = 0.0f;               // 每秒发射粒子数（0 = 仅手动 Emit 爆发）
    glm::vec3 origin{0.0f};          // 发射中心（世界坐标）
    float spawnRadius = 0.0f;        // 在 origin 周围球内随机偏移
    glm::vec3 initialVelocity{0.0f}; // 基准初速度
    float speed = 0.0f;              // 在 initialVelocity 方向上的附加随机速率
    float lifetimeMin = 1.0f;        // 寿命区间
    float lifetimeMax = 1.0f;
    float sizeMin = 1.0f; // 尺寸区间
    float sizeMax = 1.0f;
    glm::vec3 color{1.0f}; // 粒子颜色
    float jitter = 0.15f;  // 初速度随机扰动幅度（0 = 确定性，便于单测）
};

class ParticleSystem
{
  public:
    explicit ParticleSystem(uint32_t capacity = 1024)
        : capacity_(capacity > 0 ? capacity : 1), particles_(capacity_), rng_(0u)
    {
    }

    // ---- 配置 ----
    void SetEmitter(const Emitter& e) noexcept { emitter_ = e; }
    [[nodiscard]] const Emitter& GetEmitter() const noexcept { return emitter_; }

    // 重力（默认 -9.81 向下）；阻尼系数（每秒速度衰减比例，0 = 无阻力）
    void SetGravity(const glm::vec3& g) noexcept { gravity_ = g; }
    [[nodiscard]] glm::vec3 GetGravity() const noexcept { return gravity_; }
    void SetDamping(float d) noexcept { damping_ = d; }
    [[nodiscard]] float GetDamping() const noexcept { return damping_; }

    [[nodiscard]] uint32_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] uint32_t AliveCount() const noexcept
    {
        uint32_t n = 0;
        for (const auto& p : particles_)
            if (p.active)
                ++n;
        return n;
    }

    // 只读视图，供渲染端上传 GPU（渲染层见 ParticleRenderer）
    [[nodiscard]] const std::vector<Particle>& GetParticles() const noexcept { return particles_; }

    // ---- 手动爆发：立即生成 count 个粒子（受容量限制，必要时环形覆盖最旧） ----
    void Emit(uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
            SpawnOne();
    }

    // ---- 每帧推进 ----
    // 1) 按发射速率累积生成；2) 对存活粒子做显式欧拉积分；3) 寿命衰减并回收。
    void Update(float dt)
    {
        if (dt <= 0.0f)
            return;

        // 速率发射：累加器跨帧累积，避免低帧率下丢发射
        if (emitter_.rate > 0.0f)
        {
            spawnAccumulator_ += emitter_.rate * dt;
            while (spawnAccumulator_ >= 1.0f)
            {
                SpawnOne();
                spawnAccumulator_ -= 1.0f;
            }
        }

        for (auto& p : particles_)
        {
            if (!p.active)
                continue;
            // 显式欧拉：先更新速度（重力+阻尼），再位移
            p.velocity += gravity_ * dt;
            if (damping_ > 0.0f)
            {
                const float factor = std::max(0.0f, 1.0f - damping_ * dt);
                p.velocity *= factor;
            }
            p.position += p.velocity * dt;
            p.life -= dt;
            if (p.life <= 0.0f)
            {
                p.active = false;
                p.life = 0.0f;
            }
        }
    }

    // 清空所有粒子
    void Clear()
    {
        for (auto& p : particles_)
            p.active = false;
        spawnAccumulator_ = 0.0f;
    }

  private:
    void SpawnOne()
    {
        const int slot = AllocSlot();
        Particle& p = particles_[static_cast<size_t>(slot)];
        // 球内随机偏移
        glm::vec3 offset(0.0f);
        if (emitter_.spawnRadius > 0.0f)
        {
            const glm::vec3 dir = RandomUnitVector();
            const float r = emitter_.spawnRadius * std::cbrt(Random01());
            offset = dir * r;
        }
        p.position = emitter_.origin + offset;

        // 初速度 = 基准 + 沿基准方向附加随机速率 + 随机扰动
        const float sp = emitter_.speed * Random01();
        p.velocity = emitter_.initialVelocity + emitter_.initialVelocity * sp;
        // 轻微随机抖动，避免完全共线（幅度由 jitter 控制，0 = 确定性）
        p.velocity += glm::vec3(RandomSym(), RandomSym(), RandomSym()) * emitter_.jitter;

        const float life = Lerp(emitter_.lifetimeMin, emitter_.lifetimeMax, Random01());
        p.life = life;
        p.maxLife = life;
        p.size = Lerp(emitter_.sizeMin, emitter_.sizeMax, Random01());
        p.color = emitter_.color;
        p.active = true;
    }

    // 分配槽位：优先空闲槽；满则环形覆盖最旧（cursor_ 推进）
    [[nodiscard]] int AllocSlot()
    {
        for (uint32_t i = 0; i < capacity_; ++i)
            if (!particles_[i].active)
                return static_cast<int>(i);
        const int s = static_cast<int>(ringCursor_);
        ringCursor_ = (ringCursor_ + 1u) % capacity_;
        return s;
    }

    float Random01() noexcept
    {
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        return d(rng_);
    }
    float RandomSym() noexcept
    {
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        return d(rng_);
    }
    glm::vec3 RandomUnitVector() noexcept
    {
        // 均匀球面采样（Marsaglia）
        const float u = Random01() * 2.0f - 1.0f;
        const float theta = Random01() * 6.2831853f;
        const float s = std::sqrt(std::max(0.0f, 1.0f - u * u));
        return glm::vec3(s * std::cos(theta), s * std::sin(theta), u);
    }

    static float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

    uint32_t capacity_ = 1;
    std::vector<Particle> particles_;
    Emitter emitter_;
    glm::vec3 gravity_{0.0f, -9.81f, 0.0f};
    float damping_ = 0.0f;
    float spawnAccumulator_ = 0.0f;
    uint32_t ringCursor_ = 0;
    std::mt19937 rng_;
};

} // namespace BigHero::Game
