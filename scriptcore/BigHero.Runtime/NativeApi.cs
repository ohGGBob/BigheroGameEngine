using System;
using System.Runtime.InteropServices;

namespace BigHero.Runtime
{
    /// <summary>脚本字段值跨界载体（与 C++ Script::ScriptFieldValue 布局逐字节对应：3 float + 2 int）。</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct FieldValueData
    {
        public float F0;
        public float F1;
        public float F2;
        public int I;
        public int B;
    }

    /// <summary>
    /// C++ 引擎注册的原生函数指针表（上行 C# → C++ 全部 blittable，无运行时 marshal 开销，
    /// 对齐 DESIGN.md §3 "引擎启动时把一张 C 函数指针表注册给托管侧"）。
    /// C++ 侧结构体（src/script/CSharpHost.cpp 的 NativeApiTable）为 9 个连续 8 字节指针，
    /// 字段顺序与本结构体逐一对应（C++ 侧有 static_assert(sizeof == 72) 防漂移）。
    /// </summary>
    public static class NativeApi
    {
        // ---- 委托签名（调用约定与 C++ 侧一致；x64 统一约定，显式标注防 x86 漂移） ----

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void GetVec3Delegate(uint entity, [Out] float[] out3);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void SetVec3Delegate(uint entity, float x, float y, float z);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void GetScaleDelegate(uint entity, out float scale);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void SetScaleDelegate(uint entity, float scale);

        // UTF-8 字符串跨界：LPUTF8Str 走引擎日志（引擎日志通道为 UTF-8）
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogWriteDelegate(int level, [MarshalAs(UnmanagedType.LPUTF8Str)] string message);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate int EntityIsAliveDelegate(uint entity);

        // U1-S1d 脚本字段描述上行：EditorSchema 反射收集后逐字段推送（UTF-8 字符串 ×2 + blittable 标量）
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void RegisterScriptFieldDelegate(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName, int fieldIndex,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string fieldName, int kind, float min, float max, int hasRange);

        // ---- 镜像 C++ NativeApiTable 的指针表（字段顺序 = C++ 声明顺序，勿动） ----
        [StructLayout(LayoutKind.Sequential)]
        private struct Table
        {
            public IntPtr TransformGetPosition;
            public IntPtr TransformSetPosition;
            public IntPtr TransformGetRotation;
            public IntPtr TransformSetRotation;
            public IntPtr TransformGetScale;
            public IntPtr TransformSetScale;
            public IntPtr LogWrite;
            public IntPtr EntityIsAlive;
            public IntPtr EditorRegisterScriptField;
        }

        // ---- 解析后的函数指针（宿主 Initialize 前为 null，各调用点判空降级） ----
        private static GetVec3Delegate? _transformGetPosition;
        private static SetVec3Delegate? _transformSetPosition;
        private static GetVec3Delegate? _transformGetRotation;
        private static SetVec3Delegate? _transformSetRotation;
        private static GetScaleDelegate? _transformGetScale;
        private static SetScaleDelegate? _transformSetScale;
        private static LogWriteDelegate? _logWrite;
        private static EntityIsAliveDelegate? _entityIsAlive;
        private static RegisterScriptFieldDelegate? _editorRegisterScriptField;

        /// <summary>宿主初始化时调用：从原生指针表构建委托。重复调用以最后一次为准。</summary>
        public static void Initialize(IntPtr nativeApiTable)
        {
            if (nativeApiTable == IntPtr.Zero)
                throw new ArgumentException("nativeApiTable 为空指针");
            var table = Marshal.PtrToStructure<Table>(nativeApiTable);

            _transformGetPosition = Marshal.GetDelegateForFunctionPointer<GetVec3Delegate>(table.TransformGetPosition);
            _transformSetPosition = Marshal.GetDelegateForFunctionPointer<SetVec3Delegate>(table.TransformSetPosition);
            _transformGetRotation = Marshal.GetDelegateForFunctionPointer<GetVec3Delegate>(table.TransformGetRotation);
            _transformSetRotation = Marshal.GetDelegateForFunctionPointer<SetVec3Delegate>(table.TransformSetRotation);
            _transformGetScale = Marshal.GetDelegateForFunctionPointer<GetScaleDelegate>(table.TransformGetScale);
            _transformSetScale = Marshal.GetDelegateForFunctionPointer<SetScaleDelegate>(table.TransformSetScale);
            _logWrite = Marshal.GetDelegateForFunctionPointer<LogWriteDelegate>(table.LogWrite);
            _entityIsAlive = Marshal.GetDelegateForFunctionPointer<EntityIsAliveDelegate>(table.EntityIsAlive);
            _editorRegisterScriptField =
                Marshal.GetDelegateForFunctionPointer<RegisterScriptFieldDelegate>(table.EditorRegisterScriptField);
        }

        // ---- Transform（失效句柄：C++ 侧判空后写零值 / no-op） ----

        public static void TransformGetPosition(uint entity, float[] out3)
        {
            var fn = _transformGetPosition;
            if (fn == null) { out3[0] = out3[1] = out3[2] = 0f; return; }
            fn(entity, out3);
        }

        public static void TransformSetPosition(uint entity, float x, float y, float z)
        {
            _transformSetPosition?.Invoke(entity, x, y, z);
        }

        public static void TransformGetRotation(uint entity, float[] out3)
        {
            var fn = _transformGetRotation;
            if (fn == null) { out3[0] = out3[1] = out3[2] = 0f; return; }
            fn(entity, out3);
        }

        public static void TransformSetRotation(uint entity, float x, float y, float z)
        {
            _transformSetRotation?.Invoke(entity, x, y, z);
        }

        public static void TransformGetScale(uint entity, out float scale)
        {
            var fn = _transformGetScale;
            if (fn == null) { scale = 1f; return; }
            fn(entity, out scale);
        }

        public static void TransformSetScale(uint entity, float scale)
        {
            _transformSetScale?.Invoke(entity, scale);
        }

        // ---- Log（level: 0=Debug 1=Info 2=Warn 3=Error，与引擎 LogLevel 枚举值一致） ----

        public static void LogWrite(int level, string message)
        {
            var fn = _logWrite;
            if (fn == null)
            {
                Console.WriteLine("[BigHero.Runtime] " + message);
                return;
            }
            fn(level, message);
        }

        // ---- Entity ----

        public static int EntityIsAlive(uint entity)
        {
            var fn = _entityIsAlive;
            return fn != null ? fn(entity) : 0;
        }

        // ---- Editor（U1-S1d：Inspector 字段描述上行；未注册时静默丢弃，面板退化为无脚本分组） ----

        public static void EditorRegisterScriptField(string typeName, int fieldIndex, string fieldName, int kind,
                                                     float min, float max, int hasRange)
        {
            _editorRegisterScriptField?.Invoke(typeName, fieldIndex, fieldName, kind, min, max, hasRange);
        }
    }
}
