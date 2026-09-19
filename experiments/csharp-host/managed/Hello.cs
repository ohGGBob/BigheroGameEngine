// Hello.cs —— 最小类库，无 NuGet 依赖。
// 两种互操作边界形态都覆盖（引擎落地时的候选方案）：
//   1) delegate 形态：C++ 侧通过 hostfxr 的 load_assembly_and_get_function_pointer
//      传 delegate_type_name 拿到函数指针（通用，任意签名）。
//   2) [UnmanagedCallersOnly] 形态：C++ 侧传 UNMANAGEDCALLERSONLY_METHOD 拿纯函数
//      指针，无 delegate 分配（引擎 API 边界推荐，与 Unity 内部 native calli 类似）。
using System.Runtime.InteropServices;

namespace Hello;

public delegate int IntIntInt(int a, int b);

public static class MathUtils
{
    public static int Add(int a, int b) => a + b;
}

public static class Native
{
    [UnmanagedCallersOnly]
    public static int AddNative(int a, int b) => a + b;
}
