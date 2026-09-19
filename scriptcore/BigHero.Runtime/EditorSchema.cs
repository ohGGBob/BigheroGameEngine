using System;
using System.Collections.Generic;
using System.Reflection;

namespace BigHero.Runtime
{
    /// <summary>字段种类常量（与 C++ 侧 Script::FieldKind 数值一一对应，勿动）。</summary>
    internal static class FieldKind
    {
        public const int Float = 0;
        public const int Int = 1;
        public const int Bool = 2;
        public const int Vec3 = 3;
        public const int Color = 4;
    }

    /// <summary>
    /// 单个可编辑字段的反射访问器（FieldInfo / PropertyInfo 二选一，值路径走装箱 object——
    /// 编辑器低频路径，DESIGN.md §5 明确"慢、无所谓"；运行时热路径不经过这里）。
    /// 生命周期纪律：本类持有用户 ALC 的 MemberInfo，会把 collectible ALC 钉活
    ///（DESIGN.md §6.4 泄漏清单"静态变量/强引用"）——ScriptManager 卸载用户程序集前
    /// 必须先 <see cref="EditorSchema.Reset"/> 清空缓存。
    /// </summary>
    internal sealed class FieldAccessor
    {
        public string Name = "";

        public int Kind;

        public float Min;

        public float Max;

        public bool HasRange;

        public FieldInfo? Field;

        public PropertyInfo? Property;

        public object? GetValue(object instance) => Field != null ? Field.GetValue(instance) : Property!.GetValue(instance);

        public void SetValue(object instance, object? value)
        {
            if (Field != null)
                Field.SetValue(instance, value);
            else
                Property!.SetValue(instance, value);
        }
    }

    /// <summary>
    /// 托管侧 Inspector 反射（U1-S1d，DESIGN.md §5 "schema 导出（托管侧，一次性）"）：
    ///  - ExportSchemas：扫描用户程序集的 Behaviour 子类，收集实例字段/属性
    ///   （public 或 [Editor] 标记；属性要求可读可写且非索引器），把字段描述
    ///   （名/类型/范围）经上行原生通道 EditorRegisterScriptField 逐字段推给 C++；
    ///   System.Reflection 对象本身【绝不】过边界。
    ///  - TryGetValue / TryWrite：编辑器读值/写值（behaviourId → 实例由 ScriptManager 提供）。
    /// 纪律：反射异常逐个吞掉转日志（单字段/单类型失败不影响其余），字段数上限 64 防御。
    /// </summary>
    internal static class EditorSchema
    {
        private static readonly object Gate = new();
        private static readonly Dictionary<Type, List<FieldAccessor>> Accessors = new();

        /// <summary>清空访问器缓存（卸载用户程序集前必须调用，否则钉活 collectible ALC）。</summary>
        public static void Reset()
        {
            lock (Gate)
            {
                if (Accessors.Count > 0)
                {
                    Accessors.Clear();
                    HostLog(1, "[EditorSchema] 字段访问器缓存已清空（ALC 卸载防线）");
                }
            }
        }

        /// <summary>
        /// 导出用户程序集全部 Behaviour 子类的字段描述（经上行通道；类型须有 ≥1 个可编辑字段）。
        /// 返回 0 成功 / 1 程序集未加载或枚举失败。
        /// </summary>
        public static int ExportSchemas(Assembly? assembly)
        {
            if (assembly == null)
            {
                HostLog(2, "[EditorSchema] ExportSchemas: 用户程序集未加载");
                return 1;
            }
            Type[] types;
            try
            {
                types = assembly.GetExportedTypes();
            }
            catch (Exception ex)
            {
                HostLog(2, "[EditorSchema] 枚举导出类型失败（吞掉）: " + ex.Message);
                return 1;
            }
            int typeCount = 0;
            int fieldTotal = 0;
            foreach (var type in types)
            {
                try
                {
                    if (type.IsAbstract || !typeof(Behaviour).IsAssignableFrom(type))
                        continue;
                    var accessors = GetAccessors(type);
                    if (accessors.Count == 0)
                        continue; // 无可编辑字段不上报（面板不出现空分组）
                    for (int i = 0; i < accessors.Count; i++)
                    {
                        var a = accessors[i];
                        NativeApi.EditorRegisterScriptField(type.FullName ?? type.Name, i, a.Name, a.Kind, a.Min,
                                                            a.Max, a.HasRange ? 1 : 0);
                        fieldTotal++;
                    }
                    typeCount++;
                }
                catch (Exception ex)
                {
                    HostLog(2, $"[EditorSchema] 反射 {type.Name} 失败（吞掉继续）: {ex.Message}");
                }
            }
            HostLog(1, $"[EditorSchema] schema 导出完成: {typeCount} 个脚本类型 / {fieldTotal} 个字段（上限每类型 64）");
            return 0;
        }

