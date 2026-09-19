// host.cpp —— 最小 CoreCLR 宿主 spike（对标 Unity C# 脚本系统的预研第 1 步）
//
// 做什么：
//   1. 运行时定位并 LoadLibrary("hostfxr.dll")（手写函数指针签名，不用 nethost / import lib）；
//   2. hostfxr_initialize_for_runtime_config 初始化运行时（读 Hello.runtimeconfig.json）；
//   3. hostfxr_get_runtime_delegate 取 load_assembly_and_get_function_pointer；
//   4. 加载 Hello.dll，调用 MathUtils.Add(2,3) 与 Native.AddNative(20,22)，打印结果。
//
// 边界纪律：
//   - 签名参考本机 SDK 的 hostfxr.h / coreclr_delegates.h（只读参考，未拷贝进仓库），
//     这里是最小手写声明。参考路径：
//     C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\<ver>\runtimes\win-x64\native\
//   - 编译：host\build.cmd（vswhere 定位 VS → vcvars64 → cl /std:c++20），独立于引擎根构建。

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 手写最小声明（Windows 上 hostfxr 的 char_t = wchar_t）
// ---------------------------------------------------------------------------
using char_t = wchar_t;

// hostfxr_delegate_type 枚举（hostfxr.h）里 hdt_load_assembly_and_get_function_pointer
// 是第 6 项（前面 5 项：com_activation / load_in_memory_assembly / winrt_activation /
// com_register / com_unregister），即值 5。
constexpr int kHdtLoadAssemblyAndGetFunctionPointer = 5;

// coreclr_delegates.h: #define UNMANAGEDCALLERSONLY_METHOD ((const char_t*)-1)
// 注意：官方头用宏定义；不能写成 constexpr（整型转指针不是常量表达式，MSVC C2131）。
const char_t* const kUnmanagedCallersOnlyMethod = reinterpret_cast<const char_t*>(-1);

// HOSTFXR_CALLTYPE = __cdecl（hostfxr.h 第 16 行）
using hostfxr_initialize_for_runtime_config_fn = int32_t(__cdecl*)(
    const char_t* runtime_config_path, const void* parameters, /*out*/ void** host_context_handle);
using hostfxr_get_runtime_delegate_fn =
    int32_t(__cdecl*)(void* host_context_handle, int delegate_type, /*out*/ void** out_delegate);
using hostfxr_close_fn = int32_t(__cdecl*)(void* host_context_handle);

// CORECLR_DELEGATE_CALLTYPE = __stdcall（coreclr_delegates.h 第 16 行；x64 下与 cdecl 同形）
using load_assembly_and_get_function_pointer_fn = int(__stdcall*)(
    const char_t* assembly_path,       // 程序集完整路径
    const char_t* type_name,           // 程序集限定类型名，如 "Hello.MathUtils, Hello"
    const char_t* method_name,         // public static 方法名
    const char_t* delegate_type_name,  // delegate 限定名 / UNMANAGEDCALLERSONLY_METHOD
    void* reserved,                    // 必须为 0
    /*out*/ void** out_delegate);

// 目标方法签名（两种形态均为 int(int,int)）
using int_int_int_fn = int(__stdcall*)(int, int);

// ---------------------------------------------------------------------------
// 工具：exe 所在目录 / 宽字符串打印
// ---------------------------------------------------------------------------
static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    const size_t slash = s.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

static void Print(const wchar_t* tag, const std::wstring& msg)
{
    wprintf(L"[%s] %s\n", tag, msg.c_str());
}

