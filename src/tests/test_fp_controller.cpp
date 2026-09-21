// 第一人称角色控制器（game/FpController.h）单元测试：纯逻辑、零 GPU，可离线运行。
// 覆盖重力落地 / 跳跃高度 / 贴墙滑动 / 台阶跨越 / 蹲伏 / 视点摇晃 / 碰撞判定。
#include "framework/test_common.h"
#include "game/FpController.h"

using namespace BigHero;

TEST_CASE("FpController.GravityAndGround")
{
    using namespace BigHero::Game;

    FpController c;
    c.Teleport(glm::vec3(0.0f, 5.0f, 0.0f));
    CHECK(!c.OnGround());

    const std::vector<BoxCollider> empty{};
    FpInput idle{};
    for (int i = 0; i < 240; ++i)
        c.Update(1.0f / 60.0f, idle, empty);

    CHECK(c.OnGround());
    CHECK_NEAR(c.FeetPosition().y, 0.0f, 1e-3f);
    CHECK_NEAR(c.Velocity().y, 0.0f, 1e-3f);
    // 站立眼高 = height - eyeOffset = 1.80 - 0.10
    CHECK_NEAR(c.EyePosition().y, 1.70f, 0.02f);
}

TEST_CASE("FpController.JumpHeight")
{
    using namespace BigHero::Game;

    FpController c;
    c.Teleport(glm::vec3(0.0f, 0.0f, 0.0f));
    const std::vector<BoxCollider> empty{};

    FpInput idle{};
    for (int i = 0; i < 30; ++i)
        c.Update(1.0f / 60.0f, idle, empty); // 先稳定落地
    CHECK(c.OnGround());

    float maxY = 0.0f;
    FpInput jumpInput{};
    jumpInput.jump = true;
    c.Update(1.0f / 60.0f, jumpInput, empty); // 起跳帧
    for (int i = 0; i < 180; ++i)
    {
        c.Update(1.0f / 60.0f, idle, empty);
        maxY = std::max(maxY, c.FeetPosition().y);
    }
    // 理论峰高 = v²/(2g) = 7²/(2*22) ≈ 1.11 m
    CHECK_NEAR(maxY, 1.11f, 0.15f);
    CHECK(c.OnGround()); // 落回地面
}

TEST_CASE("FpController.WallSlide")
{
    using namespace BigHero::Game;

    // 墙：中心 (3,1,0)，半尺寸 (0.5,1,50) → 阻挡 x∈[2.5,3.5]，z 方向足够长（不会绕过）
    const std::vector<BoxCollider> wall{{glm::vec3(3.0f, 1.0f, 0.0f), glm::vec3(0.5f, 1.0f, 50.0f)}};

    FpController c;
    c.Teleport(glm::vec3(0.0f, 0.0f, 0.0f));
    FpInput input{};
    input.forward = 1.0f; // +Z
    input.right = 1.0f;   // +X（被墙挡住）
    for (int i = 0; i < 120; ++i)
        c.Update(1.0f / 60.0f, input, wall);

    // X 被挡在墙前（脚底 x < 2.5 - radius）
    CHECK(c.FeetPosition().x < 2.15f + 0.05f);
    CHECK(c.FeetPosition().x > 1.5f);
    // Z 仍可滑动前进（2 秒 × 4.2 m/s ≈ 8.4 m，扣掉加速段）
    CHECK(c.FeetPosition().z > 4.0f);
}

TEST_CASE("FpController.StepUp")
{
    using namespace BigHero::Game;

    // 矮平台：顶面 0.4 m（≤ stepHeight 0.45），x∈[1,11] 足够宽（跨上去后不会从另一侧掉下）
    const std::vector<BoxCollider> step{{glm::vec3(6.0f, 0.2f, 0.0f), glm::vec3(5.0f, 0.2f, 5.0f)}};

    FpController c;
    c.Teleport(glm::vec3(0.0f, 0.0f, 0.0f));
    FpInput input{};
    input.right = 1.0f;
    for (int i = 0; i < 90; ++i) // 1.5 s：跨上平台后仍在平台上（平台 x∈[1,11]）
        c.Update(1.0f / 60.0f, input, step);

    // 应当已经跨上平台（脚底 ≈ 0.4）而不是被挡在 x≈0.65（1 - radius）
    CHECK(c.FeetPosition().x > 1.5f);
    CHECK_NEAR(c.FeetPosition().y, 0.4f, 0.12f);
    CHECK(c.OnGround());
}

TEST_CASE("FpController.Crouch")
{
    using namespace BigHero::Game;

    FpController c;
    c.Teleport(glm::vec3(0.0f, 0.0f, 0.0f));
    const std::vector<BoxCollider> empty{};

    FpInput walk{};
    walk.forward = 1.0f;
    for (int i = 0; i < 90; ++i)
        c.Update(1.0f / 60.0f, walk, empty);
    const float standEye = c.EyePosition().y;
    const float standSpeed = c.HorizontalSpeed();

    FpInput crouchWalk{};
    crouchWalk.forward = 1.0f;
    crouchWalk.crouch = true;
    for (int i = 0; i < 120; ++i)
        c.Update(1.0f / 60.0f, crouchWalk, empty);

    CHECK(c.Crouching());
    CHECK(c.EyePosition().y < standEye - 0.4f);
    CHECK(c.HorizontalSpeed() < standSpeed - 1.0f); // 蹲行更慢
}

