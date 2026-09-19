using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace BigHero.Runtime
{
    /// <summary>单个脚本实例的登记项：GCHandle 强引用 + 生命周期状态。</summary>
    internal sealed class ScriptRecord
    {
        public ScriptRecord(GCHandle handle, Behaviour behaviour)
        {
            Handle = handle;
            Behaviour = behaviour;
        }

        public GCHandle Handle;
        public Behaviour Behaviour;
        public bool Started;
    }

    /// <summary>
    /// 托管侧脚本管理器（本类位于【不可卸载】的 BigHero.Runtime / 默认 ALC）：
    ///  - 用户程序集装载进 collectible ALC（可整体卸载热重载，DESIGN.md §6）；
    ///  - Behaviour 实例登记进本类字典，C++ 侧只持 behaviourId，托管侧以 GCHandle(Normal) 强持有
    ///    （GC 移动安全，杜绝指针固化，DESIGN.md §4 关键设计 1）；
    ///  - UpdateAll 每帧单次跨界批量派发 OnStart/OnUpdate（DESIGN.md §3 "单次跨界批量 dispatch"）；
    ///  - 卸载防线（DESIGN.md §6.4 泄漏清单）：Unload 前解除全部 GCHandle 并清空字典、
    ///    置空本类对 ALC 的静态引用，再用 WeakReference + GC 循环确认卸载完成。
    /// </summary>
    internal static class ScriptManager
    {
        private static readonly object Gate = new();
        private static readonly Dictionary<int, ScriptRecord> Scripts = new();
        private static int _nextId = 1;
        private static AssemblyLoadContext? _userAlc;
        private static Assembly? _userAssembly;
        private static int _loadCounter;

        /// <summary>把用户程序集（dll 绝对路径）装载进新的 collectible ALC。已装载则先卸载。</summary>
        public static int LoadUserAssembly(string dllPath)
        {
            lock (Gate)
            {
                if (string.IsNullOrEmpty(dllPath) || !File.Exists(dllPath))
                {
                    HostLog(3, $"[ScriptManager] 用户程序集不存在: {dllPath}");
                    return 1;
                }
                if (_userAlc != null)
                    UnloadUserAssemblyNoLock();

                try
                {
                    var resolver = new AssemblyDependencyResolver(dllPath);
                    var alc = new AssemblyLoadContext("BigHeroUserScripts#" + _loadCounter++, isCollectible: true);
                    // 依赖解析：优先绑定【默认 ALC】（BigHero.Runtime 已由宿主经 hostfxr 载入其中——
                    // 两层架构的关键约束：引擎 API 投影层全进程唯一，否则 Behaviour 基类出现两个
                    // 身份、IsAssignableFrom 失败且 collectible ALC 卸不干净）；默认 ALC 缺失时
                    // 才按 deps.json 定位到用户输出目录。
                    alc.Resolving += (context, name) =>
                    {
                        try
                        {
                            return Assembly.Load(name);
                        }
                        catch
                        {
                            var path = resolver.ResolveAssemblyToPath(name);
                            return path != null ? context.LoadFromAssemblyPath(path) : null;
                        }
                    };
                    var asm = alc.LoadFromAssemblyPath(dllPath);
                    _userAlc = alc;
                    _userAssembly = asm;
                    HostLog(1, $"[ScriptManager] 用户程序集已载入 collectible ALC: {Path.GetFileName(dllPath)}");
                    return 0;
                }
                catch (Exception ex)
                {
                    HostLog(3, $"[ScriptManager] 加载用户程序集失败: {ex.Message}");
                    _userAlc = null;
                    _userAssembly = null;
                    return 2;
                }
            }
        }

        /// <summary>
        /// 卸载用户程序集：先 DetachAll（OnDestroy + GCHandle 全释放），再 alc.Unload()，
        /// 并以 WeakReference + GC 循环（≤10 轮，官方姿势）确认卸载完成。
        /// 返回 0=完全卸载；1=WeakReference 仍存活（疑似泄漏根：栈槽/静态/句柄/线程，见 DESIGN.md §6.4）。
        /// </summary>
        public static int UnloadUserAssembly()
        {
            lock (Gate)
            {
                return UnloadUserAssemblyNoLock();
            }
        }

        private static int UnloadUserAssemblyNoLock()
        {
            DetachAllNoLock();
            // U1-S1d 泄漏防线：字段访问器缓存持用户 ALC 的 MemberInfo，会把 LoaderAllocator
            // 钉活（DESIGN.md §6.4 泄漏清单），必须在 alc.Unload() 前清空。
            EditorSchema.Reset();
            // 卸载必须在【不含 ALC 局部引用的方法帧】之外等待（官方 unloadability 样板的
            // NoInlining 模式）：若 GC 循环仍在持有 ALC 局部槽位的帧内执行，JIT 报告的
            // 栈槽会把 LoaderAllocator 钉活，10 轮 GC 也卸不干净。
            var wr = UnloadCore();
            if (wr == null)
                return 0;
            for (int i = 0; wr.IsAlive && i < 32; i++)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }

            if (wr.IsAlive)
            {
                HostLog(2, "[ScriptManager] ALC 卸载 32 轮 GC 后仍存活（泄漏自检告警，继续运行）");
                return 1;
            }
            HostLog(1, "[ScriptManager] ALC 已完全卸载（WeakReference 死亡确认）");
            return 0;
        }

        /// <summary>
        /// 执行 alc.Unload() 的独立帧：方法返回后本帧的 ALC 局部引用即消亡，
        /// 随后调用方的 GC 等待循环才能观察到 LoaderAllocator 死亡。
        /// NoInlining 禁止内联回调用方（否则局部槽位又回到 GC 循环所在帧）。
        /// </summary>
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static WeakReference? UnloadCore()
        {
            var alc = _userAlc;
            _userAlc = null; // 先断开本类静态引用，否则 ALC 被 default ALC 的静态字段钉活
            _userAssembly = null;
            if (alc == null)
                return null;

            var wr = new WeakReference(alc);
            alc.Unloading += _ => HostLog(1, "[ScriptManager] ALC.Unloading（强引用清理挂点）");
            alc.Unload();
            return wr;
        }

        /// <summary>实例化 typeName（须为 Behaviour 子类）并绑定实体，返回 behaviourId（失败 -1）。</summary>
        public static int Attach(uint entityValue, string typeName)
        {
            lock (Gate)
            {
                if (_userAssembly == null)
                {
                    HostLog(3, "[ScriptManager] Attach 失败: 用户程序集未加载");
                    return -1;
                }

                Type? type = null;
                try
                {
                    type = _userAssembly.GetType(typeName);
                    if (type == null)
                    {
                        foreach (var t in _userAssembly.GetExportedTypes())
                        {
                            if (t.Name == typeName || t.FullName == typeName)
                            {
                                type = t;
                                break;
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    HostLog(3, $"[ScriptManager] 反射类型 {typeName} 失败: {ex.Message}");
                    return -1;
                }

                if (type == null || !typeof(Behaviour).IsAssignableFrom(type))
                {
                    HostLog(3, $"[ScriptManager] Attach 失败: 类型 {typeName} 不存在或不是 Behaviour 子类");
                    return -1;
                }

                Behaviour behaviour;
                try
                {
                    behaviour = (Behaviour)(Activator.CreateInstance(type)
                                            ?? throw new InvalidOperationException("Activator 返回 null"));
                }
                catch (Exception ex)
                {
                    HostLog(3, $"[ScriptManager] 实例化 {type.FullName} 失败: {ex.Message}");
                    return -1;
                }

                behaviour.Entity = new Entity(entityValue);
                int id = _nextId++;
                Scripts[id] = new ScriptRecord(GCHandle.Alloc(behaviour), behaviour);
                HostLog(1, $"[ScriptManager] Attach: {type.FullName} -> entity={entityValue}, behaviourId={id}");
                return id;
            }
        }

        /// <summary>解除全部脚本：逐个回调 OnDestroy（已 Start 过的）并释放 GCHandle。</summary>
        public static int DetachAll()
        {
            lock (Gate)
            {
                return DetachAllNoLock();
            }
        }

        private static int DetachAllNoLock()
        {
            int count = Scripts.Count;
            foreach (var kv in Scripts)
            {
                try
                {
                    if (kv.Value.Started)
                        kv.Value.Behaviour.OnDestroy();
                }
                catch (Exception ex)
                {
                    HostLog(2, $"[ScriptManager] OnDestroy 异常（已吞掉继续清理）: {ex.Message}");
                }
                if (kv.Value.Handle.IsAllocated)
                    kv.Value.Handle.Free(); // 卸载泄漏防线：重载/退出前解除全部 GCHandle
            }
            Scripts.Clear();
            if (count > 0)
                HostLog(1, $"[ScriptManager] DetachAll: 已解除 {count} 个脚本（GCHandle 全部释放）");
            return 0;
        }

        /// <summary>当前存活脚本数。</summary>
        public static int AttachedCount
        {
            get { lock (Gate) return Scripts.Count; }
        }

        // ---- U1-S1d：Inspector 脚本字段（描述表导出 + 读值/写值；低频编辑器路径） ----

        /// <summary>
        /// 导出当前用户程序集全部 Behaviour 子类的字段描述表（经上行原生通道推给 C++）。
        /// 宿主在程序集装载成功后调用（首次 Init 与每次热重载各一次）；失败返回非 0。
        /// </summary>
        public static int ExportSchemas()
        {
            lock (Gate)
            {
                return EditorSchema.ExportSchemas(_userAssembly);
            }
        }

        /// <summary>读脚本实例字段值（behaviourId 失效/下标越界/反射异常 → 1，异常已吞）。</summary>
        public static int GetFieldValue(int behaviourId, int fieldIndex, out FieldValueData value)
        {
            lock (Gate)
            {
                value = default;
                if (!Scripts.TryGetValue(behaviourId, out var rec))
                    return 1;
                return EditorSchema.TryGetValue(rec.Behaviour, fieldIndex, out value) ? 0 : 1;
            }
        }

        /// <summary>写脚本实例字段值（Inspector 写回通道；失败返回 1，异常已吞）。</summary>
        public static int SetFieldValue(int behaviourId, int fieldIndex, ref FieldValueData value)
        {
            lock (Gate)
            {
                if (!Scripts.TryGetValue(behaviourId, out var rec))
                    return 1;
                return EditorSchema.TryWrite(rec.Behaviour, fieldIndex, ref value) ? 0 : 1;
            }
        }

        /// <summary>
        /// 每帧批量派发：首次对每个脚本先补 OnStart，再逐个 OnUpdate(dt)。
        /// 返回托管侧耗时（毫秒）。脚本异常逐个吞掉（不影响其他脚本与引擎主循环）。
        /// </summary>
        public static float UpdateAll(float dt)
        {
            Time.DeltaTime = dt;
            var sw = Stopwatch.StartNew();
            foreach (var kv in Scripts)
            {
                var rec = kv.Value;
                if (!rec.Started)
                {
                    rec.Started = true;
                    try { rec.Behaviour.OnStart(); }
                    catch (Exception ex) { HostLog(2, $"[ScriptManager] OnStart 异常: {ex.Message}"); }
                }
                try { rec.Behaviour.OnUpdate(dt); }
                catch (Exception ex) { HostLog(2, $"[ScriptManager] OnUpdate 异常: {ex.Message}"); }
            }
            sw.Stop();
            return (float)sw.Elapsed.TotalMilliseconds;
        }

        /// <summary>经引擎日志转发（未初始化时退化为 Console）。</summary>
        private static void HostLog(int level, string message) => NativeApi.LogWrite(level, message);
    }
}