        /// <summary>读一个字段值（失败 false——下标越界/反射异常，异常吞掉转日志）。</summary>
        public static bool TryGetValue(Behaviour behaviour, int fieldIndex, out FieldValueData value)
        {
            value = default;
            try
            {
                var accessors = GetAccessors(behaviour.GetType());
                if (fieldIndex < 0 || fieldIndex >= accessors.Count)
                    return false;
                var a = accessors[fieldIndex];
                object? boxed = a.GetValue(behaviour);
                switch (a.Kind)
                {
                    case FieldKind.Float: value.F0 = (float)boxed!; break;
                    case FieldKind.Int: value.I = (int)boxed!; break;
                    case FieldKind.Bool: value.B = (bool)boxed! ? 1 : 0; break;
                    case FieldKind.Vec3:
                    {
                        var t = (Vec3)boxed!;
                        value.F0 = t.X;
                        value.F1 = t.Y;
                        value.F2 = t.Z;
                        break;
                    }
                    case FieldKind.Color:
                    {
                        var c = (Color)boxed!;
                        value.F0 = c.R;
                        value.F1 = c.G;
                        value.F2 = c.B;
                        break;
                    }
                    default: return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                HostLog(2, "[EditorSchema] 读字段失败（吞掉）: " + ex.Message);
                return false;
            }
        }

        /// <summary>写一个字段值（失败 false——下标越界/反射异常，异常吞掉转日志）。</summary>
        public static bool TryWrite(Behaviour behaviour, int fieldIndex, ref FieldValueData value)
        {
            try
            {
                var accessors = GetAccessors(behaviour.GetType());
                if (fieldIndex < 0 || fieldIndex >= accessors.Count)
                    return false;
                var a = accessors[fieldIndex];
                object? boxed;
                switch (a.Kind)
                {
                    case FieldKind.Float: boxed = value.F0; break;
                    case FieldKind.Int: boxed = value.I; break;
                    case FieldKind.Bool: boxed = value.B != 0; break;
                    case FieldKind.Vec3: boxed = new Vec3(value.F0, value.F1, value.F2); break;
                    case FieldKind.Color: boxed = new Color(value.F0, value.F1, value.F2); break;
                    default: return false;
                }
                a.SetValue(behaviour, boxed);
                return true;
            }
            catch (Exception ex)
            {
                HostLog(2, "[EditorSchema] 写字段失败（吞掉）: " + ex.Message);
                return false;
            }
        }

        /// <summary>
        /// 懒建 + 缓存（Type → 字段访问器表）。收集顺序按 MetadataToken 排序 = 声明顺序，
        /// 保证热重载前后同版本程序集的字段下标稳定（撤销值表按下标对应）。
        /// </summary>
        private static List<FieldAccessor> GetAccessors(Type type)
        {
            lock (Gate)
            {
                if (Accessors.TryGetValue(type, out var cached))
                    return cached;
                var list = Collect(type);
                Accessors[type] = list;
                return list;
            }
        }

        private static List<FieldAccessor> Collect(Type type)
        {
            const BindingFlags Flags =
                BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly;
            var members = new List<MemberInfo>();
            // 字段：实例、非 const/readonly、public 或 [Editor]；跳过编译器生成的自动属性后备字段
            //（自动属性由下方 PropertyInfo 分支处理，避免同名双计）
            foreach (var f in type.GetFields(Flags))
            {
                if (f.IsStatic || f.IsInitOnly || f.IsLiteral)
                    continue;
                if (f.Name.Contains("k__BackingField", StringComparison.Ordinal))
                    continue;
                if (!f.IsPublic && f.GetCustomAttribute<EditorAttribute>(false) == null)
                    continue;
                members.Add(f);
            }
            // 属性：实例、可读可写、非索引器，public 或 [Editor]
            foreach (var p in type.GetProperties(Flags))
            {
                if (!p.CanRead || !p.CanWrite || p.GetIndexParameters().Length > 0)
                    continue;
                var getter = p.GetMethod;
                if (getter == null || getter.IsStatic)
                    continue;
                if (!getter.IsPublic && p.GetCustomAttribute<EditorAttribute>(false) == null)
                    continue;
                members.Add(p);
            }
            members.Sort((x, y) => x.MetadataToken.CompareTo(y.MetadataToken));

            var list = new List<FieldAccessor>();
            foreach (var m in members)
            {
                if (list.Count >= MaxFieldsPerScript)
                {
                    HostLog(2, $"[EditorSchema] {type.Name} 可编辑字段超过上限 {MaxFieldsPerScript}，其余已忽略");
                    break;
                }
                var a = TryMakeAccessor(m);
                if (a != null)
                    list.Add(a);
            }
            return list;
        }

        /// <summary>成员 → 访问器；类型不在五类支持集（float/int/bool/Vec3/Color）内返回 null（静默跳过）。</summary>
        private static FieldAccessor? TryMakeAccessor(MemberInfo m)
        {
            Type? valueType;
            var accessor = new FieldAccessor { Name = m.Name };
            if (m is FieldInfo f)
            {
                accessor.Field = f;
                valueType = f.FieldType;
            }
            else if (m is PropertyInfo p)
            {
                accessor.Property = p;
                valueType = p.PropertyType;
            }
            else
            {
                return null;
            }

            accessor.Kind = valueType == typeof(float) ? FieldKind.Float
                            : valueType == typeof(int) ? FieldKind.Int
                            : valueType == typeof(bool) ? FieldKind.Bool
                            : valueType == typeof(Vec3) ? FieldKind.Vec3
                            : valueType == typeof(Color) ? FieldKind.Color
                            : -1;
            if (accessor.Kind < 0)
                return null;

            var range = m.GetCustomAttribute<RangeAttribute>(false);
            if (range != null)
            {
                accessor.HasRange = true;
                accessor.Min = range.Min;
                accessor.Max = range.Max;
            }
            return accessor;
        }

        /// <summary>单类型可编辑字段上限（与 C++ 侧 kMaxScriptFieldsPerType 一致的防御值）。</summary>
        private const int MaxFieldsPerScript = 64;

        private static void HostLog(int level, string message) => NativeApi.LogWrite(level, message);
    }
}
