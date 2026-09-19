#include "script/CSharpHost.h"

#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <utility>

#ifdef _WIN32

#include <windows.h>
#include <winreg.h>

#include "core/ecs.h"
#include "scene/EcsScene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace BigHero::Script
{
// ============================================================================
// 编码辅助（hostfxr/Win32 为 wchar_t 世界；引擎日志与路径统一 UTF-8）
// ============================================================================

namespace
{
std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string TailOf(const std::string& text, size_t maxChars = 1200)
{
    if (text.size() <= maxChars)
        return text;
    return text.substr(text.size() - maxChars);
}

// 十六进制格式化（LOG 宏基于 ostringstream 链式展开，std::hex 会让返回类型退化为
// ostream& 导致 .str() 不可用，故用预格式化字符串）
std::string Hex32(int32_t v)
{
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(v));
    return buf;
}
} // namespace

// ============================================================================
// 纯逻辑（可离线单测，伪造目录结构）：hostfxr / dotnet 定位。
// 严禁裸 LoadLibrary("hostfxr.dll")——PATH 上 Windows Performance Toolkit 副本会遮蔽
//（DESIGN.md §2.4#5 实测地雷）。
// ============================================================================

bool IsVersionDirectoryName(const std::string& name)
{
    if (name.empty())
        return false;
    // 逐段校验：以 '.' 分段，每段须为 1+ 位数字（拒绝 "8..0" / ".8" / "8." / "8a.0"）
    bool hasDigit = false;
    size_t segStart = 0;
    for (size_t i = 0; i <= name.size(); ++i)
    {
        if (i == name.size() || name[i] == '.')
        {
            if (i == segStart)
                return false; // 空段（开头/结尾/连续点）
            segStart = i + 1;
            continue;
        }
        if (name[i] < '0' || name[i] > '9')
            return false;
        hasDigit = true;
    }
    return hasDigit;
}

std::string PickHighestVersionDirectory(const std::vector<std::string>& names)
{
    std::string best;
    std::vector<int> bestSegs;
    for (const std::string& name : names)
    {
        if (!IsVersionDirectoryName(name))
            continue;
        std::vector<int> segs;
        size_t i = 0;
        while (i <= name.size())
        {
            const size_t j = name.find('.', i);
            const size_t end = (j == std::string::npos) ? name.size() : j;
            segs.push_back(std::atoi(name.substr(i, end - i).c_str()));
            if (j == std::string::npos)
                break;
            i = j + 1;
        }
        if (best.empty() || segs > bestSegs)
        {
            best = name;
            bestSegs = std::move(segs);
        }
    }
    return best;
}

std::vector<std::string> CollectSearchRoots(const std::string& envDotnetRoot,
                                            const std::vector<std::string>& registryRoots,
                                            const std::string& defaultRoot)
{
    std::vector<std::string> roots;
    auto push = [&roots](const std::string& root)
    {
        if (root.empty())
            return;
        if (std::find(roots.begin(), roots.end(), root) != roots.end())
            return; // 去重保序
        roots.push_back(root);
    };
    push(envDotnetRoot); // 1) DOTNET_ROOT 显式覆盖
    for (const std::string& r : registryRoots)
        push(r); // 2) 注册表安装位置（依序）
    push(defaultRoot); // 3) 默认安装根
    return roots;
}

std::vector<std::string> CollectDotnetCandidates(const std::string& envDotnetRoot, const std::string& defaultRoot)
{
    return CollectSearchRoots(envDotnetRoot, {}, defaultRoot);
}

// 在 <root>/host/fxr/ 下枚举版本子目录取最高，命中 hostfxr.dll 返回 UTF-8 绝对路径
std::string FindHostfxrUnderRoot(const std::string& rootUtf8)
{
    const fs::path fxrDir = fs::path(Utf8ToWide(rootUtf8)) / L"host" / L"fxr";
    std::error_code ec;
    if (!fs::is_directory(fxrDir, ec))
        return {};
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(fxrDir, ec))
        if (entry.is_directory(ec))
            names.push_back(WideToUtf8(entry.path().filename().wstring()));
    const std::string best = PickHighestVersionDirectory(names);
    if (best.empty())
        return {};
    const fs::path dll = fxrDir / Utf8ToWide(best) / L"hostfxr.dll";
    if (!fs::is_regular_file(dll, ec))
        return {};
    return WideToUtf8(dll.wstring());
}

// ============================================================================
// 内部工具（Win32）
// ============================================================================

namespace
{
// 注册表安装位置（HKLM\SOFTWARE\dotnet\Setup\InstalledVersions\{x64, WOW6432Node\x64}）
std::vector<std::string> ReadRegistryInstallRoots()
{
    std::vector<std::string> roots;
    const char* keys[] = {
        "SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64",
        "SOFTWARE\\WOW6432Node\\dotnet\\Setup\\InstalledVersions\\x64",
    };
    for (const char* key : keys)
    {
        char buf[MAX_PATH * 2] = {};
        DWORD size = sizeof(buf);
        const LSTATUS st = RegGetValueA(HKEY_LOCAL_MACHINE, key, "InstallLocation", RRF_RT_REG_SZ, nullptr, buf, &size);
        if (st == ERROR_SUCCESS && size > 1)
        {
            std::string value(buf);
            while (!value.empty() && (value.back() == '\\' || value.back() == '/'))
                value.pop_back();
            if (!value.empty())
                roots.push_back(value);
        }
    }
    return roots;
}

std::string GetEnvUtf8(const char* name)
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(Utf8ToWide(name).c_str(), buf, MAX_PATH);
    if (n <= 0 || n >= MAX_PATH)
        return {};
    return WideToUtf8(buf);
}