// ---------------------------------------------------------------------------
// 定位 hostfxr.dll：
//   1) 先试裸 LoadLibrary("hostfxr.dll")（引擎发布形态：DLL 随引擎 exe 分发）；
//   2) 失败则开发机形态：DOTNET_ROOT 或默认安装根下 host\fxr\<最高版本>\hostfxr.dll。
// 返回模块句柄；how 返回命中方式（spike 报告用）。
// ---------------------------------------------------------------------------
static HMODULE LoadHostfxr(std::wstring& how)
{
    HMODULE m = LoadLibraryW(L"hostfxr.dll");
    if (m)
    {
        wchar_t resolved[MAX_PATH];
        GetModuleFileNameW(m, resolved, MAX_PATH);
        how = std::wstring(L"裸 LoadLibraryW(L\"hostfxr.dll\") 命中 DLL 搜索路径 → ") + resolved;
        return m;
    }

    wchar_t envBuf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"DOTNET_ROOT", envBuf, MAX_PATH);
    std::wstring root = (n > 0 && n < MAX_PATH) ? std::wstring(envBuf) : L"C:\\Program Files\\dotnet";
    const std::wstring fxrDir = root + L"\\host\\fxr";

    // 枚举 fxr 目录，取版本号最高的子目录
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((fxrDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        Print(L"ERR", L"找不到 " + fxrDir + L"（且裸 LoadLibrary 失败）");
        return nullptr;
    }
    std::wstring best;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        const std::wstring name = fd.cFileName;
        if (name.find_first_not_of(L"0123456789.") != std::wstring::npos)
            continue; // 只接受纯数字+点（版本目录）
        // 按数字段比较版本
        auto segs = [](const std::wstring& v) {
            std::vector<int> out;
            size_t i = 0;
            while (i <= v.size())
            {
                size_t j = v.find(L'.', i);
                if (j == std::wstring::npos)
                    j = v.size();
                out.push_back(_wtoi(v.substr(i, j - i).c_str()));
                i = j + 1;
            }
            return out;
        };
        if (best.empty() || segs(name) > segs(best))
            best = name;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (best.empty())
    {
        Print(L"ERR", L"fxr 目录下没有版本子目录: " + fxrDir);
        return nullptr;
    }
    const std::wstring dllPath = fxrDir + L"\\" + best + L"\\hostfxr.dll";
    m = LoadLibraryW(dllPath.c_str());
    if (!m)
    {
        Print(L"ERR", L"LoadLibraryW 失败: " + dllPath);
        return nullptr;
    }
    how = L"开发机形态：加载 " + dllPath;
    return m;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int wmain()
{
    _setmode(_fileno(stdout), _O_U8TEXT); // 控制台输出按 UTF-8
    Print(L"STEP 0", L"最小 CoreCLR 宿主 spike 启动");

    // 1. 加载 hostfxr.dll
    std::wstring how;
    HMODULE fxr = LoadHostfxr(how);
    if (!fxr)
        return 1;
    Print(L"STEP 1", how);

    // 2. GetProcAddress 取三个入口
    const auto init_for_runtime_config = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(fxr, "hostfxr_initialize_for_runtime_config"));
    const auto get_runtime_delegate =
        reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(fxr, "hostfxr_get_runtime_delegate"));
    const auto close_fxr = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(fxr, "hostfxr_close"));
    if (!init_for_runtime_config || !get_runtime_delegate || !close_fxr)
    {
        Print(L"ERR", L"GetProcAddress 失败（hostfxr_initialize_for_runtime_config / "
                      L"hostfxr_get_runtime_delegate / hostfxr_close）");
        return 1;
    }
    Print(L"STEP 2", L"GetProcAddress: init_for_runtime_config / get_runtime_delegate / close 全部就绪");

    // 3. 用 Hello.runtimeconfig.json 初始化运行时（路径相对 exe 解析）
    const std::wstring exeDir = ExeDir();
    const std::wstring managedDir = exeDir + L"\\..\\managed\\bin\\Release\\net8.0";
    const std::wstring configPath = managedDir + L"\\Hello.runtimeconfig.json";
    const std::wstring asmPath = managedDir + L"\\Hello.dll";

    void* hostContext = nullptr;
    const int32_t initResult = init_for_runtime_config(configPath.c_str(), nullptr, &hostContext);
    if (initResult != 0 || hostContext == nullptr)
    {
        wchar_t msg[256];
        swprintf(msg, 256, L"hostfxr_initialize_for_runtime_config 失败: 0x%08X  config=%s", (unsigned)initResult,
            configPath.c_str());
        Print(L"ERR", msg);
        return 1;
    }
    Print(L"STEP 3", L"运行时初始化成功（CoreCLR 已启动）");

    // 4. 取 load_assembly_and_get_function_pointer 委托
    void* loadAsmAndGetFnPtr = nullptr;
    const int32_t dlgResult =
        get_runtime_delegate(hostContext, kHdtLoadAssemblyAndGetFunctionPointer, &loadAsmAndGetFnPtr);
    if (dlgResult != 0 || loadAsmAndGetFnPtr == nullptr)
    {
        wchar_t msg[128];
        swprintf(msg, 128, L"hostfxr_get_runtime_delegate(hdt_load_assembly_and_get_function_pointer) 失败: 0x%08X",
            (unsigned)dlgResult);
        Print(L"ERR", msg);
        close_fxr(hostContext);
        return 1;
    }
    const auto loadAsm = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAsmAndGetFnPtr);
    Print(L"STEP 4", L"取得 load_assembly_and_get_function_pointer");

    // 5. 形态一：delegate 形态调 MathUtils.Add(2,3)
    int_int_int_fn add = nullptr;
    const int r1 = loadAsm(asmPath.c_str(), L"Hello.MathUtils, Hello", L"Add", L"Hello.IntIntInt, Hello", nullptr,
        reinterpret_cast<void**>(&add));
    if (r1 != 0 || add == nullptr)
    {
        wchar_t msg[256];
        swprintf(msg, 256, L"加载 Hello.MathUtils.Add（delegate 形态）失败: 0x%08X", (unsigned)r1);
        Print(L"ERR", msg);
        close_fxr(hostContext);
        return 1;
    }
    const int sum = add(2, 3);
    wprintf(L"\n  >>> MathUtils.Add(2, 3) = %d   （期望 5）%s\n\n", sum, sum == 5 ? L"[OK]" : L"[MISMATCH]");

    // 6. 形态二：UnmanagedCallersOnly 调 Native.AddNative(20,22)
    int_int_int_fn addNative = nullptr;
    const int r2 = loadAsm(asmPath.c_str(), L"Hello.Native, Hello", L"AddNative", kUnmanagedCallersOnlyMethod, nullptr,
        reinterpret_cast<void**>(&addNative));
    if (r2 != 0 || addNative == nullptr)
    {
        wchar_t msg[256];
        swprintf(msg, 256, L"加载 Hello.Native.AddNative（UnmanagedCallersOnly 形态）失败: 0x%08X", (unsigned)r2);
        Print(L"ERR", msg);
        close_fxr(hostContext);
        return 1;
    }
    const int sum2 = addNative(20, 22);
    wprintf(L"\n  >>> Native.AddNative(20, 22) = %d  （期望 42）%s\n\n", sum2, sum2 == 42 ? L"[OK]" : L"[MISMATCH]");

    // 7. 收尾
    close_fxr(hostContext);
    FreeLibrary(fxr);
    Print(L"STEP 7", L"hostfxr_close + FreeLibrary 完成");

    const bool pass = (sum == 5) && (sum2 == 42);
    wprintf(L"\nSPIKE %s\n", pass ? L"PASSED" : L"FAILED");
    return pass ? 0 : 2;
}
