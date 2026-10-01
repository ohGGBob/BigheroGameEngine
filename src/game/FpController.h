#pragma once
// 第一人称角色控制器（纯逻辑、零 GPU / 零窗口依赖，可离线单测）。
//
// 与 scene/FirstPersonCamera（纯运动学飞行相机）分工：本类负责「有重力的陆行角色」——
// 走 / 跑 / 蹲 / 跳、与场景 AABB 碰撞体的滑动求解、台阶跨越、以及行走时的视点摇晃
// （head bob）。相机侧只消费本类输出的「眼位 + 摇晃偏移」。
//
// 设计要点：
// - 角色身体简化为竖直胶囊的圆柱近似：XZ 圆（radius_）+ Y 区间（脚底 feetY_ ~ 头顶）。
// - 碰撞求解按「先垂直、后水平、再台阶」的顺序迭代若干次，天然产生贴墙滑动
//   （水平位移被墙阻挡时只保留未被阻挡的分量）。
// - 全部分支无 NaN：除法有下限、长度过小回退，保证沙箱单测可覆盖极端输入。

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace BigHero::Game
{
// 轴对齐碰撞盒（世界空间，中心 + 半尺寸）。由场景侧（SceneObject / ECS Transform）投影得到。
struct BoxCollider
{
    glm::vec3 center{0.0f};
    glm::vec3 half{0.5f};
};

// 逐帧输入（由 Application 从窗口按键状态翻译而来，纯数据，便于单测直接构造）
struct FpInput
{
    float forward = 0.0f; // 前向分量（W=+1 / S=-1，可叠加为 0）
    float right = 0.0f;   // 右向分量（D=+1 / A=-1）
    bool jump = false;    // 跳跃边沿（空格按下）
    bool crouch = false;  // 蹲下持续态（Ctrl）
    bool sprint = false;  // 冲刺持续态（Shift）
};

// 角色运动参数（默认值 = 成年人在游戏尺度下的手感）
struct FpParams
{
    float radius = 0.35f;        // 身体圆柱半径（米）
    float height = 1.80f;        // 站立身高（脚底到头顶）
    float crouchHeight = 1.10f;  // 蹲下身高
    float eyeOffset = 0.10f;     // 眼位距头顶的下降量（站立眼高 = height - eyeOffset）
    float walkSpeed = 4.2f;      // 步行速度（m/s）
    float sprintSpeed = 8.0f;    // 冲刺速度（m/s）
    float crouchSpeed = 1.9f;    // 蹲行速度（m/s）
    float accel = 45.0f;         // 水平加速度（m/s²，越大越「跟手」）
    float airAccel = 8.0f;       // 空中加速度（明显小于地面，保留惯性）
    float friction = 12.0f;      // 地面阻尼（无输入时的减速）
    float gravity = 22.0f;       // 重力加速度（m/s²，比现实大 → 手感利落）
    float jumpSpeed = 7.0f;      // 起跳初速度（m/s）
    float maxFallSpeed = 45.0f;  // 终端下落速度（防穿透）
    float stepHeight = 0.45f;    // 可自动跨越的台阶高度
    float bobAmplitude = 0.045f; // 行走视点上下摇晃幅度（米）
    float bobFrequency = 9.5f;   // 摇晃频率（弧度/秒系数）
};

// 第一人称角色控制器：积分速度、求解碰撞、输出眼位。
class FpController
{
  public:
    explicit FpController(const FpParams& params = FpParams{}) : params_(params), baseParams_(params)
    {
        position_ = glm::vec3(0.0f, 0.0f, 0.0f); // position_ = 脚底位置
        velocity_ = glm::vec3(0.0f);
    }

    // 用世界碰撞盒集合推进一帧。dt 建议钳制在 [0, 0.1]（卡帧防穿透）。
    void Update(float dt, const FpInput& input, const std::vector<BoxCollider>& colliders)
    {
        if (!(dt > 0.0f))
            return;
        dt = std::min(dt, 0.1f);

        // ---- 1. 身高切换（蹲下/站起）：头顶有障碍时保持蹲姿 ----
        const float wantHeight = input.crouch ? params_.crouchHeight : params_.height;
        const float targetHeight =
            (!input.crouch && CeilingBlocked(position_, params_.height, colliders)) ? params_.crouchHeight : wantHeight;
        height_ += (targetHeight - height_) * std::min(1.0f, dt * 12.0f);
        crouching_ = (height_ < (params_.height + params_.crouchHeight) * 0.5f);

        // ---- 2. 期望水平速度（相对朝向由调用方把输入旋转到世界空间后传入）----
        const float speed = crouching_ ? params_.crouchSpeed : (input.sprint ? params_.sprintSpeed : params_.walkSpeed);
        glm::vec3 wishDir(input.right, 0.0f, input.forward);
        const float wishLen = glm::length(wishDir);
        if (wishLen > 1.0f)
            wishDir /= wishLen; // 斜向不加速
        const glm::vec3 wishVel = wishDir * speed;

        // ---- 3. 水平速度趋近（地面/空中加速度不同）----
        const float accel = onGround_ ? params_.accel : params_.airAccel;
        velocity_.x += (wishVel.x - velocity_.x) * std::min(1.0f, accel * dt / std::max(speed, 1e-3f));
        velocity_.z += (wishVel.z - velocity_.z) * std::min(1.0f, accel * dt / std::max(speed, 1e-3f));
        if (onGround_ && wishLen < 1e-4f)
        {
            const float decay = std::max(0.0f, 1.0f - params_.friction * dt);
            velocity_.x *= decay;
            velocity_.z *= decay;
        }

        // ---- 4. 跳跃与重力 ----
        if (input.jump && (onGround_ || inWater_))
        {
            // 水中：空格给一记上浮推进（不清除 onGround，便于踩在水底时也能起跳）
            velocity_.y = std::max(velocity_.y, params_.jumpSpeed);
            if (!inWater_)
                onGround_ = false;
        }
        velocity_.y -= params_.gravity * dt;
        velocity_.y = std::max(velocity_.y, -params_.maxFallSpeed);

        // ---- 5. 位移 + 碰撞求解 ----
        MoveAndCollide(velocity_ * dt, colliders);

        // ---- 6. 行走摇晃（head bob）：水平速度驱动，落地瞬间有一记下沉 ----
        UpdateBob(dt);
    }

    // 步行速度档位（滚轮/编辑器调）：冲刺与蹲行速度按固定比例联动
    void SetWalkSpeed(float v) noexcept
    {
        params_.walkSpeed = std::max(v, 0.1f);
        params_.sprintSpeed = params_.walkSpeed * 1.9f;
        params_.crouchSpeed = params_.walkSpeed * 0.45f;
        if (!inWater_)
        {
            // 陆地调速同步进基准，退出游泳时才能还原到用户设定的档位
            baseParams_.walkSpeed = params_.walkSpeed;
            baseParams_.sprintSpeed = params_.sprintSpeed;
            baseParams_.crouchSpeed = params_.crouchSpeed;
        }
    }

    // 水中状态：重力 / 终端速度 / 移速整体降低，空格改为上浮；退出时按基准精确还原。
    // （纯逻辑，可离线单测；场景侧只需每帧告知「头部所在格是否为水体」。）
    // 隐式地面高度（无碰撞体时的兜底地面；默认 y=0 与旧行为逐位一致）。
    // 地形场景每帧喂高度场采样值（Application 侧 SampleWorld），实现「走在山地上」。
    void SetGroundHeight(float h) noexcept { groundHeight_ = h; }

    void SetInWater(bool inWater) noexcept
    {
        if (inWater == inWater_)
            return;
        inWater_ = inWater;
        if (!inWater)
        {
            params_.gravity = baseParams_.gravity;
            params_.maxFallSpeed = baseParams_.maxFallSpeed;
            params_.jumpSpeed = baseParams_.jumpSpeed;
            params_.walkSpeed = baseParams_.walkSpeed;
            params_.sprintSpeed = baseParams_.sprintSpeed;
            params_.crouchSpeed = baseParams_.crouchSpeed;
            return;
        }
        params_.gravity = baseParams_.gravity * 0.28f;    // 浮力抵消大部分重力
        params_.maxFallSpeed = 3.2f;                      // 水中下沉缓慢
        params_.jumpSpeed = baseParams_.jumpSpeed * 0.5f; // 上浮推力（可连续按）
        params_.walkSpeed = baseParams_.walkSpeed * 0.62f;
        params_.sprintSpeed = baseParams_.sprintSpeed * 0.66f;
        params_.crouchSpeed = baseParams_.crouchSpeed * 0.70f;
    }

    [[nodiscard]] bool InWater() const noexcept { return inWater_; }

    // 直接传送（重生 / 场景切换），清空速度
    void Teleport(const glm::vec3& feetPos) noexcept
    {
        position_ = feetPos;
        velocity_ = glm::vec3(0.0f);
        onGround_ = false;
        bobPhase_ = 0.0f;
        bobOffset_ = 0.0f;
    }

    // 眼位（世界空间）：脚底 + 当前身高 - 眼偏移 + 摇晃偏移（摇晃只作用于 Y）
    [[nodiscard]] glm::vec3 EyePosition() const noexcept
    {
        return glm::vec3(position_.x, position_.y + height_ - params_.eyeOffset + bobOffset_, position_.z);
    }

    [[nodiscard]] const glm::vec3& FeetPosition() const noexcept { return position_; }
    [[nodiscard]] const glm::vec3& Velocity() const noexcept { return velocity_; }
    [[nodiscard]] bool OnGround() const noexcept { return onGround_; }
    [[nodiscard]] bool Crouching() const noexcept { return crouching_; }
    [[nodiscard]] float Height() const noexcept { return height_; }
    [[nodiscard]] float HorizontalSpeed() const noexcept
    {
        return std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    }
    [[nodiscard]] const FpParams& Params() const noexcept { return params_; }
    [[nodiscard]] float BobOffset() const noexcept { return bobOffset_; }

    // ---- 纯函数工具：供场景侧与单测复用 ----

    // 圆柱（XZ 圆 + Y 区间）是否与 AABB 相交（分离轴，忽略圆角处的精确距离，保守判定）
    [[nodiscard]] static bool Overlaps(const glm::vec3& feet, float radius, float height,
                                       const BoxCollider& box) noexcept
    {
        const glm::vec3 lo = feet + glm::vec3(0.0f, 0.0f, 0.0f);
        const glm::vec3 hi = feet + glm::vec3(0.0f, height, 0.0f);
        if (hi.y <= box.center.y - box.half.y || lo.y >= box.center.y + box.half.y)
            return false;
        // XZ：圆 vs 矩形（圆心到矩形最近点的距离 <= radius）
        const float dx = std::max(std::abs(feet.x - box.center.x) - box.half.x, 0.0f);
        const float dz = std::max(std::abs(feet.z - box.center.z) - box.half.z, 0.0f);
        return (dx * dx + dz * dz) < (radius * radius);
    }

  private:
    // 头顶净空检查：站立是否会撞到天花板（用于蹲下解除判定）
    bool CeilingBlocked(const glm::vec3& feet, float height, const std::vector<BoxCollider>& colliders) const
    {
        for (const BoxCollider& b : colliders)
        {
            if (Overlaps(feet, params_.radius, height, b) && (feet.y + height) > (b.center.y + b.half.y - 1e-3f) &&
                feet.y < (b.center.y + b.half.y))
                return true;
        }
        return false;
    }

    // 分轴求解：Y 轴先解（落地/顶头），再解 XZ（产生贴墙滑动），台阶兜底。
    void MoveAndCollide(const glm::vec3& delta, const std::vector<BoxCollider>& colliders)
    {
        const bool wasFalling = (velocity_.y <= 0.0f);

        // ---- Y ----
        position_.y += delta.y;
        onGround_ = false;
        if (!colliders.empty())
        {
            ResolveVertical(wasFalling, colliders);
        }
        else if (position_.y <= groundHeight_) // 无碰撞体时以 groundHeight_ 为地面（默认 y=0，与旧 FP 相机一致）
        {
            position_.y = groundHeight_;
            velocity_.y = 0.0f;
            onGround_ = true;
        }

        // ---- XZ（逐轴分离 → 天然滑动）----
        if (!colliders.empty())
        {
            glm::vec3 axisX(delta.x, 0.0f, 0.0f);
            glm::vec3 axisZ(0.0f, 0.0f, delta.z);
            if (!TryMoveHorizontal(position_ + axisX, colliders))
            {
                // 被挡：尝试台阶（障碍顶面不高于脚底 + stepHeight 时抬腿跨过）
                if (!TryStep(delta.x, 0.0f, colliders))
                    velocity_.x = 0.0f;
            }
            if (!TryMoveHorizontal(position_ + axisZ, colliders))
            {
                if (!TryStep(0.0f, delta.z, colliders))
                    velocity_.z = 0.0f;
            }
        }
        else
        {
            position_.x += delta.x;
            position_.z += delta.z;
        }
    }

    void ResolveVertical(bool wasFalling, const std::vector<BoxCollider>& colliders)
    {
        bool hitCeiling = false;
        for (const BoxCollider& b : colliders)
        {
            if (!Overlaps(position_, params_.radius, height_, b))
                continue;
            const float boxTop = b.center.y + b.half.y;
            const float boxBottom = b.center.y - b.half.y;
            if (wasFalling && velocity_.y <= 0.0f && position_.y < boxTop &&
                position_.y > boxTop - std::max(0.6f, -velocity_.y * 0.05f))
            {
                position_.y = boxTop;
                velocity_.y = 0.0f;
                onGround_ = true;
            }
            else if (!wasFalling && velocity_.y > 0.0f && (position_.y + height_) > boxBottom &&
                     position_.y < boxBottom)
            {
                position_.y = boxBottom - height_;
                velocity_.y = 0.0f;
                hitCeiling = true;
            }
        }
        (void)hitCeiling;
        // 世界地板兜底
        if (position_.y <= 0.0f)
        {
            position_.y = 0.0f;
            if (velocity_.y < 0.0f)
                velocity_.y = 0.0f;
            onGround_ = true;
        }
    }

    // 尝试把角色挪到 candidate，成功返回 true（并写入 position_）
    bool TryMoveHorizontal(const glm::vec3& candidate, const std::vector<BoxCollider>& colliders)
    {
        for (const BoxCollider& b : colliders)
        {
            if (Overlaps(candidate, params_.radius, height_, b))
                return false;
        }
        position_ = candidate;
        return true;
    }

    // 台阶：向 (dx,dz) 移动被挡时，若抬升 stepHeight 后不再相交且抬升后头顶不撞，则跨上去。
    bool TryStep(float dx, float dz, const std::vector<BoxCollider>& colliders)
    {
        const float moveLen = std::sqrt(dx * dx + dz * dz);
        if (moveLen < 1e-5f || !onGround_)
            return false;

        for (int i = 1; i <= 4; ++i)
        {
            const float lift = params_.stepHeight * (static_cast<float>(i) / 4.0f);
            const glm::vec3 cand(position_.x + dx, position_.y + lift, position_.z + dz);
            bool blocked = false;
            for (const BoxCollider& b : colliders)
            {
                if (Overlaps(cand, params_.radius, height_, b))
                {
                    blocked = true;
                    break;
                }
            }
            if (!blocked)
            {
                position_ = cand;
                return true;
            }
        }
        return false;
    }

    void UpdateBob(float dt)
    {
        const float hs = HorizontalSpeed();
        if (onGround_ && hs > 0.35f)
        {
            bobPhase_ += dt * params_.bobFrequency * std::min(hs / params_.walkSpeed, 1.6f);
            const float target = std::sin(bobPhase_) * params_.bobAmplitude * std::min(hs / params_.walkSpeed, 1.3f);
            bobOffset_ += (target - bobOffset_) * std::min(1.0f, dt * 12.0f);
        }
        else
        {
            bobOffset_ += (0.0f - bobOffset_) * std::min(1.0f, dt * 8.0f);
        }
    }

    FpParams params_{};
    FpParams baseParams_{}; // 陆地基准（游泳退出的还原依据）
    bool inWater_ = false;
    glm::vec3 position_{0.0f}; // 脚底
    glm::vec3 velocity_{0.0f};
    float height_ = 1.80f;
    float groundHeight_ = 0.0f; // 隐式地面高度（无碰撞体时的兜底；默认 y=0）
    bool onGround_ = false;
    bool crouching_ = false;
    float bobPhase_ = 0.0f;
    float bobOffset_ = 0.0f;
};
} // namespace BigHero::Game