// dotnet.exe 定位：候选根（DOTNET_ROOT → 默认根）逐项探测，最后 PATH 兜底
std::string FindDotnetExe()
{
    for (const std::string& root : CollectDotnetCandidates(GetEnvUtf8("DOTNET_ROOT"), "C:\\Program Files\\dotnet"))
    {
        std::error_code ec;
        const fs::path p = Utf8ToWide(root) + L"\\dotnet.exe";
        if (fs::is_regular_file(p, ec))
            return WideToUtf8(p.wstring());
    }
    wchar_t pathBuf[MAX_PATH] = {};
    const DWORD r = SearchPathW(nullptr, L"dotnet.exe", nullptr, MAX_PATH, pathBuf, nullptr);
    if (r > 0 && r < MAX_PATH)
        return WideToUtf8(std::wstring(pathBuf, r));
    return {};
}

// 子进程执行（合并 stdout/stderr，超时终止）。返回 exitCode==0。
bool RunProcess(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd, std::string& outText,
                DWORD timeoutMs = 180000)
{
    outText.clear();
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
    {
        outText = "[RunProcess] CreatePipe 失败";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + exe + L"\" " + args;
    const BOOL created = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(writeEnd); // 父进程关写端，子进程退出后读端得 EOF
    if (!created)
    {
        const DWORD err = GetLastError();
        CloseHandle(readEnd);
        outText = "[RunProcess] CreateProcessW 失败 (GetLastError=" + std::to_string(err) + ")";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char buf[4096];
    bool timedOut = false;
    for (;;)
    {
        DWORD pending = 0;
        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &pending, nullptr))
            break; // 管道已关闭（子进程退出）
        if (pending > 0)
        {
            DWORD read = 0;
            if (!ReadFile(readEnd, buf, sizeof(buf), &read, nullptr) || read == 0)
                break;
            outText.append(buf, read);
            continue;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
        WaitForSingleObject(pi.hProcess, 30);
    }
    for (;;) // 排干残余输出
    {
        DWORD read = 0;
        if (!ReadFile(readEnd, buf, sizeof(buf), &read, nullptr) || read == 0)
            break;
        outText.append(buf, read);
    }
    CloseHandle(readEnd);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (timedOut)
    {
        outText += "\n[RunProcess] 超时被终止";
        return false;
    }
    return exitCode == 0;
}
} // namespace

// ============================================================================
// 原生函数指针表（C# → C++ 上行通道，全 blittable）。
// 与 C# 侧 BigHero.Runtime.NativeApi.Table 字段顺序逐一对应（勿动顺序）。
// ============================================================================

namespace
{
struct NativeApiTable
{
    void (*transformGetPosition)(uint32_t entity, float* out3);
    void (*transformSetPosition)(uint32_t entity, float x, float y, float z);
    void (*transformGetRotation)(uint32_t entity, float* out3);
    void (*transformSetRotation)(uint32_t entity, float x, float y, float z);
    void (*transformGetScale)(uint32_t entity, float* out1);
    void (*transformSetScale)(uint32_t entity, float scale);
    void (*logWrite)(int level, const char* utf8);
    int (*entityIsAlive)(uint32_t entity);
};
static_assert(sizeof(NativeApiTable) == 8 * sizeof(void*),
              "NativeApiTable 必须与 C# 侧 NativeApi.Table 逐字段对齐（8 个连续指针）");

// 单进程单 CLR（hostfxr 官方限制）；g_activeScene 供原生回调访问 ECS 权威数据。
Scene::EcsScene* g_activeScene = nullptr;
bool g_clrOwned = false;

const Scene::ecs::Transform* FindTransform(uint32_t entityValue)
{
    if (g_activeScene == nullptr)
        return nullptr;
    const Core::Entity e{entityValue};
    const Core::Registry& reg = g_activeScene->Registry();
    if (!reg.Alive(e))
        return nullptr; // 失效句柄：version 校验（防悬垂复用）
    return reg.TryGet<Scene::ecs::Transform>(e);
}

void BH_TransformGetPosition(uint32_t entityValue, float* out3)
{
    if (out3 != nullptr)
        out3[0] = out3[1] = out3[2] = 0.0f;
    if (const Scene::ecs::Transform* t = FindTransform(entityValue); t != nullptr && out3 != nullptr)
    {
        out3[0] = t->position.x;
        out3[1] = t->position.y;
        out3[2] = t->position.z;
    }
}

void BH_TransformSetPosition(uint32_t entityValue, float x, float y, float z)
{
    if (g_activeScene == nullptr)
        return;
    const Core::Entity e{entityValue};
    const Core::Registry& reg = g_activeScene->Registry();
    if (!reg.Alive(e))
        return;
    // 走 EcsScene 增量写回路径（SetObjectPosition 内部同步刷新层级局部矩阵缓存，
    // 脚本写入当帧即可见）；实体 → 稳定序下标线性反查（第一增量脚本数量小，可接受）
    const std::vector<Core::Entity>& order = g_activeScene->Order();
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (order[i] == e)
        {
            g_activeScene->SetObjectPosition(i, glm::vec3{x, y, z});
            return;
        }
    }
}

void BH_TransformGetRotation(uint32_t entityValue, float* out3)
{
    if (out3 != nullptr)
        out3[0] = out3[1] = out3[2] = 0.0f;
    if (const Scene::ecs::Transform* t = FindTransform(entityValue); t != nullptr && out3 != nullptr)
    {
        out3[0] = t->rotation.x;
        out3[1] = t->rotation.y;
        out3[2] = t->rotation.z;
    }
}

