using BigHero.Runtime;

namespace MyGame
{
    /// <summary>
    /// 演示脚本（U1-S1a/b/c 冒烟用）：让宿主实体绕 Y 轴按 Speed（度/秒）自转。
    /// 对齐方案文档示例（OnUpdate 按 Speed 自转 + Space 加速）——其中 Space 加速依赖输入 API，
    /// 输入投影不在第一增量范围（诚实边界，见任务说明），留待 S1d 之后的增量补齐。
    ///
    /// 热重载演示：改 <see cref="Speed"/> 初始值 → 保存 → 引擎每秒轮询源码时间戳自动
    /// 重编译 + 卸载/重载 collectible ALC，OnStart 日志会打印新 Speed 值作为行为变化证据。
    /// </summary>
    public class Spinner : Behaviour
    {
        /// <summary>自转速度（度/秒）。热重载冒烟：修改此值并保存，引擎侧无需重启。</summary>
        public float Speed = 90.0f;

        private float _accumulated;
        private int _frames;

        public override void OnStart()
        {
            _accumulated = 0f;
            _frames = 0;
            Log.Info($"[Spinner] OnStart: entity={Entity.Value}, Speed={Speed:0.#} deg/s, initial rotation={Transform.RotationEuler}");
        }

        public override void OnUpdate(float dt)
        {
            // Transform 为 readonly struct 属性返回副本（CS1612）：先取副本再经副本写回，
            // setter 内部仍以 Entity 句柄直达引擎权威数据，语义不变
            var t = Transform;
            var rot = t.RotationEuler;
            float step = Speed * dt;
            _accumulated += step;
            ++_frames;
            // 绕 Y 轴自转；角度回绕到 [0,360) 防止长期运行浮点精度劣化
            t.RotationEuler = new Vec3(rot.X, (rot.Y + step) % 360f, rot.Z);
            if (_frames == 60)
                Log.Info($"[Spinner] 第 60 帧自检: 累计自转 {_accumulated:0.#} deg, rotation={t.RotationEuler}");
        }

        public override void OnDestroy()
        {
            Log.Info($"[Spinner] OnDestroy: entity={Entity.Value}, 本次会话累计自转 {_accumulated:0.#} deg");
        }
    }
}
