#pragma once
// 四元数（Quaternion）：3D 旋转表示，提供常用运算与旋转向量。
// 纯标准库、仅头文件。
//
// 商业化价值：3D 物体朝向、摄像机旋转、骨骼动画插值的标准数学类型；
// 比欧拉角无万向锁、比矩阵少存储，适合插值（nlerp/slerp）。

#include <cmath>

namespace BigHero::Core
{
class Quaternion
{
  public:
    float x = 0, y = 0, z = 0, w = 1; // w 为实部

    Quaternion() = default;
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static Quaternion Identity() { return Quaternion(0, 0, 0, 1); }

    // 轴角构造（axis 需单位化，angleDeg 以度计）。
    static Quaternion FromAxisAngle(float ax, float ay, float az, float angleDeg)
    {
        float h = angleDeg * 3.14159265358979323846f / 180.0f * 0.5f;
        float s = std::sin(h);
        return Quaternion(ax * s, ay * s, az * s, std::cos(h));
    }

    Quaternion operator*(const Quaternion& q) const
    {
        // Hamilton 乘积
        return Quaternion(
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z);
    }

    Quaternion Normalized() const
    {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len < 1e-8f) return Identity();
        return Quaternion(x / len, y / len, z / len, w / len);
    }

    Quaternion Conjugate() const { return Quaternion(-x, -y, -z, w); }
    Quaternion Inverse() const { return Normalized().Conjugate(); }

    // 归一化线性插值（比 slerp 快，接近匀速时略不精确）。
    static Quaternion Nlerp(const Quaternion& a, const Quaternion& b, float t)
    {
        float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        Quaternion bn = b;
        if (dot < 0.0f)
            bn = Quaternion(-b.x, -b.y, -b.z, -b.w); // 取最短路径
        Quaternion r(a.x + (bn.x - a.x) * t, a.y + (bn.y - a.y) * t,
                     a.z + (bn.z - a.z) * t, a.w + (bn.w - a.w) * t);
        return r.Normalized();
    }

    // 旋转一个向量（v 视为纯四元数 q*v*q^-1）。
    void RotateVector(float& vx, float& vy, float& vz) const
    {
        float ix = w * vx + y * vz - z * vy;
        float iy = w * vy + z * vx - x * vz;
        float iz = w * vz + x * vy - y * vx;
        float iw = -x * vx - y * vy - z * vz;
        // 乘共轭
        vx = ix * w + iw * -x + iy * -z - iz * -y;
        vy = iy * w + iw * -y + iz * -x - ix * -z;
        vz = iz * w + iw * -z + ix * -y - iy * -x;
    }

    [[nodiscard]] float Length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
};
} // namespace BigHero::Core
