#pragma once
// 构建执行器 —— Unity 对标清单 U1-B1"一键构建"的落地端。
// 输入 BuildSettingsModel 生成的 BuildManifest，把清单逐文件拷贝到输出目录：
//   - 纯 C++ std::filesystem 拷贝（不用 std::system / 外部进程）；
//   - 逐文件收集成功/失败与错误消息，单条失败不中断其余拷贝；
//   - 输出目录（builds/<时间戳>/）在拷贝前整体创建，失败记 fatalError 短路返回。
// exe 自我复制的来源路径经 ResolveExePath 解析：Windows 专用 API（GetModuleFileName）
// 以 #ifdef _WIN32 隔离在 .cpp 内，其余平台留 TODO（返回空路径 → 清单阶段产生告警）。

#include "editor/BuildSettingsModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace BigHero::Editor::BuildSettings
{
// 单文件拷贝结果（逐文件成功/失败报告行）
struct CopyResult
{
    std::string source;
    std::string destination;
    bool success = false;
    std::string error; // 失败原因（来源缺失 / 目录创建失败 / std::error_code 消息）
};

// 执行报告：fatalError 非空表示输出目录创建失败（results 为空，整体短路）
struct ExecuteReport
{
    std::string outputDirectory;
    std::string fatalError;
    std::vector<CopyResult> results;

    [[nodiscard]] size_t SuccessCount() const noexcept
    {
        size_t n = 0;
        for (const CopyResult& r : results)
            if (r.success)
                ++n;
        return n;
    }
    [[nodiscard]] size_t FailureCount() const noexcept { return results.size() - SuccessCount(); }
    [[nodiscard]] bool AllSucceeded() const noexcept { return fatalError.empty() && FailureCount() == 0; }
};

// 解析当前进程可执行文件完整路径（exe 自我复制的来源）。
// Windows：GetModuleFileNameW（宽字符，规避 ANSI 代码页截断）；
// 其他平台：TODO——暂返回空路径（清单生成阶段给出"路径未知"告警）。
[[nodiscard]] std::filesystem::path ResolveExePath();

// 执行清单拷贝：相对来源按 workDir 解析，目的地 = manifest.outputDirectory / entry.destination。
// 已存在的目标文件覆盖（重复构建幂等）；返回逐文件成功/失败报告，不抛异常。
[[nodiscard]] ExecuteReport ExecuteManifest(const BuildManifest& manifest, const std::filesystem::path& workDir);
} // namespace BigHero::Editor::BuildSettings
