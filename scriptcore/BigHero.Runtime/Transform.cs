using System.Runtime.InteropServices;

namespace BigHero.Runtime
{
    /// <summary>
    /// 三维向量（blittable 值类型，与引擎 glm::vec3 逐字节对应）。
    /// 独立于 System.Numerics.Vector3：脚本侧显式 X/Y/Z 语义，避免隐式行/列向量约定混淆。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct Vec3
    {
        public float X;
        public float Y;
        public float Z;

        public Vec3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public override string ToString() => $"({X:0.###}, {Y:0.###}, {Z:0.###})";
    }

    /// <summary>
    /// 实体空间变换视图：位置 / 欧拉旋转（度，XYZ 顺序，与引擎编辑器语义一致）/ 均匀缩放。
    /// 每次读写都经原生 C API 直达引擎 ECS 权威数据（Transform 组件），
    /// 不做缓存——脚本对 Transform 的写入当帧即反映到渲染。
    /// 注意：引擎旋转为欧拉角（度），与 Unity 的四元数语义不同（DESIGN.md §4）。
    /// </summary>
    public readonly struct Transform
    {
        public Transform(Entity entity)
        {
            Entity = entity;
        }

        public Entity Entity { get; }

        /// <summary>世界空间位置（米）。</summary>
        public Vec3 Position
        {
            get
            {
                var v = new float[3];
                NativeApi.TransformGetPosition(Entity.Value, v);
                return new Vec3(v[0], v[1], v[2]);
            }
            set => NativeApi.TransformSetPosition(Entity.Value, value.X, value.Y, value.Z);
        }

        /// <summary>欧拉旋转（度，XYZ 顺序）。写入当帧经引擎增量路径刷新层级缓存。</summary>
        public Vec3 RotationEuler
        {
            get
            {
                var v = new float[3];
                NativeApi.TransformGetRotation(Entity.Value, v);
                return new Vec3(v[0], v[1], v[2]);
            }
            set => NativeApi.TransformSetRotation(Entity.Value, value.X, value.Y, value.Z);
        }

        /// <summary>均匀缩放（引擎 Transform 为 uniform scale）。</summary>
        public float Scale
        {
            get
            {
                float s = 1.0f;
                NativeApi.TransformGetScale(Entity.Value, out s);
                return s;
            }
            set => NativeApi.TransformSetScale(Entity.Value, value);
        }

        public override string ToString() => $"Transform(pos={Position}, rot={RotationEuler}, scale={Scale:0.###})";
    }
}
