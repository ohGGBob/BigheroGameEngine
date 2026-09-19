namespace BigHero.Runtime
{
    /// <summary>
    /// 脚本组件基类（对标 Unity MonoBehaviour 的最小生命周期三段：Start/Update/OnDestroy）。
    /// 子类放在用户程序集（collectible ALC，可热重载）；由 ScriptManager 经反射实例化，
    /// 挂接时注入所属 <see cref="Entity"/>。生命周期由 C++ 宿主每帧单次跨界批量派发。
    /// 脚本纪律（DESIGN.md §7 坑 3）：不要自建常驻线程、不要把实例挂到非用户程序集的静态字段、
    /// 热路径避免逐帧分配（GC 压力）。
    /// </summary>
    public abstract class Behaviour
    {
        /// <summary>所属实体（挂接时由宿主注入；默认句柄为空）。</summary>
        public Entity Entity { get; internal set; }

        /// <summary>所属实体的空间变换视图（每次读写直达引擎 ECS 权威数据）。</summary>
        public Transform Transform => new Transform(Entity);

        /// <summary>首次 Update 前调用一次（语义对齐 Unity Start）。</summary>
        public virtual void OnStart()
        {
        }

        /// <summary>每帧调用（dt 为帧间隔秒）。</summary>
        public virtual void OnUpdate(float dt)
        {
        }

        /// <summary>实体解绑 / 脚本重载 / 引擎退出时调用。</summary>
        public virtual void OnDestroy()
        {
        }
    }
}