void BH_TransformSetRotation(uint32_t entityValue, float x, float y, float z)
{
    if (g_activeScene == nullptr)
        return;
    const Core::Entity e{entityValue};
    const Core::Registry& reg = g_activeScene->Registry();
    if (!reg.Alive(e))
        return;
    const std::vector<Core::Entity>& order = g_activeScene->Order();
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (order[i] == e)
        {
            g_activeScene->SetObjectRotation(i, glm::vec3{x, y, z});
            return;
        }
    }
}

void BH_TransformGetScale(uint32_t entityValue, float* out1)
{
    if (out1 != nullptr)
        *out1 = 1.0f;
    if (const Scene::ecs::Transform* t = FindTransform(entityValue); t != nullptr && out1 != nullptr)
        *out1 = t->scale;
}

void BH_TransformSetScale(uint32_t entityValue, float scale)
{
    if (g_activeScene == nullptr)
        return;
    const Core::Entity e{entityValue};
    const Core::Registry& reg = g_activeScene->Registry();
    if (!reg.Alive(e))
        return;
    const std::vector<Core::Entity>& order = g_activeScene->Order();
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (order[i] == e)
        {
            g_activeScene->SetObjectScale(i, scale);
            return;
        }
    }
}

void BH_LogWrite(int level, const char* utf8)
{
    if (utf8 == nullptr)
        return;
    LogLevel lv = LogLevel::Info;
    if (level <= 0)
        lv = LogLevel::Debug;
    else if (level == 2)
        lv = LogLevel::Warn;
    else if (level >= 3)
        lv = LogLevel::Error;
    LogMessage(lv, std::string(utf8));
}

int BH_EntityIsAlive(uint32_t entityValue)
{
    if (g_activeScene == nullptr)
        return 0;
    return g_activeScene->Registry().Alive(Core::Entity{entityValue}) ? 1 : 0;
}
} // namespace

// ============================================================================
// hostfxr / 托管入口函数指针（手写最小声明，签名对齐 spike host.cpp 与官方头）
// ============================================================================

namespace
{
using char_t = wchar_t;

constexpr int kHdtLoadAssemblyAndGetFunctionPointer = 5;

using hostfxr_initialize_for_runtime_config_fn =
    int32_t(__cdecl*)(const char_t* runtime_config_path, const void* parameters, void** host_context_handle);
using hostfxr_get_runtime_delegate_fn = int32_t(__cdecl*)(void* host_context_handle, int delegate_type, void** out);
using hostfxr_close_fn = int32_t(__cdecl*)(void* host_context_handle);
using load_assembly_and_get_function_pointer_fn = int(__stdcall*)(const char_t* assembly_path,
                                                                  const char_t* type_name, const char_t* method_name,
                                                                  const char_t* delegate_type_name, void* reserved,
                                                                  void** out_delegate);

// BigHero.Runtime.ScriptApi 静态入口（delegate 形态；wchar_t*/string 由 hostfxr 桩自动封送）
using ScriptApi_InitializeFn = void(__stdcall*)(void* nativeApiTable);
using ScriptApi_GetApiVersionFn = int(__stdcall*)();
using ScriptApi_LoadUserAssemblyFn = int(__stdcall*)(const char_t* path);
using ScriptApi_AttachFn = int(__stdcall*)(uint32_t entityValue, const char_t* typeName);
using ScriptApi_VoidFn = int(__stdcall*)();
using ScriptApi_UpdateAllFn = float(__stdcall*)(float dt);
} // namespace

// ============================================================================
// 构建辅助（用户脚本 / BigHero.Runtime 的 dotnet 编译与缓存）
// ============================================================================

namespace
{
// 用户脚本源码（*.cs / *.csproj）最新修改时间；跳过 obj/bin/.bighero 等构建产物目录
//（obj/ 内含 MSBuild 生成的 .cs，不剪枝会把构建产物误判为源码变更 → 热重载死循环）
fs::file_time_type NewestSourceTime(const std::wstring& scriptsDirW)
{
    std::error_code ec;
    auto newest = fs::file_time_type::min();
    if (!fs::is_directory(scriptsDirW, ec))
        return newest;
    for (auto it = fs::recursive_directory_iterator(scriptsDirW, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        const std::wstring name = it->path().filename().wstring();
        if (it->is_directory(ec))
        {
            if (name == L"obj" || name == L"bin" || name == L".bighero" || name == L".vs" || name == L".git")
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec))
            continue;
        const std::wstring ext = it->path().extension().wstring();
        if (ext != L".cs" && ext != L".csproj")
            continue;
        const auto t = fs::last_write_time(it->path(), ec);
        if (!ec && t > newest)
            newest = t;
    }
    return newest;
}

// 扫描 <buildBase>/scripts 下已存在的最高构建版本（目录名 vN 且含 <assembly>.dll）；无则 0
int FindLatestBuildVersion(const std::string& buildBase, const std::wstring& assemblyDllName)
{
    std::error_code ec;
    const fs::path scriptsRoot = fs::path(Utf8ToWide(buildBase)) / L"scripts";
    if (!fs::is_directory(scriptsRoot, ec))
        return 0;
    int best = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(scriptsRoot, ec))
    {
        if (!entry.is_directory(ec))
            continue;
        const std::string name = WideToUtf8(entry.path().filename().wstring());
        if (name.size() < 2 || name[0] != 'v')
            continue;
        const std::string digits = name.substr(1);
        if (digits.find_first_not_of("0123456789") != std::string::npos)
            continue;
        best = std::max(best, std::atoi(digits.c_str()));
    }
    if (best > 0 && !fs::is_regular_file(scriptsRoot / (L"v" + std::to_wstring(best)) / assemblyDllName, ec))
        return 0;
    return best;
}

