#pragma once
// 文件系统工具（FileSystemUtils）：基于 std::filesystem 的跨平台常用操作。
// 纯标准库（C++17）、仅头文件。
//
// 商业化价值：资源目录枚举、资产加载、配置读写、路径校验的统一起点；
// 屏蔽 win32 / posix 差异，提供错误安全的文件处理原语。
//
// 提供：Exists/IsDirectory/ListFiles(支持扩展名过滤)/ReadText/WriteText/GetSize/CreateDirs/Remove。

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>

namespace BigHero::Core
{
namespace FileSystem
{
    inline bool Exists(const std::string& path)
    {
        return std::filesystem::exists(path);
    }
    inline bool IsDirectory(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    // 列出 path 下全部文件（不递归，默认返回所有；ext 过滤时只留扩展名匹配）。
    inline std::vector<std::string> ListFiles(const std::string& path,
                                              const std::string& extFilter = "")
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(path, ec))
        {
            if (!e.is_regular_file())
                continue;
            std::string p = e.path().string();
            if (!extFilter.empty())
            {
                if (e.path().extension() != extFilter)
                    continue;
            }
            out.push_back(p);
        }
        return out;
    }

    inline bool ReadText(const std::string& path, std::string& out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    inline bool WriteText(const std::string& path, std::string_view content)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return false;
        f.write(content.data(), (std::streamsize)content.size());
        return f.good();
    }

    inline std::uintmax_t GetSize(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::file_size(path, ec);
    }

    inline bool CreateDirs(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return !ec;
    }

    inline bool Remove(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    inline std::string CurrentPath()
    {
        std::error_code ec;
        return std::filesystem::current_path(ec).string();
    }
} // namespace FileSystem
} // namespace BigHero::Core
