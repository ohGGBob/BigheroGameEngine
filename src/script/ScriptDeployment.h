#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace BigHero::Script
{
struct PrecompiledScriptPackage
{
    std::filesystem::path assembly;
    std::filesystem::path runtimeDirectory;
};

// 显式标记发布模式；损坏或不完整的包不得回退到开发源码编译。
[[nodiscard]] inline bool ReadPrecompiledScriptPackage(const std::filesystem::path& directory,
                                                       PrecompiledScriptPackage& package, std::string& error)
{
    try
    {
        std::ifstream input(directory / "script-package.json");
        const auto manifest = nlohmann::json::parse(input);
        const std::string name = manifest.at("assembly").get<std::string>();
        if (manifest.at("version").get<int>() != 1 || name.empty() ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
                std::string::npos ||
            std::filesystem::path(name).extension() != ".dll")
        {
            error = "Invalid script package version or assembly filename";
            return false;
        }
        PrecompiledScriptPackage candidate{directory / name, directory / "runtime"};
        for (const auto& path : {candidate.assembly, candidate.runtimeDirectory / "BigHero.Runtime.dll",
                                 candidate.runtimeDirectory / "BigHero.Runtime.runtimeconfig.json"})
        {
            if (!std::filesystem::is_regular_file(path))
            {
                error = "Missing script package file: " + path.string();
                return false;
            }
        }
        package = std::move(candidate);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

// 发布包优先，开发树兜底。显式传根目录，便于验证与原源码目录无关的部署。
[[nodiscard]] inline std::filesystem::path FindRuntimeProject(const std::filesystem::path& executableDirectory,
                                                              const std::filesystem::path& workingDirectory,
                                                              const std::filesystem::path& sourceRoot = {})
{
    const auto relative = std::filesystem::path("scriptcore") / "BigHero.Runtime" / "BigHero.Runtime.csproj";
    auto probe = [&relative](const std::filesystem::path& root)
    {
        std::error_code ec;
        const auto project = root / relative;
        return !root.empty() && std::filesystem::is_regular_file(project, ec) ? project : std::filesystem::path{};
    };
    if (const auto project = probe(executableDirectory); !project.empty())
        return project;
    auto root = workingDirectory;
    for (int i = 0; i < 6 && !root.empty(); ++i)
    {
        if (const auto project = probe(root); !project.empty())
            return project;
        const auto parent = root.parent_path();
        if (parent == root)
            break;
        root = parent;
    }
    return probe(sourceRoot);
}
} // namespace BigHero::Script