// 定位 scriptcore/BigHero.Runtime/BigHero.Runtime.csproj：编译期注入源码根 → 从 cwd 向上回溯兜底
std::string LocateRuntimeProject()
{
    std::error_code ec;
#if defined(BIGHERO_SOURCE_ROOT)
    {
        const fs::path p = fs::path(Utf8ToWide(BIGHERO_SOURCE_ROOT)) / L"scriptcore" / L"BigHero.Runtime"
                           / L"BigHero.Runtime.csproj";
        if (fs::is_regular_file(p, ec))
            return WideToUtf8(p.wstring());
    }
#endif
    fs::path cur = fs::current_path(ec);
    for (int i = 0; i < 6 && !cur.empty(); ++i)
    {
        const fs::path p = cur / L"scriptcore" / L"BigHero.Runtime" / L"BigHero.Runtime.csproj";
        if (fs::is_regular_file(p, ec))
            return WideToUtf8(p.wstring());
        if (!cur.has_parent_path())
            break;
        cur = cur.parent_path();
    }
    return {};
}

// 编译 BigHero.Runtime（默认 ALC 层，EnableDynamicLoading 生成 runtimeconfig.json）。
// 产物新鲜（dll 不早于任何源码）时复用，跳过 dotnet 调用（加速重复启动/测试）。
bool EnsureRuntimeBuilt(const std::string& dotnet, const std::string& runtimeProj, const std::string& runtimeDir,
                        std::string& errTail)
{
    std::error_code ec;
    if (runtimeProj.empty())
    {
        errTail = "未找到 scriptcore/BigHero.Runtime/BigHero.Runtime.csproj";
        return false;
    }
    const fs::path dllPath = fs::path(Utf8ToWide(runtimeDir)) / L"BigHero.Runtime.dll";
    const fs::path projDir = fs::path(Utf8ToWide(runtimeProj)).parent_path();
    if (fs::is_regular_file(dllPath, ec))
    {
        const auto dllTime = fs::last_write_time(dllPath, ec);
        bool stale = false;
        for (auto it = fs::recursive_directory_iterator(projDir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file(ec))
                continue;
            const std::wstring ext = it->path().extension().wstring();
            if (ext != L".cs" && ext != L".csproj")
                continue;
            if (fs::last_write_time(it->path(), ec) > dllTime)
            {
                stale = true;
                break;
            }
        }
        if (!stale)
            return true;
    }
    std::error_code mkEc;
    fs::create_directories(Utf8ToWide(runtimeDir), mkEc);
    const std::wstring args = L"build \"" + Utf8ToWide(runtimeProj) + L"\" -c Release -o \"" + Utf8ToWide(runtimeDir)
                              + L"\" --nologo -v q";
    std::string out;
    if (!RunProcess(Utf8ToWide(dotnet), args, {}, out))
    {
        errTail = TailOf(out);
        return false;
    }
    return fs::is_regular_file(dllPath, ec);
}
} // namespace

// ============================================================================
// PIMPL 状态
// ============================================================================

struct CSharpHost::Impl
{
    // hostfxr
    HMODULE fxrModule = nullptr;
    void* hostContext = nullptr;
    hostfxr_initialize_for_runtime_config_fn initForRuntimeConfig = nullptr;
    hostfxr_get_runtime_delegate_fn getRuntimeDelegate = nullptr;
    hostfxr_close_fn closeFxr = nullptr;
    load_assembly_and_get_function_pointer_fn loadAsmAndGetFn = nullptr;

    // BigHero.Runtime（默认 ALC）托管入口
    ScriptApi_InitializeFn initialize = nullptr;
    ScriptApi_GetApiVersionFn getApiVersion = nullptr;
    ScriptApi_LoadUserAssemblyFn loadUserAssembly = nullptr;
    ScriptApi_VoidFn unloadUserAssembly = nullptr;
    ScriptApi_AttachFn attach = nullptr;
    ScriptApi_VoidFn detachAll = nullptr;
    ScriptApi_UpdateAllFn updateAll = nullptr;

    NativeApiTable nativeApi{};

    // 状态
    Scene::EcsScene* scene = nullptr;
    std::string dotnetExe;     // dotnet CLI 绝对路径（Init 时定位，热重载复用）
    std::string scriptsDir;    // 用户脚本工程目录（UTF-8）
    std::wstring scriptsDirW;
    std::string assemblyName;  // 用户程序集名（由 csproj 文件名推导，如 "MyGame"）
    std::string projFileName;  // 工程文件名（如 "MyGame.csproj"）
    std::string buildBase;     // <scriptsDir>/.bighero
    std::string runtimeDir;    // <buildBase>/runtime（BigHero.Runtime 构建输出，永不热重载）
    std::wstring runtimeDllW;
    int version = 0;      // 当前加载的用户程序集构建版本（scripts/vN）
    std::string userDll;  // 当前加载的用户程序集 dll 路径（UTF-8）
    float pollTimer = 0.0f;
    bool clrRunning = false;

    // 挂接记录：热重载后按同一份记录自动重挂（orderIndex 越界说明实体已销毁 → 跳过）
    struct Binding
    {
        size_t orderIndex = 0;
        std::string typeName;
        int behaviourId = -1;
    };
    std::vector<Binding> bindings;

