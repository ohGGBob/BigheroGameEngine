// 光照探针（render/LightProbe.h）单元测试：纯逻辑、零 GPU，可离线运行。
// 覆盖球谐基正交归一 / 均匀环境不变量 / 半球光辐照度 / 三线性插值 / 无效探针剔除 /
// 全无效兜底 / 越界钳制 / 烘焙梯度 / 网格合法性。
#include "framework/test_common.h"
#include "render/LightProbe.h"

using namespace BigHero;
using BigHero::Render::LerpSh;
using BigHero::Render::LightProbeVolume;
using BigHero::Render::Sh9;

namespace
{
// Fibonacci 球面（与被测实现同构），用于独立验证球谐基的正交归一性。
void FibonacciDirs(int n, std::vector<glm::vec3>& out)
{
    out.clear();
    out.reserve(static_cast<size_t>(n));
    constexpr float kGolden = 2.39996322972865332f; // π(3-√5)
    for (int i = 0; i < n; ++i)
    {
        const float z = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(n);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phi = kGolden * static_cast<float>(i);
        out.emplace_back(r * std::cos(phi), r * std::sin(phi), z);
    }
}

// 均匀环境是最强的解析不变量：各方向辐射亮度恒为 L 时，漫反射辐照度必须恒为 L。
Sh9 Ambient(float l)
{
    return Sh9::FromAmbient(glm::vec3(l));
}
} // namespace

TEST_CASE("LightProbe.BasisOrthonormal")
{
    // ∫Y_i·Y_j dω 必须等于 δ_ij：这是「归一化球谐」的定义，
    // 也是 ProjectRadiance / Evaluate 能互为逆运算的前提。
    const int n = 4096;
    std::vector<glm::vec3> dirs;
    FibonacciDirs(n, dirs);
    const float w = 4.0f * 3.14159265f / static_cast<float>(n);

    float ba[9];
    float bb[9];
    float maxDiagErr = 0.0f;
    float maxOffDiag = 0.0f;
    for (int i = 0; i < 9; ++i)
    {
        for (int j = 0; j < 9; ++j)
        {
            float sum = 0.0f;
            for (const glm::vec3& d : dirs)
            {
                Sh9::Basis(d, ba);
                Sh9::Basis(d, bb);
                sum += ba[i] * bb[j] * w;
            }
            if (i == j)
                maxDiagErr = std::max(maxDiagErr, std::fabs(sum - 1.0f));
            else
                maxOffDiag = std::max(maxOffDiag, std::fabs(sum));
        }
    }
    CHECK_LT(maxDiagErr, 1.0e-3f); // 数值积分误差
    CHECK_LT(maxOffDiag, 1.0e-3f);
}

TEST_CASE("LightProbe.AmbientInvariant")
{
    // 均匀环境：Evaluate 必须在任意法线下都返回同一个 L（文件头约定的核心不变量）。
    const Sh9 sh = Ambient(0.7f);
    const glm::vec3 dirs[] = {glm::vec3(0.0f, 1.0f, 0.0f),       glm::vec3(0.0f, -1.0f, 0.0f),
                              glm::vec3(1.0f, 0.0f, 0.0f),       glm::vec3(0.0f, 0.0f, 1.0f),
                              glm::vec3(0.577f, 0.577f, 0.577f), glm::vec3(-0.3f, 0.5f, -0.81f)};
    for (const glm::vec3& d : dirs)
        CHECK_NEAR(Sh9::Evaluate(sh, d).r, 0.7f, 1.0e-3f);

    // 由 ProjectRadiance 走完整链路（投影 + 卷积）也必须收敛到同一个值。
    const Sh9 projected =
        Sh9::ToDiffuseIrradiance(Sh9::ProjectRadiance([](const glm::vec3&) { return glm::vec3(0.7f); }, 512));
    CHECK_NEAR(Sh9::Evaluate(projected, glm::vec3(0.0f, 1.0f, 0.0f)).r, 0.7f, 5.0e-3f);
}

