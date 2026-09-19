#include "app/Application.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>

// 崩溃时打印符号化调用栈（诊断偶发 AV 用）：VEH first-chance 抓 AV，
// 用 WinAPI 直写文件，绕开可能已损坏的 CRT/重定向。
namespace
{
constexpr LONG kAvCode = 0xC0000005;

void WriteStackReport(LPEXCEPTION_POINTERS ep, const char* tag)
{
    HANDLE file = CreateFileA("D:\\BigheroGameEngine\\out\\veh_crash.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    char line[512];
    int len = wsprintfA(line, "\n===== %s: code=0x%08lX addr=%p thread=%lu\n", tag,
                        static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                        ep->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(len), &written, nullptr);

    void* stack[62];
    const USHORT n = CaptureStackBackTrace(0, 62, stack, nullptr);
    auto* sym = static_cast<SYMBOL_INFO*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SYMBOL_INFO) + 256));
    if (sym)
    {
        sym->MaxNameLen = 255;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        for (USHORT i = 0; i < n; ++i)
        {
            DWORD64 disp = 0;
            if (SymFromAddr(proc, reinterpret_cast<DWORD64>(stack[i]), &disp, sym))
                len = wsprintfA(line, "  #%02u %s + 0x%llx [%p]\n", i, sym->Name, static_cast<unsigned long long>(disp),
                                stack[i]);
            else
                len = wsprintfA(line, "  #%02u ? [%p]\n", i, stack[i]);
            WriteFile(file, line, static_cast<DWORD>(len), &written, nullptr);
        }
        HeapFree(GetProcessHeap(), 0, sym);
    }
    FlushFileBuffers(file);
    CloseHandle(file);
}

LONG WINAPI CrashVeh(LPEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == kAvCode)
        WriteStackReport(ep, "VEH-AV");
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI CrashUef(LPEXCEPTION_POINTERS ep)
{
    WriteStackReport(ep, "UEF");
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, CrashVeh);
    SetUnhandledExceptionFilter(CrashUef);
#endif

    // 崩溃时不丢日志：stdout 改为无缓冲（崩溃前最后一条日志可见）
    setvbuf(stdout, nullptr, _IONBF, 0);

    BigHero::Application::AppConfig config;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--headless") == 0)
        {
            config.headless = true;
        }
        else if (std::strcmp(argv[i], "--validate-only") == 0)
        {
            config.validateOnly = true;
        }
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            config.width = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            config.height = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--title") == 0 && i + 1 < argc)
        {
            config.title = argv[++i];
        }
        else if (std::strcmp(argv[i], "--post-process") == 0)
        {
            config.postProcess = true;
        }
        else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc)
        {
            config.cameraMode = argv[++i]; // "orbit" / "fp"
        }
        else if (std::strcmp(argv[i], "--demo-person") == 0)
        {
            config.demoPerson = true;
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            config.screenshotPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::cout << "BigHero Engine - Vulkan\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --headless         Run without window (for CI)\n";
            std::cout << "  --validate-only    Initialize Vulkan + pipelines and exit\n";
            std::cout << "  --width <w>        Window width (default: 1600)\n";
            std::cout << "  --height <h>       Window height (default: 900)\n";
            std::cout << "  --title <t>        Window title\n";
            std::cout << "  --post-process     Enable post-processing at startup\n";
            std::cout << "  --camera <m>       Camera mode at startup: orbit | fp (default: orbit)\n";
            std::cout << "  --screenshot <p>   Render a few frames then save screenshot to <p> and exit\n";
            std::cout << "  --demo-person      Spawn a demo person at scene center (smoke test)\n";
            std::cout << "  --help             Show this help\n";
            return 0;
        }
    }

    if (config.validateOnly)
    {
        BigHero::Application app(config);
        return app.ValidateOnly();
    }

    BigHero::Application app(config);
    return app.Run();
}