    // 编译用户脚本到指定版本目录；成功时输出 dll 路径，失败时输出日志尾部
    bool BuildUserScriptsTo(int targetVersion, std::string& dllOut, std::string& errTail)
    {
        const std::string outDir = buildBase + "/scripts/v" + std::to_string(targetVersion);
        std::error_code ec;
        fs::create_directories(Utf8ToWide(outDir), ec);
        const std::wstring args = L"build \"" + (scriptsDirW + L"\\" + Utf8ToWide(projFileName)) + L"\" -c Release -o \""
                                  + Utf8ToWide(outDir) + L"\" --nologo -v q";
        std::string out;
        if (!RunProcess(Utf8ToWide(dotnetExe), args, scriptsDirW, out))
        {
            errTail = TailOf(out);
            return false;
        }
        dllOut = outDir + "/" + assemblyName + ".dll";
        return fs::is_regular_file(Utf8ToWide(dllOut), ec);
    }
};

namespace
{
constexpr float kPollIntervalSeconds = 1.0f; // 热重载轮询周期（秒）
} // namespace

// ============================================================================
// 宿主实现
// ============================================================================

bool CSharpHost::Init(Scene::EcsScene* scene, const std::string& scriptsDirUtf8)
{
    if (enabled_)
        return true;
    if (impl_ == nullptr)
        impl_ = new Impl();
    Impl& im = *impl_;
    std::error_code ec;

    // ---- 0. 前置校验（快速失败，不依赖 .NET：降级路径可确定性单测） ----
    if (g_clrOwned)
    {
        LOG_WARN("CSharpHost: 进程内已存在已初始化的脚本宿主（单进程单 CLR），拒绝重复初始化");
        return false;
    }
    if (scene == nullptr)
    {
        LOG_WARN("CSharpHost: Init 缺少 EcsScene，脚本系统降级为禁用");
        return false;
    }
    const fs::path dir = Utf8ToWide(scriptsDirUtf8);
    if (scriptsDirUtf8.empty() || !fs::is_directory(dir, ec))
    {
        LOG_WARN("CSharpHost: 脚本目录不存在: " << scriptsDirUtf8 << "（脚本系统降级为禁用，引擎正常继续）");
        return false;
    }
    std::string projName;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == L".csproj")
        {
            projName = WideToUtf8(entry.path().filename().wstring());
            break;
        }
    }
    if (projName.empty())
    {
        LOG_WARN("CSharpHost: 脚本目录下没有 .csproj 工程文件: " << scriptsDirUtf8 << "（脚本系统降级为禁用）");
        return false;
    }
    im.scene = scene;
    im.scriptsDir = scriptsDirUtf8;
    im.scriptsDirW = dir.wstring();
    im.projFileName = projName;
    im.assemblyName = projName.substr(0, projName.size() - 7); // 去掉 ".csproj" = 程序集名
    im.buildBase = scriptsDirUtf8 + "/.bighero";

    // ---- 1. dotnet CLI（用户程序集编译用） ----
    im.dotnetExe = FindDotnetExe();
    if (im.dotnetExe.empty())
    {
        LOG_WARN("CSharpHost: 未找到 dotnet CLI（DOTNET_ROOT / 默认安装根 / PATH 均未命中），C# 脚本系统降级为禁用");
        return false;
    }

    // ---- 2. hostfxr 显式解析（顺序：DOTNET_ROOT → 注册表 → 默认安装根；DESIGN.md §5.1） ----
    const std::vector<std::string> roots =
        CollectSearchRoots(GetEnvUtf8("DOTNET_ROOT"), ReadRegistryInstallRoots(), "C:\\Program Files\\dotnet");
    std::string fxrPath;
    for (const std::string& root : roots)
    {
        fxrPath = FindHostfxrUnderRoot(root);
        if (!fxrPath.empty())
            break;
    }
    if (fxrPath.empty())
    {
        LOG_WARN("CSharpHost: 在 " << roots.size()
                                   << " 个候选根下均未找到 host/fxr/<ver>/hostfxr.dll（.NET 运行时未安装？），"
                                      "C# 脚本系统降级为禁用");
        return false;
    }
    im.fxrModule = LoadLibraryW(Utf8ToWide(fxrPath).c_str());
    if (im.fxrModule == nullptr)
    {
        LOG_WARN("CSharpHost: LoadLibraryW 失败: " << fxrPath << "（脚本系统降级为禁用）");
        return false;
    }
    im.initForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(im.fxrModule, "hostfxr_initialize_for_runtime_config"));
    im.getRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        GetProcAddress(im.fxrModule, "hostfxr_get_runtime_delegate"));
    im.closeFxr = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(im.fxrModule, "hostfxr_close"));
    if (im.initForRuntimeConfig == nullptr || im.getRuntimeDelegate == nullptr || im.closeFxr == nullptr)
    {
        LOG_WARN("CSharpHost: hostfxr 导出函数缺失（版本异常），脚本系统降级为禁用");
        return false;
    }
    LOG_INFO("CSharpHost: hostfxr 已加载: " << fxrPath << "（显式路径解析，非裸 LoadLibrary）");

    // ---- 3. 编译/复用 BigHero.Runtime（默认 ALC 层，永不热重载） ----
    im.runtimeDir = im.buildBase + "/runtime";
    std::string runtimeErr;
    if (!EnsureRuntimeBuilt(im.dotnetExe, LocateRuntimeProject(), im.runtimeDir, runtimeErr))
    {
        LOG_ERROR("CSharpHost: BigHero.Runtime 编译失败，脚本系统降级为禁用。dotnet 输出尾部:\n" << runtimeErr);
        return false;
    }
    im.runtimeDllW = fs::path(Utf8ToWide(im.runtimeDir)) / L"BigHero.Runtime.dll";

    // ---- 4. 启动 CoreCLR ----
    const fs::path runtimeConfigPath = fs::path(Utf8ToWide(im.runtimeDir)) / L"BigHero.Runtime.runtimeconfig.json";
    if (!fs::is_regular_file(runtimeConfigPath, ec))
    {
        LOG_ERROR("CSharpHost: 缺少 runtimeconfig.json: " << im.runtimeDir
                                                          << "（BigHero.Runtime.csproj 须 <EnableDynamicLoading>true）");
        return false;
    }
    const int32_t initRc = im.initForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &im.hostContext);
    if (initRc != 0 || im.hostContext == nullptr)
    {
        LOG_WARN("CSharpHost: hostfxr_initialize_for_runtime_config 失败: " << Hex32(initRc)
                                                                           << "（脚本系统降级为禁用）");
        im.hostContext = nullptr;
        return false;
    }
    void* loadAsmFn = nullptr;
    const int32_t dlgRc = im.getRuntimeDelegate(im.hostContext, kHdtLoadAssemblyAndGetFunctionPointer, &loadAsmFn);
    if (dlgRc != 0 || loadAsmFn == nullptr)
    {
        LOG_WARN("CSharpHost: hostfxr_get_runtime_delegate 失败: " << Hex32(dlgRc) << "（脚本系统降级为禁用）");
        return false;
    }
    im.loadAsmAndGetFn = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAsmFn);
    im.clrRunning = true;

    // ---- 5. 加载 BigHero.Runtime + 注册原生函数表 + API 版本校验 ----
    const wchar_t* scriptApiType = L"BigHero.Runtime.ScriptApi, BigHero.Runtime";
    auto loadManagedFn = [&](const wchar_t* method, const wchar_t* delegateTypeName, void** out) {
        void* fnPtr = nullptr;
        const int rc = im.loadAsmAndGetFn(im.runtimeDllW.c_str(), scriptApiType, method, delegateTypeName, nullptr, &fnPtr);
        if (rc != 0 || fnPtr == nullptr)
        {
            LOG_ERROR("CSharpHost: 加载托管入口 " << WideToUtf8(method) << " 失败: " << Hex32(rc));
            return false;
        }
        *out = fnPtr;
        return true;
    };
    const bool allLoaded =
        loadManagedFn(L"Initialize", L"BigHero.Runtime.ScriptApi+InitializeDelegate, BigHero.Runtime",
                      reinterpret_cast<void**>(&im.initialize))
        && loadManagedFn(L"GetApiVersion", L"BigHero.Runtime.ScriptApi+GetApiVersionDelegate, BigHero.Runtime",
                         reinterpret_cast<void**>(&im.getApiVersion))
        && loadManagedFn(L"LoadUserAssembly", L"BigHero.Runtime.ScriptApi+PathDelegate, BigHero.Runtime",
                         reinterpret_cast<void**>(&im.loadUserAssembly))
        && loadManagedFn(L"UnloadUserAssembly", L"BigHero.Runtime.ScriptApi+VoidDelegate, BigHero.Runtime",
                         reinterpret_cast<void**>(&im.unloadUserAssembly))
        && loadManagedFn(L"Attach", L"BigHero.Runtime.ScriptApi+AttachDelegate, BigHero.Runtime",
                         reinterpret_cast<void**>(&im.attach))
        && loadManagedFn(L"DetachAll", L"BigHero.Runtime.ScriptApi+VoidDelegate, BigHero.Runtime",
                         reinterpret_cast<void**>(&im.detachAll))
        && loadManagedFn(L"UpdateAll", L"BigHero.Runtime.ScriptApi+FloatFloatDelegate, BigHero.Runtime",
                         reinterpret_cast<void**>(&im.updateAll));
    if (!allLoaded)
        return false;

    im.nativeApi = NativeApiTable{&BH_TransformGetPosition, &BH_TransformSetPosition, &BH_TransformGetRotation,
                                  &BH_TransformSetRotation, &BH_TransformGetScale,    &BH_TransformSetScale,
                                  &BH_LogWrite,              &BH_EntityIsAlive};
    im.initialize(&im.nativeApi);
    const int apiVersion = im.getApiVersion();
    if (apiVersion != 1)
    {
        LOG_ERROR("CSharpHost: BigHero.Runtime API 版本不匹配（期望 1，实际 " << apiVersion << "），脚本系统降级为禁用");
        return false;
    }
    g_activeScene = scene; // 原生回调的 ECS 访问通道（与 CLR 生命周期一致，Shutdown 时清空）

    // ---- 6. 编译（或复用缓存）用户脚本程序集 + 装载 collectible ALC ----
    const std::wstring asmDllName = Utf8ToWide(im.assemblyName) + L".dll";
    int startVersion = FindLatestBuildVersion(im.buildBase, asmDllName);
    if (startVersion > 0)
    {
        // 复用判定：已编译产物不早于最新源码（脚本源码 + BigHero.Runtime 依赖库）→ 跳过编译
        std::error_code stEc;
        const auto dllTime = fs::last_write_time(
            fs::path(Utf8ToWide(im.buildBase)) / L"scripts" / (L"v" + std::to_wstring(startVersion)) / asmDllName, stEc);
        const auto runtimeDllTime = fs::last_write_time(im.runtimeDllW, stEc);
        if (stEc || dllTime < NewestSourceTime(im.scriptsDirW) || dllTime < runtimeDllTime)
            startVersion = 0; // 源码有更新 → 走编译路径
    }

    std::string dllPathUtf8;
    if (startVersion > 0)
    {
        dllPathUtf8 = im.buildBase + "/scripts/v" + std::to_string(startVersion) + "/" + im.assemblyName + ".dll";
        LOG_INFO("CSharpHost: 复用用户脚本构建缓存 v" << startVersion << "（源码无变化）");
    }
    else
    {
        const int latestAny = FindLatestBuildVersion(im.buildBase, asmDllName);
        const int version = (latestAny > 0) ? latestAny : 1; // 本进程尚未加载任何版本 → 可覆写旧目录（无文件锁）
        LOG_INFO("CSharpHost: 编译用户脚本 " << im.projFileName << " → v" << version << " ...");
        std::string buildErr;
        if (!im.BuildUserScriptsTo(version, dllPathUtf8, buildErr))
        {
            LOG_ERROR("CSharpHost: 用户脚本编译失败，脚本系统降级为禁用。dotnet 输出尾部:\n" << buildErr);
            return false;
        }
    }

    if (im.loadUserAssembly(Utf8ToWide(dllPathUtf8).c_str()) != 0)
    {
        LOG_ERROR("CSharpHost: 用户程序集装载失败: " << dllPathUtf8 << "（脚本系统降级为禁用）");
        return false;
    }
    im.version = startVersion > 0 ? startVersion : 1;
    im.userDll = dllPathUtf8;

    enabled_ = true;
    g_clrOwned = true;
    LOG_INFO("CSharpHost: 初始化完成（API v1，用户程序集 v" << im.version << "，dotnet=" << im.dotnetExe << "）");
    return true;
}

