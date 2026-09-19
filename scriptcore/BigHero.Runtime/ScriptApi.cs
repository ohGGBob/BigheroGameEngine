using System;

namespace BigHero.Runtime
{
    /// <summary>
    /// C++ 宿主经 hostfxr load_assembly_and_get_function_pointer 调用的静态入口集合
    /// （下行 C++ → C# 的全部通道；委托类型嵌套在本类中，限定名形如
    /// "BigHero.Runtime.ScriptApi+InitializeDelegate, BigHero.Runtime"）。
    /// 所有方法吞异常转错误码/日志，防止托管异常穿透到原生调用栈。
    /// 错误码约定：0 成功；正值 = 各自语义的失败类别（详见 ScriptManager）。
    /// </summary>
    public static class ScriptApi
    {
        // ---- 宿主侧加载用的委托类型（签名必须与下面各静态方法一致） ----
        // CharSet.Unicode 必须显式标注：hostfxr 传入的 char_t* 是 UTF-16；委托字符串参数
        // 默认按 CharSet.Ansi 封送，会把 L"D:\..." 截断成 "D"（首个 0x00 字节即止）。
        // CallingConvention 显式 StdCall，与 C++ 侧 __stdcall 声明一致。

        [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.StdCall,
            CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        public delegate void InitializeDelegate(IntPtr nativeApiTable);

        [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.StdCall)]
        public delegate int GetApiVersionDelegate();

        [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.StdCall,
            CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        public delegate int PathDelegate(string path);

        [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.StdCall,
            CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        public delegate int AttachDelegate(uint entityValue, string typeName);

        [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.StdCall)]
        public delegate int VoidDelegate();

        [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.StdCall)]
        public delegate float FloatFloatDelegate(float dt);

        // ---- 静态入口 ----

        /// <summary>注册 C++ 原生函数指针表（必须是首个被调用的入口）。</summary>
        public static void Initialize(IntPtr nativeApiTable)
        {
            try
            {
                NativeApi.Initialize(nativeApiTable);
                HostLog(1, "[ScriptApi] 原生函数表已注册（8 个入口）");
            }
            catch (Exception ex)
            {
                HostLog(3, "[ScriptApi] Initialize 失败: " + ex.Message);
            }
        }

        /// <summary>API 版本协商：宿主校验不匹配则拒绝初始化。</summary>
        public static int GetApiVersion()
        {
            try
            {
                return BigHeroApi.ApiVersion;
            }
            catch
            {
                return -1;
            }
        }

        public static int LoadUserAssembly(string path)
        {
            try { return ScriptManager.LoadUserAssembly(path); }
            catch (Exception ex) { HostLog(3, "[ScriptApi] LoadUserAssembly: " + ex.Message); return 3; }
        }

        public static int UnloadUserAssembly()
        {
            try { return ScriptManager.UnloadUserAssembly(); }
            catch (Exception ex) { HostLog(3, "[ScriptApi] UnloadUserAssembly: " + ex.Message); return 3; }
        }

        /// <summary>实例化并绑定脚本，返回 behaviourId（失败 -1）。</summary>
        public static int Attach(uint entityValue, string typeName)
        {
            try { return ScriptManager.Attach(entityValue, typeName); }
            catch (Exception ex) { HostLog(3, "[ScriptApi] Attach: " + ex.Message); return -1; }
        }

        public static int DetachAll()
        {
            try { return ScriptManager.DetachAll(); }
            catch (Exception ex) { HostLog(3, "[ScriptApi] DetachAll: " + ex.Message); return 3; }
        }

        /// <summary>批量派发 OnStart/OnUpdate，返回托管侧耗时毫秒（异常 -1）。</summary>
        public static float UpdateAll(float dt)
        {
            try { return ScriptManager.UpdateAll(dt); }
            catch (Exception ex) { HostLog(3, "[ScriptApi] UpdateAll: " + ex.Message); return -1f; }
        }

        private static void HostLog(int level, string message) => NativeApi.LogWrite(level, message);
    }
}
