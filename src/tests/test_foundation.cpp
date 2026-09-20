// src/core 基础积木块（bighero 命名空间）运行时单元测试。
// 覆盖：向量/矩阵数学、Mathf、动画曲线、几何原语、二进制序列化（Reader/Writer/CRC32/Base64）、
// 容器（RingBuffer/ObjectPool/BitVector）、确定性随机、程序化噪声。
// 全部为纯 CPU 逻辑，无 GPU/窗口依赖，沙箱可运行。
#include "framework/test_common.h"

#include "core/AABB.h"
#include "core/Base64.h"
#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "core/BitVector.h"
#include "core/CRC32.h"
#include "core/ColorCurve.h"
#include "core/CurveKey.h"
#include "core/EasingCurve_v2.h"
#include "core/FbmNoise.h"
#include "core/FloatCurve.h"
#include "core/Mathf.h"
#include "core/Matrix4.h"
#include "core/ObjectPool.h"
#include "core/Plane3_v2.h"
#include "core/Random.h"
#include "core/RingBuffer.h"
#include "core/SeededRandom.h"
#include "core/Sphere3_v2.h"
#include "core/Triangle.h"
#include "core/Vector2.h"
#include "core/Vector3.h"
#include "core/Vector3Curve.h"
#include "core/Vector4.h"

#include <cmath>

using namespace bighero;
namespace BHCore = BigHero::Core;

namespace
{
constexpr float kEps = 1e-4f;
}

TEST_CASE("Foundation.VectorMath")
{
    // Vector2
    const Vector2 a(1, 2), b(3, 4);
    CHECK_NEAR(a.Dot(b), 11.0f, kEps);
    CHECK_NEAR(a.Cross(b), 1.0f * 4 - 2.0f * 3, kEps); // = -2
    CHECK_NEAR(Vector2(3, 4).Magnitude(), 5.0f, kEps);
    const Vector2 n = Vector2(3, 4).Normalized();
    CHECK_NEAR(n.x, 0.6f, kEps);
    CHECK_NEAR(n.y, 0.8f, kEps);
    CHECK(Vector2(0, 0).Normalized().SqrMagnitude() == 0.0f); // 零向量防护
    const Vector2 l = Vector2::Lerp(a, b, 0.5f);
    CHECK_NEAR(l.x, 2.0f, kEps);
    CHECK_NEAR(l.y, 3.0f, kEps);
    CHECK_NEAR(Vector2::Distance(a, b), std::sqrt(8.0f), kEps);
    CHECK(Vector2::Min(a, b).x == 1.0f && Vector2::Max(a, b).y == 4.0f);

    // Vector3
    const Vector3 x(1, 0, 0), y(0, 1, 0);
    const Vector3 cr = x.Cross(y);
    CHECK_NEAR(cr.x, 0.0f, kEps);
    CHECK_NEAR(cr.y, 0.0f, kEps);
    CHECK_NEAR(cr.z, 1.0f, kEps); // x × y = z
    CHECK_NEAR(x.Dot(y), 0.0f, kEps);
    CHECK_NEAR(Vector3(2, 0, 0).Magnitude(), 2.0f, kEps);
    const Vector3 l3 = Vector3::Lerp(Vector3::Zero(), Vector3::One(), 0.25f);
    CHECK_NEAR(l3.z, 0.25f, kEps);

    // Vector4
    const Vector4 v4(1, 2, 3, 4), w4(5, 6, 7, 8);
    CHECK_NEAR(v4.Dot(w4), 70.0f, kEps);
    CHECK_NEAR(v4.LengthSquared(), 30.0f, kEps);
    CHECK_NEAR(Vector4(0, 2, 0, 0).Normalized().y, 1.0f, kEps);
    CHECK(v4.IsZero() == false);
    CHECK(Vector4().IsZero());
    const Vector4 l4 = Vector4::Lerp(v4, w4, 0.5f);
    CHECK_NEAR(l4.z, 5.0f, kEps);
    CHECK((2.0f * v4).x == 2.0f); // 标量左乘
    CHECK_NEAR(v4[2], 3.0f, kEps);

    // Matrix4
    const Matrix4 id = Matrix4::Identity();
    float ox, oy, oz;
    id.TransformPoint3D(1, 2, 3, ox, oy, oz);
    CHECK_NEAR(ox, 1.0f, kEps);
    CHECK_NEAR(oz, 3.0f, kEps);

    const Matrix4 t = Matrix4::Translation(10, 20, 30);
    t.TransformPoint3D(1, 2, 3, ox, oy, oz);
    CHECK_NEAR(ox, 11.0f, kEps);
    CHECK_NEAR(oy, 22.0f, kEps);
    CHECK_NEAR(oz, 33.0f, kEps);

    const Matrix4 s = Matrix4::Scale(2, 3, 4);
    s.TransformPoint3D(1, 1, 1, ox, oy, oz);
    CHECK_NEAR(ox, 2.0f, kEps);
    CHECK_NEAR(oy, 3.0f, kEps);
    CHECK_NEAR(oz, 4.0f, kEps);

    // 组合（行向量约定，从左到右）：t*s 表示先平移后缩放
    const Matrix4 ts = t * s;
    ts.TransformPoint3D(1, 1, 1, ox, oy, oz);
    CHECK_NEAR(ox, 22.0f, kEps);  // (1+10)*2
    CHECK_NEAR(oy, 63.0f, kEps);  // (1+20)*3
    CHECK_NEAR(oz, 124.0f, kEps); // (1+30)*4

    // 反序：s*t 表示先缩放后平移
    const Matrix4 st = s * t;
    st.TransformPoint3D(1, 1, 1, ox, oy, oz);
    CHECK_NEAR(ox, 12.0f, kEps); // 1*2+10
    CHECK_NEAR(oy, 23.0f, kEps); // 1*3+20
    CHECK_NEAR(oz, 34.0f, kEps); // 1*4+30

    // 绕 Z 轴旋转 90°：X 轴单位向量转到 Y 轴
    const Matrix4 rz = Matrix4::RotationZ(1.5707963f);
    rz.TransformPoint3D(1, 0, 0, ox, oy, oz);
    CHECK_NEAR(ox, 0.0f, kEps);
    CHECK_NEAR(oy, 1.0f, kEps);
}