int CSharpHost::AttachToOrderIndex(size_t orderIndex, const std::string& typeName)
{
    if (!enabled_ || impl_ == nullptr || impl_->attach == nullptr)
        return -1;
    Impl& im = *impl_;
    if (im.scene == nullptr || orderIndex >= im.scene->ObjectCount())
    {
        LOG_WARN("CSharpHost: 挂接失败，实体下标越界 " << orderIndex << "/"
                << (im.scene != nullptr ? im.scene->ObjectCount() : 0));
        return -1;
    }
    const uint32_t entityValue = im.scene->At(orderIndex).Value();
    const int id = im.attach(entityValue, Utf8ToWide(typeName).c_str());
    if (id < 0)
    {
        LOG_WARN("CSharpHost: 挂接失败 " << typeName << " → 实体#" << orderIndex);
        return -1;
    }
    im.bindings.push_back(Impl::Binding{orderIndex, typeName, id});
    attachedCount_ = static_cast<uint32_t>(im.bindings.size());
    LOG_INFO("CSharpHost: 脚本已挂接 " << typeName << " → 实体#" << orderIndex << " (handle=" << entityValue
                                      << ", behaviourId=" << id << ")");
    return id;
}

bool CSharpHost::ReloadScripts()
{
    lastFrameScriptMs_ = 0.0f;
    if (!enabled_ || impl_ == nullptr)
    {
        LOG_WARN("CSharpHost: ReloadScripts 被忽略（脚本系统未启用）");
        return false;
    }
    Impl& im = *impl_;
    const int next = im.version + 1;
    const auto t0 = std::chrono::steady_clock::now();

    // 编译到新版本目录：被加载的旧 vN 文件被内存映射锁定（Windows），vN+1 目录互不干扰——
    // DESIGN.md §6.3 "卸载确认完成前不得覆盖写 DLL" 的规避方案：永远不覆盖已加载版本
    LOG_INFO("CSharpHost: 热重载 → 编译 v" << next << " ...");
    std::string buildErr;
    std::string newDll;
    if (!im.BuildUserScriptsTo(next, newDll, buildErr))
    {
        LOG_ERROR("CSharpHost: 热重载编译失败，保留旧脚本继续运行。dotnet 输出尾部:\n" << buildErr);
        return false;
    }

    // 卸载旧 ALC：托管侧先 DetachAll（OnDestroy + GCHandle 全释放——DESIGN.md §6.4 泄漏防线），
    // 再 alc.Unload() + WeakReference/GC 循环确认
    const int unloadRc = im.unloadUserAssembly != nullptr ? im.unloadUserAssembly() : 3;
    if (unloadRc != 0)
        LOG_WARN("CSharpHost: 旧 ALC 卸载未完全确认 (rc=" << unloadRc << ")，已解除全部 GCHandle"
                                                          << "（泄漏自检告警，见 DESIGN.md §6.4）");

    if (im.loadUserAssembly == nullptr || im.loadUserAssembly(Utf8ToWide(newDll).c_str()) != 0)
    {
        LOG_ERROR("CSharpHost: 热重载新程序集装载失败: " << newDll);
        return false;
    }
    im.version = next;
    im.userDll = newDll;

    // 按绑定记录重挂（orderIndex 越界说明实体已被销毁 → 跳过）
    int reattached = 0;
    for (Impl::Binding& b : im.bindings)
    {
        if (im.scene == nullptr || b.orderIndex >= im.scene->ObjectCount())
            continue;
        const int id = im.attach(im.scene->At(b.orderIndex).Value(), Utf8ToWide(b.typeName).c_str());
        if (id >= 0)
        {
            b.behaviourId = id;
            ++reattached;
        }
    }
    attachedCount_ = static_cast<uint32_t>(reattached);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    LOG_INFO("CSharpHost: 热重载完成 v" << im.version << "，重挂 " << reattached << "/" << im.bindings.size()
                                       << " 个脚本，总耗时 " << ms << " ms");
    return true;
}

