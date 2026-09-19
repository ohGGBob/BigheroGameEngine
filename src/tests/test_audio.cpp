// 音频系统纯逻辑测试（test_audio）。
//
// S1 3D 空间化 + S2 总线混音升级回归：
//   - 总线混音纯逻辑：音量钳制、静音优先级（muted 恒 0）、Master×Bus 串联解算、全链路增益；
//   - 3D 距离衰减曲线：反比（min(1, minDist/max(dist,minDist))^rolloff）与线性对拍表；
//   - SoundSource 纯数据校验：IsValid 非法域拒绝 + Normalize 规整/幂等。
// 全部为离线纯函数测试，不依赖音频设备（CI 无声卡可跑；节点图/设备行为由运行冒烟覆盖）。
#include "framework/test_common.h"

#include "audio/AudioMixer.h"
#include "audio/SoundSource.h"

TEST_CASE("Audio.VolumeClamp")
{
    using namespace BigHero::Audio;
    CHECK_NEAR(ClampVolume(-1.0f), 0.0f, 1e-6f);
    CHECK_NEAR(ClampVolume(0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(ClampVolume(0.75f), 0.75f, 1e-6f);
    CHECK_NEAR(ClampVolume(1.0f), 1.0f, 1e-6f);
    CHECK_NEAR(ClampVolume(kMaxBusVolume), kMaxBusVolume, 1e-6f);
    CHECK_NEAR(ClampVolume(99.0f), kMaxBusVolume, 1e-6f);
}

TEST_CASE("Audio.MutePriority")
{
    using namespace BigHero::Audio;
    // 静音优先级最高：无论音量为多少，增益恒 0
    CHECK_NEAR(BusGain(BusState{1.0f, true}), 0.0f, 1e-6f);
    CHECK_NEAR(BusGain(BusState{0.5f, true}), 0.0f, 1e-6f);
    CHECK_NEAR(BusGain(BusState{kMaxBusVolume, true}), 0.0f, 1e-6f);
    // 未静音：增益 = 钳制后音量
    CHECK_NEAR(BusGain(BusState{0.5f, false}), 0.5f, 1e-6f);
    CHECK_NEAR(BusGain(BusState{1.0f, false}), 1.0f, 1e-6f);
    CHECK_NEAR(BusGain(BusState{-3.0f, false}), 0.0f, 1e-6f);
}

TEST_CASE("Audio.SolveGain")
{
    using namespace BigHero::Audio;
    // 串联解算：Master × Bus
    CHECK_NEAR(SolveGain(1.0f, false, BusState{0.5f, false}), 0.5f, 1e-6f);
    CHECK_NEAR(SolveGain(0.5f, false, BusState{0.4f, false}), 0.2f, 1e-6f);
    CHECK_NEAR(SolveGain(1.0f, false, BusState{1.0f, false}), 1.0f, 1e-6f);
    // 任一级静音 → 0
    CHECK_NEAR(SolveGain(1.0f, true, BusState{0.5f, false}), 0.0f, 1e-6f);
    CHECK_NEAR(SolveGain(1.0f, false, BusState{0.5f, true}), 0.0f, 1e-6f);
    CHECK_NEAR(SolveGain(1.0f, true, BusState{0.5f, true}), 0.0f, 1e-6f);
    // 负音量按 0 处理，超上限钳制
    CHECK_NEAR(SolveGain(-0.5f, false, BusState{1.0f, false}), 0.0f, 1e-6f);
    CHECK_NEAR(SolveGain(10.0f, false, BusState{2.0f, false}), kMaxBusVolume * 2.0f, 1e-6f);
}

TEST_CASE("Audio.SolveFinalGain")
{
    using namespace BigHero::Audio;
    // 全链路：Master × Bus × 音源音量 × 距离衰减
    CHECK_NEAR(SolveFinalGain(0.5f, false, BusState{1.0f, false}, 0.8f, 0.5f), 0.2f, 1e-6f);
    CHECK_NEAR(SolveFinalGain(1.0f, false, BusState{0.5f, false}, 1.0f, 1.0f), 0.5f, 1e-6f);
    // 任一级静音穿透为 0
    CHECK_NEAR(SolveFinalGain(1.0f, false, BusState{0.6f, true}, 1.0f, 1.0f), 0.0f, 1e-6f);
    // 音源音量越界被钳制
    CHECK_NEAR(SolveFinalGain(1.0f, false, BusState{1.0f, false}, -2.0f, 0.5f), 0.0f, 1e-6f);
    CHECK_NEAR(SolveFinalGain(1.0f, false, BusState{1.0f, false}, 99.0f, 1.0f), kMaxBusVolume, 1e-6f);
}

TEST_CASE("Audio.DistanceAttenuation.Inverse")
{
    using namespace BigHero::Audio;
    const float eps = 1e-5f;
    // 对拍表（规格公式 min(1, minDist/max(dist,minDist))^rolloff）：
    // minDist=1, rolloff=1：dist 0.25/1/2/4/8 → 1/1/0.5/0.25/0.125
    CHECK_NEAR(InverseDistanceAttenuation(0.25f, 1.0f, 1.0f), 1.0f, eps);
    CHECK_NEAR(InverseDistanceAttenuation(1.0f, 1.0f, 1.0f), 1.0f, eps);
    CHECK_NEAR(InverseDistanceAttenuation(2.0f, 1.0f, 1.0f), 0.5f, eps);
    CHECK_NEAR(InverseDistanceAttenuation(4.0f, 1.0f, 1.0f), 0.25f, eps);
    CHECK_NEAR(InverseDistanceAttenuation(8.0f, 1.0f, 1.0f), 0.125f, eps);
    // rolloff=2：平方衰减（1/d²）
    CHECK_NEAR(InverseDistanceAttenuation(2.0f, 1.0f, 2.0f), 0.25f, eps);
    CHECK_NEAR(InverseDistanceAttenuation(4.0f, 1.0f, 2.0f), 0.0625f, eps);
    // rolloff=0：不衰减
    CHECK_NEAR(InverseDistanceAttenuation(100.0f, 1.0f, 0.0f), 1.0f, eps);
    // minDist=2：dist 8 → (2/8)^1 = 0.25
    CHECK_NEAR(InverseDistanceAttenuation(8.0f, 2.0f, 1.0f), 0.25f, eps);
    // 统一入口分发 + 非法 minDistance（<=0）兜底为 1
    CHECK_NEAR(DistanceAttenuation(AttenuationModel::Inverse, 2.0f, 1.0f, 60.0f, 1.0f), 0.5f, eps);
    CHECK_NEAR(DistanceAttenuation(AttenuationModel::Inverse, 2.0f, 0.0f, 60.0f, 1.0f), 0.5f, eps);
    CHECK_NEAR(DistanceAttenuation(AttenuationModel::Inverse, 2.0f, -1.0f, 60.0f, 1.0f), 0.5f, eps);
}

TEST_CASE("Audio.DistanceAttenuation.Linear")
{
    using namespace BigHero::Audio;
    const float eps = 1e-5f;
    // 对拍表（AL_LINEAR_DISTANCE_CLAMPED 风格）：min=1 处 1，max=11 处 0，区间线性
    CHECK_NEAR(LinearDistanceAttenuation(1.0f, 1.0f, 11.0f), 1.0f, eps);
    CHECK_NEAR(LinearDistanceAttenuation(6.0f, 1.0f, 11.0f), 0.5f, eps);
    CHECK_NEAR(LinearDistanceAttenuation(11.0f, 1.0f, 11.0f), 0.0f, eps);
    // 区间外钳制：<min 恒 1，>max 恒 0
    CHECK_NEAR(LinearDistanceAttenuation(0.0f, 1.0f, 11.0f), 1.0f, eps);
    CHECK_NEAR(LinearDistanceAttenuation(-5.0f, 1.0f, 11.0f), 1.0f, eps);
    CHECK_NEAR(LinearDistanceAttenuation(50.0f, 1.0f, 11.0f), 0.0f, eps);
    // 退化域：max <= min 按阶跃处理
    CHECK_NEAR(LinearDistanceAttenuation(0.5f, 1.0f, 1.0f), 1.0f, eps);
    CHECK_NEAR(LinearDistanceAttenuation(2.0f, 1.0f, 1.0f), 0.0f, eps);
    // 统一入口分发
    CHECK_NEAR(DistanceAttenuation(AttenuationModel::Linear, 6.0f, 1.0f, 11.0f, 1.0f), 0.5f, eps);
    CHECK_NEAR(DistanceAttenuation(AttenuationModel::Linear, 6.0f, 1.0f, 11.0f, 0.0f), 0.5f, eps);
}

TEST_CASE("Audio.SoundSource.Validate")
{
    using namespace BigHero::Audio;
    // 默认值合法
    CHECK(SoundSource{}.IsValid());

    // minDistance 必须为正
    SoundSource s;
    s.minDistance = 0.0f;
    CHECK(!s.IsValid());
    s.minDistance = -1.0f;
    CHECK(!s.IsValid());

    // maxDistance 不得小于 minDistance
    s = SoundSource{};
    s.maxDistance = 0.5f;
    CHECK(!s.IsValid());

    // rolloff / volume 非负
    s = SoundSource{};
    s.rolloff = -1.0f;
    CHECK(!s.IsValid());
    s = SoundSource{};
    s.volume = -0.1f;
    CHECK(!s.IsValid());

    // 位置/速度不允许 NaN/Inf
    s = SoundSource{};
    s.position = glm::vec3(0.0f, std::nanf(""), 0.0f);
    CHECK(!s.IsValid());
    s = SoundSource{};
    s.velocity = glm::vec3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
    CHECK(!s.IsValid());
}

TEST_CASE("Audio.SoundSource.Normalize")
{
    using namespace BigHero::Audio;
    // 非法值规整到安全默认
    SoundSource n;
    n.minDistance = -3.0f;
    n.maxDistance = -1.0f;
    n.rolloff = -2.0f;
    n.volume = 99.0f;
    n.position = glm::vec3(0.0f, std::nanf(""), 0.0f);
    n.Normalize();
    CHECK(n.IsValid());
    CHECK_NEAR(n.minDistance, 1.0f, 1e-6f);
    CHECK(n.maxDistance >= n.minDistance);
    CHECK_NEAR(n.rolloff, 1.0f, 1e-6f);
    CHECK_NEAR(n.volume, kMaxBusVolume, 1e-6f);
    CHECK(std::isfinite(n.position.y));
    // Normalize 幂等
    n.Normalize();
    CHECK(n.IsValid());
    CHECK_NEAR(n.minDistance, 1.0f, 1e-6f);

    // 合法字段不被 Normalize 改动
    SoundSource keep;
    keep.minDistance = 2.0f;
    keep.maxDistance = 40.0f;
    keep.rolloff = 2.0f;
    keep.volume = 0.7f;
    keep.Normalize();
    CHECK_NEAR(keep.minDistance, 2.0f, 1e-6f);
    CHECK_NEAR(keep.maxDistance, 40.0f, 1e-6f);
    CHECK_NEAR(keep.rolloff, 2.0f, 1e-6f);
    CHECK_NEAR(keep.volume, 0.7f, 1e-6f);

    // max < min 时规整后夹回 min
    SoundSource squeezed;
    squeezed.minDistance = 5.0f;
    squeezed.maxDistance = 2.0f;
    squeezed.Normalize();
    CHECK(squeezed.IsValid());
    CHECK_NEAR(squeezed.maxDistance, 5.0f, 1e-6f);
}