TEST_CASE("Foundation.Mathf")
{
    CHECK_NEAR(Mathf::Clamp(5.0f, 0.0f, 1.0f), 1.0f, kEps);
    CHECK_NEAR(Mathf::Clamp(-5.0f, 0.0f, 1.0f), 0.0f, kEps);
    CHECK(Mathf::ClampInt(7, 0, 5) == 5);
    CHECK_NEAR(Mathf::Lerp(0.0f, 10.0f, 0.2f), 2.0f, kEps);
    CHECK_NEAR(Mathf::LerpClamped(0.0f, 10.0f, 2.0f), 10.0f, kEps);
    CHECK_NEAR(Mathf::InverseLerp(0.0f, 10.0f, 2.5f), 0.25f, kEps);
    CHECK_NEAR(Mathf::InverseLerp(5.0f, 5.0f, 1.0f), 0.0f, kEps); // 零跨度防护
    CHECK_NEAR(Mathf::SmoothStep(0.0f, 1.0f, 0.5f), 0.5f, kEps);
    CHECK_NEAR(Mathf::SmoothStep(0.0f, 1.0f, 0.0f), 0.0f, kEps);
    CHECK_NEAR(Mathf::Repeat(7.5f, 5.0f), 2.5f, kEps);
    CHECK_NEAR(Mathf::PingPong(7.5f, 5.0f), 2.5f, kEps);
    CHECK_NEAR(Mathf::PingPong(2.5f, 5.0f), 2.5f, kEps);
    CHECK_NEAR(Mathf::Sign(-3.0f), -1.0f, kEps);
    CHECK_NEAR(Mathf::MoveTowards(0.0f, 10.0f, 3.0f), 3.0f, kEps);
    CHECK_NEAR(Mathf::MoveTowards(8.0f, 10.0f, 3.0f), 10.0f, kEps); // 不越过目标
    CHECK_NEAR(Mathf::Deg2Rad(180.0f), 3.14159265f, 1e-3f);
    CHECK_NEAR(Mathf::Rad2Deg(3.14159265f), 180.0f, 1e-2f);
    CHECK_NEAR(Mathf::Sin(Mathf::Deg2Rad(30.0f)), 0.5f, 1e-3f);
}

