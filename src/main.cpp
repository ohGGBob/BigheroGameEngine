#include "app/Application.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

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
    bool sceneKindGiven = false; // --scene 显式指定过（--ui-demo 默认走 slice 场景时的回退判定）

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
        else if (std::strcmp(argv[i], "--no-ui") == 0)
        {
            config.noUi = true;
        }
        else if (std::strcmp(argv[i], "--exposure") == 0 && i + 1 < argc)
        {
            float exposure = 1.0f;
            bool valid = true;
            try
            {
                exposure = std::stof(argv[++i]);
            }
            catch (const std::exception&)
            {
                valid = false; // 非数字或越界（invalid_argument / out_of_range）
            }
            if (!valid || !std::isfinite(exposure) || exposure <= 0.0f)
            {
                std::cout << "Warning: invalid --exposure value '" << argv[i] << "', using default 1.0\n";
                exposure = 1.0f;
            }
            config.exposure = exposure;
        }
        else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc)
        {
            config.cameraMode = argv[++i]; // "orbit" / "fp"
        }
        else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
        {
            config.sceneKind = argv[++i]; // "default" / "slice" / "openworld" / "cybercity" / "voxel" / "terrain"
            sceneKindGiven = true;
        }
        else if (std::strcmp(argv[i], "--ui-demo") == 0)
        {
            config.uiDemo = true; // 运行时 UI 演示画布（U1-UI 第一增量）
        }
        else if (std::strcmp(argv[i], "--editor-ui") == 0)
        {
            // 方块世界下也保留编辑器面板（默认收起 = 纯游戏模式；运行期 F1 切换）
            config.editorUiInVoxel = true;
        }
        else if (std::strcmp(argv[i], "--demo-person") == 0)
        {
            config.demoPerson = true;
        }
        else if (std::strcmp(argv[i], "--demo-events") == 0)
        {
            config.demoEvents = true; // 动画事件运行期演示：注入程序化循环 clip + "click" 事件 + 自动进 Play 态
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            config.screenshotPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--screenshot2") == 0 && i + 1 < argc)
        {
            config.screenshot2Path = argv[++i];
        }
        else if (std::strcmp(argv[i], "--screenshot2-delay") == 0 && i + 1 < argc)
        {
            float delay = 3.0f;
            bool valid = true;
            try
            {
                delay = std::stof(argv[++i]);
            }
            catch (const std::exception&)
            {
                valid = false;
            }
            if (!valid || !std::isfinite(delay) || delay < 0.1f)
            {
                std::cout << "Warning: invalid --screenshot2-delay value '" << argv[i] << "', using default 3.0\n";
                delay = 3.0f;
            }
            config.screenshot2DelaySeconds = delay;
        }
        else if (std::strcmp(argv[i], "--scripts") == 0 && i + 1 < argc)
        {
            config.scriptsDir = argv[++i];
        }
        else if (std::strcmp(argv[i], "--bake-probes") == 0)
        {
            config.bakeProbes = true; // 启动即触发光照探针烘焙（性能对比自动化）
        }
        else if (std::strcmp(argv[i], "--bake-occlusion") == 0)
        {
            config.bakeOcclusion = true; // 启动即触发遮挡剔除烘焙（性能对比自动化）
        }
        else if (std::strcmp(argv[i], "--no-reflection-probes") == 0)
        {
            config.noReflectionProbes = true; // 禁用反射探针烘焙（U2-L2 接线 A/B 对照验证）
        }
        else if (std::strcmp(argv[i], "--no-probe-capture") == 0)
        {
            config.noProbeCapture = true; // 旁路 GPU 立方图捕获（探针回退解析 SH；A/B 对照）
        }
        else if (std::strcmp(argv[i], "--no-lightmap") == 0)
        {
            config.noLightmap = true; // 禁用静态光照贴图烘焙与批次绘制（U2-L1 渲染接线 A/B 对照）
        }
        else if (std::strcmp(argv[i], "--bake-lightmap") == 0)
        {
            config.bakeLightmap = true; // 启动即烘焙光照贴图并写盘退出（U2-L1 接线 2a，纯 CPU）
            if (i + 1 < argc && argv[i + 1][0] != '-')
                config.lightmapPath = argv[++i]; // 可选覆盖输出路径
        }
        else if (std::strcmp(argv[i], "--lod-off") == 0)
        {
            config.lodOff = true; // 旁路球/胶囊 LOD 选档，强制全高模（量化 LOD 收益）
        }
        else if (std::strcmp(argv[i], "--bench-frames") == 0 && i + 1 < argc)
        {
            uint32_t frames = 0;
            bool valid = true;
            try
            {
                frames = static_cast<uint32_t>(std::stoul(argv[++i]));
            }
            catch (const std::exception&)
            {
                valid = false; // 非数字或越界（invalid_argument / out_of_range）
            }
            if (!valid || frames < 1)
            {
                std::cout << "Warning: invalid --bench-frames value '" << argv[i] << "', disabled\n";
                frames = 0;
            }
            config.benchFrames = frames;
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::cout << "BigHero Engine - Vulkan\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --headless         Run without window (for CI)\n";
            std::cout << "  --validate-only    Check required shader files exist and exit (no Vulkan init)\n";
            std::cout << "  --width <w>        Window width (default: 1600)\n";
            std::cout << "  --height <h>       Window height (default: 900)\n";
            std::cout << "  --title <t>        Window title\n";
            std::cout << "  --post-process     Enable post-processing at startup\n";
            std::cout
                << "  --no-ui            Skip editor overlay recording (pure scene render; for imaging baselines)\n";
            std::cout << "  --exposure <f>     Initial exposure (default: 1.0), same as editor light slider\n";
            std::cout << "  --camera <m>       Camera mode at startup: orbit | fp (default: orbit)\n";
            std::cout
                << "  --scene <name>     Scene to load: default | slice | openworld | cybercity | voxel | terrain "
                   "(default: default)\n";
            std::cout << "                     cybercity = 赛博城市展示厅：第一人称漫游 + 八座引擎特性展台\n";
            std::cout << "                     voxel = 方块世界：可挖掘 / 放置的体素地形（第一人称）\n";
            std::cout << "                     terrain = 地形场景（U2-T1 接线 v1）：高度场笔刷地貌 +\n";
            std::cout << "                     splat 顶点色，分块网格直接绘制（编辑器下拉框暂未收录，CLI 专属）\n";
            std::cout << "  --screenshot <p>   Render a few frames then save screenshot to <p> and exit\n";
            std::cout
                << "  --screenshot2 <p>  Take a second screenshot at --screenshot2-delay (timing/script compare)\n";
            std::cout << "  --screenshot2-delay <s>  Delay (seconds, default 3.0) before the second screenshot\n";
            std::cout << "  --scripts <dir>    Enable C# scripting: user script project dir (contains .csproj),\n";
            std::cout << "                     e.g. samples/scripts/MyGame. Graceful degrade if .NET is missing.\n";
            std::cout << "  --demo-person      Spawn a demo person at scene center (smoke test)\n";
            std::cout << "  --demo-events      Animation-event runtime demo: inject a 2s looping procedural clip\n";
            std::cout << "                     when the glTF prop has no animation, set the builtin track event\n";
            std::cout << "                     name to 'click', and auto-enter Play mode so AnimationEventPlayer\n";
            std::cout << "                     fires events on its timeline (visible in log + SfxId::Click path).\n";
            std::cout << "                     e.g. --scene default --demo-events --bench-frames 120\n";
            std::cout << "  --ui-demo          Overlay the runtime-UI demo canvas (panel + title +\n";
            std::cout << "                     spawn/clear buttons). Defaults to --scene slice unless\n";
            std::cout << "                     a scene was explicitly requested.\n";
            std::cout << "  --editor-ui        Keep the editor panels in the voxel scene (default: voxel\n";
            std::cout << "                     runs in play mode - panels hidden; press F1 to toggle).\n";
            std::cout << "  --bake-probes      Bake light probes at startup (same as editor panel button;\n";
            std::cout << "                     for bake/no-bake performance comparison)\n";
            std::cout << "  --bake-occlusion   Bake occlusion culling (PVS) at startup (same as editor panel\n";
            std::cout << "                     button; for bake/no-bake performance comparison)\n";
            std::cout << "  --bake-lightmap [p] Bake the static-scene lightmap offline (pure CPU, U2-L1 wiring\n";
            std::cout << "                     2a): chartless direct light + sky AO -> RGBE atlas snapshot,\n";
            std::cout << "                     written to <p> (default lightmap.lm), then exit. 0=ok 1=failed.\n";
            std::cout << "  --no-reflection-probes Disable U2-L2 reflection-probe baking (A/B verification)\n";
            std::cout << "  --no-probe-capture  Bypass reflection-probe GPU cubemap capture (probes fall\n";
            std::cout << "                     back to analytic SH; A/B verification)\n";
            std::cout << "  --no-lightmap       Disable static lightmap baking + batch drawing (U2-L1\n";
            std::cout << "                     rendering wiring A/B; static cubes fall back to realtime)\n";
            std::cout << "  --bench-frames <N> Benchmark mode: render N frames, print avg/min/max frame time\n";
            std::cout << "                     + per-stage CPU avg to stdout, then exit (scripted perf compare)\n";
            std::cout << "  --lod-off          Bypass sphere/capsule LOD selection: force all high-detail buckets\n";
            std::cout << "                     and skip LOD culling. Does not affect frustum/PVS culling.\n";
            std::cout << "  --help             Show this help\n";
            return 0;
        }
    }

    // --ui-demo 未显式指定场景时默认叠加在垂直切片场景上（1200 实体群上的 HUD 面板演示）
    if (config.uiDemo && !sceneKindGiven)
        config.sceneKind = "slice";

    if (config.validateOnly)
    {
        BigHero::Application app(config);
        return app.ValidateOnly();
    }

    BigHero::Application app(config);
    return app.Run();
}