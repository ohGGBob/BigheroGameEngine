#pragma once
// 构建设置模型（纯逻辑）—— Unity 对标清单 U1-B1：Build Settings + 一键构建。
// 与 HierarchyModel/InspectorModel 同一约定：纯函数 + 纯数据，不依赖 ImGui/Vulkan，
// 可离线单元测试。不执行任何写 IO——存在性检查只读，实际拷贝由 BuildExecutor 负责。
//
// 职责：
//   - BuildConfig 描述一次构建：目标平台（占位 win-x64）、输出根目录、版本字符串、
//     打包开关（assets//shaders//exe）与场景清单（内置 default/slice + 外部场景文件）；
//   - GenerateManifest 由 配置 + 工作目录 生成"构建清单"：需要拷贝的文件列表（来源→目的地）、
//     逐项去重、存在性检查与缺失告警。内置场景随 exe 编译，不产生文件条目；
//   - TimestampDirName / CurrentTimestampDirName 产出 builds/<时间戳> 的时间戳目录名。
//
// 产物语义：exe 旁所需运行时集合的独立可分发目录（拷走即能跑）——
// 当前 exe + shaders/（.spv）+ assets/（勾选项）+ 场景文件，输出到 builds/<时间戳>/。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace BigHero::Editor::BuildSettings
{
// 构建配置（面板可编辑；默认值 = 当前引擎现状的最小可用构建）
struct BuildConfig
{
    std::string platformTarget = "win-x64"; // 目标平台占位（后续扩展 android-arm64 等）
    std::string outputRoot = "builds";      // 输出根目录（相对工作目录；实际输出再下接时间戳目录）
    std::string versionString = "0.18.0";   // 版本字符串（与 CMake project VERSION 对齐）
    bool copyExecutable = true;             // 拷贝当前 exe（产物自包含可运行）
    bool copyShaders = true;                // 拷贝 shaders/（.spv 编译产物）
    bool packageAssets = true;              // 打包 assets/
    std::vector<std::string> builtinScenes{"default", "slice", "openworld"}; // 内置场景名（随 exe 编译，无需拷文件）
    std::vector<std::string> sceneFiles{"scene.json"};                       // 随包分发的场景文件（相对工作目录）
};

// 一条待拷贝条目：source 相对工作目录（exe 条目为绝对路径），destination 相对输出目录。
// 路径统一正斜杠（generic_string），保证清单/日志/去重键跨平台一致。
struct CopyEntry
{
    std::string source;
    std::string destination;
    std::string kind; // "exe" | "shaders" | "assets" | "scene"（面板映射为中文标签）
};

// 构建清单：输出目录 + 待拷贝文件列表 + 缺失告警（清单生成阶段的存在性检查结果）
struct BuildManifest
{
    std::string outputDirectory;
    std::vector<CopyEntry> entries;
    std::vector<std::string> warnings;
};

// 时间戳目录名：YYYYMMDD_HHMMSS（零填充；纯拼接便于单测锁定格式）
inline std::string TimestampDirName(int year, int month, int day, int hour, int minute, int second)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d", year, month, day, hour, minute, second);
    return buf;
}

// 当前时间戳目录名（便捷入口；格式锁定测试请用 TimestampDirName）
inline std::string CurrentTimestampDirName()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return TimestampDirName(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                            local.tm_sec);
}

namespace Detail
{
// 统一路径为正斜杠窄串（清单显示与去重键）
inline std::string Normalize(const std::filesystem::path& p)
{
    return p.lexically_normal().generic_string();
}

// 递归枚举 root 下全部普通文件，返回相对 root 的正斜杠路径（升序，保证清单确定性）。
// 目录不存在时返回空表并置 outExists=false（由调用方产生缺失告警）。
inline std::vector<std::string> ListFilesRelative(const std::filesystem::path& root, bool& outExists)
{
    outExists = std::filesystem::is_directory(root);
    std::vector<std::string> files;
    if (!outExists)
        return files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc)
            continue;
        files.push_back(Normalize(it->path().lexically_relative(root)));
    }
    std::sort(files.begin(), files.end());
    return files;
}
} // namespace Detail

// 构建清单生成（纯函数）：配置 + 工作目录 + exe 路径 + 时间戳目录名 → 清单。
// exePath 由调用方解析（Windows 经 BuildExecutor::ResolveExePath 的 GetModuleFileName），
// 测试可直接传假路径；为空或不存在时产生告警而非条目。
// 存在性检查只读；缺失项（目录/场景/exe）进 warnings，不中断清单生成。
inline BuildManifest GenerateManifest(const BuildConfig& config, const std::filesystem::path& workDir,
                                      const std::filesystem::path& exePath, const std::string& timestampDirName)
{
    BuildManifest m;
    m.outputDirectory = Detail::Normalize(workDir / config.outputRoot / timestampDirName);

    // 去重键（source, destination）：目录枚举 + 场景列表可能撞同一文件
    std::set<std::pair<std::string, std::string>> seen;
    auto addEntry = [&m, &seen](std::string source, std::string destination, const char* kind)
    {
        if (seen.emplace(source, destination).second)
            m.entries.push_back({std::move(source), std::move(destination), kind});
    };

    // ---- exe：自我复制（来源为绝对路径，独立于工作目录） ----
    if (config.copyExecutable)
    {
        if (exePath.empty())
            m.warnings.push_back("可执行文件路径未知，产物不含 exe（无法自包含运行）");
        else if (!std::filesystem::exists(exePath))
            m.warnings.push_back("可执行文件不存在: " + exePath.string());
        else
            addEntry(Detail::Normalize(exePath), exePath.filename().generic_string(), "exe");
    }

    // ---- shaders/ / assets/：目录递归枚举，逐文件条目（升序，确定性） ----
    const std::pair<const char*, const char*> kDirs[] = {{"shaders", "shaders"}, {"assets", "assets"}};
    const bool wantDir[] = {config.copyShaders, config.packageAssets};
    for (int i = 0; i < 2; ++i)
    {
        if (!wantDir[i])
            continue;
        const std::filesystem::path dir = workDir / kDirs[i].first;
        bool exists = false;
        const std::vector<std::string> files = Detail::ListFilesRelative(dir, exists);
        if (!exists)
        {
            m.warnings.push_back(std::string("目录缺失: ") + kDirs[i].first + "/（产物将不包含该目录）");
            continue;
        }
        for (const std::string& rel : files)
        {
            const std::string prefixed = std::string(kDirs[i].first) + "/" + rel;
            addEntry(prefixed, prefixed, kDirs[i].second);
        }
    }

    // ---- 外部场景文件：去重 + 存在性检查（内置场景随 exe 编译，不产生条目） ----
    std::set<std::string> sceneSeen;
    for (const std::string& raw : config.sceneFiles)
    {
        const std::string rel = Detail::Normalize(std::filesystem::path(raw));
        if (rel.empty() || !sceneSeen.insert(rel).second)
            continue;
        const std::filesystem::path full = workDir / rel;
        if (!std::filesystem::exists(full))
        {
            m.warnings.push_back("场景文件缺失: " + rel);
            continue;
        }
        addEntry(rel, std::filesystem::path(rel).filename().generic_string(), "scene");
    }

    return m;
}
} // namespace BigHero::Editor::BuildSettings
