namespace BigHero.Runtime
{
    /// <summary>
    /// 引擎实体句柄：32 位值语义，与 C++ Core::Entity（src/core/ecs.h）逐位对应。
    /// 低 20 位为 index，高位为 version（防悬垂复用）。全程值传递，无指针跨界。
    /// 失效句柄的语义与 Unity 的 "destroyed object" 对齐：<see cref="IsAlive"/> 返回 false，
    /// Transform 读写静默退化为 no-op（读回零值）。
    /// </summary>
    public readonly struct Entity : IEquatable<Entity>
    {
        public Entity(uint value)
        {
            Value = value;
        }

        /// <summary>32 位打包句柄原值（低 20 位 index + 高位 version）。</summary>
        public uint Value { get; }

        /// <summary>是否为空句柄（默认值）。</summary>
        public bool IsNull => Value == 0;

        /// <summary>句柄当前是否指向存活实体（走引擎 Registry::Alive 的 version 校验）。</summary>
        public bool IsAlive => !IsNull && NativeApi.EntityIsAlive(Value) != 0;

        public bool Equals(Entity other) => Value == other.Value;

        public override bool Equals(object? obj) => obj is Entity e && Equals(e);

        public override int GetHashCode() => unchecked((int)Value);

        public static bool operator ==(Entity a, Entity b) => a.Value == b.Value;
        public static bool operator !=(Entity a, Entity b) => a.Value != b.Value;

        public override string ToString() => $"Entity({Value})";
    }
}
