using BigHero.Runtime;

namespace MyGame
{
    /// <summary>
    /// 演示脚本（U1-S1a/b/c 冒烟用）：让宿主实体绕 Y 轴按 Speed（度/秒）自转。
    /// 对齐方案文档示例（OnUpdate 按 Speed 自转 + Space 加速）——其中 Space 加速依赖输入 API，
    /// 输入投影不在第一增量范围（诚实边界，见任务说明），留待 S1d 之后的增量补齐。
    ///
    /// U1-S1d（脚本公开字段进 Inspector）：实例公开字段即编辑器可调参数——
    ///   public 成员默认可见；[Editor] 把 private 成员暴露进面板；[Range] 提供滑杆范围
    ///   与写回 clamp。支持类型：float/int/bool/Vec3/Color。
    ///
    /// 热重载演示：改 <see cref="Speed"/> 初始值 → 保存 → 引擎每秒轮询源码时间戳自动
    /// 重编译 + 卸载/重载 collectible ALC，OnStart 日志会打印新 Speed 值作为行为变化证据。
    /// 字段值语义：重载 = 实例全部重建，字段回落为新实例默认值（与 Unity 域重载一致）。
    /// </summary>
    public class Spinner : Behaviour
    {
        /// <summary>自转速度（度/秒），滑杆范围 [0, 360]。热重载冒烟：修改此值并保存，引擎侧无需重启。</summary>
        [BigHero.Runtime.Range(0f, 360f)]
        public float Speed = 90.0f;

        /// <summary>是否自转（Inspector 勾选框可实时启停）。</summary>
        public bool SpinEnabled = true;

        /// <summary>自转轴权重（欧拉角增量 = Speed * dt * Axis 分量；默认绕 Y）。</summary>
        public Vec3 Axis = new Vec3(0f, 1f, 0f);

        /// <summary>编辑器可见的私有字段样本（[Editor] 标记；public 无需标注即可见）。</summary>
        [BigHero.Runtime.Editor]
        private float WarmupSeconds = 0.5f;

        /// <summary>编辑器不可见样本（类型不在 U1-S1d 五类支持集 → 反射静默跳过）。</summary>
        public string UnsupportedNote = "string 类型不进 Inspector";

        private float _accumulated;
        private int _frames;

        public override void OnStart()
        {
            _accumulated = 0f;
            _frames = 0;
            Log.Info($"[Spinner] OnStart: entity={Entity.Value}, Speed={Speed:0.#} deg/s, SpinEnabled={SpinEnabled}, "
                     + $"Axis={Axis}, Warmup={WarmupSeconds:0.##}s, initial rotation={Transform.RotationEuler}");
        }

        public override void OnUpdate(float dt)
        {
            if (!SpinEnabled)
                return;

            // Transform 为 readonly struct 属性返回副本（CS1612）：先取副本再经副本写回，
            // setter 内部仍以 Entity 句柄直达引擎权威数据，语义不变
            var t = Transform;
            var rot = t.RotationEuler;
            float step = Speed * dt;
            _accumulated += step;
            ++_frames;
            // 轴权重加权自转；角度回绕到 [0,360) 防止长期运行浮点精度劣化
            float nx = (rot.X + step * Axis.X) % 360f;
            float ny = (rot.Y + step * Axis.Y) % 360f;
            float nz = (rot.Z + step * Axis.Z) % 360f;
            t.RotationEuler = new Vec3(nx, ny, nz);
            if (_frames == 60)
                Log.Info($"[Spinner] 第 60 帧自检: 累计自转 {_accumulated:0.#} deg, rotation={t.RotationEuler}");
        }

        public override void OnDestroy()
        {
            Log.Info($"[Spinner] OnDestroy: entity={Entity.Value}, 本次会话累计自转 {_accumulated:0.#} deg");
        }
    }
}