void CSharpHost::Update(float dt)
{
    lastFrameScriptMs_ = 0.0f;
    if (!enabled_ || impl_ == nullptr)
        return;
    Impl& im = *impl_;

    // 热重载轮询：每 1s 比对源码最新时间戳 vs 已加载程序集的编译时间。
    // 说明：第一增量未引入进程内文件监听线程（watcher 需独立线程 + 跨线程派发，
    // 与 GC/主循环交互风险见 DESIGN.md §7#4），按任务说明采用低频轮询；
    // 1s 的感知延迟对"改脚本 → 保存 → 重载"工作流可接受。
    im.pollTimer += dt;
    if (im.pollTimer >= kPollIntervalSeconds)
    {
        im.pollTimer = 0.0f;
        std::error_code ec;
        const auto loadedDllTime = fs::last_write_time(Utf8ToWide(im.userDll), ec);
        if (NewestSourceTime(im.scriptsDirW) > loadedDllTime)
        {
            LOG_INFO("CSharpHost: 检测到脚本源码变更（时间戳轮询），触发热重载");
            (void)ReloadScripts();
            if (!enabled_)
                return;
        }
    }

    // 批量派发：单次跨界（C++ → ScriptManager.UpdateAll → 全部 OnStart/OnUpdate）
    if (im.updateAll == nullptr || im.bindings.empty())
        return;
    const auto t0 = std::chrono::steady_clock::now();
    const float managedMs = im.updateAll(dt);
    const auto t1 = std::chrono::steady_clock::now();
    lastFrameScriptMs_ = static_cast<float>(std::chrono::duration<double, std::milli>(t1 - t0).count());
    (void)managedMs;
}

