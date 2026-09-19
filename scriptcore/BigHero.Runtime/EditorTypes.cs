using System;

namespace BigHero.Runtime
{
    /// <summary>
    /// 把非 public 的实例字段/属性暴露进 Inspector（U1-S1d）。
    /// public 成员默认可见，无需标注；属性须为可读可写（get;set;）。
    /// 对齐任务规格中的 [BH(Editor)] 语义——即 BigHero.Runtime 自有 Attribute 体系
    ///（DESIGN.md §5 "自建 Attribute 体系"），不依赖 System.ComponentModel。
    /// </summary>
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, Inherited = false)]
    public sealed class EditorAttribute : Attribute
    {
    }

    /// <summary>
    /// Inspector 范围约束（滑杆与写回 clamp）：float 作用于标量，int 走整数 clamp，
    /// Vec3/Color 逐分量生效。缺失时不限制（Drag 控件，无 clamp）。
    /// </summary>
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, Inherited = false)]
    public sealed class RangeAttribute : Attribute
    {
        public float Min { get; }

        public float Max { get; }

        public RangeAttribute(float min, float max)
        {
            Min = min;
            Max = max;
        }
    }

    /// <summary>
    /// RGB 颜色（blittable 值类型，R/G/B 与引擎 glm::vec3 色调逐字节对应）。
    /// Inspector 以 ColorEdit3 呈现（U1-S1d 五类支持字段之一）。
    /// </summary>
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    public struct Color
    {
        public float R;
        public float G;
        public float B;

        public Color(float r, float g, float b)
        {
            R = r;
            G = g;
            B = b;
        }

        public override string ToString() => $"({R:0.###}, {G:0.###}, {B:0.###})";
    }
}