TEST_CASE("FpController.HeadBob")
{
    using namespace BigHero::Game;

    FpController c;
    c.Teleport(glm::vec3(0.0f, 0.0f, 0.0f));
    const std::vector<BoxCollider> empty{};

    FpInput walk{};
    walk.forward = 1.0f;
    float maxAbsBob = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        c.Update(1.0f / 60.0f, walk, empty);
        maxAbsBob = std::max(maxAbsBob, std::abs(c.BobOffset()));
    }
    CHECK(maxAbsBob > 0.005f); // 行走中确有上下摇晃
    CHECK(maxAbsBob < 0.12f);  // 幅度受控（不晕）

    FpInput idle{};
    for (int i = 0; i < 180; ++i)
        c.Update(1.0f / 60.0f, idle, empty);
    CHECK(std::abs(c.BobOffset()) < 0.005f); // 静止后归零
}

TEST_CASE("FpController.ColliderOverlaps")
{
    using namespace BigHero::Game;

    const BoxCollider box{glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};

    CHECK(FpController::Overlaps(glm::vec3(0.0f, 0.5f, 0.0f), 0.35f, 1.8f, box));  // 正中插入
    CHECK(!FpController::Overlaps(glm::vec3(5.0f, 0.0f, 0.0f), 0.35f, 1.8f, box)); // 远处
    CHECK(!FpController::Overlaps(glm::vec3(5.0f, 5.0f, 0.0f), 0.35f, 1.8f, box)); // 高空（Y 分离）
    CHECK(FpController::Overlaps(glm::vec3(1.3f, 0.0f, 0.0f), 0.35f, 1.8f, box));  // 侧面贴住（圆角距离内）
    CHECK(!FpController::Overlaps(glm::vec3(1.5f, 0.0f, 0.0f), 0.35f, 1.8f, box)); // 恰好离开
}

TEST_CASE("FpController.SwimState")
{
    using namespace BigHero::Game;

    FpController c;
    const FpParams land = c.Params();
    CHECK(!c.InWater());

    // ---- 入水：重力 / 终端速度 / 移速都必须下降，跳跃推力变弱 ----
    c.SetInWater(true);
    CHECK(c.InWater());
    CHECK(c.Params().gravity < land.gravity * 0.5f);
    CHECK(c.Params().maxFallSpeed < land.maxFallSpeed);
    CHECK(c.Params().walkSpeed < land.walkSpeed);
    CHECK(c.Params().jumpSpeed < land.jumpSpeed);

    // ---- 出水：必须精确还原到基准档位（不能残留缩放）----
    c.SetInWater(false);
    CHECK(!c.InWater());
    CHECK_NEAR(c.Params().gravity, land.gravity, 1e-5f);
    CHECK_NEAR(c.Params().maxFallSpeed, land.maxFallSpeed, 1e-5f);
    CHECK_NEAR(c.Params().walkSpeed, land.walkSpeed, 1e-5f);
    CHECK_NEAR(c.Params().sprintSpeed, land.sprintSpeed, 1e-5f);
    CHECK_NEAR(c.Params().jumpSpeed, land.jumpSpeed, 1e-5f);

    // ---- 行为验证：同样自由下落 0.4s，水中下沉距离显著更小 ----
    const std::vector<BoxCollider> empty{};
    FpInput idle{};
    FpController air;
    FpController water;
    air.Teleport(glm::vec3(0.0f, 20.0f, 0.0f));
    water.Teleport(glm::vec3(0.0f, 20.0f, 0.0f));
    water.SetInWater(true);
    for (int i = 0; i < 24; ++i)
    {
        air.Update(1.0f / 60.0f, idle, empty);
        water.Update(1.0f / 60.0f, idle, empty);
    }
    const float airDrop = 20.0f - air.FeetPosition().y;
    const float waterDrop = 20.0f - water.FeetPosition().y;
    CHECK(airDrop > 0.0f);
    CHECK(waterDrop > 0.0f); // 水中仍会缓慢下沉（浮力抵消大部分而非全部重力）
    CHECK(waterDrop * 3.0f < airDrop);

    // ---- 水中按空格可以上浮（不受 onGround 限制）----
    FpInput swimUp{};
    swimUp.jump = true;
    water.Update(1.0f / 60.0f, swimUp, empty);
    CHECK(water.Velocity().y > 0.0f);

    // ---- 重复设置同一状态不应叠加缩放（幂等）----
    const FpParams once = water.Params();
    water.SetInWater(true);
    water.SetInWater(true);
    CHECK_NEAR(water.Params().gravity, once.gravity, 1e-5f);
}