void CSharpHost::Shutdown()
{
    if (impl_ == nullptr)
        return;
    Impl& im = *impl_;
    if (enabled_ && im.clrRunning)
    {
        // 卸载用户程序集（OnDestroy + GCHandle 全释放 + collectible ALC unload）
        if (im.unloadUserAssembly != nullptr)
        {
            const int rc = im.unloadUserAssembly();
            if (rc != 0)
                LOG_WARN("CSharpHost: 退出时 ALC 卸载未完全确认 (rc=" << rc << ")");
        }
        enabled_ = false;
        attachedCount_ = 0;
    }
    if (im.hostContext != nullptr && im.closeFxr != nullptr)
    {
        (void)im.closeFxr(im.hostContext);
        im.hostContext = nullptr;
    }
    if (im.fxrModule != nullptr)
    {
        FreeLibrary(im.fxrModule);
        im.fxrModule = nullptr;
    }
    im.clrRunning = false;
    g_clrOwned = false;
    g_activeScene = nullptr;
}

CSharpHost::~CSharpHost()
{
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

} // namespace BigHero::Script

#else // !_WIN32 —— 非 Windows 桩：优雅降级为永久禁用（引擎正常跑）

namespace BigHero::Script
{
bool IsVersionDirectoryName(const std::string& name)
{
    if (name.empty())
        return false;
    // 逐段校验：以 '.' 分段，每段须为 1+ 位数字（拒绝 "8..0" / ".8" / "8." / "8a.0"）
    bool hasDigit = false;
    size_t segStart = 0;
    for (size_t i = 0; i <= name.size(); ++i)
    {
        if (i == name.size() || name[i] == '.')
        {
            if (i == segStart)
                return false; // 空段（开头/结尾/连续点）
            segStart = i + 1;
            continue;
        }
        if (name[i] < '0' || name[i] > '9')
            return false;
        hasDigit = true;
    }
    return hasDigit;
}

std::string PickHighestVersionDirectory(const std::vector<std::string>& names)
{
    std::string best;
    std::vector<int> bestSegs;
    for (const std::string& name : names)
    {
        if (!IsVersionDirectoryName(name))
            continue;
        std::vector<int> segs;
        size_t i = 0;
        while (i <= name.size())
        {
            const size_t j = name.find('.', i);
            const size_t end = (j == std::string::npos) ? name.size() : j;
            segs.push_back(std::atoi(name.substr(i, end - i).c_str()));
            if (j == std::string::npos)
                break;
            i = j + 1;
        }
        if (best.empty() || segs > bestSegs)
        {
            best = name;
            bestSegs = std::move(segs);
        }
    }
    return best;
}

std::vector<std::string> CollectSearchRoots(const std::string& envDotnetRoot,
                                            const std::vector<std::string>& registryRoots,
                                            const std::string& defaultRoot)
{
    std::vector<std::string> roots;
    auto push = [&roots](const std::string& root)
    {
        if (root.empty() || std::find(roots.begin(), roots.end(), root) != roots.end())
            return;
        roots.push_back(root);
    };
    push(envDotnetRoot);
    for (const std::string& r : registryRoots)
        push(r);
    push(defaultRoot);
    return roots;
}

std::vector<std::string> CollectDotnetCandidates(const std::string& envDotnetRoot, const std::string& defaultRoot)
{
    return CollectSearchRoots(envDotnetRoot, {}, defaultRoot);
}

std::string FindHostfxrUnderRoot(const std::string& rootUtf8)
{
    std::error_code ec;
    const std::filesystem::path fxrDir = std::filesystem::path(rootUtf8) / "host" / "fxr";
    if (!std::filesystem::is_directory(fxrDir, ec))
        return {};
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(fxrDir, ec))
        if (entry.is_directory(ec))
            names.push_back(entry.path().filename().string());
    const std::string best = PickHighestVersionDirectory(names);
    if (best.empty())
        return {};
    const auto dll = fxrDir / best / "hostfxr.dll";
    return std::filesystem::is_regular_file(dll, ec) ? dll.string() : std::string{};
}

struct CSharpHost::Impl
{
};
CSharpHost::~CSharpHost()
{
    delete impl_;
}
bool CSharpHost::Init(Scene::EcsScene*, const std::string&)
{
    return false;
}
void CSharpHost::Shutdown() {}
int CSharpHost::AttachToOrderIndex(size_t, const std::string&)
{
    return -1;
}
bool CSharpHost::ReloadScripts()
{
    return false;
}
void CSharpHost::Update(float) {}
} // namespace BigHero::Script

#endif // _WIN32
