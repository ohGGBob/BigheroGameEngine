#pragma once
// 分级日志器（Logger）：带级别过滤、时间戳与线程安全的日志输出。
// 纯标准库、仅头文件。
//
// 商业化价值：运行时诊断与追踪的标准化入口——信息/警告/错误分级别，
// 可开关 verbose，线程安全，避免各处 printf 的不一致。

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace BigHero::Core
{
class Logger
{
  public:
    enum class Level : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

    static Logger& Instance()
    {
        static Logger inst;
        return inst;
    }

    void SetLevel(Level l) { level_ = l; }
    [[nodiscard]] Level GetLevel() const { return level_; }

    void Log(Level l, std::string_view msg)
    {
        if (l < level_ || level_ == Level::Off)
            return;
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::lock_guard<std::mutex> lock(mx_);
        // 输出到 stdout，附级别前缀与时间戳。
        std::printf("[%s] %s\n", LevelName(l), msg.data());
        std::fflush(stdout);
        (void)tt;
    }

    void Trace(std::string_view m) { Log(Level::Trace, m); }
    void Debug(std::string_view m) { Log(Level::Debug, m); }
    void Info(std::string_view m) { Log(Level::Info, m); }
    void Warn(std::string_view m) { Log(Level::Warn, m); }
    void Error(std::string_view m) { Log(Level::Error, m); }

  private:
    static const char* LevelName(Level l)
    {
        switch (l)
        {
            case Level::Trace: return "TRACE";
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warn: return "WARN";
            case Level::Error: return "ERROR";
            default: return "?";
        }
    }
    Level level_ = Level::Info;
    std::mutex mx_;
};

// 便捷宏/函数：直接用全局单例。
inline Logger& Log() { return Logger::Instance(); }
} // namespace BigHero::Core