TEST_CASE("LightProbe.HemisphereIrradiance")
{
    // 上半球恒定亮度 L、下半球全黑（经典「天光」解析解）：
    //   E(+Y) = ∫ L·cosθ dω = πL   → 归一化后 = L
    //   E(侧面) = πL/2             → 归一化后 = L/2
    //   E(-Y) = 0
    const float L = 1.25f;
    const Sh9 radiance =
        Sh9::ProjectRadiance([L](const glm::vec3& d) { return d.y > 0.0f ? glm::vec3(L) : glm::vec3(0.0f); }, 1024);
    const Sh9 sh = Sh9::ToDiffuseIrradiance(radiance);

    const float up = Sh9::Evaluate(sh, glm::vec3(0.0f, 1.0f, 0.0f)).r;
    const float side = Sh9::Evaluate(sh, glm::vec3(1.0f, 0.0f, 0.0f)).r;
    const float down = Sh9::Evaluate(sh, glm::vec3(0.0f, -1.0f, 0.0f)).r;

    CHECK_NEAR(up, L, 0.02f);
    CHECK_NEAR(side, L * 0.5f, 0.02f);
    CHECK_NEAR(down, 0.0f, 0.02f); // 负值被钳到 0
    CHECK_GT(up, side);
    CHECK_GT(side, down);
}

TEST_CASE("LightProbe.ProjectReconstructDirectional")
{
    // 平滑方向性光源（cos² 瓣）应在 SH 截断下被较好重建，且方向性单调。
    const float L = 2.0f;
    const auto fn = [L](const glm::vec3& d)
    {
        const float c = std::max(0.0f, d.z);
        return glm::vec3(L * c * c);
    };
    const Sh9 sh = Sh9::ToDiffuseIrradiance(Sh9::ProjectRadiance(fn, 1024));

    const float front = Sh9::Evaluate(sh, glm::vec3(0.0f, 0.0f, 1.0f)).r;
    const float side = Sh9::Evaluate(sh, glm::vec3(1.0f, 0.0f, 0.0f)).r;
    const float back = Sh9::Evaluate(sh, glm::vec3(0.0f, 0.0f, -1.0f)).r;

    CHECK_NEAR(front, L * 0.5f, 0.02f); // 解析解：∫L·cos²θ·cosθ dω / π = L/2
    CHECK_NEAR(back, 0.0f, 0.02f);
    CHECK_GT(front, side);
    CHECK_GT(side, back);

    // L1 截断只保留直流 + 线性项：平均亮度必须与全阶一致（截断不改变 l=0 系数）。
    const Sh9 l1 = sh.TruncateToL1();
    CHECK_NEAR(l1.c[0].r, sh.c[0].r, 1.0e-6f);
    CHECK_NEAR(l1.c[5].r, 0.0f, 1.0e-6f); // L2 项被清零
    // 均匀环境下截断不应改变任何结果。
    const Sh9 amb = Ambient(0.4f);
    CHECK_NEAR(Sh9::Evaluate(amb.TruncateToL1(), glm::vec3(0.0f, -1.0f, 0.0f)).r, 0.4f, 1.0e-3f);
}

TEST_CASE("LightProbe.VolumeTrilinear")
{
    LightProbeVolume v;
    CHECK(v.Resize(glm::ivec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK_EQ(v.ProbeCount(), 8u);

    // 沿 x 做线性梯度：x=0 全部 0.2，x=1 全部 0.8。
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
        {
            CHECK(v.SetProbe(glm::ivec3(0, y, z), Ambient(0.2f)));
            CHECK(v.SetProbe(glm::ivec3(1, y, z), Ambient(0.8f)));
        }

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    // 端点应精确取到该探针值
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.0f, 0.0f, 0.0f), up).r, 0.2f, 1.0e-4f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(1.0f, 0.0f, 0.0f), up).r, 0.8f, 1.0e-4f);
    // 中点 = 线性插值
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.5f, 0.0f, 0.0f), up).r, 0.5f, 1.0e-4f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.25f, 0.0f, 0.0f), up).r, 0.35f, 1.0e-4f);
    // 体心 = 8 个探针的平均（本配置下仍是 0.5）
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.5f, 0.5f, 0.5f), up).r, 0.5f, 1.0e-4f);

    // 空体 → 兜底
    LightProbeVolume empty;
    CHECK_EQ(empty.ProbeCount(), 0u);
    CHECK_NEAR(empty.SampleIrradiance(glm::vec3(0.0f), up).r, 0.0f, 1.0e-6f);
}