TEST_CASE("Foundation.Curves")
{
    // CurveKey 三种插值模式
    const CurveKey lin(0, 0, CurveKey::Mode::Linear);
    CHECK_NEAR(lin.Evaluate(10, 0.5f), 5.0f, kEps);
    const CurveKey con(0, 3, CurveKey::Mode::Constant);
    CHECK_NEAR(con.Evaluate(10, 0.9f), 3.0f, kEps); // 恒值保持
    const CurveKey smo(0, 0, CurveKey::Mode::Smooth);
    CHECK_NEAR(smo.Evaluate(10, 0.5f), 5.0f, kEps); // smoothstep 中点对称
    CHECK_NEAR(smo.Evaluate(10, 0.0f), 0.0f, kEps);
    CHECK_NEAR(smo.Evaluate(10, 1.0f), 10.0f, kEps);

    // FloatCurve：乱序插入自动排序、求值、积分
    FloatCurve fc;
    fc.AddKey(2.0f, 20.0f);
    fc.AddKey(0.0f, 0.0f);
    fc.AddKey(1.0f, 10.0f);
    CHECK(fc.KeyCount() == 3);
    CHECK_NEAR(fc.Start(), 0.0f, kEps);
    CHECK_NEAR(fc.End(), 2.0f, kEps);
    CHECK_NEAR(fc.Evaluate(0.5f), 5.0f, kEps);  // 线性段中点
    CHECK_NEAR(fc.Evaluate(-1.0f), 0.0f, kEps); // 范围外钳制
    CHECK_NEAR(fc.Evaluate(99.0f), 20.0f, kEps);
    CHECK_NEAR(fc.Integrate(0.0f, 2.0f), 20.0f, kEps); // 线性 0→20 梯形积分

    // Vector3Curve
    Vector3Curve vc;
    vc.AddKey(0, 0, 0, 0);
    vc.AddKey(1, 10, -10, 5);
    float vx, vy, vz;
    vc.Evaluate(0.5f, vx, vy, vz);
    CHECK_NEAR(vx, 5.0f, kEps);
    CHECK_NEAR(vy, -5.0f, kEps);
    CHECK_NEAR(vz, 2.5f, kEps);
    CHECK(vc.KeyCount() == 2);
    CHECK(!vc.IsEmpty());

    // ColorCurve：RGBA 通道求值 + 钳制
    ColorCurve cc;
    cc.AddKey(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    cc.AddKey(1.0f, 1.0f, 0.5f, 2.0f, -1.0f); // >1 与 <0 会被钳制
    float cr, cg, cb, ca;
    cc.Evaluate(1.0f, cr, cg, cb, ca);
    CHECK_NEAR(cr, 1.0f, kEps);
    CHECK_NEAR(cg, 0.5f, kEps);
    CHECK_NEAR(cb, 1.0f, kEps); // 2 → 钳到 1
    CHECK_NEAR(ca, 0.0f, kEps); // -1 → 钳到 0

    // EasingCurve：线性与平滑采样
    EasingCurve ec;
    ec.AddKey(1.0f, 10.0f);
    ec.AddKey(0.0f, 0.0f); // 乱序插入
    CHECK(ec.KeyCount() == 2);
    CHECK_NEAR(ec.Sample(0.5f), 5.0f, kEps);
    CHECK_NEAR(ec.Sample(-1.0f), 0.0f, kEps);
    CHECK_NEAR(ec.Sample(2.0f), 10.0f, kEps);
    CHECK_NEAR(ec.SampleSmooth(0.0f), 0.0f, kEps);
    CHECK_NEAR(ec.SampleSmooth(1.0f), 10.0f, kEps);
    CHECK_NEAR(ec.SampleSmooth(0.5f), 5.0f, kEps); // smoothstep 中点同值
}

TEST_CASE("Foundation.Geometry")
{
    // AABB（2D）
    const AABB box = AABB::FromCenter(0, 0, 2, 1); // [-2,-1]~[2,1]
    CHECK_NEAR(box.Width(), 4.0f, kEps);
    CHECK_NEAR(box.Height(), 2.0f, kEps);
    CHECK(box.Contains(0, 0));
    CHECK(box.Contains(2, 1)); // 边界含
    CHECK(!box.Contains(2.1f, 0));
    CHECK(box.Overlaps(AABB(1, 0, 3, 2)));
    CHECK(!box.Overlaps(AABB(3, 0, 4, 1)));
    AABB e = box;
    e.Expand(1);
    CHECK_NEAR(e.minX, -3.0f, kEps);
    e.Encapsulate(10, 10);
    CHECK(e.Contains(10, 10));
    CHECK_NEAR(e.CenterX(), 3.5f, kEps);

    // Triangle（2D）
    const Triangle tri(0, 0, 4, 0, 0, 4);
    CHECK_NEAR(tri.Area(), 8.0, 1e-5);
    float cx, cy;
    tri.Centroid(cx, cy);
    CHECK_NEAR(cx, 4.0f / 3, kEps);
    CHECK_NEAR(cy, 4.0f / 3, kEps);
    CHECK(tri.Contains(1, 1));
    CHECK(!tri.Contains(3, 3)); // u+v > 1
    CHECK(!tri.Contains(2, 2.1f));
    float ux, uy;
    tri.Circumcenter(ux, uy);
    CHECK_NEAR(ux, 2.0f, kEps); // 等腰直角三角形外心在斜边中点
    CHECK_NEAR(uy, 2.0f, kEps);

    // Plane3（3D，y=0 地面平面）
    const Plane3 ground(0, 1, 0, 0);
    CHECK_NEAR(ground.SignedDistance(1, 5, 1), 5.0f, kEps);
    CHECK_NEAR(ground.SignedDistance(1, -2, 1), -2.0f, kEps);
    CHECK(ground.Side(0, 3, 0) == 1);
    CHECK(ground.Side(0, -1, 0) == -1);
    CHECK(ground.Side(0, 0, 0) == 0);
    float px = 1, py = 7, pz = 2;
    ground.Project(px, py, pz);
    CHECK_NEAR(py, 0.0f, kEps);
    CHECK_NEAR(px, 1.0f, kEps); // 投影不动面内坐标
    float t = -1;
    const float hit = ground.RayIntersect(0, 5, 0, 0, -1, 0, t);
    CHECK(hit >= 0);
    CHECK_NEAR(t, 5.0f, kEps);
    CHECK(ground.RayIntersect(0, 5, 0, 0, 1, 0, t) < 0); // 平行射线无交

    // Sphere3
    const Sphere3 sa(0, 0, 0, 2);
    CHECK_NEAR(sa.Volume() / ((4.0f / 3.0f) * 3.14159265f * 8.0f), 1.0f, 1e-4f);
    CHECK(sa.Contains(1, 1, 1)); // √3 < 2
    CHECK(!sa.Contains(2, 0, 0.1f));
    const Sphere3 sb(3, 0, 0, 2);
    CHECK(sa.Intersects(sb)); // 圆心距 3 < 2+2
    const Sphere3 sc(10, 0, 0, 2);
    CHECK(!sa.Intersects(sc));
    CHECK_NEAR(sa.DistanceTo(sb), 3.0f, kEps);
    CHECK(sa.Contains(sb) == false);
    const Sphere3 big(0, 0, 0, 10);
    CHECK(big.Contains(sa));
}

TEST_CASE("Foundation.Binary")
{
    // BinaryWriter/Reader 全类型 round-trip
    BinaryWriter w;
    w.WriteU8(0xAB);
    w.WriteI8(-3);
    w.WriteU16(0x1234);
    w.WriteI16(-300);
    w.WriteU32(0xDEADBEEFu);
    w.WriteI32(-100000);
    w.WriteU64(0x0123456789ABCDEFull);
    w.WriteI64(-1);
    w.WriteF32(3.14f);
    w.WriteF64(-2.718281828);
    w.WriteBool(true);
    w.WriteBool(false);
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    w.WriteBytes(payload, 5);
    CHECK(w.Size() == 1 + 1 + 2 + 2 + 4 + 4 + 8 + 8 + 4 + 8 + 1 + 1 + 5);

    BinaryReader r(w.Data());
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    float f32;
    double f64;
    bool bl;
    REQUIRE(r.ReadU8(u8) && r.ReadI8(i8) && r.ReadU16(u16) && r.ReadI16(i16));
    REQUIRE(r.ReadU32(u32) && r.ReadI32(i32) && r.ReadU64(u64) && r.ReadI64(i64));
    REQUIRE(r.ReadF32(f32) && r.ReadF64(f64) && r.ReadBool(bl));
    CHECK(u8 == 0xAB);
    CHECK(i8 == -3);
    CHECK(u16 == 0x1234);
    CHECK(i16 == -300);
    CHECK(u32 == 0xDEADBEEFu);
    CHECK(i32 == -100000);
    CHECK(u64 == 0x0123456789ABCDEFull);
    CHECK(i64 == -1);
    CHECK_NEAR(f32, 3.14f, 1e-5f);
    CHECK_NEAR(f64, -2.718281828, 1e-9);
    CHECK(bl == true);
    bool bl2;
    REQUIRE(r.ReadBool(bl2));
    CHECK(bl2 == false);
    std::vector<uint8_t> bytes;
    REQUIRE(r.ReadBytes(bytes, 5));
    CHECK(bytes.size() == 5 && bytes[4] == 5);
    CHECK(r.Eof());
    CHECK(!r.ReadU8(u8)); // 越界读取失败
    CHECK(!r.Skip(10));

    // CRC32 标准校验值（CRC-32/ISO-HDLC："123456789" → 0xCBF43926）
    CHECK(CRC32::ComputeString("123456789") == 0xCBF43926u);
    CHECK(CRC32::ComputeString("") == 0x00000000u);
    // 增量计算与一次性计算一致
    CRC32 inc;
    const uint32_t part1 = inc.Update(0, "12345", 5);
    const uint32_t part2 = inc.Update(part1, "6789", 4);
    CHECK(part2 == 0xCBF43926u);

    // Base64：标准向量 + round-trip（含非 3 倍数长度）
    namespace B64 = BigHero::Core::Base64;
    CHECK(B64::Encode(std::string("Man")) == "TWFu");
    CHECK(B64::Encode(std::string("hello")) == "aGVsbG8=");
    CHECK(B64::Encode(std::string("hell")) == "aGVsbA==");
    CHECK(B64::Encode(std::string("h")) == "aA==");
    std::string dec;
    REQUIRE(B64::Decode("TWFu", dec));
    CHECK(dec == "Man");
    REQUIRE(B64::Decode("aGVsbG8=", dec));
    CHECK(dec == "hello");
    // 任意字节 round-trip
    std::vector<uint8_t> raw;
    for (int i = 0; i < 257; ++i)
        raw.push_back((uint8_t)i);
    const std::string enc = B64::Encode(raw.data(), raw.size());
    std::vector<uint8_t> back;
    REQUIRE(B64::Decode(enc, back));
    CHECK(back == raw);
    std::vector<uint8_t> ws;
    REQUIRE(B64::Decode("TW Fu\r\n", ws)); // 空白被忽略
    CHECK(ws.size() == 3);
    std::vector<uint8_t> bad;
    CHECK(B64::Decode("AB$C", bad) == false); // 非法字符
}

TEST_CASE("Foundation.Containers")
{
    // RingBuffer：FIFO + 绕回 + 满/空语义
    RingBuffer<int> rb(4);
    CHECK(rb.Empty());
    CHECK(rb.PushBack(1) && rb.PushBack(2) && rb.PushBack(3) && rb.PushBack(4));
    CHECK(rb.Full());
    CHECK(!rb.PushBack(5)); // 满时拒绝
    CHECK(rb.Size() == 4 && rb.Capacity() == 4);
    CHECK(rb.Front() == 1 && rb.Back() == 4);
    CHECK(rb[0] == 1 && rb[3] == 4);
    int out = 0;
    REQUIRE(rb.PopFront(out));
    CHECK(out == 1);
    REQUIRE(rb.PushBack(5)); // 腾出空间后可再入队（绕回）
    CHECK(rb.Size() == 4);
    CHECK(rb[0] == 2 && rb[3] == 5);
    REQUIRE(rb.PopFront(out));
    CHECK(out == 2);
    rb.Clear();
    CHECK(rb.Empty());
    CHECK(!rb.PopFront(out));

    // ObjectPool：获取/回收/上限
    ObjectPool<int> pool(3);
    CHECK(pool.FreeCount() == 3);
    CHECK(pool.Acquire() == 0); // 预填充默认值
    CHECK(pool.FreeCount() == 2);
    pool.Release(std::move(0));
    CHECK(pool.FreeCount() == 3);
    pool.Clear();
    CHECK(pool.Empty());
    CHECK(pool.Acquire() == 0); // 空池构造新对象

    // BitVector：位语义 + 部分字高位清理
    BHCore::BitVector bv(100);
    CHECK(bv.Size() == 100 && bv.WordCount() == 2 && bv.None());
    bv.Set(0);
    bv.Set(63); // 首字最高位
    bv.Set(64); // 次字最低位
    bv.Set(99); // 次字部分位
    CHECK(bv.CountSetBits() == 4);
    CHECK(bv.Test(63) && bv.Test(99) && !bv.Test(62));
    bv.Toggle(0);
    CHECK(!bv.Test(0) && bv.CountSetBits() == 3);
    bv.Clear(63);
    CHECK(bv.CountSetBits() == 2);
    bv.SetAll();
    CHECK(bv.CountSetBits() == 100); // 128 位容量但只有 100 有效位
    CHECK(bv.Any());
    bv.ClearAll();
    CHECK(bv.None());
    bv.Set(1000); // 越界安全
    CHECK(bv.CountSetBits() == 0);
}

TEST_CASE("Foundation.Random")
{
    // 同种子序列可复现（xorshift128）
    Random ra(42), rb2(42);
    bool same = true;
    for (int i = 0; i < 128; ++i)
        if (ra.Next() != rb2.Next())
        {
            same = false;
            break;
        }
    CHECK(same);
    CHECK(ra.NextFloat() >= 0.0f && ra.NextFloat() < 1.0f);
    CHECK(ra.NextFloatSym() >= -1.0f && ra.NextFloatSym() < 1.0f);
    for (int i = 0; i < 64; ++i)
    {
        const float v = ra.Range(3.0f, 7.0f);
        CHECK(v >= 3.0f && v < 7.0f);
        const int n = ra.RangeInt(-2, 2);
        CHECK(n >= -2 && n <= 2);
    }
    // 不同种子序列不同（抽样 8 个值至少 5 个不同，避免偶碰撞）
    Random s1(1), s2(0xFFFFFFFFFFFFFFFFull);
    int diff = 0;
    for (int i = 0; i < 8; ++i)
        if (s1.Next() != s2.Next())
            ++diff;
    CHECK(diff >= 5);

    // SeededRandom（SplitMix64）
    SeededRandom sa(7), sb3(7);
    same = true;
    for (int i = 0; i < 128; ++i)
        if (sa.NextU64() != sb3.NextU64())
        {
            same = false;
            break;
        }
    CHECK(same);
    for (int i = 0; i < 64; ++i)
    {
        const int n = sa.Int(-5, 5);
        CHECK(n >= -5 && n <= 5);
        CHECK(sa.IntBelow(10) < 10);
    }
    CHECK(sa.IntBelow(0) == 0 && sa.IntBelow(-3) == 0); // 非法参数安全
    CHECK(sa.Int(5, 5) == 5);
}

TEST_CASE("Foundation.Noise")
{
    // FbmNoise：确定性 + 值域 + 连续性（相邻点差值有界）
    const FbmNoise n1(1337), n2(1337);
    bool det = true;
    for (int i = 0; i < 32; ++i)
    {
        const float fx = (float)i * 0.37f, fy = (float)i * 0.53f;
        if (std::fabs(n1.Noise(fx, fy) - n2.Noise(fx, fy)) > 1e-6f)
        {
            det = false;
            break;
        }
    }
    CHECK(det);
    for (int i = 0; i < 64; ++i)
    {
        const float v = n1.Noise((float)i * 0.71f, (float)i * 1.13f);
        CHECK(v >= 0.0f && v <= 1.0f);
    }
    // 空间连续性：相邻采样点差值远小于 1
    CHECK(std::fabs(n1.Noise(1.0f, 1.0f) - n1.Noise(1.01f, 1.0f)) < 0.35f);
    // 不同种子产生不同场
    const FbmNoise n3(999);
    int diff = 0;
    for (int i = 0; i < 16; ++i)
        if (std::fabs(n1.Noise((float)i, 0.5f) - n3.Noise((float)i, 0.5f)) > 1e-4f)
            ++diff;
    CHECK(diff >= 8);
    // 八度/持久度参数被接受（不崩溃且仍值域合法）
    const FbmNoise n4(5, 1, 0.5f, 2.0f);
    const FbmNoise n5(5, 8, 0.35f, 3.0f);
    CHECK(n4.Noise(2.5f, 2.5f) >= 0.0f && n4.Noise(2.5f, 2.5f) <= 1.0f);
    CHECK(n5.Noise(2.5f, 2.5f) >= 0.0f && n5.Noise(2.5f, 2.5f) <= 1.0f);
}
