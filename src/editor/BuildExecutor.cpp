#include "editor/BuildExecutor.h"

#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace BigHero::Editor::BuildSettings
{
std::filesystem::path ResolveExePath()
{
#ifdef _WIN32
    // Windows 专用：取当前进程 exe 完整路径（宽字符版，路径含非 ANSI 字符也不截断）
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return std::filesystem::path(buf);
    return {};
#else
    // TODO(跨平台)：Linux 读 /proc/self/exe，macOS 用 _NSGetExecutablePath。
    // 当前先返回空路径，由清单生成阶段产生"可执行文件路径未知"告警。
    return {};
#endif
}

ExecuteReport ExecuteManifest(const BuildManifest& manifest, const std::filesystem::path& workDir)
{
    ExecuteReport report;
    report.outputDirectory = manifest.outputDirectory;

    // 输出目录整体创建（含 builds/<时间戳> 全链路）；失败则短路返回
    const std::filesystem::path outDir(manifest.outputDirectory);
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec)
    {
        report.fatalError = "无法创建输出目录: " + manifest.outputDirectory + " (" + ec.message() + ")";
        return report;
    }

    for (const CopyEntry& entry : manifest.entries)
    {
        CopyResult r;
        r.source = entry.source;
        r.destination = entry.destination;

        const std::filesystem::path src = std::filesystem::path(entry.source).is_absolute()
                                              ? std::filesystem::path(entry.source)
                                              : workDir / entry.source;
        const std::filesystem::path dst = outDir / entry.destination;

        std::error_code fileEc;
        const bool srcExists = std::filesystem::exists(src, fileEc);
        if (fileEc || !srcExists)
        {
            r.error = "来源文件不存在: " + src.string();
        }
        else
        {
            std::filesystem::create_directories(dst.parent_path(), fileEc);
            if (fileEc)
            {
                r.error = "无法创建目标目录: " + dst.parent_path().string() + " (" + fileEc.message() + ")";
            }
            else
            {
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, fileEc);
                if (fileEc)
                    r.error = "拷贝失败: " + fileEc.message();
            }
        }

        r.success = r.error.empty();
        report.results.push_back(std::move(r));
    }

    return report;
}
} // namespace BigHero::Editor::BuildSettings