TEST_CASE("LightProbe.InvalidProbeExcluded")
{
    LightProbeVolume v;
    CHECK(v.Resize(glm::ivec3(2, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    // 探针 0 埋在实体里（无效），探针 1 有效。中点采样必须只取探针 1，
    // 否则墙内的黑/亮会漏到墙外——这是探针漏光的经典 bug。
    CHECK(v.SetProbe(glm::ivec3(0, 0, 0), Ambient(0.05f), false));
    CHECK(v.SetProbe(glm::ivec3(1, 0, 0), Ambient(0.9f), true));
    CHECK(!v.IsProbeValid(glm::ivec3(0, 0, 0)));
    CHECK(v.IsProbeValid(glm::ivec3(1, 0, 0)));

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.5f, 0.0f, 0.0f), up).r, 0.9f, 1.0e-4f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.0f, 0.0f, 0.0f), up).r, 0.9f, 1.0e-4f);

    // 重新标记有效后应恢复插值
    CHECK(v.SetProbe(glm::ivec3(0, 0, 0), Ambient(0.05f), true));
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.5f, 0.0f, 0.0f), up).r, 0.475f, 1.0e-4f);

    // 采样点正好落在无效探针格上：应退化为「最近的有效探针」，而不是发黑。
    // （直接走 AmbientFallback 会让贴墙的角色忽明忽暗，这是必须避免的 artifact。）
    LightProbeVolume w;
    CHECK(w.Resize(glm::ivec3(3, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(w.SetProbe(glm::ivec3(0, 0, 0), Ambient(0.1f), false));
    CHECK(w.SetProbe(glm::ivec3(1, 0, 0), Ambient(0.5f), false));
    CHECK(w.SetProbe(glm::ivec3(2, 0, 0), Ambient(0.9f), true));
    CHECK_NEAR(w.SampleIrradiance(glm::vec3(0.0f, 0.0f, 0.0f), up).r, 0.9f, 1.0e-4f);
    CHECK_NEAR(w.SampleIrradiance(glm::vec3(1.0f, 0.0f, 0.0f), up).r, 0.9f, 1.0e-4f);
    CHECK_NEAR(w.SampleIrradiance(glm::vec3(2.0f, 0.0f, 0.0f), up).r, 0.9f, 1.0e-4f);
}

TEST_CASE("LightProbe.FallbackWhenAllInvalid")
{
    LightProbeVolume v;
    CHECK(v.Resize(glm::ivec3(2, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    v.SetAmbientFallback(Ambient(0.33f));
    CHECK(v.SetProbe(glm::ivec3(0, 0, 0), Ambient(0.05f), false));
    CHECK(v.SetProbe(glm::ivec3(1, 0, 0), Ambient(0.9f), false));

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.5f, 0.0f, 0.0f), up).r, 0.33f, 1.0e-4f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(0.0f, 0.0f, 0.0f), up).r, 0.33f, 1.0e-4f);
}

TEST_CASE("LightProbe.OutOfBoundsClampsToEdge")
{
    LightProbeVolume v;
    CHECK(v.Resize(glm::ivec3(2, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    v.SetAmbientFallback(Ambient(0.01f));
    CHECK(v.SetProbe(glm::ivec3(0, 0, 0), Ambient(0.2f)));
    CHECK(v.SetProbe(glm::ivec3(1, 0, 0), Ambient(0.8f)));

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    // 越界按边界钳制：远处取最外侧探针，而不是兜底（避免场景边缘突然发黑）。
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(100.0f, 0.0f, 0.0f), up).r, 0.8f, 1.0e-4f);
    CHECK_NEAR(v.SampleIrradiance(glm::vec3(-100.0f, 0.0f, 0.0f), up).r, 0.2f, 1.0e-4f);

    // 原点/间距非平凡时的坐标换算
    LightProbeVolume w;
    CHECK(w.Resize(glm::ivec3(3, 1, 1), glm::vec3(-1.0f, 5.0f, 2.0f), glm::vec3(0.5f)));
    CHECK_NEAR(w.ProbePosition(glm::ivec3(0, 0, 0)).x, -1.0f, 1.0e-6f);
    CHECK_NEAR(w.ProbePosition(glm::ivec3(2, 0, 0)).x, 0.0f, 1.0e-6f);
    CHECK_NEAR(w.ProbePosition(glm::ivec3(0, 0, 0)).y, 5.0f, 1.0e-6f);
    CHECK_NEAR(w.ToGridSpace(glm::vec3(-1.0f, 5.0f, 2.0f)).x, 0.0f, 1.0e-6f);
    CHECK_NEAR(w.ToGridSpace(glm::vec3(0.0f, 5.0f, 2.0f)).x, 2.0f, 1.0e-6f);
}

TEST_CASE("LightProbe.BakeGradientAndValidity")
{
    LightProbeVolume v;
    // 竖直方向 8 层、间距 0.5 的一维塔：模拟「越往上越亮」的室内外过渡。
    CHECK(v.Resize(glm::ivec3(1, 8, 1), glm::vec3(0.0f), glm::vec3(1.0f, 0.5f, 1.0f)));

    // y < 1.0 视为埋在地板/实体里（无效）
    v.Bake(
        [](const glm::vec3& pos, const glm::vec3& dir)
        {
            // 上方来光随高度增强，下方恒暗
            const float sky = std::max(0.0f, dir.y);
            return glm::vec3(sky * (0.2f + 0.1f * pos.y));
        },
        [](const glm::vec3& pos) { return pos.y > 0.5f; }, 256);

    // y=0 / y=0.5 的探针埋在实体里 → 无效
    CHECK(!v.IsProbeValid(glm::ivec3(0, 0, 0)));
    CHECK(!v.IsProbeValid(glm::ivec3(0, 1, 0)));
    CHECK(v.IsProbeValid(glm::ivec3(0, 2, 0)));

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float low = v.SampleIrradiance(glm::vec3(0.0f, 2.0f, 0.0f), up).r;
    const float high = v.SampleIrradiance(glm::vec3(0.0f, 3.5f, 0.0f), up).r;
    CHECK_GT(high, low);  // 越高越亮
    CHECK_GT(high, 0.0f); // 上方确实有光
    CHECK_LT(low, high);  // 单调性（方向一致）
    CHECK_GE(low, 0.0f);  // 钳到非负

    // 向下看的法线应当明显暗于向上（方向性生效，而不只是平均值）
    const float upLit = v.SampleIrradiance(glm::vec3(0.0f, 3.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)).r;
    const float downLit = v.SampleIrradiance(glm::vec3(0.0f, 3.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)).r;
    CHECK_GT(upLit, downLit);

    // 只重算有效性：把 y<=2 全判为无效，顶部探针数应减少
    v.RefreshValidity([](const glm::vec3& pos) { return pos.y > 1.0f; });
    CHECK(!v.IsProbeValid(glm::ivec3(0, 2, 0)));
    CHECK(v.IsProbeValid(glm::ivec3(0, 3, 0)));
}

TEST_CASE("LightProbe.ResizeValidation")
{
    LightProbeVolume v;
    CHECK(!v.Resize(glm::ivec3(0, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f))); // 维度为 0
    CHECK(!v.Resize(glm::ivec3(-1, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(!v.Resize(glm::ivec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(0.0f))); // 间距为 0
    CHECK(!v.Resize(glm::ivec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(-1.0f)));
    CHECK_EQ(v.ProbeCount(), 0u); // 全部失败，保持原状态

    CHECK(v.Resize(glm::ivec3(2, 3, 4), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK_EQ(v.ProbeCount(), 24u);
    // 越界写入被拒
    CHECK(!v.SetProbe(glm::ivec3(2, 0, 0), Ambient(1.0f)));
    CHECK(!v.SetProbe(glm::ivec3(-1, 0, 0), Ambient(1.0f)));
    CHECK(v.Probe(glm::ivec3(9, 9, 9)) == nullptr);
    CHECK(v.Probe(glm::ivec3(0, 0, 0)) != nullptr);
}

TEST_CASE("LightProbe.ShArithmetic")
{
    const Sh9 a = Ambient(0.2f);
    const Sh9 b = Ambient(0.8f);

    const Sh9 mid = LerpSh(a, b, 0.5f);
    CHECK_NEAR(Sh9::Evaluate(mid, glm::vec3(0.0f, 1.0f, 0.0f)).r, 0.5f, 1.0e-4f);
    // 权重越界被钳制
    CHECK_NEAR(Sh9::Evaluate(LerpSh(a, b, -5.0f), glm::vec3(0.0f, 1.0f, 0.0f)).r, 0.2f, 1.0e-4f);
    CHECK_NEAR(Sh9::Evaluate(LerpSh(a, b, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f)).r, 0.8f, 1.0e-4f);

    // 相加 / 数乘
    const Sh9 sum = a + b;
    CHECK_NEAR(Sh9::Evaluate(sum, glm::vec3(0.0f, 1.0f, 0.0f)).r, 1.0f, 1.0e-4f);
    const Sh9 scaled = b * 0.5f;
    CHECK_NEAR(Sh9::Evaluate(scaled, glm::vec3(1.0f, 0.0f, 0.0f)).r, 0.4f, 1.0e-4f);

    // 零向量法线退化处理（不应产生 NaN）
    const float degenerate = Sh9::Evaluate(Ambient(0.5f), glm::vec3(0.0f)).r;
    CHECK(degenerate > 0.0f);
    CHECK(degenerate < 10.0f);
}

TEST_CASE("LightProbe.PackProbeIrradianceUp")
{
    // 未烘焙（空体）→ 返回 0（片元回退单探针，零变化）
    LightProbeVolume empty;
    glm::vec4 dst[4] = {};
    CHECK_EQ(Render::PackProbeIrradianceUp(empty, dst, 4), 0);
    // 非法参数
    CHECK_EQ(Render::PackProbeIrradianceUp(empty, nullptr, 4), 0);

    LightProbeVolume v;
    CHECK(v.Resize(glm::ivec3(2, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(v.SetProbe(glm::ivec3(0, 0, 0), Ambient(0.2f), true));
    CHECK(v.SetProbe(glm::ivec3(1, 0, 0), Ambient(0.8f), false)); // 无效探针

    // 打包到 4 槽缓冲
    glm::vec4 packed[4] = {};
    const int n = Render::PackProbeIrradianceUp(v, packed, 4);
    CHECK_EQ(n, 2);
    // 线性索引序：idx=(z*dy+y)*dx+x → (0,0,0)=0, (1,0,0)=1
    CHECK_NEAR(packed[0].r, 0.2f, 1.0e-5f);
    CHECK_EQ(packed[0].a, 1.0f);  // 有效
    CHECK_NEAR(packed[1].r, 0.8f, 1.0e-5f);
    CHECK_EQ(packed[1].a, 0.0f);  // 无效（片元插值时剔除）

    // 槽位上限截断：maxCount=1 只写第 0 个
    glm::vec4 small[2] = {};
    CHECK_EQ(Render::PackProbeIrradianceUp(v, small, 1), 1);
    CHECK_NEAR(small[0].r, 0.2f, 1.0e-5f);
}
